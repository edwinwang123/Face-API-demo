#include <cstdio>
#include <opencv2/opencv.hpp>
#include "include/faceapi.h"
using namespace cv;


#define CAPTURE_WINDOW "Capture_Window"

#define SERVER "westcentralus"
#define FACEAPI_KEY "1ac0993db5a6482f97e5f92745b65972"

int main(){
	VideoCapture video("/dev/video6");
	if (!video.isOpened()){
		return -1;
	}
	Size videoSize = Size((int)video.get(CV_CAP_PROP_FRAME_WIDTH),(int)video.get(CV_CAP_PROP_FRAME_HEIGHT));
	namedWindow("video demo", CV_WINDOW_AUTOSIZE);
	namedWindow(CAPTURE_WINDOW, CV_WINDOW_AUTOSIZE);
	Mat videoFrame;
	bool bIsStop = false;

	while(!bIsStop){
		video >> videoFrame;
		if(videoFrame.empty()){
			break;
		}
		imshow("video demo", videoFrame);

		if(face_login(SERVER, FACEAPI_KEY) != EXIT_SUCCESS)
		{
			printf("face_login failed.\n");
			break;
		}

		switch(waitKey(33)){
			// Key "D" or "d" triggers detection
			case 'd':
			case 'D':
				{
					FILE *file = NULL;
					struct stat file_info = {0};
					DetectResultTable *detect_result_table = (DetectResultTable*)calloc(1, sizeof(DetectResultTable));
					if(detect_result_table == NULL){
						printf("DetectResultTable calloc failed.\n");
						break;
					}
					imwrite("detect.jpg", videoFrame);
					if(stat("detect.jpg", &file_info)){
						printf("failed(%s)\n", strerror(errno));
						break;
					}
					file = fopen("detect.jpg", "rb");
					demo_detect(file, file_info.st_size, detect_result_table);
					DetectResult * arr = detect_result_table->resultArr;
					int len = detect_result_table->length;
					char info[20] = {0};
					for (int i = 0; i < len; ++i) {
						// draw rectangle around face
						rectangle(videoFrame, Point((arr + i)->rt.x, (arr + i)->rt.y), Point((arr + i)->rt.x + (arr + i)->rt.width - 1, (arr + i)->rt.y + (arr + i)->rt.height - 1), Scalar(255, 0, 0), 5);

						// extract face information
						sprintf(info, "%s,%d", (arr + i)->attr.gender, (int)((arr + i)->attr.age));

						// write face information on top of the rectangle
						putText(videoFrame, info, Point((arr + i)->rt.x, (arr + i)->rt.y - 5), FONT_HERSHEY_PLAIN, 2.0, Scalar(255, 0, 0), 2);
					}
					imshow(CAPTURE_WINDOW, videoFrame);
					/*char szMsg[4096] = {0};
					  sprintf(szMsg, "%s x:%d y:%d width:%d height:%d\n", json_object_to_json_string_ext(resp, JSON_C_TO_STRING_PRETTY), face_result->rt.x, face_result->rt.y, face_result->rt.width, face_result->rt.height);
					  printf(szMsg);*/
					pclose(file);
					system("rm detect.jpg");
					detect_result_table_free(detect_result_table);
				}
				break;

			// Key "r" or "R" triggers registration
			case 'r':
			case 'R':
				{
					FILE * file = NULL;
					struct stat file_info = {0};
					char pid[BUFSIZ] = {0};
					imwrite("register.jpg", videoFrame);
					if (stat("register.jpg", &file_info)) {
						printf("failed(%s)\n", strerror(errno));
						break;
					}
					file = fopen("register.jpg", "rb");
					demo_register(file, file_info.st_size, pid);
					if (*pid) {
						printf("Register succeeded\npid: %s\n", pid);
					}
					else {
						printf("Register failed\n");
					}
					pclose(file);
					system("rm register.jpg");
				}
				break;

			// Key "i" or "I" triggers identification
			case 'i':
			case 'I':
				{
					FILE * file = NULL;
					struct stat file_info = {0};
					IdentResultTable * ident_result_table = (IdentResultTable *)calloc(1, sizeof(IdentResultTable));
					if (ident_result_table == NULL) {
						printf("IdentResultTable calloc failed.\n");
						break;
					}
					imwrite("ident.jpg", videoFrame);
					if (stat("ident.jpg", &file_info)) {
						printf("failed(%s)\n", strerror(errno));
						break;
					}
					file = fopen("ident.jpg", "rb");
					demo_identify(file, file_info.st_size, ident_result_table);
					IdentResult * arr = ident_result_table->resultArr;
					int len = ident_result_table->length;
					char info[20] = {0};
					char pid_char[6] = {0};
					for (int i = 0; i < len; ++i) {
						// draw rectangle around face
						rectangle(videoFrame, Point((arr + i)->rt.x, (arr + i)->rt.y), Point((arr + i)->rt.x + (arr + i)->rt.width - 1, (arr + i)->rt.y + (arr + i)->rt.height - 1), Scalar(255, 0, 0), 5);

						// null check the identification result
						if (!(arr + i)->pid || !(arr + i)->confidence) sprintf(info, "No result");
						else {
							// extract face information
							snprintf(pid_char, 6, "%s", (arr + i)->pid);
							sprintf(info, "%s,%.2lf", pid_char, (arr + i)->confidence);
						}

						// write face information on top of the rectangle
						putText(videoFrame, info, Point((arr + i)->rt.x, (arr + i)->rt.y - 5), FONT_HERSHEY_PLAIN, 2.0, Scalar(255, 0, 0), 2);
					}
					imshow(CAPTURE_WINDOW, videoFrame);

					pclose(file);
					system("rm ident.jpg");
					ident_result_table_free(ident_result_table);
				}
				break;

			// Key ";" exits program
			case ';':
				bIsStop = true;	
				break;
		}
	}
	return 0;
}
