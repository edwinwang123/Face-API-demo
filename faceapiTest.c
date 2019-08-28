
#include "faceapi.h"

int main(int argc, char * argv[]) {
	char * imagePath = "./face1.jpg";		// path of the face image
	FILE * image;
	struct stat file_info;		// information of the image file
	size_t fsize;				// size of the image fil
	json_object * body;			// request body
	json_object * resp;			// response
	json_object * tmp_obj;		// temporary json object
	char * pgid = "test";		// id of the persongroup
	char * pName = "person1";	// name of the person

	int flag;					// flag for printing json

	// setting preferred print option
	flag = JSON_C_TO_STRING_PRETTY;

	// acquiring file information
	if (stat(imagePath, &file_info)) {
		fprintf(stderr, "Can't open file %s: %s\n", imagePath, strerror(errno));
		exit(EXIT_FAILURE);
	}
	fsize = (curl_off_t)file_info.st_size;
	printf("Image file size: %" CURL_FORMAT_CURL_OFF_T " bytes.\n", fsize);

	// open image file for detection
	image = fopen(imagePath, "rb");

	// setting user information
	face_login("westcentralus", "3141bb74587a4f43838f7194649be51b");

	/* first request */

	// image face detection
	face_detect_local(image, file_info.st_size, NULL, &resp);

	// print face detection result
	printf("%s\n", json_object_to_json_string_ext(resp, flag));

	/* second request */

	// setting request body for create_pg request
	body = json_object_new_object();

	// building the request body
	tmp_obj = json_object_new_string("group1");
	json_object_object_add(body, "name", tmp_obj);

	// create a persongroup
	face_create_pg(pgid, body, &resp);

	// print any error response from server
	// (create_pg will not return any response if request is successful)
	printf("%s\n", json_object_to_json_string_ext(resp, flag));

	/* third request */

	// setting request body for create_p
	json_object_put(body);
	body = json_object_new_object();	// possile to improve; will check if manually deleting each item is better;
	json_object_object_add(body, "name", json_object_new_string(pName));

	// create a person
	face_create_p(pgid, body, &resp);

	// prints any error response from server
	// (create_pg will not return any response if request is successful)
	printf("%s\n", json_object_to_json_string_ext(resp, flag));

	/* fourth request */

	// gets a persongroup's information
	face_get_pg(pgid, &resp);

	// prints persongroup information
	printf("%s\n", json_object_to_json_string_ext(resp, flag));

	/* fifth request */

	// trains persongroup
	face_train_pg(pgid, &resp);

	// prints training result
	// (shouldn't print anything if successful)
	printf("%s\n", json_object_to_json_string_ext(resp, flag));

	/* sixth request */

	// deleting persongroup
	face_delete_pg(pgid, &resp);

	// print any error response from server
	// (create_pg will not return any response if request is successful)
	printf("%s\n", json_object_to_json_string_ext(resp, flag));

	// deallocating all json_object
	// (tmp_obj doesn't need to be explicitly freed because it is added to body)
	json_object_put(body);
	json_object_put(resp);

	return 0;
}
