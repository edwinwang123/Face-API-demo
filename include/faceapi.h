#ifndef FACEAPI_H
#define FACEAPI_H

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <curl/curl.h>
#include <json/json.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <Windows.h>
#endif

#if !CURL_AT_LEAST_VERSION(7, 62, 0)
#error "This library requires curl 7.62.0 or later"
#endif

typedef struct tagRect{
	int x;
	int y;
	int width;
	int height;
}Rect;

typedef struct tagAttr{
	char gender[10];
	double age;
}Attr;

typedef struct tagRegResult {
	Rect rt;
	char pid[BUFSIZ];
} RegResult;

typedef struct tagRegResultTable {
	RegResult * resultArr;
	int length;
} RegResultTable;

typedef struct tagDetectResult {
	Rect rt;
	Attr attr;
} DetectResult;

typedef struct tagDetectResultTable {
	DetectResult * resultArr;	// an array of result
	int length;					// length of the table
} DetectResultTable;

typedef struct tagIdentResult {
	Rect rt;
	char pid[BUFSIZ];
	double confidence;
} IdentResult;

typedef struct tagIdentResultTable {
	IdentResult * resultArr;
	int length;
} IdentResultTable;

typedef struct faceTable Table;

struct faceTable {
	void * arr;
	int length;
	int (*append)(Table *, void *);
};

typedef int (*RequestFunc)(FILE *, size_t, Table *);

typedef struct faceRequest {
	char rqst_type;
	FILE * file;
	size_t fsize;
	Table * table;
	RequestFunc rqst_func;
} Request;

typedef struct faceResponse {
	char resp_type;
	Table * table;
	FILE * file;
} Response;

typedef enum faceQueueType {
	FACE_QUEUETYPE_REQUEST,
	FACE_QUEUETYPE_RESPONSE
} QueueType;

typedef union faceQueueArr {
	Request * rqstArr;
	Response * respArr;
} QueueArr;

typedef struct faceQueue {
	QueueType type;
	QueueArr arr;
	int front, rear, size;
	unsigned int capacity;
} Queue;



/* Main functions */
/* Note: json_object is typedefed, still using struct here for clarity */

//int face_login(char * region, char * key);
long face_create_pg(char * pgid, struct json_object * body, struct json_object ** resp);
long face_detect(struct json_object * param, struct json_object * body, struct json_object ** resp);
long face_detect_local(FILE * image, size_t fsize, struct json_object * param, struct json_object ** resp);
long face_verify(struct json_object * body, struct json_object ** resp);
long face_identify(struct json_object * body, struct json_object ** resp);
long face_create_p(char * pgid, struct json_object * body, struct json_object ** resp);
long face_add_face(char * pgid, char * pid, struct json_object * param, struct json_object * body, struct json_object ** resp);
long face_add_face_local(FILE * image, size_t fsize, char * pgid, char * pid, struct json_object * param, struct json_object ** resp);
long face_delete_face(char * pgid, char * pid, char * fid, struct json_object ** resp);
long face_delete_p(char * pgid, char * pid, struct json_object ** resp);
long face_delete_pg(char * pgid, struct json_object ** resp);
long face_get_pg(char * pgid, struct json_object ** resp);
long face_get_p(char * pgid, char * pid, struct json_object ** resp);
long face_get_face(char * pgid, char * pid, char * fid, struct json_object ** resp);
long face_train_pg(char * pgid, struct json_object ** resp);
long face_list_p(char * pgid, struct json_object ** resp);

/*

TODO:

int face_search_p(char * pgid, char * name, char * value);

*/

/* For Demo */

#ifdef __cplusplus
extern "C" {
	/* initialize thread, semaphore, and workQueue */
	void face_init();

	/* close off thread and free workQueue */
	void face_cleanup();

	/* enter account information */
	int face_login(char * region, char * key);

	/* registers the face as a new person under the default persongroup and pass
	   its face id in fid */
	int demo_register(FILE * image, size_t fsize, Table *table);

	/* detects print the default attributes of a face */
	int demo_detect(FILE * image, size_t fsize, Table *table);

	/* identify the face image from the default persongroup */
	int demo_identify(FILE * image, size_t fsize, Table *table);

	/* checks if response queue has item and return the item if so */
	Response * getResponse();

	/* creates a Table for DetectResult */
	Table * detect_result_table_new();

	/* creates a Table for RegResult */
	Table * reg_result_table_new();

	/* creates a Table for IdentResult */
	Table * ident_result_table_new();

	/* frees Table */
	void table_free(Table * table);
}
#else
	/* initialize thread, semaphore, and workQueue */
extern	void face_init();
	/* close off thread and free workQueue */
extern	void face_cleanup();
	/* enter account information */
extern	int face_login(char * region, char * key);
        /* registers the face as a new person under the default persongroup and pass
           its face id in fid */
extern	int demo_register(FILE * image, size_t fsize, Table *table);
        /* detects print the default attributes of a face */
extern  int demo_detect(FILE * image, size_t fsize, Table *table);
	/* identify the face image from the default persongroup */
extern  int demo_identify(FILE * image, size_t fsize, Table *table);
	/* checks if response queue has item and return the item if so */
extern	Response * getResponse();
	/* creates a Table for DetectResult */
extern	Table * detect_result_table_new();
	/* creates a Table for RegResult */
extern	Table * reg_result_table_new();
	/* creates a Table for IdentResult */
extern	Table * ident_result_table_new();
	/* frees Table */
extern	void table_free(Table * table);
#endif

#endif /* FACEAPI_H */
