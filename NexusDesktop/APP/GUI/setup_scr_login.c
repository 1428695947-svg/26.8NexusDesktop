/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#include "lvgl.h"
#include <stdio.h>
#include "gui_guider.h"
#include "events_init.h"
#include "widgets_init.h"
#include "custom.h"



void setup_scr_login(lv_ui *ui)
{
    //Write codes login
    ui->login = lv_obj_create(NULL);
    lv_obj_set_size(ui->login, 320, 480);
    lv_obj_set_scrollbar_mode(ui->login, LV_SCROLLBAR_MODE_OFF);

    //Write style for login, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(ui->login, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->login, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->login, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes login_btn_login
    ui->login_btn_login = lv_btn_create(ui->login);
    ui->login_btn_login_label = lv_label_create(ui->login_btn_login);
    lv_label_set_text(ui->login_btn_login_label, "登 录");
    lv_label_set_long_mode(ui->login_btn_login_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui->login_btn_login_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(ui->login_btn_login, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(ui->login_btn_login_label, LV_PCT(100));
    lv_obj_set_pos(ui->login_btn_login, 40, 340);
    lv_obj_set_size(ui->login_btn_login, 240, 50);

    //Write style for login_btn_login, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(ui->login_btn_login, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->login_btn_login, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->login_btn_login, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->login_btn_login, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->login_btn_login, 25, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->login_btn_login, 8, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui->login_btn_login, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui->login_btn_login, 60, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui->login_btn_login, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_x(ui->login_btn_login, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(ui->login_btn_login, 4, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->login_btn_login, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->login_btn_login, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->login_btn_login, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->login_btn_login, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write style for login_btn_login, Part: LV_PART_MAIN, State: LV_STATE_PRESSED.
    lv_obj_set_style_bg_opa(ui->login_btn_login, 255, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui->login_btn_login, lv_color_hex(0x1a7cd6), LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_dir(ui->login_btn_login, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_border_width(ui->login_btn_login, 0, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_radius(ui->login_btn_login, 25, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(ui->login_btn_login, 0, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_text_color(ui->login_btn_login, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_text_font(ui->login_btn_login, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_text_opa(ui->login_btn_login, 255, LV_PART_MAIN|LV_STATE_PRESSED);

    //Write codes login_cb_show_pwd
    ui->login_cb_show_pwd = lv_checkbox_create(ui->login);
    lv_checkbox_set_text(ui->login_cb_show_pwd, "显示密码");
    lv_obj_set_pos(ui->login_cb_show_pwd, 40, 268);

    //Write style for login_cb_show_pwd, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_pad_top(ui->login_cb_show_pwd, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->login_cb_show_pwd, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->login_cb_show_pwd, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->login_cb_show_pwd, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->login_cb_show_pwd, lv_color_hex(0x555a66), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->login_cb_show_pwd, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->login_cb_show_pwd, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->login_cb_show_pwd, 1, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->login_cb_show_pwd, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->login_cb_show_pwd, 6, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->login_cb_show_pwd, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->login_cb_show_pwd, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write style for login_cb_show_pwd, Part: LV_PART_INDICATOR, State: LV_STATE_DEFAULT.
    lv_obj_set_style_pad_all(ui->login_cb_show_pwd, 2, LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->login_cb_show_pwd, 2, LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui->login_cb_show_pwd, 255, LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui->login_cb_show_pwd, lv_color_hex(0xc9d1e0), LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(ui->login_cb_show_pwd, LV_BORDER_SIDE_FULL, LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->login_cb_show_pwd, 5, LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->login_cb_show_pwd, 255, LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->login_cb_show_pwd, lv_color_hex(0xffffff), LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->login_cb_show_pwd, LV_GRAD_DIR_NONE, LV_PART_INDICATOR|LV_STATE_DEFAULT);

    //Write style for login_cb_show_pwd, Part: LV_PART_INDICATOR, State: LV_STATE_CHECKED.
    lv_obj_set_style_pad_all(ui->login_cb_show_pwd, 2, LV_PART_INDICATOR|LV_STATE_CHECKED);
    lv_obj_set_style_border_width(ui->login_cb_show_pwd, 2, LV_PART_INDICATOR|LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(ui->login_cb_show_pwd, 255, LV_PART_INDICATOR|LV_STATE_CHECKED);
    lv_obj_set_style_border_color(ui->login_cb_show_pwd, lv_color_hex(0x1a7cd6), LV_PART_INDICATOR|LV_STATE_CHECKED);
    lv_obj_set_style_border_side(ui->login_cb_show_pwd, LV_BORDER_SIDE_FULL, LV_PART_INDICATOR|LV_STATE_CHECKED);
    lv_obj_set_style_radius(ui->login_cb_show_pwd, 5, LV_PART_INDICATOR|LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(ui->login_cb_show_pwd, 255, LV_PART_INDICATOR|LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(ui->login_cb_show_pwd, lv_color_hex(0x2195f6), LV_PART_INDICATOR|LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_dir(ui->login_cb_show_pwd, LV_GRAD_DIR_NONE, LV_PART_INDICATOR|LV_STATE_CHECKED);

    //Write codes login_ta_password
    ui->login_ta_password = lv_textarea_create(ui->login);
    lv_textarea_set_text(ui->login_ta_password, "");
    lv_textarea_set_placeholder_text(ui->login_ta_password, "请输入密码");
    lv_textarea_set_password_bullet(ui->login_ta_password, "*");
    lv_textarea_set_password_mode(ui->login_ta_password, true);
    lv_textarea_set_one_line(ui->login_ta_password, true);
    lv_textarea_set_accepted_chars(ui->login_ta_password, "");
    lv_textarea_set_max_length(ui->login_ta_password, 20);
#if LV_USE_KEYBOARD != 0 || LV_USE_ZH_KEYBOARD != 0
    lv_obj_add_event_cb(ui->login_ta_password, ta_event_cb, LV_EVENT_ALL, ui->g_kb_top_layer);
#endif
    lv_obj_set_pos(ui->login_ta_password, 40, 200);
    lv_obj_set_size(ui->login_ta_password, 240, 48);

    //Write style for login_ta_password, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_text_color(ui->login_ta_password, lv_color_hex(0x1a1a2e), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->login_ta_password, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->login_ta_password, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->login_ta_password, 1, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->login_ta_password, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->login_ta_password, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->login_ta_password, lv_color_hex(0xf2f4f8), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->login_ta_password, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->login_ta_password, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui->login_ta_password, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui->login_ta_password, lv_color_hex(0xc9d1e0), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(ui->login_ta_password, LV_BORDER_SIDE_FULL, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->login_ta_password, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->login_ta_password, 10, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->login_ta_password, 10, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->login_ta_password, 12, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->login_ta_password, 10, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write style for login_ta_password, Part: LV_PART_SCROLLBAR, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(ui->login_ta_password, 255, LV_PART_SCROLLBAR|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->login_ta_password, lv_color_hex(0xc9d1e0), LV_PART_SCROLLBAR|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->login_ta_password, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->login_ta_password, 3, LV_PART_SCROLLBAR|LV_STATE_DEFAULT);

    //Write codes login_label_hint
    ui->login_label_hint = lv_label_create(ui->login);
    lv_label_set_text(ui->login_label_hint, "请输入密码登录");
    lv_label_set_long_mode(ui->login_label_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(ui->login_label_hint, 60, 145);
    lv_obj_set_size(ui->login_label_hint, 200, 30);

    //Write style for login_label_hint, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_border_width(ui->login_label_hint, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->login_label_hint, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->login_label_hint, lv_color_hex(0x8a8f9c), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->login_label_hint, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->login_label_hint, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->login_label_hint, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui->login_label_hint, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->login_label_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->login_label_hint, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->login_label_hint, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->login_label_hint, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->login_label_hint, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->login_label_hint, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->login_label_hint, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes login_label_title
    ui->login_label_title = lv_label_create(ui->login);
    lv_label_set_text(ui->login_label_title, "欢迎使用");
    lv_label_set_long_mode(ui->login_label_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(ui->login_label_title, 60, 80);
    lv_obj_set_size(ui->login_label_title, 200, 44);

    //Write style for login_label_title, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_border_width(ui->login_label_title, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->login_label_title, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->login_label_title, lv_color_hex(0x1a1a2e), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->login_label_title, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->login_label_title, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->login_label_title, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui->login_label_title, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->login_label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->login_label_title, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->login_label_title, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->login_label_title, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->login_label_title, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->login_label_title, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->login_label_title, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes login_btn_switch (鼠标样式切换, 左上角)
    ui->login_btn_switch = lv_btn_create(ui->login);
    ui->login_btn_switch_label = lv_label_create(ui->login_btn_switch);
    lv_label_set_text(ui->login_btn_switch_label, "鼠标:猫");
    lv_label_set_long_mode(ui->login_btn_switch_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui->login_btn_switch_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(ui->login_btn_switch, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(ui->login_btn_switch_label, LV_PCT(100));
    lv_obj_set_pos(ui->login_btn_switch, 10, 15);
    lv_obj_set_size(ui->login_btn_switch, 120, 34);
    lv_obj_set_style_bg_opa(ui->login_btn_switch, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->login_btn_switch, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->login_btn_switch, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->login_btn_switch, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->login_btn_switch, 8, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->login_btn_switch, 4, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui->login_btn_switch, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui->login_btn_switch, 40, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui->login_btn_switch, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_x(ui->login_btn_switch, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(ui->login_btn_switch, 3, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->login_btn_switch, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->login_btn_switch, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->login_btn_switch, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->login_btn_switch, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->login_btn_switch, lv_color_hex(0x1a7cd6), LV_PART_MAIN|LV_STATE_PRESSED);

    //The custom code of login.


    //Update current screen layout.
    lv_obj_update_layout(ui->login);

}
