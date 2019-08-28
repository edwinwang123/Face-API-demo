#ifndef FACEAPI_STRINGS_H
#define FACEAPI_STRINGS_H

// request type constants
#define FACE_DELETE "DELETE"
#define FACE_PATCH "PATCH"

// face api request constants
#define FACE_JSON "Content-Type: application/json"
#define FACE_OCTET "Content-Type: application/octet-stream"
#define FACE_KEYTYPE "Ocp-Apim-Subscription-Key: "

// URLs
#define FACE_HTTPS "https://"
#define FACE_DETECT_URL ".api.cognitive.microsoft.com/face/v1.0/detect"
#define FACE_IDENTIFY_URL ".api.cognitive.microsoft.com/face/v1.0/identify"
#define FACE_VERIFY_URL ".api.cognitive.microsoft.com/face/v1.0/verify"
#define FACE_PG_URL ".api.cognitive.microsoft.com/face/v1.0/persongroups/"

// URL parts
#define FACE_SLASH "/"
#define FACE_URLPART_P "persons"
#define FACE_URLPART_FACE "persistedFaces"
#define FACE_URLPART_TRAIN "train"
#define FACE_URLPART_GET_PG_PARAM "returnRecognitionModel=true"

// error strings
#define FACE_LOGIN_ERROR "Error: need to call face_login to initialize region and key\n"

// default values
#define FACE_DEFAULT_REGION "westcentralus"

// debug strings
#define FACE_LATENCY "Latency: %lf sec\n"
#define FACE_READ_IMAGE "We've read %lu bytes from file\n"
#define FACE_READ_TEXT "We've read %lu bytes from text\n"
#define FACE_WRITE_DATA "We've written %lu bytes\n"
#define FACE_REQUEST_URL "Request URL: %s\n"
#define FACE_HTTP_STATUS "Status Code: %ld\n"

// Face API json response keys
#define FACE_PGID "personGroupId"
#define FACE_PID "personId"
#define FACE_FID "faceId"
#define FACE_FIDS "faceIds"			// used by identify
#define FACE_RECT "faceRectangle"

// demo string
#define FACE_DEMO_PRINT_FACE "Face information:\n%s\n"
#define FACE_DEMO_PRINT_IDENTIFY "Identification result:\n%s\n"
#define FACE_DEMO_FACE_ATTR "age,gender,emotion,makeup"
#define FACE_DEMO_DETECT_PARAM "returnFaceAttributes"
#define FACE_DEMO_PGID "demo_group"

#endif /* _FACEAPI_STRINGS_H */
