/**
  ******************************************************************************
  * @file    app_logview.c
  * @brief   日志查看应用实现
  * @author  Doubao
  * @version V1.0
  * @date    2026-09-04
  * @note    从 SD 日志文件读取最近若干行显示, 支持刷新与清空；
  *          文件访问由后台存储工作线程执行，不阻塞 LVGL 回调。
  ******************************************************************************
  */

#include "app_logview.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "gui_guider.h"
#include "custom.h"
#include "app_log.h"
#include "app_storage.h"
#include "ff.h"

/* ========================= 布局常量 ========================= */
#define LV_W            320
#define LV_H            480

/* ========================= 私有变量 ========================= */
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_log_panel = NULL;
static lv_obj_t *s_label_body = NULL;
static lv_obj_t *s_label_size = NULL;
static char s_log_buf[1024];        /* 日志文本缓冲 (读最近若干行) */
static App_StorageRequest_t s_request;
static uint8_t s_fg = 0U;

/* ========================= 私有函数 ========================= */

/* 返回桌面 */
static void logview_back_cb(lv_event_t * e)
{
    (void)e;
    App_LogView_Close();
}

/* 重新读取日志 */
static void logview_submit(App_StorageOperation_t operation)
{
    if (s_request.state != APP_STORAGE_REQ_IDLE) {
        lv_label_set_text(s_label_body, "已有日志操作正在后台执行...");
        return;
    }
    memset(&s_request, 0, sizeof(s_request));
    s_request.operation = (uint8_t)operation;
    strncpy(s_request.path, LOG_FILE_PATH, sizeof(s_request.path) - 1U);
    s_request.data = s_log_buf;
    s_request.data_capacity = sizeof(s_log_buf);
    s_request.max_lines = 30U;
    if (!App_StorageSubmit(&s_request)) {
        lv_label_set_text(s_label_body, "存储服务忙, 请稍后重试。");
        return;
    }
    lv_label_set_text(s_label_body,
        operation == APP_STORAGE_OP_TRUNCATE ? "正在后台清空日志..." : "正在后台读取日志...");
}

static void logview_poll_cb(lv_timer_t *timer)
{
    App_StorageOperation_t operation;
    FRESULT result;
    uint32_t size;
    uint32_t length;
    char tmp[48];

    (void)timer;
    if (s_request.state != APP_STORAGE_REQ_DONE) {
        return;
    }
    operation = (App_StorageOperation_t)s_request.operation;
    result = (FRESULT)s_request.result;
    size = s_request.file_size;
    length = s_request.data_length;
    App_StorageRequestReset(&s_request);
    if (!s_fg) {
        return;
    }
    if (operation == APP_STORAGE_OP_TRUNCATE && result == FR_OK) {
        logview_submit(APP_STORAGE_OP_READ_TAIL);
        return;
    }
    if (result == FR_NO_FILE || (result == FR_OK && length == 0U)) {
        lv_label_set_text(s_label_body, "日志为空。\n(系统事件将自动记录到 SYSLOG.LOG)");
        size = 0U;
    } else if (result != FR_OK) {
        snprintf(tmp, sizeof(tmp), "日志读取失败, FR=%u", (unsigned int)result);
        lv_label_set_text(s_label_body, tmp);
    } else {
        lv_label_set_text(s_label_body, s_log_buf);
    }
    if (s_label_size != NULL) {
        snprintf(tmp, sizeof(tmp), "日志大小: %lu B", (unsigned long)size);
        lv_label_set_text(s_label_size, tmp);
    }
}

static void logview_refresh_cb(lv_event_t * e)
{
    (void)e;
    logview_submit(APP_STORAGE_OP_READ_TAIL);
}

static void logview_clear_cb(lv_event_t * e)
{
    (void)e;
    logview_submit(APP_STORAGE_OP_TRUNCATE);
}

/* 创建日志查看屏幕 */
static void logview_screen_create(void)
{
    lv_obj_t * btn_back;
    lv_obj_t * btn_refresh;
    lv_obj_t * btn_clear;

    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, LV_W, LV_H);
    lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x0f172a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_screen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 顶部: 返回 + 标题 */
    btn_back = lv_btn_create(s_screen);
    lv_obj_set_pos(btn_back, 10, 8);
    lv_obj_set_size(btn_back, 70, 32);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x2195f6), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_back, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_back, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_back, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_back, logview_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * bl = lv_label_create(btn_back);
    lv_label_set_text(bl, "返回");
    lv_obj_center(bl);

    lv_obj_t * title = lv_label_create(s_screen);
    lv_label_set_text(title, "系统日志");
    lv_obj_set_pos(title, 90, 10);
    lv_obj_set_style_text_font(title, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);

    s_label_size = lv_label_create(s_screen);
    lv_obj_set_pos(s_label_size, 90, 34);
    lv_obj_set_style_text_font(s_label_size, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_label_size, lv_color_hex(0x94a3b8), LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 日志正文: 外层只做垂直滚动，正文固定左对齐并自动换行。
       禁止 LONG_SCROLL_CIRCULAR，否则 LVGL 会持续执行横向跑马灯动画，
       造成大面积无效区、文字漂移和光标移动卡顿。 */
    s_log_panel = lv_obj_create(s_screen);
    lv_obj_set_pos(s_log_panel, 8, 70);
    lv_obj_set_size(s_log_panel, LV_W - 16, LV_H - 70 - 52);
    lv_obj_set_scroll_dir(s_log_panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_log_panel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(s_log_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_log_panel, 0, 0);
    lv_obj_set_style_pad_all(s_log_panel, 4, 0);

    s_label_body = lv_label_create(s_log_panel);
    lv_obj_set_pos(s_label_body, 0, 0);
    lv_obj_set_width(s_label_body, LV_W - 28);
    lv_obj_set_height(s_label_body, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(s_label_body, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_label_body, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(s_label_body, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(s_label_body, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_label_body, LV_LABEL_LONG_WRAP);

    /* 底部: 刷新 / 清空 */
    btn_refresh = lv_btn_create(s_screen);
    lv_obj_set_pos(btn_refresh, 20, LV_H - 46);
    lv_obj_set_size(btn_refresh, 110, 36);
    lv_obj_set_style_bg_color(btn_refresh, lv_color_hex(0x16a34a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_refresh, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_refresh, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_refresh, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_refresh, logview_refresh_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * rl = lv_label_create(btn_refresh);
    lv_label_set_text(rl, "刷新");
    lv_obj_center(rl);

    btn_clear = lv_btn_create(s_screen);
    lv_obj_set_pos(btn_clear, 190, LV_H - 46);
    lv_obj_set_size(btn_clear, 110, 36);
    lv_obj_set_style_bg_color(btn_clear, lv_color_hex(0xdc2626), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_clear, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_clear, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_clear, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_clear, logview_clear_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * cl = lv_label_create(btn_clear);
    lv_label_set_text(cl, "清空");
    lv_obj_center(cl);

    (void)lv_timer_create(logview_poll_cb, 50U, NULL);
}

/* ========================= 公共接口实现 ========================= */

void App_LogView_Open(void)
{
    if (s_screen == NULL) {
        logview_screen_create();
    }
    s_fg = 1U;
    lv_scr_load(s_screen);
    logview_submit(APP_STORAGE_OP_READ_TAIL);
}

void App_LogView_Close(void)
{
    s_fg = 0U;
    if (guider_ui.desktop != NULL) {
        lv_scr_load(guider_ui.desktop);
    }
}
