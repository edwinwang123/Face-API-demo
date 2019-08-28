#include <cstdio>
#include <opencv2/opencv.hpp>
#include "include/faceapi.h"
using namespace cv;


#define CAPTURE_WINDOW "Capture_Window"

#define SERVER "westcentralus"
#define FACEAPI_KEY "85607bdb3b22476a913a2834d22cd3b5"
#define IMAGE_NAME_SIZE 64



int main(){
	VideoCapture video("/dev/video0");
	if (!video.isOpened()){
		return -1;
	}
//	Size videoSize = Size((int)video.get(CV_CAP_PROP_FRAME_WIDTH),(int)video.get(CV_CAP_PROP_FRAME_HEIGHT));
	namedWindow("video demo", CV_WINDOW_AUTOSIZE);
	namedWindow(CAPTURE_WINDOW, CV_WINDOW_AUTOSIZE);
	Mat videoFrame;
	bool bIsStop = false;
	int counter = 0;
	char filename [IMAGE_NAME_SIZE];
	Response * resp = NULL;

	while(!bIsStop){
		video >> videoFrame;
		if(videoFrame.empty()){
			break;
		}
		imshow("video demo", videoFrame);

		face_init();

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
					Table * detect_result_table = detect_result_table_new();
					if(detect_result_table == NULL){
						printf("detect_result_table_new failed.\n");
						break;
					}
					sprintf(filename, "detect%d.jpg", counter);
					++counter;
					imwrite(filename, videoFrame);
					if(stat(filename, &file_info)){
						printf("failed(%s)\n", strerror(errno));
						break;
					}
					file = fopen(filename, "rb");
					memset(filename, 0, IMAGE_NAME_SIZE);
					demo_detect(file, file_info.st_size, detect_result_table);
				}
				break;

			// Key "r" or "R" triggers registration
			case 'r':
			case 'R':
				{
					FILE * file = NULL;
					struct stat file_info = {0};
					Table * reg_result_table = reg_result_table_new();
					if (reg_result_table == NULL) {
						printf("reg_result_table_new failed.\n");
						break;
					}
					sprintf(filename, "reg%d.jpg", counter);
					++counter;
					imwrite(filename, videoFrame);
					if(stat(filename, &file_info)){
						printf("failed(%s)\n", strerror(errno));
						break;
					}
					file = fopen(filename, "rb");
					memset(filename, 0, IMAGE_NAME_SIZE);
					demo_register(file, file_info.st_size, reg_result_table);

				}
				break;

			// Key "i" or "I" triggers identification
			case 'i':
			case 'I':
				{
					FILE * file = NULL;
					struct stat file_info = {0};
					Table * ident_result_table = ident_result_table_new();
					if (ident_result_table == NULL) {
						printf("ident_result_table_new failed.\n");
						break;
					}
					sprintf(filename, "reg%d.jpg", counter);
					++counter;
					imwrite(filename, videoFrame);
					if(stat(filename, &file_info)){
						printf("failed(%s)\n", strerror(errno));
						break;
					}
					file = fopen(filename, "rb");
					memset(filename, 0, IMAGE_NAME_SIZE);
					demo_identify(file, file_info.st_size, ident_result_table);
				}
				break;

			// Key ";"
			case ';':
				bIsStop = true;
				face_cleanup();
				system("rm *.jpg");
				break;
		}

		if (resp = getResponse()) {
			switch (resp->resp_type) {
				case 'd':
					{
						Table * detect_result_table = resp->table;
						FILE * file = resp->file;
						DetectResult * arr = (DetectResult *)detect_result_table->arr;
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
						pclose(file);
						table_free(detect_result_table);
						break;
					}
				case 'r':
					{
						Table * reg_result_table = resp->table;
						FILE * file = resp->file;
						RegResult * arr = (RegResult *)reg_result_table->arr;
						int len = reg_result_table->length;
						char info[6] = {0};
						for (int i = 0; i < len; ++i) {
							// draw rectangle around face
							rectangle(videoFrame, Point((arr + i)->rt.x, (arr + i)->rt.y), Point((arr + i)->rt.x + (arr + i)->rt.width - 1, (arr + i)->rt.y + (arr + i)->rt.height - 1), Scalar(255, 0, 0), 5);

							// extract last 5 character of the personId
							snprintf(info, 6, "%s", (arr + i)->pid + strlen((arr + i)->pid) - 5);

							// write face information on top of the rectangle
							putText(videoFrame, info, Point((arr + i)->rt.x, (arr + i)->rt.y - 5), FONT_HERSHEY_PLAIN, 2.0, Scalar(255, 0, 0), 2);
						}
						imshow(CAPTURE_WINDOW, videoFrame);
						pclose(file);
						table_free(reg_result_table);
						break;
					}
				case 'i':
					{
						Table * ident_result_table = resp->table;
						FILE * file = resp->file;
						IdentResult * arr = (IdentResult *)ident_result_table->arr;
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
								snprintf(pid_char, 6, "%s", (arr + i)->pid + strlen((arr + i)->pid) - 5);
								sprintf(info, "%s,%.2lf", pid_char, (arr + i)->confidence);
							}

							// write face information on top of the rectangle
							putText(videoFrame, info, Point((arr + i)->rt.x, (arr + i)->rt.y - 5), FONT_HERSHEY_PLAIN, 2.0, Scalar(255, 0, 0), 2);
						}
						imshow(CAPTURE_WINDOW, videoFrame);
						pclose(file);
						table_free(ident_result_table);
						break;
					}
			}
		}
	}
	return 0;
}
