#ifndef MENU_VIDEO_H
#define MENU_VIDEO_H
#include <stdint.h>
int menu_video_init(void);
void menu_video_start(void);
void menu_video_stop(void);
void menu_video_service(void);
int menu_video_active(void);
uint32_t menu_video_time(void);
int menu_video_ui_begin(void);
void menu_video_ui_present(void);
#endif
