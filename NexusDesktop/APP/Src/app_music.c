/**
  ******************************************************************************
  * @file    app_music.c
  * @brief   音乐应用占位页
  * @note    当前硬件未连接播放设备，不创建解码/输出任务，只保留应用入口。
  ******************************************************************************
  */

#include "app_music.h"

#include "lvgl.h"
#include "gui_guider.h"
#include "app_log.h"

static lv_obj_t *s_music_screen = NULL;

static void music_back_cb(lv_event_t *event)
{
    (void)event;
    if (guider_ui.desktop != NULL) {
        lv_scr_load(guider_ui.desktop);
    }
}

static void music_screen_create(void)
{
    lv_obj_t *title;
    lv_obj_t *status;
    lv_obj_t *detail;
    lv_obj_t *back;
    lv_obj_t *back_label;

    s_music_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_music_screen, 320, 480);
    lv_obj_set_style_bg_color(s_music_screen, lv_color_hex(0x111827), LV_PART_MAIN | LV_STATE_DEFAULT);

    title = lv_label_create(s_music_screen);
    lv_label_set_text(title, "MUSIC");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xf8fafc), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 52);

    status = lv_label_create(s_music_screen);
    lv_label_set_text(status, "Playback device not connected");
    lv_obj_set_width(status, 280);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(status, lv_color_hex(0xf59e0b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, -18);

    detail = lv_label_create(s_music_screen);
    lv_label_set_text(detail, "Player interface reserved\nAudio output is disabled");
    lv_obj_set_width(detail, 260);
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(detail, lv_color_hex(0x94a3b8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(detail, LV_ALIGN_CENTER, 0, 42);

    back = lv_btn_create(s_music_screen);
    lv_obj_set_size(back, 120, 42);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -38);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x2563eb), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(back, music_back_cb, LV_EVENT_CLICKED, NULL);
    back_label = lv_label_create(back);
    lv_label_set_text(back_label, "BACK");
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(back_label);
}

void App_MusicOpen(void)
{
    if (s_music_screen == NULL) {
        music_screen_create();
    }
    App_Log_Event(LOG_LEVEL_WARN, "音乐播放设备未连接");
    lv_scr_load(s_music_screen);
}
