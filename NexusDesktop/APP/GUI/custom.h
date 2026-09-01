/*
* Copyright 2023 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#ifndef __CUSTOM_H_
#define __CUSTOM_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "gui_guider.h"

void custom_init(lv_ui *ui);

/* Mouse cursor (cat) interface: place the cat so its TAIL is at (x, y). */
void gui_cursor_init(void);
void gui_cursor_set_pos(int x, int y);
void gui_cursor_cycle(void);
void gui_cursor_hide(void);
void gui_cursor_set_zoom(uint16_t zoom_percent); /* 光标缩放百分比 100..200 */
void gui_lock_screen(void);              /* 返回登录界面并清空密码 */
void gui_update_mouse_conn(uint8_t connected); /* 更新鼠标连接状态显示(未连接/当前图案) */
void gui_open_settings(void);            /* 打开系统设置界面 */
void gui_set_brightness(uint8_t brightness_percent); /* 0..100, 通过顶层半透明遮罩调节 */
uint8_t gui_get_brightness(void);                    /* 读取当前生效亮度 (0..100) */
void gui_save_settings(void);            /* 将当前设置写回 Flash (掉电保持) */
uint8_t gui_settings_is_dirty(void);     /* 设置是否有未保存的修改 */
void gui_request_calibration(void);      /* 请求执行触摸校准 (由 LVGL 任务实际执行) */
uint8_t gui_cal_requested(void);         /* 查询是否有待处理的校准请求 */
void gui_cal_request_clear(void);        /* 清除校准请求 (消费后必须调用, 防止反复进入) */
void gui_request_save(void);             /* 请求把当前设置写回 Flash (由 LVGL 任务执行) */
uint8_t gui_save_requested(void);        /* 查询是否有待处理的保存请求 */

/* 熄屏时间/灵敏度 文案助手 (定义于 setup_scr_settings.c) */
uint16_t settings_sleep_seconds(int idx);
int      settings_sleep_index(uint16_t sec);
const char *settings_sleep_text(int idx);
const char *settings_sens_text(int v);

#ifdef __cplusplus
}
#endif
#endif /* EVENT_CB_H_ */
