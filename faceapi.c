
#include "faceapi.h"
#include "faceapi_strings.h"

static char region[BUFSIZ];
static char key[BUFSIZ];
static char login = 0;		// check if user have logged in

Queue * request_queue;
Queue * response_queue;

sem_t request_counter;
pthread_t request_thread;
pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;

void setUriParam(CURLU * curlu, const char * name, struct json_object * value);
void setUriBase(CURLU * curlu, const char * base);
int statusOk(long status);
int reg_result_append(Table * table, void * item);
int detect_result_append(Table * table, void * item);
int ident_result_append(Table * table, void * item);
Queue * queue_new(QueueType type, unsigned int capacity);
void queue_free(Queue * queue);
int queue_isempty(Queue * queue);
int queue_isfull(Queue * queue);
void * queue_rear(Queue * queue);
void * queue_front(Queue * queue);
int dequeue(Queue * queue);
int enqueue(Queue * queue, void * item);
void * request();

typedef struct ReadData
{
	char * content;			// this is the content of the readData
	size_t length;			// this is the length of the readData
} ReadData;

void face_init() {
	QueueType type = FACE_QUEUETYPE_REQUEST;
	request_queue = queue_new(type, FACE_QUEUE_CAPACITY);
	type = FACE_QUEUETYPE_RESPONSE;
	response_queue = queue_new(type, FACE_QUEUE_CAPACITY);
	sem_init(&request_counter, 0, 0);
	pthread_create(&request_thread, NULL, request, NULL);
}

void face_cleanup() {
	Request rqst = {
		.rqst_type = FACE_RQSTTYPE_END,
		.file = NULL,
		.fsize = 0,
		.table = NULL,
		.rqst_func = NULL
	};

	// wait and send end signal if request_queue is full
	while (enqueue(request_queue, &rqst)) {
		sleep(1);
	}
	sem_post(&request_counter);

	// testing
	printf("Test cleanup\n");

	while (1) {
		// testing
		printf("test cleanup while loop\n");
		if (((Response *)(queue_rear(response_queue)))->resp_type == FACE_RQSTTYPE_END) {
			queue_free(request_queue);
			queue_free(response_queue);
			break;
		}
		sleep(1);
	}
}

void * request() {

	// detach this thread from main thread
	pthread_detach(pthread_self());

	while(1) {

		// thread will only be activated when sem_post is called
		sem_wait(&request_counter);

		// retrieve latest request from request_queue
		Request * rqst = queue_rear(request_queue);

		// send program end signal
		if (rqst->rqst_type == FACE_RQSTTYPE_END) {
			Response resp = {
				.resp_type = rqst->rqst_type,
				.table = NULL,
				.file = NULL
			};

			// wait and pass on end signal if response_queue is full
			while (enqueue(response_queue, &resp)) {
				sleep(1);
			}
			break;	// closes this thread
		}

		// call request function and pass the result to response_queue
		if ((*rqst->rqst_func)(rqst->file, rqst->fsize, rqst->table)) {
			// if request function failed, pop it from request_queue
			// and continue
			dequeue(request_queue);
			continue;
		}

		// push result to response_queue
		Response resp = {
			.resp_type = rqst->rqst_type,
			.table = rqst->table,
			.file = rqst->file
		};
		while (enqueue(response_queue, &resp)) {
			sleep(1);
		}

		// pop the request from request_queue
		dequeue(request_queue);
	}
	pthread_exit(NULL);
}

Response * getResponse() {
	if (queue_isempty(response_queue)) {
		return NULL;
	}
	Response * resp = queue_rear(response_queue);
	dequeue(response_queue);
	return resp;
}

/**
 * Description:
 *		This method is repeatedly called by curl_easy_setopt(curl, WRITEDATA, response)
 *		until all the contents in the http response is written into response
 * 
 * Params: 
 *		contents: where the response will be written from
 *		size: size of the element to be written
 *		nmemb: number of elements to write; in this function this is always 1
 *		response: return parameter; accumulates the data written from this
 *		  			  function to form the complete http response
 *
 * Return:
 *		size of the data written
 */
static size_t write_callback(void * contents, size_t size, size_t nmemb, void * response)
{
	errno = 0;								// setting errno for error detection
	char * buffer = NULL;					// buffer for response in case realloc fail

	// copy from response if response is not null
	if (((ReadData *)response)->content) {
		buffer = ((ReadData *)response)->content;	
	}

	// dynamically adjust the buffer size
	buffer = realloc(buffer, ((ReadData *)response)->length + size * nmemb);

	// setting pointer pointing to where memcpy should start copying to
	char * start = buffer + ((ReadData *)response)->length;

	// checking for no memory error for realloc
	if (errno == ENOMEM) {
		fprintf(stderr, "Error: not enough memory\n");
		free(buffer);
		return EXIT_FAILURE;
	}

	// updating ReadData length
	((ReadData *)response)->length += (size * nmemb);

	// copying the content to the response
	memcpy(start, contents, size * nmemb);

	// point the updated memory back to response
	((ReadData *)response)->content = buffer;

#ifdef _DEBUG_
	fprintf(stderr, FACE_WRITE_DATA, size * nmemb);
#endif

	// return actual bytes written
	return size * nmemb;
}

/**
 * Description:
 *		This method is repeatedly called by curl_easy_setopt(curl, READDATA, response)
 *		until all the image data is read into the buffer
 * 
 * Params:
 *		buffer: return parameter; where the image data will be read into
 *		size: size of the element to be read
 *		nmemb: number of elements to be read
 *		image: where the image data will be read from
 *
 * Return:
 *		size of the data read
 */
static size_t read_image(void * buffer, size_t size, size_t nmemb, void * image)
{
	size_t retcode = fread(buffer, size, nmemb, image);

#ifdef _DEBUG_
	fprintf(stderr, FACE_READ_IMAGE, retcode);
#endif

	return retcode;
}

/**
 * Description: 
 *		This method is repeatedly called by curl_easy_setopt(curl, READDATA, response)
 *		until all the text is read into the buffer
 * 
 * Params:
 *		buffer: return parameter; where the text will be read into
 *		size: size of the element to be read
 *		nmemb: number of elements to be read
 *		text: where the text will be read from
 *
 * Return:
 *		size of the data read
 */
static size_t read_text(void * buffer, size_t size, size_t nmemb, void * text)
{
	size_t retcode;

	char * str = (char *)text;

	if ((strlen(str) + 1) > (size * nmemb)) {
		retcode = size * nmemb;
	}
	else {
		retcode = strlen(str) + 1;
	}

	memcpy(buffer, text, retcode);

#ifdef _DEBUG_
	fprintf(stderr, FACE_READ_TEXT, retcode);
#endif

	return retcode;
}

/**
 * Description:
 *		Sets the region and subscription key for calling Face API
 *
 * Params:
 *		rg: sets the region
 *		ky: sets the subscription key
 *
 * Return:
 *		0 if successful; corresponding errno code if unsuccessful
 */
int face_login(char * rg, char * ky) {
	errno = 0;
	memset(region, 0, BUFSIZ);
	memset(key, 0, BUFSIZ);
	if (rg) {
		strcpy(region, rg);
	}
	else {
		strcpy(region, FACE_DEFAULT_REGION);
	}
	strcpy(key, ky);

	if (errno) {
		fprintf(stderr, "%s\n", strerror(errno));
		return errno;
	}
	login = 1;
	return EXIT_SUCCESS;
}

/**
 * Description:
 *		Creates a persongroup by calling PersonGroup Create (PUT)
 *
 * Params:
 *		pgid: the persongroupId chosed by the caller
 *		body: the request body
 *		resp: return parameter; collects the response
 *
 * Return:
 *		http status code; -1 if user hasn't logged in via face_login
 */
long face_create_pg(char * pgid, struct json_object * body, struct json_object ** resp) {
	CURL * curl;					// handle of the libcurl interface
	CURLU * curlu;					// handle for the request url
	CURLcode res;					// result of the curl command
	ReadData * response;			// collects response
	char * url;						// the request url
	char buffer[BUFSIZ] = {0};		// buffer for url parsing
	long ret;						// response http status code
	double lat;						// latency

	errno = 0;						// setting errno for error detection

	// check if region and key are initialized
	if (!login) {
		fprintf(stderr, FACE_LOGIN_ERROR);
		return -1;
	}

	curlu = curl_url();

	// setting the request url
	setUriBase(curlu, FACE_PG_URL);

	// including the pgid in the request url
	strcat(buffer, pgid);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);

	// retrieving the url from curlu
	curl_url_get(curlu, CURLUPART_URL, &url, CURLU_NON_SUPPORT_SCHEME);

#ifdef _DEBUG_
	fprintf(stderr, FACE_REQUEST_URL, url);
#endif

	response = calloc(1, sizeof(ReadData));

	// loading up libcurl environment
	curl_global_init(CURL_GLOBAL_ALL);

	// loading up curl_easy interface
	curl = curl_easy_init();
	if(curl)
	{
		/* First set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */
		curl_easy_setopt(curl, CURLOPT_URL, url);

		// setting request header
		struct curl_slist * plist = curl_slist_append(NULL, FACE_JSON);
		strcat(buffer, FACE_KEYTYPE);
		strcat(buffer, key);
		plist = curl_slist_append(plist, buffer);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plist);

		// setting request type (CURLOPT_PUT is deprecated; use CURLOPT_UPLOAD instead)
		curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

		// setting read callback function and data
		curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_text);
		curl_easy_setopt(curl, CURLOPT_READDATA, json_object_to_json_string(body));

		// setting size of request body
		curl_easy_setopt(curl, CURLOPT_INFILESIZE, strlen(json_object_to_json_string(body)) + 1);

		// setting write callback function and buffer
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

		/* Perform the request, res will get the return code */ 
		res = curl_easy_perform(curl);

		/* Check for errors */
		if(res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

		// acquire http status code
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);

#ifdef _DEBUG_
		// acquire latency
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &lat);
#endif

		curl_slist_free_all(plist);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	curl_url_cleanup(curlu);
	curl_global_cleanup();

	// printing http status code and latency
#ifdef _DEBUG_
	fprintf(stderr, FACE_HTTP_STATUS, ret);
	fprintf(stderr, FACE_LATENCY, lat);
#endif

	// null terminating the response string
	response->content = realloc(response->content, response->length + 1);
	response->content[response->length] = '\0';
	response->length++;

	// copying the result to resp
	*resp = json_tokener_parse(response->content);

	// memory deallocation
	free(response->content);
	free(response);

	return ret;
}

/**
 * Description:
 *		detcts a face image from calling Face Detect (POST)
 *
 * Params:
 *		param: the query parameter of the request; use NULL for default
 *		body: the request body; needs to include the image url
 *		resp: return parameter; collects the response
 *
 * Return:
 *		http status code; -1 if user hasn't logged in via face_login
 */
long face_detect(struct json_object * param, struct json_object * body, struct json_object ** resp) {
	CURL * curl;					// handle of the libcurl interface
	CURLU * curlu;					// handle for the request url
	CURLcode res;					// result of the curl command
	ReadData * response;			// collects response
	char * url;						// the request url
	char buffer[BUFSIZ] = {0};		// buffer for url parsing
	long ret;						// response http status code
	double lat;						// latency
	
	errno = 0;						// setting errno for error detection

	// check if region and key are initialized
	if (!login) {
		fprintf(stderr, FACE_LOGIN_ERROR);
		return -1;
	}

	curlu = curl_url();

	// setting the request url
	setUriBase(curlu, FACE_DETECT_URL);

	// setting request param
	if (param) {
		json_object_object_foreach(param, key, val) {
			setUriParam(curlu, key, val);
		}
	}

	// retrieving the url from curlu
	curl_url_get(curlu, CURLUPART_URL, &url, CURLU_NON_SUPPORT_SCHEME);

#ifdef _DEBUG_
	fprintf(stderr, FACE_REQUEST_URL, url);
#endif

	response = calloc(1, sizeof(ReadData));

	// loading up libcurl environment
	curl_global_init(CURL_GLOBAL_ALL);

	// loading up curl_easy interface
	curl = curl_easy_init();
	if(curl)
	{
		/* First set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */
		curl_easy_setopt(curl, CURLOPT_URL, url);

		// setting request header
		struct curl_slist * plist = curl_slist_append(NULL, FACE_JSON);
		strcat(buffer, FACE_KEYTYPE);
		strcat(buffer, key);
		plist = curl_slist_append(plist, buffer);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plist);

		// setting request type
		curl_easy_setopt(curl, CURLOPT_POST, 1L);

		// setting posting data (request body)
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_object_to_json_string(body));

		// libcurl will automatically measure the length of request body with
		// strlen so no need to set CURLOPT_POSTFIELDSIZE

		// setting write callback function and buffer
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

		/* Perform the request, res will get the return code */ 
		res = curl_easy_perform(curl);

		/* Check for errors */
		if(res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

		// acquire http status code
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);

#ifdef _DEBUG_
		// acquire latency
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &lat);
#endif

		curl_slist_free_all(plist);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	curl_url_cleanup(curlu);
	curl_global_cleanup();

	// printing http status code
#ifdef _DEBUG_
	fprintf(stderr, FACE_HTTP_STATUS, ret);
	fprintf(stderr, FACE_LATENCY, lat);
#endif

	// null terminating the response string
	response->content = realloc(response->content, response->length + 1);
	response->content[response->length] = '\0';
	response->length++;

	// copying the result to resp
	*resp = json_tokener_parse(response->content);

	// memory deallocation
	free(response->content);
	free(response);

	return ret;
}

/**
 * Description:
 *		detects a face image by calling Face Detect (POST)
 * 
 * Params: 
 *		image: the binary face image file to be detected
 *		fsize: the size of the image file
 *      param: the query parameter of the request; use NULL for default
 *		resp: return parameter; collects the response
 *
 * return:
 *		http status code; -1 if user hasn't logged in with face_login
 */
long face_detect_local(FILE * image, size_t fsize, json_object * param, struct json_object ** resp) {
	CURL * curl;					// handle of the libcurl interface
	CURLU * curlu;					// handle for the request url
	CURLcode res;					// result of the curl command
	ReadData * response;			// collects response
	char * url;						// the request url
	char buffer[BUFSIZ] = {0};		// buffer for url parsing
	long ret;						// response http status code
	double lat;						// latency
	
	errno = 0;						// setting errno for error detection

	// check if region and key are initialized
	if (!login) {
		fprintf(stderr, FACE_LOGIN_ERROR);
		return -1;
	}

	curlu = curl_url();

	// setting the request url
	setUriBase(curlu, FACE_DETECT_URL);

	// setting request parameter
	if (param) {
		json_object_object_foreach(param, key, val) {
			setUriParam(curlu, key, val);
		}
	}

	// retrieving the url from curlu
	curl_url_get(curlu, CURLUPART_URL, &url, CURLU_NON_SUPPORT_SCHEME);

#ifdef _DEBUG_
	fprintf(stderr, FACE_REQUEST_URL, url);
#endif

#ifdef _DEBUG_
	printf("Image file size: %" CURL_FORMAT_CURL_OFF_T " bytes.\n", fsize);
#endif

	response = calloc(1, sizeof(ReadData));

	// loading up libcurl environment
	curl_global_init(CURL_GLOBAL_ALL);

	// loading up curl_easy interface
	curl = curl_easy_init();
	if(curl)
	{
		/* First set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */
		curl_easy_setopt(curl, CURLOPT_URL, url);

		// setting request header
		struct curl_slist * plist = curl_slist_append(NULL, FACE_OCTET);
		strcat(buffer, FACE_KEYTYPE);
		strcat(buffer, key);
		plist = curl_slist_append(plist, buffer);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plist);

		// setting request type
		curl_easy_setopt(curl, CURLOPT_POST, 1L);

		// need to set CURLOPT_POSTFIELD option to NULL for libcurl to get
		// post data from read callback
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, NULL);

		// setting post data size
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, fsize);

		// setting read callback function and data
		curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_image);
		curl_easy_setopt(curl, CURLOPT_READDATA, image);

		// setting write callback function and buffer
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

		/* Perform the request, res will get the return code */ 
		res = curl_easy_perform(curl);

		/* Check for errors */
		if(res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

		// acquire http status code
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);

#ifdef _DEBUG_
		// acquire latency
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &lat);
#endif

		curl_slist_free_all(plist);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	curl_url_cleanup(curlu);
	curl_global_cleanup();

	// printing http status code
#ifdef _DEBUG_
	fprintf(stderr, FACE_HTTP_STATUS, ret);
	fprintf(stderr, FACE_LATENCY, lat);
#endif

	// null terminating the response string
	response->content = realloc(response->content, response->length + 1);
	response->content[response->length] = '\0';
	response->length++;

	// copying the result to resp
	*resp = json_tokener_parse(response->content);

	// memory deallocation
	free(response->content);
	free(response);

	return ret;
}

/**
 * Description:
 *		verifies two face images or a face image with a person by calling
 *		Face Verify (POST)
 * 
 * Params:
 *		body: the request body; should either contain 2 faceIds or 1 faceId,
 *			  1 persongoupId, and 1 personId
 *		resp: return parameter; collects the response
 *
 * return:
 *		http status code; -1 if user hasn't logged in with face_login
 */
long face_verify(struct json_object * body, struct json_object ** resp) {
	CURL * curl;					// handle of the libcurl interface
	CURLU * curlu;					// handle for the request url
	CURLcode res;					// result of the curl command
	ReadData * response;			// collects response
	char * url;						// the request url
	char buffer[BUFSIZ] = {0};		// buffer for url parsing
	long ret;						// response http status code
	double lat;						// latency
	
	errno = 0;						// setting errno for error detection

	// check if region and key are initialized
	if (!login) {
		fprintf(stderr, FACE_LOGIN_ERROR);
		return -1;
	}

	curlu = curl_url();

	// setting the request url
	setUriBase(curlu, FACE_VERIFY_URL);

	// retrieving the url from curlu
	curl_url_get(curlu, CURLUPART_URL, &url, CURLU_NON_SUPPORT_SCHEME);

#ifdef _DEBUG_
	fprintf(stderr, FACE_REQUEST_URL, url);
#endif

	response = calloc(1, sizeof(ReadData));

	// loading up libcurl environment
	curl_global_init(CURL_GLOBAL_ALL);

	// loading up curl_easy interface
	curl = curl_easy_init();
	if(curl)
	{
		/* First set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */
		curl_easy_setopt(curl, CURLOPT_URL, url);

		// setting request header
		struct curl_slist * plist = curl_slist_append(NULL, FACE_JSON);
		strcat(buffer, FACE_KEYTYPE);
		strcat(buffer, key);
		plist = curl_slist_append(plist, buffer);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plist);

		// setting request type
		curl_easy_setopt(curl, CURLOPT_POST, 1L);

		// setting posting data (request body)
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_object_to_json_string(body));

		// libcurl will automatically measure the length of request body with
		// strlen so no need to set CURLOPT_POSTFIELDSIZE

		// setting write callback function and buffer
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

		/* Perform the request, res will get the return code */ 
		res = curl_easy_perform(curl);

		/* Check for errors */
		if(res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

		// acquire http status code
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);

#ifdef _DEBUG_
		// acquire latency
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &lat);
#endif

		curl_slist_free_all(plist);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	curl_url_cleanup(curlu);
	curl_global_cleanup();

	// printing http status code
#ifdef _DEBUG_
	fprintf(stderr, FACE_HTTP_STATUS, ret);
	fprintf(stderr, FACE_LATENCY, lat);
#endif

	// null terminating the response string
	response->content = realloc(response->content, response->length + 1);
	response->content[response->length] = '\0';
	response->length++;

	// copying the result to resp
	*resp = json_tokener_parse(response->content);

	// memory deallocation
	free(response->content);
	free(response);

	return ret;
}

/**
 * Description:
 *		identifies a face image from a persongroup by calling Face Identify (POST)
 * 
 * Params:
 *		body: the request body; should contain 1 faceId, 1 persongoupId,
 *			  and 1 personId
 *		resp: return parameter; collects the response
 *
 * return:
 *		http status code; -1 if user hasn't logged in with face_login
 */
long face_identify(struct json_object * body, struct json_object ** resp) {
	CURL * curl;					// handle of the libcurl interface
	CURLU * curlu;					// handle for the request url
	CURLcode res;					// result of the curl command
	ReadData * response;			// collects response
	char * url;						// the request url
	char buffer[BUFSIZ] = {0};		// buffer for url parsing
	long ret;						// response http status code
	double lat;						// latency
	
	errno = 0;						// setting errno for error detection

	// check if region and key are initialized
	if (!login) {
		fprintf(stderr, FACE_LOGIN_ERROR);
		return -1;
	}

	curlu = curl_url();

	// setting the request url
	setUriBase(curlu, FACE_IDENTIFY_URL);

	// retrieving the url from curlu
	curl_url_get(curlu, CURLUPART_URL, &url, CURLU_NON_SUPPORT_SCHEME);

#ifdef _DEBUG_
	fprintf(stderr, FACE_REQUEST_URL, url);
#endif

	response = calloc(1, sizeof(ReadData));

	// loading up libcurl environment
	curl_global_init(CURL_GLOBAL_ALL);

	// loading up curl_easy interface
	curl = curl_easy_init();
	if(curl)
	{
		/* First set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */
		curl_easy_setopt(curl, CURLOPT_URL, url);

		// setting request header
		struct curl_slist * plist = curl_slist_append(NULL, FACE_JSON);
		strcat(buffer, FACE_KEYTYPE);
		strcat(buffer, key);
		plist = curl_slist_append(plist, buffer);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plist);

		// setting request type
		curl_easy_setopt(curl, CURLOPT_POST, 1L);

		// setting posting data (request body)
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_object_to_json_string(body));

		// libcurl will automatically measure the length of request body with
		// strlen so no need to set CURLOPT_POSTFIELDSIZE

		// setting write callback function and buffer
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

		/* Perform the request, res will get the return code */ 
		res = curl_easy_perform(curl);

		/* Check for errors */
		if(res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

		// acquire http status code
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);

#ifdef _DEBUG_
		// acquire latency
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &lat);
#endif

		curl_slist_free_all(plist);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	curl_url_cleanup(curlu);
	curl_global_cleanup();

	// printing http status code
#ifdef _DEBUG_
	fprintf(stderr, FACE_HTTP_STATUS, ret);
	fprintf(stderr, FACE_LATENCY, lat);
#endif

	// null terminating the response string
	response->content = realloc(response->content, response->length + 1);
	response->content[response->length] = '\0';
	response->length++;

	// copying the result to resp
	*resp = json_tokener_parse(response->content);

	// memory deallocation
	free(response->content);
	free(response);

	return ret;
}

/**
 * Description:
 *		Creates a person by calling PersonGroup Person Create (POST)
 * 
 * Params:
 *		pgid: the persongroupId chosed by the user
 *		body: the request body; should at least contain 1 persongoupId and person name
 *		resp: return parameter; collects the response
 *
 * return:
 *		http status code; -1 if user hasn't logged in with face_login
 */
long face_create_p(char * pgid, struct json_object * body, struct json_object ** resp) {
	CURL * curl;					// handle of the libcurl interface
	CURLU * curlu;					// handle for the request url
	CURLcode res;					// result of the curl command
	ReadData * response;			// collects response
	char * url;						// the request url
	char buffer[BUFSIZ] = {0};		// buffer for url parsing
	long ret;						// response http status code
	double lat;						// latency
	
	errno = 0;						// setting errno for error detection

	// check if region and key are initialized
	if (!login) {
		fprintf(stderr, FACE_LOGIN_ERROR);
		return -1;
	}

	curlu = curl_url();

	// setting the request url
	setUriBase(curlu, FACE_PG_URL);
	strcat(buffer, pgid);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	curl_url_set(curlu, CURLUPART_URL, FACE_URLPART_P, 0);
	memset(buffer, 0, BUFSIZ);

	// retrieving the url from curlu
	curl_url_get(curlu, CURLUPART_URL, &url, CURLU_NON_SUPPORT_SCHEME);

#ifdef _DEBUG_
	fprintf(stderr, FACE_REQUEST_URL, url);
#endif

	response = calloc(1, sizeof(ReadData));

	// loading up libcurl environment
	curl_global_init(CURL_GLOBAL_ALL);

	// loading up curl_easy interface
	curl = curl_easy_init();
	if(curl)
	{
		/* First set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */
		curl_easy_setopt(curl, CURLOPT_URL, url);

		// setting request header
		struct curl_slist * plist = curl_slist_append(NULL, FACE_JSON);
		strcat(buffer, FACE_KEYTYPE);
		strcat(buffer, key);
		plist = curl_slist_append(plist, buffer);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plist);

		// setting request type
		curl_easy_setopt(curl, CURLOPT_POST, 1L);

		// setting posting data (request body)
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_object_to_json_string(body));

		// libcurl will automatically measure the length of request body with
		// strlen so no need to set CURLOPT_POSTFIELDSIZE

		// setting write callback function and buffer
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

		/* Perform the request, res will get the return code */ 
		res = curl_easy_perform(curl);

		/* Check for errors */
		if(res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

		// acquire http status code
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);

#ifdef _DEBUG_
		// acquire latency
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &lat);
#endif

		curl_slist_free_all(plist);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	curl_url_cleanup(curlu);
	curl_global_cleanup();

	// printing http status code and latency
#ifdef _DEBUG_
	fprintf(stderr, FACE_HTTP_STATUS, ret);
	fprintf(stderr, FACE_LATENCY, lat);
#endif

	// null terminating the response string
	response->content = realloc(response->content, response->length + 1);
	response->content[response->length] = '\0';
	response->length++;

	// copying the result to resp
	*resp = json_tokener_parse(response->content);

	// memory deallocation
	free(response->content);
	free(response);

	return ret;
}

/**
 * Description:
 *		Adds a face image to a Person by calling PersonGroup Person Add Face (POST)
 * 
 * Params:
 *		pgid: the persongroupId of the person group the person is located in
 *		pid: the personId of the person the face is added to
 *		param: the query parameter of the request; use NULL for default
 *		body: the request body; should contain the image url
 *		resp: return parameter; collects the response
 *
 * return:
 *		http status code; -1 if user hasn't logged in with face_login
 */
long face_add_face(char * pgid, char * pid, struct json_object * param, struct json_object * body, struct json_object ** resp) {
	CURL * curl;					// handle of the libcurl interface
	CURLU * curlu;					// handle for the request url
	CURLcode res;					// result of the curl command
	ReadData * response;			// collects response
	char * url;						// the request url
	char buffer[BUFSIZ] = {0};		// buffer for url parsing
	long ret;						// response http status code
	double lat;						// latency
	
	errno = 0;						// setting errno for error detection

	// check if region and key are initialized
	if (!login) {
		fprintf(stderr, FACE_LOGIN_ERROR);
		return -1;
	}

	curlu = curl_url();

	// setting the request url
	setUriBase(curlu, FACE_PG_URL);
	strcat(buffer, pgid);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	strcat(buffer, FACE_URLPART_P);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	strcat(buffer, pid);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	curl_url_set(curlu, CURLUPART_URL, FACE_URLPART_FACE, 0);


	// setting request param
	if (param) {
		json_object_object_foreach(param, key, val) {
			setUriParam(curlu, key, val);
		}
	}

	// retrieving the url from curlu
	curl_url_get(curlu, CURLUPART_URL, &url, CURLU_NON_SUPPORT_SCHEME);

#ifdef _DEBUG_
	fprintf(stderr, FACE_REQUEST_URL, url);
#endif

	response = calloc(1, sizeof(ReadData));

	// loading up libcurl environment
	curl_global_init(CURL_GLOBAL_ALL);

	// loading up curl_easy interface
	curl = curl_easy_init();
	if(curl)
	{
		/* First set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */
		curl_easy_setopt(curl, CURLOPT_URL, url);

		// setting request header
		struct curl_slist * plist = curl_slist_append(NULL, FACE_JSON);
		strcat(buffer, FACE_KEYTYPE);
		strcat(buffer, key);
		plist = curl_slist_append(plist, buffer);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plist);

		// setting request type
		curl_easy_setopt(curl, CURLOPT_POST, 1L);

		// setting posting data (request body)
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_object_to_json_string(body));

		// libcurl will automatically measure the length of request body with
		// strlen so no need to set CURLOPT_POSTFIELDSIZE

		// setting write callback function and buffer
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

		/* Perform the request, res will get the return code */ 
		res = curl_easy_perform(curl);

		/* Check for errors */
		if(res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

		// acquire http status code
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);

#ifdef _DEBUG_
		// acquire latency
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &lat);
#endif

		curl_slist_free_all(plist);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	curl_url_cleanup(curlu);
	curl_global_cleanup();

	// printing http status code and latency
#ifdef _DEBUG_
	fprintf(stderr, FACE_HTTP_STATUS, ret);
	fprintf(stderr, FACE_LATENCY, lat);
#endif

	// null terminating the response string
	response->content = realloc(response->content, response->length + 1);
	response->content[response->length] = '\0';
	response->length++;

	// copying the result to resp
	*resp = json_tokener_parse(response->content);

	// memory deallocation
	free(response->content);
	free(response);

	return ret;
}

/**
 * Description:
 *		Adds a face image to a Person by calling PersonGroup Person Add Face (POST)
 * 
 * Params:
 *		image: the binary data of the face image
 *		fsize: the size of the image
 *		pgid: the persongroupId of the person group the person is located in
 *		pid: the personId of the person the face is added to
 *		param: the query parameter of the request; use NULL for default
 *		resp: return parameter; collects the response
 *
 * return:
 *		http status code; -1 if user hasn't logged in with face_login
 */
long face_add_face_local(FILE * image, size_t fsize, char * pgid, char * pid, struct json_object * param, struct json_object ** resp) {
	CURL * curl;					// handle of the libcurl interface
	CURLU * curlu;					// handle for the request url
	CURLcode res;					// result of the curl command
	ReadData * response;			// collects response
	char * url;						// the request url
	char buffer[BUFSIZ] = {0};		// buffer for url parsing
	long ret;						// response http status code
	double lat;						// latency
	
	errno = 0;						// setting errno for error detection

	// check if region and key are initialized
	if (!login) {
		fprintf(stderr, FACE_LOGIN_ERROR);
		return -1;
	}

	curlu = curl_url();

	// setting the request url
	setUriBase(curlu, FACE_PG_URL);
	strcat(buffer, pgid);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	strcat(buffer, FACE_URLPART_P);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	strcat(buffer, pid);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	curl_url_set(curlu, CURLUPART_URL, FACE_URLPART_FACE, 0);

	// setting request parameter
	if (param) {
		json_object_object_foreach(param, key, val) {
			setUriParam(curlu, key, val);
		}
	}

	// retrieving the url from curlu
	curl_url_get(curlu, CURLUPART_URL, &url, CURLU_NON_SUPPORT_SCHEME);

#ifdef _DEBUG_
	fprintf(stderr, FACE_REQUEST_URL, url);
#endif

#ifdef _DEBUG_
	printf("Image file size: %" CURL_FORMAT_CURL_OFF_T " bytes.\n", fsize);
#endif

	response = calloc(1, sizeof(ReadData));

	// loading up libcurl environment
	curl_global_init(CURL_GLOBAL_ALL);

	// loading up curl_easy interface
	curl = curl_easy_init();
	if(curl)
	{
		/* First set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */
		curl_easy_setopt(curl, CURLOPT_URL, url);

		// setting request header
		struct curl_slist * plist = curl_slist_append(NULL, FACE_OCTET);
		strcat(buffer, FACE_KEYTYPE);
		strcat(buffer, key);
		plist = curl_slist_append(plist, buffer);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plist);

		// setting request type
		curl_easy_setopt(curl, CURLOPT_POST, 1L);

		// need to set CURLOPT_POSTFIELD option to NULL for libcurl to get
		// post data from read callback
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, NULL);

		// setting post data size
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, fsize);

		// setting read callback function and data
		curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_image);
		curl_easy_setopt(curl, CURLOPT_READDATA, image);

		// setting write callback function and buffer
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

		/* Perform the request, res will get the return code */ 
		res = curl_easy_perform(curl);

		/* Check for errors */
		if(res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

		// acquire http status code
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);

#ifdef _DEBUG_
		// acquire latency
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &lat);
#endif

		curl_slist_free_all(plist);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	curl_url_cleanup(curlu);
	curl_global_cleanup();

	// printing http status code and latency
#ifdef _DEBUG_
	fprintf(stderr, FACE_HTTP_STATUS, ret);
	fprintf(stderr, FACE_LATENCY, lat);
#endif

	// null terminating the response string
	response->content = realloc(response->content, response->length + 1);
	response->content[response->length] = '\0';
	response->length++;

	// copying the result to resp
	*resp = json_tokener_parse(response->content);

	// memory deallocation
	free(response->content);
	free(response);

	return ret;
}

/**
 * Description:
 *		Deletes a face image by calling PersonGroup Person Delete Face (DELETE)
 * 
 * Params:
 *		pgid: the persongroupId of the person group the person is located in
 *		pid: the personId of the person the face is deleted from
 *		fid: the faceId of the face to be deleted
 *		resp: return parameter; collects the response
 *
 * return:
 *		http status code; -1 if user hasn't logged in with face_login
 */
long face_delete_face(char * pgid, char * pid, char * fid, struct json_object ** resp) {
	CURL * curl;					// handle of the libcurl interface
	CURLU * curlu;					// handle for the request url
	CURLcode res;					// result of the curl command
	ReadData * response;			// collects response
	char * url;						// the request url
	char buffer[BUFSIZ] = {0};		// buffer for url parsing
	long ret;						// response http status code
	double lat;						// latency
	
	errno = 0;						// setting errno for error detection

	// check if region and key are initialized
	if (!login) {
		fprintf(stderr, FACE_LOGIN_ERROR);
		return -1;
	}

	curlu = curl_url();

	// setting the request url
	setUriBase(curlu, FACE_PG_URL);
	strcat(buffer, pgid);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	strcat(buffer, FACE_URLPART_P);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	strcat(buffer, pid);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	strcat(buffer, FACE_URLPART_FACE);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	curl_url_set(curlu, CURLUPART_URL, fid, 0);

	// retrieving the url from curlu
	curl_url_get(curlu, CURLUPART_URL, &url, CURLU_NON_SUPPORT_SCHEME);

#ifdef _DEBUG_
	fprintf(stderr, FACE_REQUEST_URL, url);
#endif

	response = calloc(1, sizeof(ReadData));

	// loading up libcurl environment
	curl_global_init(CURL_GLOBAL_ALL);

	// loading up curl_easy interface
	curl = curl_easy_init();
	if(curl)
	{
		/* First set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */
		curl_easy_setopt(curl, CURLOPT_URL, url);

		// setting request header
		strcat(buffer, FACE_KEYTYPE);
		strcat(buffer, key);
		struct curl_slist * plist = curl_slist_append(NULL, buffer);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plist);

		// setting request type
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, FACE_DELETE);

		// setting write callback function and buffer
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

		/* Perform the request, res will get the return code */ 
		res = curl_easy_perform(curl);

		/* Check for errors */
		if(res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

		// acquire http status code
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);

#ifdef _DEBUG_
		// acquire latency
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &lat);
#endif

		curl_slist_free_all(plist);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	curl_url_cleanup(curlu);
	curl_global_cleanup();

	// printing http status code
#ifdef _DEBUG_
	fprintf(stderr, FACE_HTTP_STATUS, ret);
	fprintf(stderr, FACE_LATENCY, lat);
#endif

	// null terminating the response string
	response->content = realloc(response->content, response->length + 1);
	response->content[response->length] = '\0';
	response->length++;

	// copying the result to resp
	*resp = json_tokener_parse(response->content);

	// memory deallocation
	free(response->content);
	free(response);

	return ret;
}

/**
 * Description:
 *		Deletes a Person by calling PersonGroup Person Delete (DELETE)
 * 
 * Params:
 *		pgid: the persongroupId of the person group the person is located in
 *		pid: the personId of the person to be deleted
 *		resp: return parameter; collects the response
 *
 * return:
 *		http status code; -1 if user hasn't logged in with face_login
 */
long face_delete_p(char * pgid, char * pid, struct json_object ** resp) {
	CURL * curl;					// handle of the libcurl interface
	CURLU * curlu;					// handle for the request url
	CURLcode res;					// result of the curl command
	ReadData * response;			// collects response
	char * url;						// the request url
	char buffer[BUFSIZ] = {0};		// buffer for url parsing
	long ret;						// response http status code
	double lat;						// latency
	
	errno = 0;						// setting errno for error detection

	// check if region and key are initialized
	if (!login) {
		fprintf(stderr, FACE_LOGIN_ERROR);
		return -1;
	}

	curlu = curl_url();

	// setting the request url
	setUriBase(curlu, FACE_PG_URL);
	strcat(buffer, pgid);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	strcat(buffer, FACE_URLPART_P);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	curl_url_set(curlu, CURLUPART_URL, pid, 0);

	// retrieving the url from curlu
	curl_url_get(curlu, CURLUPART_URL, &url, CURLU_NON_SUPPORT_SCHEME);

#ifdef _DEBUG_
	fprintf(stderr, FACE_REQUEST_URL, url);
#endif

	response = calloc(1, sizeof(ReadData));

	// loading up libcurl environment
	curl_global_init(CURL_GLOBAL_ALL);

	// loading up curl_easy interface
	curl = curl_easy_init();
	if(curl)
	{
		/* First set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */
		curl_easy_setopt(curl, CURLOPT_URL, url);

		// setting request header
		strcat(buffer, FACE_KEYTYPE);
		strcat(buffer, key);
		struct curl_slist * plist = curl_slist_append(NULL, buffer);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plist);

		// setting request type
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, FACE_DELETE);

		// setting write callback function and buffer
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

		/* Perform the request, res will get the return code */ 
		res = curl_easy_perform(curl);

		/* Check for errors */
		if(res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

		// acquire http status code
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);

#ifdef _DEBUG_
		// acquire latency
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &lat);
#endif

		curl_slist_free_all(plist);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	curl_url_cleanup(curlu);
	curl_global_cleanup();

	// printing http status code and latency
#ifdef _DEBUG_
	fprintf(stderr, FACE_HTTP_STATUS, ret);
	fprintf(stderr, FACE_LATENCY, lat);
#endif

	// null terminating the response string
	response->content = realloc(response->content, response->length + 1);
	response->content[response->length] = '\0';
	response->length++;

	// copying the result to resp
	*resp = json_tokener_parse(response->content);

	// memory deallocation
	free(response->content);
	free(response);

	return ret;
}

/**
 * Description:
 *		Deletes a Person Group by calling PersonGroup Delete (DELETE)
 * 
 * Params:
 *		pgid: the persongroupId of the person group to be deleted
 *		resp: return parameter; collects the response
 *
 * return:
 *		http status code; -1 if user hasn't logged in with face_login
 */
long face_delete_pg(char * pgid, struct json_object ** resp) {
	CURL * curl;					// handle of the libcurl interface
	CURLU * curlu;					// handle for the request url
	CURLcode res;					// result of the curl command
	ReadData * response;			// collects response
	char * url;						// the request url
	char buffer[BUFSIZ] = {0};		// buffer for url parsing
	long ret;						// response http status code
	double lat;						// latency
	
	errno = 0;						// setting errno for error detection

	// check if region and key are initialized
	if (!login) {
		fprintf(stderr, FACE_LOGIN_ERROR);
		return -1;
	}

	curlu = curl_url();

	// setting the request url
	setUriBase(curlu, FACE_PG_URL);
	curl_url_set(curlu, CURLUPART_URL, pgid, 0);

	// retrieving the url from curlu
	curl_url_get(curlu, CURLUPART_URL, &url, CURLU_NON_SUPPORT_SCHEME);

#ifdef _DEBUG_
	fprintf(stderr, FACE_REQUEST_URL, url);
#endif

	response = calloc(1, sizeof(ReadData));

	// loading up libcurl environment
	curl_global_init(CURL_GLOBAL_ALL);

	// loading up curl_easy interface
	curl = curl_easy_init();
	if(curl)
	{
		/* First set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */
		curl_easy_setopt(curl, CURLOPT_URL, url);

		// setting request header
		strcat(buffer, FACE_KEYTYPE);
		strcat(buffer, key);
		struct curl_slist * plist = curl_slist_append(NULL, buffer);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plist);

		// setting request type
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, FACE_DELETE);

		// setting write callback function and buffer
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

		/* Perform the request, res will get the return code */ 
		res = curl_easy_perform(curl);

		/* Check for errors */
		if(res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

		// acquire http status code
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);

#ifdef _DEBUG_
		// acquire latency
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &lat);
#endif

		curl_slist_free_all(plist);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	curl_url_cleanup(curlu);
	curl_global_cleanup();

	// printing http status code and latency
#ifdef _DEBUG_
	fprintf(stderr, FACE_HTTP_STATUS, ret);
	fprintf(stderr, FACE_LATENCY, lat);
#endif

	// null terminating the response string
	response->content = realloc(response->content, response->length + 1);
	response->content[response->length] = '\0';
	response->length++;

	// copying the result to resp
	*resp = json_tokener_parse(response->content);

	// memory deallocation
	free(response->content);
	free(response);

	return ret;
}

/**
 * Description:
 *		Gets a Person Group's information by calling PersonGroup Get (GET)
 * 
 * Params:
 *		pgid: the persongroupId of the person group to get information from
 *		resp: return parameter; collects the response
 *
 * return:
 *		http status code; -1 if user hasn't logged in with face_login
 */
long face_get_pg(char * pgid, struct json_object ** resp) {
	CURL * curl;					// handle of the libcurl interface
	CURLU * curlu;					// handle for the request url
	CURLcode res;					// result of the curl command
	ReadData * response;			// collects response
	char * url;						// the request url
	char buffer[BUFSIZ] = {0};		// buffer for url parsing
	long ret;						// response http status code
	double lat;						// latency
	
	errno = 0;						// setting errno for error detection

	// check if region and key are initialized
	if (!login) {
		fprintf(stderr, FACE_LOGIN_ERROR);
		return -1;
	}

	curlu = curl_url();

	// setting the request url
	setUriBase(curlu, FACE_PG_URL);
	curl_url_set(curlu, CURLUPART_URL, pgid, 0);

	// hard coding request parameter to always return recognition model
	curl_url_set(curlu, CURLUPART_QUERY, FACE_URLPART_GET_PG_PARAM, CURLU_APPENDQUERY);

	// retrieving the url from curlu
	curl_url_get(curlu, CURLUPART_URL, &url, CURLU_NON_SUPPORT_SCHEME);

#ifdef _DEBUG_
	fprintf(stderr, FACE_REQUEST_URL, url);
#endif

	response = calloc(1, sizeof(ReadData));

	// loading up libcurl environment
	curl_global_init(CURL_GLOBAL_ALL);

	// loading up curl_easy interface
	curl = curl_easy_init();
	if(curl)
	{
		/* First set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */
		curl_easy_setopt(curl, CURLOPT_URL, url);

		// setting request header
		strcat(buffer, FACE_KEYTYPE);
		strcat(buffer, key);
		struct curl_slist * plist = curl_slist_append(NULL, buffer);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plist);

		// setting request type
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);

		// setting write callback function and buffer
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

		/* Perform the request, res will get the return code */ 
		res = curl_easy_perform(curl);

		/* Check for errors */
		if(res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

		// acquire http status code
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);

#ifdef _DEBUG_
		// acquire latency
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &lat);
#endif

		curl_slist_free_all(plist);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	curl_url_cleanup(curlu);
	curl_global_cleanup();

	// printing http status code and latency
#ifdef _DEBUG_
	fprintf(stderr, FACE_HTTP_STATUS, ret);
	fprintf(stderr, FACE_LATENCY, lat);
#endif

	// null terminating the response string
	response->content = realloc(response->content, response->length + 1);
	response->content[response->length] = '\0';
	response->length++;

	// copying the result to resp
	*resp = json_tokener_parse(response->content);

	// memory deallocation
	free(response->content);
	free(response);

	return ret;
}

/**
 * Description:
 *		Gets a Person's information by calling PersonGroup Person Get (GET)
 * 
 * Params:
 *		pgid: the persongroupId of the person group the person is located in
 *		pid: the personId of the person to get information from
 *		resp: return parameter; collects the response
 *
 * return:
 *		http status code; -1 if user hasn't logged in with face_login
 */
long face_get_p(char * pgid, char * pid, struct json_object ** resp) {
	CURL * curl;					// handle of the libcurl interface
	CURLU * curlu;					// handle for the request url
	CURLcode res;					// result of the curl command
	ReadData * response;			// collects response
	char * url;						// the request url
	char buffer[BUFSIZ] = {0};		// buffer for url parsing
	long ret;						// response http status code
	double lat;						// latency
	
	errno = 0;						// setting errno for error detection

	// check if region and key are initialized
	if (!login) {
		fprintf(stderr, FACE_LOGIN_ERROR);
		return -1;
	}

	curlu = curl_url();

	// setting the request url
	setUriBase(curlu, FACE_PG_URL);
	strcat(buffer, pgid);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	strcat(buffer, FACE_URLPART_P);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	curl_url_set(curlu, CURLUPART_URL, pid, 0);

	// retrieving the url from curlu
	curl_url_get(curlu, CURLUPART_URL, &url, CURLU_NON_SUPPORT_SCHEME);

#ifdef _DEBUG_
	fprintf(stderr, FACE_REQUEST_URL, url);
#endif

	response = calloc(1, sizeof(ReadData));

	// loading up libcurl environment
	curl_global_init(CURL_GLOBAL_ALL);

	// loading up curl_easy interface
	curl = curl_easy_init();
	if(curl)
	{
		/* First set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */
		curl_easy_setopt(curl, CURLOPT_URL, url);

		// setting request header
		strcat(buffer, FACE_KEYTYPE);
		strcat(buffer, key);
		struct curl_slist * plist = curl_slist_append(NULL, buffer);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plist);

		// setting request type
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);

		// setting write callback function and buffer
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

		/* Perform the request, res will get the return code */ 
		res = curl_easy_perform(curl);

		/* Check for errors */
		if(res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

		// acquire http status code
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);

#ifdef _DEBUG_
		// acquire latency
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &lat);
#endif

		curl_slist_free_all(plist);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	curl_url_cleanup(curlu);
	curl_global_cleanup();

	// printing http status code and latency
#ifdef _DEBUG_
	fprintf(stderr, FACE_HTTP_STATUS, ret);
	fprintf(stderr, FACE_LATENCY, lat);
#endif

	// null terminating the response string
	response->content = realloc(response->content, response->length + 1);
	response->content[response->length] = '\0';
	response->length++;

	// copying the result to resp
	*resp = json_tokener_parse(response->content);

	// memory deallocation
	free(response->content);
	free(response);

	return ret;
}

/**
 * Description:
 *		Gets a Face's information by calling PersonGroup Person Get Face (GET)
 * 
 * Params:
 *		pgid: the persongroupId of the person group the person is located in
 *		pid: the personId of the person that owns the face
 *		fid: the faceId of the face to get information from
 *		resp: return parameter; collects the response
 *
 * return:
 *		http status code; -1 if user hasn't logged in with face_login
 */
long face_get_face(char * pgid, char * pid, char * fid, struct json_object ** resp) {
	CURL * curl;					// handle of the libcurl interface
	CURLU * curlu;					// handle for the request url
	CURLcode res;					// result of the curl command
	ReadData * response;			// collects response
	char * url;						// the request url
	char buffer[BUFSIZ] = {0};		// buffer for url parsing
	long ret;						// response http status code
	double lat;						// latency

	errno = 0;						// setting errno for error detection

	// check if region and key are initialized
	if (!login) {
		fprintf(stderr, FACE_LOGIN_ERROR);
		return -1;
	}

	curlu = curl_url();

	// setting the request url
	setUriBase(curlu, FACE_PG_URL);
	strcat(buffer, pgid);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	strcat(buffer, FACE_URLPART_P);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	strcat(buffer, pid);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	strcat(buffer, FACE_URLPART_FACE);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	curl_url_set(curlu, CURLUPART_URL, fid, 0);

	// retrieving the url from curlu
	curl_url_get(curlu, CURLUPART_URL, &url, CURLU_NON_SUPPORT_SCHEME);

#ifdef _DEBUG_
	fprintf(stderr, FACE_REQUEST_URL, url);
#endif

	response = calloc(1, sizeof(ReadData));

	// loading up libcurl environment
	curl_global_init(CURL_GLOBAL_ALL);

	// loading up curl_easy interface
	curl = curl_easy_init();
	if(curl)
	{
		/* First set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */
		curl_easy_setopt(curl, CURLOPT_URL, url);

		// setting request header
		strcat(buffer, FACE_KEYTYPE);
		strcat(buffer, key);
		struct curl_slist * plist = curl_slist_append(NULL, buffer);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plist);

		// setting request type
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);

		// setting write callback function and buffer
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

		/* Perform the request, res will get the return code */ 
		res = curl_easy_perform(curl);

		/* Check for errors */
		if(res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

		// acquire http status code
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);

#ifdef _DEBUG_
		// acquire latency
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &lat);
#endif

		curl_slist_free_all(plist);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	curl_url_cleanup(curlu);
	curl_global_cleanup();

	// printing http status code and latency
#ifdef _DEBUG_
	fprintf(stderr, FACE_HTTP_STATUS, ret);
	fprintf(stderr, FACE_LATENCY, lat);
#endif

	// null terminating the response string
	response->content = realloc(response->content, response->length + 1);
	response->content[response->length] = '\0';
	response->length++;

	// copying the result to resp
	*resp = json_tokener_parse(response->content);

	// memory deallocation
	free(response->content);
	free(response);

	return ret;
}

/**
 * Description:
 *		Trains a person group by calling PersonGroup Train (POST)
 * 
 * Params:
 *		pgid: the persongroupId of the person group to be trained
 *		resp: return parameter; collects the response
 *
 * return:
 *		http status code; -1 if user hasn't logged in with face_login
 */
long face_train_pg(char * pgid, struct json_object ** resp) {
	CURL * curl;					// handle of the libcurl interface
	CURLU * curlu;					// handle for the request url
	CURLcode res;					// result of the curl command
	ReadData * response;			// collects response
	char * url;						// the request url
	char buffer[BUFSIZ] = {0};		// buffer for url parsing
	long ret;						// response http status code
	double lat;						// latency

	errno = 0;						// setting errno for error detection

	// check if region and key are initialized
	if (!login) {
		fprintf(stderr, FACE_LOGIN_ERROR);
		return -1;
	}

	curlu = curl_url();

	// setting the request url
	setUriBase(curlu, FACE_PG_URL);
	strcat(buffer, pgid);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	curl_url_set(curlu, CURLUPART_URL, FACE_URLPART_TRAIN, 0);

	// retrieving the url from curlu
	curl_url_get(curlu, CURLUPART_URL, &url, CURLU_NON_SUPPORT_SCHEME);

#ifdef _DEBUG_
	fprintf(stderr, FACE_REQUEST_URL, url);
#endif

	response = calloc(1, sizeof(ReadData));

	// loading up libcurl environment
	curl_global_init(CURL_GLOBAL_ALL);

	// loading up curl_easy interface
	curl = curl_easy_init();
	if(curl)
	{
		/* First set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */
		curl_easy_setopt(curl, CURLOPT_URL, url);

		// setting request header
		strcat(buffer, FACE_KEYTYPE);
		strcat(buffer, key);
		struct curl_slist * plist = curl_slist_append(NULL, buffer);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plist);

		// setting request type
		curl_easy_setopt(curl, CURLOPT_POST, 1L);

		// explicitly setting post field size because CURLOPT_POSTFIELD is not
		// set, thus, is not capable of setting field size
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0);

		// setting write callback function and buffer
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

		/* Perform the request, res will get the return code */ 
		res = curl_easy_perform(curl);

		/* Check for errors */
		if(res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

		// acquire http status code
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);

#ifdef _DEBUG_
		// acquire latency
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &lat);
#endif

		curl_slist_free_all(plist);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	curl_url_cleanup(curlu);
	curl_global_cleanup();

	// printing http status code and latency
#ifdef _DEBUG_
	fprintf(stderr, FACE_HTTP_STATUS, ret);
	fprintf(stderr, FACE_LATENCY, lat);
#endif

	// null terminating the response string
	response->content = realloc(response->content, response->length + 1);
	response->content[response->length] = '\0';
	response->length++;

	// copying the result to resp
	*resp = json_tokener_parse(response->content);

	// memory deallocation
	free(response->content);
	free(response);

	return ret;
}

/**
 * Description:
 *		Lists all the person in a person group by calling PersonGroup Person
 *		List (GET)
 * 
 * Params:
 *		pgid: the persongroupId of the person group to list persons from
 *		resp: return parameter; collects the response
 *
 * return:
 *		http status code; -1 if user hasn't logged in with face_login
 */
long face_list_p(char * pgid, struct json_object ** resp) {
	CURL * curl;					// handle of the libcurl interface
	CURLU * curlu;					// handle for the request url
	CURLcode res;					// result of the curl command
	ReadData * response;			// collects response
	char * url;						// the request url
	char buffer[BUFSIZ] = {0};		// buffer for url parsing
	long ret;						// response http status code
	double lat;						// latency
	
	errno = 0;						// setting errno for error detection

	// check if region and key are initialized
	if (!login) {
		fprintf(stderr, FACE_LOGIN_ERROR);
		return -1;
	}

	curlu = curl_url();

	// setting the request url
	setUriBase(curlu, FACE_PG_URL);
	strcat(buffer, pgid);
	strcat(buffer, FACE_SLASH);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	memset(buffer, 0, BUFSIZ);
	curl_url_set(curlu, CURLUPART_URL, FACE_URLPART_P, 0);

	// retrieving the url from curlu
	curl_url_get(curlu, CURLUPART_URL, &url, CURLU_NON_SUPPORT_SCHEME);

#ifdef _DEBUG_
	fprintf(stderr, FACE_REQUEST_URL, url);
#endif

	response = calloc(1, sizeof(ReadData));

	// loading up libcurl environment
	curl_global_init(CURL_GLOBAL_ALL);

	// loading up curl_easy interface
	curl = curl_easy_init();
	if(curl)
	{
		/* First set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */
		curl_easy_setopt(curl, CURLOPT_URL, url);

		// setting request header
		strcat(buffer, FACE_KEYTYPE);
		strcat(buffer, key);
		struct curl_slist * plist = curl_slist_append(NULL, buffer);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plist);

		// setting request type
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);

		// setting write callback function and buffer
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

		/* Perform the request, res will get the return code */ 
		res = curl_easy_perform(curl);

		/* Check for errors */
		if(res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

		// acquire http status code
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);

#ifdef _DEBUG_
		// acquire latency
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &lat);
#endif

		curl_slist_free_all(plist);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	curl_url_cleanup(curlu);
	curl_global_cleanup();

	// printing http status code and latency
#ifdef _DEBUG_
	fprintf(stderr, FACE_HTTP_STATUS, ret);
	fprintf(stderr, FACE_LATENCY, lat);
#endif

	// null terminating the response string
	response->content = realloc(response->content, response->length + 1);
	response->content[response->length] = '\0';
	response->length++;

	// copying the result to resp
	*resp = json_tokener_parse(response->content);

	// memory deallocation
	free(response->content);
	free(response);

	return ret;
}

int _demo_register(FILE * image, size_t fsize, Table * table) {
	char * pgName = "demo_group_1";			// default persongroup name	
	char * pName = "demo_person";			// default person name
	json_object * body;						// request body
	json_object * param;					// request parameter
	json_object * detect_resp;				// response from detect
	json_object * resp;						// response from api call
	json_object * tmp_obj;					// temporary json object
	long status;							// HTTP request status code
	int len;								// length of the response array
	RegResult reg_result = {0};				// stores result of registration
	char buffer[BUFSIZ] = {0};				// string buffer
	int i;

	// creating the request body for create_pg
	body = json_object_new_object();
	json_object_object_add(body, "name", json_object_new_string(pgName));

	// creating the default person group if it is not already created
	face_create_pg(FACE_DEMO_PGID, body, &resp);

	// detect the image to check for number and location of faces
	status = face_detect_local(image, fsize, NULL, &detect_resp);

	if(detect_resp != NULL && table != NULL && statusOk(status)){
		len = json_object_array_length(detect_resp);
		for (i = 0; i < len; ++i) {
			json_object * mem = NULL;
			json_object *detect_rect = NULL;
			mem = json_object_array_get_idx(detect_resp, i);
			if(mem != NULL){
				json_object *top = NULL, *left = NULL, *width = NULL, *height = NULL;
				json_object_object_get_ex(mem, "faceRectangle", &detect_rect);
				if(detect_rect != NULL){
					json_object_object_get_ex(detect_rect, "top", &top);
					json_object_object_get_ex(detect_rect, "left", &left);
					json_object_object_get_ex(detect_rect, "width", &width);
					json_object_object_get_ex(detect_rect, "height", &height);
					if(top != NULL && left != NULL && width != NULL && height != NULL){
						reg_result.rt.x = json_object_get_int(left);
						reg_result.rt.y = json_object_get_int(top);
						reg_result.rt.width = json_object_get_int(width);
						reg_result.rt.height = json_object_get_int(height);
					}
					else
						printf("top or left or width or height is null\n");
				}
				else
					printf("detect_rect is null\n");
			}
			else
				printf("mem is null\n");

			// creating the request body for create_p
			json_object_put(body);
			body = json_object_new_object();
			json_object_object_add(body, "name", json_object_new_string(pName));

			// creating a new person for the face image
			status = face_create_p(FACE_DEMO_PGID, body, &resp);

			if (!statusOk(status)) {
				printf("Create Person fail:\n");
				printf("%s\n", json_object_to_json_string(resp));
				json_object_put(body);
				return -1;
			}

			// extracting the personId from the response of create_p
			json_object_object_get_ex(resp, FACE_PID, &tmp_obj);
			strcpy(reg_result.pid, json_object_get_string(tmp_obj));

			// building add face's param
			param = json_object_new_object();
			sprintf(buffer, "%d,%d,%d,%d", reg_result.rt.x, reg_result.rt.y, reg_result.rt.width, reg_result.rt.height);
			tmp_obj = json_object_new_string(buffer);
			memset(buffer, 0, BUFSIZ);
			json_object_object_add(param, "targetFace", tmp_obj);

			// adding face image to the person just created
			fseek(image, 0, SEEK_SET);
			status = face_add_face_local(image, fsize, FACE_DEMO_PGID, reg_result.pid, param, &resp);

			if (!statusOk(status)) {
				printf("Add face fail:\n");
				printf("%s\n", json_object_to_json_string(resp));
				return -1;
			}

			printf("Register successful\npid: %s\n", reg_result.pid);

			table->append(table, &reg_result);
			memset(&reg_result, 0, sizeof(RegResult));
		}
	}



	// training the person group after a new person is added
	status = face_train_pg(FACE_DEMO_PGID, &resp);

	if (!statusOk(status)) {
		printf("train fail:\n");
		printf("%s\n", json_object_to_json_string(resp));
		return -1;
	}

	// deallocating json_objects
	json_object_put(body);
	json_object_put(resp);
	json_object_put(detect_resp);
	json_object_put(tmp_obj);

	return 0;
}

int _demo_detect(FILE * image, size_t fsize, Table * table) {
	json_object * param = NULL;			// request paramete
	json_object * resp = NULL; 			// response from api call
	int flag;							// flag for printing json
	long status;						// status code of the HTTP request
	DetectResult detect_result = {0};	// stores detection result
	int i;								// foreach iterator
	int len;							// response json array length
	int ret = 0;						// return code

	// setting preferred print option
	flag = JSON_C_TO_STRING_PRETTY;

	// setting the request parameter for detect
	param = json_object_new_object();
	json_object_object_add(param, FACE_DEMO_DETECT_PARAM,
		json_object_new_string(FACE_DEMO_FACE_ATTR));

	// detect the face image
	status = face_detect_local(image, fsize, param, &resp);

	if(resp != NULL && table != NULL && statusOk(status)){
		len = json_object_array_length(resp);
		for (i = 0; i < len; ++i) {
			json_object * mem = NULL;
			json_object *detect_rect = NULL, *detect_attr = NULL;
			mem = json_object_array_get_idx(resp, i);
			if(mem != NULL){
				json_object *top = NULL, *left = NULL, *width = NULL, *height = NULL;
				json_object *gender = NULL, *age = NULL;
				json_object_object_get_ex(mem, "faceRectangle", &detect_rect);
				if(detect_rect != NULL){
					json_object_object_get_ex(detect_rect, "top", &top);
					json_object_object_get_ex(detect_rect, "left", &left);
					json_object_object_get_ex(detect_rect, "width", &width);
					json_object_object_get_ex(detect_rect, "height", &height);
					if(top != NULL && left != NULL && width != NULL && height != NULL){
						detect_result.rt.x = json_object_get_int(left);
						detect_result.rt.y = json_object_get_int(top);
						detect_result.rt.width = json_object_get_int(width);
						detect_result.rt.height = json_object_get_int(height);
				}
					else
						printf("top or left or width or height is null\n");
				}
				else
					printf("detect_rect is null\n");

				json_object_object_get_ex(mem, "faceAttributes", &detect_attr);
				if (detect_attr != NULL) {
					json_object_object_get_ex(detect_attr, "gender", &gender);
					json_object_object_get_ex(detect_attr, "age", &age);
					if (gender && age) {
						strcpy(detect_result.attr.gender, json_object_get_string(gender));
						detect_result.attr.age = json_object_get_double(age);
					}
					else
						printf("gender or age is null\n");
				}
				else
					printf("face_attr is null\n");
			}
			else
				printf("val is null\n");

			table->append(table, &detect_result);
			memset(&detect_result, 0, sizeof(DetectResult));
		}
	}
	else {
		printf("HTTP status code indicate error/resp or face_result is null\n");
		ret = -1;
	}

	// printing the result of detect
	printf(FACE_DEMO_PRINT_FACE, json_object_to_json_string_ext(resp, flag));

	// deallocating json_objects
	json_object_put(param);
	json_object_put(resp);
	return ret;
}

int _demo_identify(FILE * image, size_t fsize, Table * table) {
	json_object * body = NULL;				// request body
	json_object * tmp_obj = NULL;			// temporary json object
	json_object * tmp_obj2 = NULL;			// second temp json object
	json_object * faceIds = NULL;			// faceIds array used for identify
	char fid[BUFSIZ] = {0};					// stores faceId
	long detect_status, ident_status;		// HTTP request status code
	int i;									// foreach iterator
	int len;								// length of the response array
	json_object *detect_resp = NULL, *ident_resp = NULL;
	IdentResult ident_result = {0};			// stores identification result

	// detect the face image to acquire its faceId
	detect_status = face_detect_local(image, fsize, NULL, &detect_resp);

	// testing
	printf("test _demo_identify\n");

	// set the length of the response array
	if (statusOk(detect_status)) {
		len = json_object_array_length(detect_resp);
	}
	else {
		// show that face_detect_local returned error
		fprintf(stderr, "face_detect_local fail\n");
		return -1;
	}

	// creating the "faceIds" array in the request body
	faceIds = json_object_new_array();

	for (i = 0; i < len; ++i) {
		// extracting the faceId from the response of detect
		tmp_obj = json_object_array_get_idx(detect_resp, i);
		json_object_object_get_ex(tmp_obj, FACE_FID, &tmp_obj2);
		strcpy(fid, json_object_get_string(tmp_obj2));

		// add the face id to the "faceIds" array
		json_object_array_add(faceIds, json_object_new_string(fid));

		// creating request body for identify
		body = json_object_new_object();

		memset(fid, 0, BUFSIZ);
	}

	// adding the "faceIds" array to the request body
	json_object_object_add(body, FACE_FIDS, faceIds);

	// adding the default persongroupId to the request body
	json_object_object_add(body, FACE_PGID, json_object_new_string(FACE_DEMO_PGID));

	// identify the face image in the default persongroup
	ident_status = face_identify(body, &ident_resp);

	json_object_put(body);


	if(detect_resp != NULL && ident_resp != NULL && table != NULL && statusOk(detect_status) && statusOk(ident_status)){
		for (i = 0; i < len; ++i) {
			json_object * detect_mem = NULL;
			json_object * ident_mem = NULL;
			json_object *detect_rect = NULL, *ident_cand = NULL;
			detect_mem = json_object_array_get_idx(detect_resp, i);
			ident_mem = json_object_array_get_idx(ident_resp, i);
			if(detect_mem != NULL){
				json_object *top = NULL, *left = NULL, *width = NULL, *height = NULL;
				json_object_object_get_ex(detect_mem, "faceRectangle", &detect_rect);
				if(detect_rect != NULL){
					json_object_object_get_ex(detect_rect, "top", &top);
					json_object_object_get_ex(detect_rect, "left", &left);
					json_object_object_get_ex(detect_rect, "width", &width);
					json_object_object_get_ex(detect_rect, "height", &height);
					if(top != NULL && left != NULL && width != NULL && height != NULL){
						ident_result.rt.x = json_object_get_int(left);
						ident_result.rt.y = json_object_get_int(top);
						ident_result.rt.width = json_object_get_int(width);
						ident_result.rt.height = json_object_get_int(height);
					}
					else
						printf("top or left or width or height is null\n");
				}
				else
					printf("detect_rect is null\n");
			}
			else
				printf("detect_mem is null\n");

			if (ident_mem != NULL) {
				json_object * cand_arr = NULL;
				json_object_object_get_ex(ident_mem, "candidates", &cand_arr);

				// check if the candidate array is empty
				if (!json_object_array_length(cand_arr)) {
					ident_result_append(table, &ident_result);
					continue;
				}

				ident_cand = json_object_array_get_idx(cand_arr, 0);
				json_object *pid = NULL, *confidence = NULL;
				if (ident_cand != NULL) {
					json_object_object_get_ex(ident_cand, "personId", &pid);
					json_object_object_get_ex(ident_cand, "confidence", &confidence);
					if (pid && confidence) {
						strcpy(ident_result.pid, json_object_get_string(pid));
						ident_result.confidence = json_object_get_double(confidence);
					}
					else
						printf("pid or confidence is null\n");
				}
				else
					printf("ident_cand is null\n");
			}
			else
				printf("ident_mem is null\n");

			table->append(table, &ident_result);
			memset(&ident_result, 0, sizeof(IdentResult));
		}
	}
	else
		printf("HTTP status code indicate error/resp or face_result is null\n");

	// printing result of identify
	printf(FACE_DEMO_PRINT_IDENTIFY, json_object_to_json_string_ext(ident_resp, JSON_C_TO_STRING_PRETTY));

	// deallocating json_objects
	json_object_put(detect_resp);
	json_object_put(ident_resp);

	return 0;
}

void setUriParam(CURLU * curlu, const char * name, struct json_object * value) {
	char buffer[BUFSIZ] = {0};
	strcat(buffer, name);
	strcat(buffer, "=");
	strcat(buffer, json_object_get_string(value));
	curl_url_set(curlu, CURLUPART_QUERY, buffer, CURLU_APPENDQUERY);
	return;
}

/**
 * Sets the base of the request url
 *
 * param: curlu: CURLU handle used for parsing url
 *		  base: the url base of the given request
 */
void setUriBase(CURLU * curlu, const char * base) {
	char buffer[BUFSIZ] = {0};
	strcat(buffer, FACE_HTTPS);
	strcat(buffer, region);
	strcat(buffer, base);
	curl_url_set(curlu, CURLUPART_URL, buffer, 0);
	return;
}

int statusOk(long status) {
	if (status >= 200 && status < 300) return 1;
	else return 0;
}

int reg_result_append(Table * table, void * item) {
	RegResult * buffer = (RegResult *)table->arr;
	errno = 0;						// for realloc error checking

	if (!item) {
		fprintf(stderr, "Error: item can't be null\n");
		return -1;
	}

	table->length++;				// update table length

	buffer = realloc(buffer, sizeof(RegResult) * table->length);

	// error checking for realloc
	if (errno) {
		fprintf(stderr, "Realloc Error: %s\n", strerror(errno));
		table_free(table);
		return -1;
	}

	// copy res to the end of resultArr
	memcpy(buffer + table->length - 1, item, sizeof(RegResult));

	// error checking for memcpy
	if (errno) {
		fprintf(stderr, "Memcpy Error: %s\n", strerror(errno));
		return -1;
	}

	table->arr = buffer;			// update resultArr

	return 0;
}

int detect_result_append(Table * table, void * item) {
	DetectResult * buffer = (DetectResult *)table->arr;
	errno = 0;						// for realloc error checking

	if (!item) {
		fprintf(stderr, "Error: item can't be null\n");
		return -1;
	}

	table->length++;				// update table length

	buffer = realloc(buffer, sizeof(DetectResult) * (table->length));

	// error checking for realloc
	if (errno) {
		fprintf(stderr, "Realloc Error: %s\n", strerror(errno));
		table_free(table);
		return -1;
	}

	// copy item to the end of resultArr
	memcpy(buffer + table->length - 1, item, sizeof(DetectResult));

	// error checking for memcpy
	if (errno) {
		fprintf(stderr, "Memcpy Error: %s\n", strerror(errno));
		return -1;
	}

	table->arr = buffer;			// update resultArr

	return 0;
}

int ident_result_append(Table * table, void * item) {
	IdentResult * buffer = (IdentResult *)table->arr;
	errno = 0;						// for realloc error checking

	if (!item) {
		fprintf(stderr, "Error: item can't be null\n");
		return -1;
	}

	table->length++;				// update table length

	buffer = realloc(buffer, sizeof(IdentResult) * table->length);

	// error checking for realloc
	if (errno) {
		fprintf(stderr, "Realloc Error: %s\n", strerror(errno));
		table_free(table);
		return -1;
	}

	// copy res to the end of resultArr
	memcpy(buffer + table->length - 1, item, sizeof(IdentResult));

	// error checking for memcpy
	if (errno) {
		fprintf(stderr, "Memcpy Error: %s\n", strerror(errno));
		return -1;
	}

	table->arr = buffer;			// update resultArr

	return 0;
}

void table_free(Table * table) {
	if (!table) return;

	if (!(table->arr)) {
		free(table);
		return;
	}

	else {
		free(table->arr);
		free(table);
	}

	return;
}

Table * detect_result_table_new() {
	errno = 0;
	Table * table = (Table *)calloc(1, sizeof(Table));
	if (errno) {
		fprintf(stderr, "Calloc Error: %s\n", strerror(errno));
		return NULL;
	}

	table->append = detect_result_append;
	return table;
}

Table * reg_result_table_new() {
	errno = 0;
	Table * table = (Table *)calloc(1, sizeof(Table));
	if (errno) {
		fprintf(stderr, "Calloc Error: %s\n", strerror(errno));
		return NULL;
	}

	table->append = reg_result_append;
	return table;
}

Table * ident_result_table_new() {
	errno = 0;
	Table * table = (Table *)calloc(1, sizeof(Table));
	if (errno) {
		fprintf(stderr, "Calloc Error: %s\n", strerror(errno));
		return NULL;
	}

	table->append = ident_result_append;
	return table;
}

int demo_detect(FILE * image, size_t fsize, Table * table) {
	Request rqst = {
		.rqst_type = 'd',
		.file = image,
		.fsize = fsize,
		.table = table,
		.rqst_func = _demo_detect
	};
	if (enqueue(request_queue, &rqst)) {
		fprintf(stderr, "Request queue full\n");
		return -1;
	}
	sem_post(&request_counter);
	return 0;
}

int demo_register(FILE * image, size_t fsize, Table * table) {
	Request rqst = {
		.rqst_type = 'r',
		.file = image,
		.fsize = fsize,
		.table = table,
		.rqst_func = _demo_register
	};
	if (enqueue(request_queue, &rqst)) {
		fprintf(stderr, "Request queue full\n");
		return -1;
	}
	sem_post(&request_counter);
	return 0;
}

int demo_identify(FILE * image, size_t fsize, Table * table) {
	Request rqst = {
		.rqst_type = 'i',
		.file = image,
		.fsize = fsize,
		.table = table,
		.rqst_func = _demo_identify
	};
	if (enqueue(request_queue, &rqst)) {
		fprintf(stderr, "Request queue full\n");
		return -1;
	}
	sem_post(&request_counter);
	return 0;
}

Queue * queue_new(QueueType type, unsigned int capacity) {
	Queue * new_queue = (Queue *)calloc(1, sizeof(Queue));
	new_queue->capacity = capacity;
	new_queue->rear = capacity - 1;
	new_queue->type = type;
	switch (type) {
		case FACE_QUEUETYPE_REQUEST:
			new_queue->arr.rqstArr = (Request *)calloc((int)capacity, sizeof(Request));
			break;
		case FACE_QUEUETYPE_RESPONSE:
			new_queue->arr.respArr = (Response *)calloc((int)capacity, sizeof(Response));
			break;
		default:
			fprintf(stderr, "Error: Queue type not supported\n");
			free(new_queue);
			break;
	}

	return new_queue;
}

void queue_free(Queue * queue) {

	// mutex lock
	pthread_mutex_lock(&queue_lock);

	switch (queue->type) {
		case FACE_QUEUETYPE_REQUEST:
			free(queue->arr.rqstArr);
			break;
		case FACE_QUEUETYPE_RESPONSE:
			free(queue->arr.respArr);
			break;
		default:
			fprintf(stderr, "Error: Queue type not supported\n");
			return;
	}
	free(queue);

	// mutex unlock
	pthread_mutex_unlock(&queue_lock);

	return;
}

int queue_isempty(Queue * queue) {
	if (queue->size) {
		return 0;
	}
	return 1;
}

int queue_isfull(Queue * queue) {
	if ((unsigned)(queue->size) == queue->capacity) {
		return 1;
	}
	return 0;
}

void * queue_rear(Queue * queue) {

	if (queue_isempty(queue)) return NULL;
	switch (queue->type) {
		case FACE_QUEUETYPE_REQUEST:
			return (void *)(queue->arr.rqstArr + queue->rear);
		case FACE_QUEUETYPE_RESPONSE:
			return (void *)(queue->arr.respArr + queue->rear);
	}

	return NULL;
}

void * queue_front(Queue * queue) {

	if (queue_isempty(queue)) return NULL;
	switch (queue->type) {
		case FACE_QUEUETYPE_REQUEST:
			return (void *)(queue->arr.rqstArr + queue->front);
		case FACE_QUEUETYPE_RESPONSE:
			return (void *)(queue->arr.respArr + queue->front);
	}

	return NULL;
}

int dequeue(Queue * queue) {

	// mutex lock
	printf("[waiting] dequeue thread id: %.4x\n", pthread_self());
	pthread_mutex_lock(&queue_lock);

	printf("[entry] dequeue thread id: %.4x\n", pthread_self());

	if (queue_isempty(queue)) {
		pthread_mutex_unlock(&queue_lock);
		printf("[exit] dequeue thread id: %.4x\n", pthread_self());
		return -1;
	}

	queue->front = (queue->front + 1) % queue->capacity;
	queue->size = queue->size - 1;

	// mutex unlock
	pthread_mutex_unlock(&queue_lock);
	printf("[exit] dequeue thread id: %.4x\n", pthread_self());

	return 0;
}

int enqueue(Queue * queue, void * item) {

	// mutex lock
	printf("[waiting] enqueue thread id: %.4x\n", pthread_self());
	pthread_mutex_lock(&queue_lock);

	printf("[entry] enqueue thread id: %.4x\n", pthread_self());

	if (queue_isfull(queue)) {
		pthread_mutex_unlock(&queue_lock);
		printf("[exit] enqueue thread id: %.4x\n", pthread_self());
		return -1;
	}
	Request * rqst;
	Response * resp;

	switch (queue->type) {
		case FACE_QUEUETYPE_REQUEST:
			rqst = (Request *)item;
			queue->rear = (queue->rear + 1) % queue->capacity;
			memcpy(queue->arr.rqstArr + queue->rear, rqst, sizeof(Request));
			break;
		case FACE_QUEUETYPE_RESPONSE:
			resp = (Response *)item;
			queue->rear = (queue->rear + 1) % queue->capacity;
			memcpy(queue->arr.respArr + queue->rear, resp, sizeof(Response));
			break;
		default:
			fprintf(stderr, "Error: Queue type not supported\n");
			return -1;
	}
	queue->size = queue->size + 1;

	// mutex unlock
	printf("[exit] enqueue thread id: %.4x\n", pthread_self());
	pthread_mutex_unlock(&queue_lock);

	return 0;
}
