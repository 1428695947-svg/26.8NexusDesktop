/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

/*
 * 系统设置界面 (光标灵敏度/光标大小/屏幕亮度/音量预留/熄屏时间)
 * 由 GUI Guider 生成的 setup_scr_* 风格手工封装, 适配 STM32 工程。
 */
#include "lvgl.h"
#include <stdio.h>
#include "gui_guider.h"
#include "events_init.h"
#include "widgets_init.h"
#include "custom.h"
#include "user_store.h"

/* 熄屏时间预设: 索引 -> 秒数 (由 custom.c 与 setup_scr_settings.c 共用) */
static const uint16_t k_sleep_presets[] = {0, 30, 60, 120, 300, 600};

/* 供 custom.c 使用 */
uint16_t settings_sleep_seconds(int idx)
{
    if (idx < 0) idx = 0;
    if (idx > 5) idx = 5;
    return k_sleep_presets[idx];
}

int settings_sleep_index(uint16_t sec)
{
    int i, best = 0;
    uint16_t bestd = 0xFFFF;
    for (i = 0; i <= 5; i++) {
        uint16_t d = (sec > k_sleep_presets[i]) ? (sec - k_sleep_presets[i]) : (k_sleep_presets[i] - sec);
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}

const char *settings_sens_text(int v)
{
    if (v <= 3) return "低";
    if (v <= 6) return "中";
    return "高";
}

const char *settings_sleep_text(int idx)
{
    uint16_t s = settings_sleep_seconds(idx);
    if (s == 0) return "从不";
    if (s < 60) return "30秒";
    if (s == 60) return "1分钟";
    if (s == 120) return "2分钟";
    if (s == 300) return "5分钟";
    return "10分钟";
}

/* 创建一个"设置卡片"子对象, 返回容器 */
static lv_obj_t *settings_card_create(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(card, lv_color_hex(0xe7ecf4), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(card, 16, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(card, 4, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(card, 20, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(card, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    return card;
}

/* 创建一个设置行: 标题 + 数值 + 滑条 */
static void settings_row_build(lv_obj_t *card,
                               lv_obj_t **title_out, lv_obj_t **val_out, lv_obj_t **slider_out,
                               const char *title, const char *val, int min, int max, int val0,
                               int slider_w, int slider_y)
{
    lv_obj_t *title_l = lv_label_create(card);
    lv_label_set_text(title_l, title);
    lv_obj_set_pos(title_l, 16, 8);
    lv_obj_set_style_text_font(title_l, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title_l, lv_color_hex(0x1a1a2e), LV_PART_MAIN|LV_STATE_DEFAULT);

    lv_obj_t *val_l = lv_label_create(card);
    lv_label_set_text(val_l, val);
    lv_obj_set_pos(val_l, slider_w + 8, 10);
    lv_obj_set_style_text_font(val_l, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(val_l, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);

    lv_obj_t *slider = lv_slider_create(card);
    lv_obj_set_pos(slider, 16, slider_y);
    lv_obj_set_size(slider, slider_w, 12);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, val0, LV_ANIM_OFF);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xe3e8f0), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(slider, 6, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2195f6), LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(slider, 6, LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xffffff), LV_PART_KNOB|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(slider, lv_color_hex(0x2195f6), LV_PART_KNOB|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(slider, 3, LV_PART_KNOB|LV_STATE_DEFAULT);

    if (title_out)  *title_out = title_l;
    if (val_out)    *val_out = val_l;
    if (slider_out) *slider_out = slider;
}

void setup_scr_settings(lv_ui *ui)
{
    const SysSettings_t *st = UserStore_GetSettings();
    int sens0   = (st != NULL) ? st->sens : SETTINGS_SENS_DEFAULT;
    int zoom0   = (st != NULL) ? st->cursor_zoom : SETTINGS_ZOOM_DEFAULT;
    int bright0 = (st != NULL) ? st->brightness : SETTINGS_BRIGHT_DEFAULT;
    int volume0 = (st != NULL) ? st->volume : SETTINGS_VOLUME_DEFAULT;
    int sleep0  = settings_sleep_index((st != NULL) ? st->sleep_sec : SETTINGS_SLEEP_DEFAULT);

    /* 根屏幕 */
    ui->settings = lv_obj_create(NULL);
    lv_obj_set_size(ui->settings, 320, 480);
    lv_obj_set_scrollbar_mode(ui->settings, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(ui->settings, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->settings, lv_color_hex(0xf2f5fa), LV_PART_MAIN|LV_STATE_DEFAULT);

    lv_obj_t *topbar = lv_obj_create(ui->settings);
    lv_obj_remove_style_all(topbar);
    lv_obj_set_pos(topbar, 0, 0);
    lv_obj_set_size(topbar, 320, 54);
    lv_obj_set_style_bg_opa(topbar, LV_OPA_COVER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(topbar, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);

    /* 标题 */
    ui->settings_label_title = lv_label_create(topbar);
    lv_label_set_text(ui->settings_label_title, "系统设置");
    lv_obj_set_style_text_font(ui->settings_label_title, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->settings_label_title, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_center(ui->settings_label_title);

    /* 返回按钮 */
    ui->settings_btn_back = lv_btn_create(topbar);
    ui->settings_btn_back_label = lv_label_create(ui->settings_btn_back);
    lv_label_set_text(ui->settings_btn_back_label, "< 返回");
    lv_obj_center(ui->settings_btn_back_label);
    lv_obj_set_style_pad_all(ui->settings_btn_back, 0, LV_STATE_DEFAULT);
    lv_obj_set_pos(ui->settings_btn_back, 8, 10);
    lv_obj_set_size(ui->settings_btn_back, 76, 34);
    lv_obj_set_style_bg_opa(ui->settings_btn_back, LV_OPA_COVER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->settings_btn_back, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->settings_btn_back, 17, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->settings_btn_back, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->settings_btn_back, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->settings_btn_back, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->settings_btn_back, lv_color_hex(0xe6f1fd), LV_PART_MAIN|LV_STATE_PRESSED);

    /* 触摸校准按钮 (顶栏右侧, 9 点校准) */
    ui->settings_btn_cal = lv_btn_create(topbar);
    ui->settings_btn_cal_label = lv_label_create(ui->settings_btn_cal);
    lv_label_set_text(ui->settings_btn_cal_label, "触摸校准");
    lv_obj_center(ui->settings_btn_cal_label);
    lv_obj_set_style_pad_all(ui->settings_btn_cal, 0, LV_STATE_DEFAULT);
    lv_obj_set_pos(ui->settings_btn_cal, 202, 10);
    lv_obj_set_size(ui->settings_btn_cal, 110, 34);
    lv_obj_set_style_bg_opa(ui->settings_btn_cal, LV_OPA_COVER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->settings_btn_cal, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->settings_btn_cal, 17, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->settings_btn_cal, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->settings_btn_cal, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->settings_btn_cal, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->settings_btn_cal, lv_color_hex(0xe6f1fd), LV_PART_MAIN|LV_STATE_PRESSED);

    /* 五张紧凑设置卡片，在 320x480 内完整展示。 */
    int card_x = 12;
    int card_w = 296;
    int card_h = 74;
    int gap    = 7;
    int y0     = 60;
    int slider_w = 200;
    int slider_y = 46;

    ui->settings_card_sens = settings_card_create(ui->settings, card_x, y0 + 0*(card_h+gap), card_w, card_h);
    settings_row_build(ui->settings_card_sens, &ui->settings_label_sens, &ui->settings_label_sens_val,
                       &ui->settings_slider_sens, "光标灵敏度", settings_sens_text(sens0),
                       SETTINGS_SENS_MIN, SETTINGS_SENS_MAX, sens0, slider_w, slider_y);

    ui->settings_card_zoom = settings_card_create(ui->settings, card_x, y0 + 1*(card_h+gap), card_w, card_h);
    {
        char buf[16]; lv_snprintf(buf, sizeof(buf), "%d%%", zoom0);
        settings_row_build(ui->settings_card_zoom, &ui->settings_label_zoom, &ui->settings_label_zoom_val,
                           &ui->settings_slider_zoom, "光标大小", buf,
                           SETTINGS_ZOOM_MIN, SETTINGS_ZOOM_MAX, zoom0, slider_w, slider_y);
    }

    ui->settings_card_bright = settings_card_create(ui->settings, card_x, y0 + 2*(card_h+gap), card_w, card_h);
    {
        char buf[16]; lv_snprintf(buf, sizeof(buf), "%d%%", bright0);
        settings_row_build(ui->settings_card_bright, &ui->settings_label_bright, &ui->settings_label_bright_val,
                           &ui->settings_slider_bright, "屏幕亮度", buf,
                           SETTINGS_BRIGHT_MIN, SETTINGS_BRIGHT_MAX, bright0, slider_w, slider_y);
    }

    ui->settings_card_volume = settings_card_create(ui->settings, card_x, y0 + 3*(card_h+gap), card_w, card_h);
    {
        char buf[16]; lv_snprintf(buf, sizeof(buf), "%d%%", volume0);
        settings_row_build(ui->settings_card_volume, &ui->settings_label_volume,
                           &ui->settings_label_volume_val, &ui->settings_slider_volume,
                           "音量(预留)", buf, SETTINGS_VOLUME_MIN, SETTINGS_VOLUME_MAX,
                           volume0, slider_w, slider_y);
    }

    ui->settings_card_sleep = settings_card_create(ui->settings, card_x, y0 + 4*(card_h+gap), card_w, card_h);
    settings_row_build(ui->settings_card_sleep, &ui->settings_label_sleep, &ui->settings_label_sleep_val,
                       &ui->settings_slider_sleep, "熄屏时间", settings_sleep_text(sleep0),
                       0, 5, sleep0, slider_w, slider_y);

    //The custom code of settings.

    lv_obj_update_layout(ui->settings);
}
