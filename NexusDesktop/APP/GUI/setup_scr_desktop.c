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



void setup_scr_desktop(lv_ui *ui)
{
    //Write codes desktop
    ui->desktop = lv_obj_create(NULL);
    lv_obj_set_size(ui->desktop, 320, 480);
    lv_obj_set_scrollbar_mode(ui->desktop, LV_SCROLLBAR_MODE_OFF);

    //Write style for desktop, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(ui->desktop, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->desktop, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->desktop, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes desktop_btn_shutdown
    ui->desktop_btn_shutdown = lv_btn_create(ui->desktop);
    ui->desktop_btn_shutdown_label = lv_label_create(ui->desktop_btn_shutdown);
    lv_label_set_text(ui->desktop_btn_shutdown_label, "关机");
    lv_label_set_long_mode(ui->desktop_btn_shutdown_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui->desktop_btn_shutdown_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(ui->desktop_btn_shutdown, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(ui->desktop_btn_shutdown_label, LV_PCT(100));
    lv_obj_set_pos(ui->desktop_btn_shutdown, 10, 338);
    lv_obj_set_size(ui->desktop_btn_shutdown, 100, 36);
    lv_obj_add_flag(ui->desktop_btn_shutdown, LV_OBJ_FLAG_HIDDEN);

    //Write style for desktop_btn_shutdown, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(ui->desktop_btn_shutdown, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->desktop_btn_shutdown, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->desktop_btn_shutdown, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->desktop_btn_shutdown, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->desktop_btn_shutdown, 8, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->desktop_btn_shutdown, 6, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui->desktop_btn_shutdown, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui->desktop_btn_shutdown, 40, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui->desktop_btn_shutdown, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_x(ui->desktop_btn_shutdown, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(ui->desktop_btn_shutdown, 3, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->desktop_btn_shutdown, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->desktop_btn_shutdown, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->desktop_btn_shutdown, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->desktop_btn_shutdown, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write style for desktop_btn_shutdown, Part: LV_PART_MAIN, State: LV_STATE_PRESSED.
    lv_obj_set_style_bg_opa(ui->desktop_btn_shutdown, 255, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui->desktop_btn_shutdown, lv_color_hex(0x1a7cd6), LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_dir(ui->desktop_btn_shutdown, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_border_width(ui->desktop_btn_shutdown, 0, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_radius(ui->desktop_btn_shutdown, 8, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(ui->desktop_btn_shutdown, 0, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_text_color(ui->desktop_btn_shutdown, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_text_font(ui->desktop_btn_shutdown, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_text_opa(ui->desktop_btn_shutdown, 255, LV_PART_MAIN|LV_STATE_PRESSED);

    //Write codes desktop_btn_lock
    ui->desktop_btn_lock = lv_btn_create(ui->desktop);
    ui->desktop_btn_lock_label = lv_label_create(ui->desktop_btn_lock);
    lv_label_set_text(ui->desktop_btn_lock_label, "登录");
    lv_label_set_long_mode(ui->desktop_btn_lock_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui->desktop_btn_lock_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(ui->desktop_btn_lock, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(ui->desktop_btn_lock_label, LV_PCT(100));
    lv_obj_set_pos(ui->desktop_btn_lock, 10, 384);
    lv_obj_set_size(ui->desktop_btn_lock, 100, 36);
    lv_obj_add_flag(ui->desktop_btn_lock, LV_OBJ_FLAG_HIDDEN);

    //Write style for desktop_btn_lock, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(ui->desktop_btn_lock, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->desktop_btn_lock, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->desktop_btn_lock, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->desktop_btn_lock, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->desktop_btn_lock, 8, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->desktop_btn_lock, 6, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui->desktop_btn_lock, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui->desktop_btn_lock, 40, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui->desktop_btn_lock, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_x(ui->desktop_btn_lock, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(ui->desktop_btn_lock, 3, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->desktop_btn_lock, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->desktop_btn_lock, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->desktop_btn_lock, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->desktop_btn_lock, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write style for desktop_btn_lock, Part: LV_PART_MAIN, State: LV_STATE_PRESSED.
    lv_obj_set_style_bg_opa(ui->desktop_btn_lock, 255, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui->desktop_btn_lock, lv_color_hex(0x1a7cd6), LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_dir(ui->desktop_btn_lock, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_border_width(ui->desktop_btn_lock, 0, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_radius(ui->desktop_btn_lock, 8, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(ui->desktop_btn_lock, 0, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_text_color(ui->desktop_btn_lock, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_text_font(ui->desktop_btn_lock, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_text_opa(ui->desktop_btn_lock, 255, LV_PART_MAIN|LV_STATE_PRESSED);

    //Write codes desktop_btn_menu
    ui->desktop_btn_menu = lv_btn_create(ui->desktop);
    ui->desktop_btn_menu_label = lv_label_create(ui->desktop_btn_menu);
    lv_label_set_text(ui->desktop_btn_menu_label, "菜单");
    lv_label_set_long_mode(ui->desktop_btn_menu_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui->desktop_btn_menu_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(ui->desktop_btn_menu, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(ui->desktop_btn_menu_label, LV_PCT(100));
    lv_obj_set_pos(ui->desktop_btn_menu, 10, 430);
    lv_obj_set_size(ui->desktop_btn_menu, 100, 40);

    //Write style for desktop_btn_menu, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(ui->desktop_btn_menu, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->desktop_btn_menu, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->desktop_btn_menu, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->desktop_btn_menu, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->desktop_btn_menu, 8, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->desktop_btn_menu, 6, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui->desktop_btn_menu, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui->desktop_btn_menu, 40, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui->desktop_btn_menu, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_x(ui->desktop_btn_menu, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(ui->desktop_btn_menu, 3, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->desktop_btn_menu, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->desktop_btn_menu, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->desktop_btn_menu, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->desktop_btn_menu, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write style for desktop_btn_menu, Part: LV_PART_MAIN, State: LV_STATE_PRESSED.
    lv_obj_set_style_bg_opa(ui->desktop_btn_menu, 255, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui->desktop_btn_menu, lv_color_hex(0x1a7cd6), LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_dir(ui->desktop_btn_menu, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_border_width(ui->desktop_btn_menu, 0, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_radius(ui->desktop_btn_menu, 8, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(ui->desktop_btn_menu, 0, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_text_color(ui->desktop_btn_menu, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_text_font(ui->desktop_btn_menu, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_text_opa(ui->desktop_btn_menu, 255, LV_PART_MAIN|LV_STATE_PRESSED);

    //Write codes desktop_btn_switch (鼠标连接/图案切换, 与登录界面同位置)
    ui->desktop_btn_switch = lv_btn_create(ui->desktop);
    ui->desktop_btn_switch_label = lv_label_create(ui->desktop_btn_switch);
    lv_label_set_text(ui->desktop_btn_switch_label, "鼠标:猫");
    lv_label_set_long_mode(ui->desktop_btn_switch_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui->desktop_btn_switch_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(ui->desktop_btn_switch, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(ui->desktop_btn_switch_label, LV_PCT(100));
    lv_obj_set_pos(ui->desktop_btn_switch, 10, 15);
    lv_obj_set_size(ui->desktop_btn_switch, 120, 34);
    lv_obj_set_style_bg_opa(ui->desktop_btn_switch, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->desktop_btn_switch, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->desktop_btn_switch, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->desktop_btn_switch, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->desktop_btn_switch, 8, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->desktop_btn_switch, 4, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui->desktop_btn_switch, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui->desktop_btn_switch, 40, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui->desktop_btn_switch, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_x(ui->desktop_btn_switch, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(ui->desktop_btn_switch, 3, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->desktop_btn_switch, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->desktop_btn_switch, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->desktop_btn_switch, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->desktop_btn_switch, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->desktop_btn_switch, lv_color_hex(0x1a7cd6), LV_PART_MAIN|LV_STATE_PRESSED);

    //Write codes desktop_btn_settings (系统设置入口, 右上角)
    ui->desktop_btn_settings = lv_btn_create(ui->desktop);
    ui->desktop_btn_settings_label = lv_label_create(ui->desktop_btn_settings);
    lv_label_set_text(ui->desktop_btn_settings_label, "设置");
    lv_label_set_long_mode(ui->desktop_btn_settings_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui->desktop_btn_settings_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(ui->desktop_btn_settings, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(ui->desktop_btn_settings_label, LV_PCT(100));
    lv_obj_set_pos(ui->desktop_btn_settings, 136, 15);
    lv_obj_set_size(ui->desktop_btn_settings, 84, 34);
    lv_obj_set_style_bg_opa(ui->desktop_btn_settings, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->desktop_btn_settings, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->desktop_btn_settings, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->desktop_btn_settings, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->desktop_btn_settings, 8, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->desktop_btn_settings, 4, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui->desktop_btn_settings, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui->desktop_btn_settings, 40, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui->desktop_btn_settings, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_x(ui->desktop_btn_settings, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(ui->desktop_btn_settings, 3, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->desktop_btn_settings, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->desktop_btn_settings, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->desktop_btn_settings, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->desktop_btn_settings, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->desktop_btn_settings, lv_color_hex(0x1a7cd6), LV_PART_MAIN|LV_STATE_PRESSED);

    //Write codes desktop_btn_draw (画图应用入口, 右上角)
    ui->desktop_btn_draw = lv_btn_create(ui->desktop);
    ui->desktop_btn_draw_label = lv_label_create(ui->desktop_btn_draw);
    lv_label_set_text(ui->desktop_btn_draw_label, "画图");
    lv_label_set_long_mode(ui->desktop_btn_draw_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui->desktop_btn_draw_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(ui->desktop_btn_draw, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(ui->desktop_btn_draw_label, LV_PCT(100));
    lv_obj_set_pos(ui->desktop_btn_draw, 224, 15);
    lv_obj_set_size(ui->desktop_btn_draw, 84, 34);
    lv_obj_set_style_bg_opa(ui->desktop_btn_draw, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->desktop_btn_draw, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->desktop_btn_draw, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->desktop_btn_draw, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->desktop_btn_draw, 8, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->desktop_btn_draw, 4, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui->desktop_btn_draw, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui->desktop_btn_draw, 40, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui->desktop_btn_draw, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_x(ui->desktop_btn_draw, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(ui->desktop_btn_draw, 3, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->desktop_btn_draw, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->desktop_btn_draw, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->desktop_btn_draw, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->desktop_btn_draw, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->desktop_btn_draw, lv_color_hex(0x1a7cd6), LV_PART_MAIN|LV_STATE_PRESSED);

    //Write codes desktop_btn_file (文件管理应用入口)
    ui->desktop_btn_file = lv_btn_create(ui->desktop);
    ui->desktop_btn_file_label = lv_label_create(ui->desktop_btn_file);
    lv_label_set_text(ui->desktop_btn_file_label, "文件管理");
    lv_label_set_long_mode(ui->desktop_btn_file_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui->desktop_btn_file_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(ui->desktop_btn_file, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(ui->desktop_btn_file_label, LV_PCT(100));
    lv_obj_set_pos(ui->desktop_btn_file, 130, 70);
    lv_obj_set_size(ui->desktop_btn_file, 180, 64);
    lv_obj_set_style_bg_opa(ui->desktop_btn_file, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->desktop_btn_file, lv_color_hex(0x16a34a), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->desktop_btn_file, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->desktop_btn_file, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->desktop_btn_file, 8, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->desktop_btn_file, 4, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui->desktop_btn_file, lv_color_hex(0x16a34a), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui->desktop_btn_file, 40, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui->desktop_btn_file, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_x(ui->desktop_btn_file, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(ui->desktop_btn_file, 3, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->desktop_btn_file, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->desktop_btn_file, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->desktop_btn_file, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->desktop_btn_file, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->desktop_btn_file, lv_color_hex(0x0e7f35), LV_PART_MAIN|LV_STATE_PRESSED);

    //Write codes desktop_btn_log (系统日志应用入口)
    ui->desktop_btn_log = lv_btn_create(ui->desktop);
    ui->desktop_btn_log_label = lv_label_create(ui->desktop_btn_log);
    lv_label_set_text(ui->desktop_btn_log_label, "系统日志");
    lv_label_set_long_mode(ui->desktop_btn_log_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui->desktop_btn_log_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(ui->desktop_btn_log, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(ui->desktop_btn_log_label, LV_PCT(100));
    lv_obj_set_pos(ui->desktop_btn_log, 130, 146);
    lv_obj_set_size(ui->desktop_btn_log, 180, 64);
    lv_obj_set_style_bg_opa(ui->desktop_btn_log, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->desktop_btn_log, lv_color_hex(0xf59e0b), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->desktop_btn_log, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->desktop_btn_log, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->desktop_btn_log, 8, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->desktop_btn_log, 4, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui->desktop_btn_log, lv_color_hex(0xf59e0b), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui->desktop_btn_log, 40, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui->desktop_btn_log, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_x(ui->desktop_btn_log, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(ui->desktop_btn_log, 3, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->desktop_btn_log, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->desktop_btn_log, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->desktop_btn_log, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->desktop_btn_log, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->desktop_btn_log, lv_color_hex(0xc07f08), LV_PART_MAIN|LV_STATE_PRESSED);

    //Write codes desktop_btn_monitor (系统监控应用入口)
    ui->desktop_btn_monitor = lv_btn_create(ui->desktop);
    ui->desktop_btn_monitor_label = lv_label_create(ui->desktop_btn_monitor);
    lv_label_set_text(ui->desktop_btn_monitor_label, "系统监控");
    lv_label_set_long_mode(ui->desktop_btn_monitor_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui->desktop_btn_monitor_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(ui->desktop_btn_monitor, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(ui->desktop_btn_monitor_label, LV_PCT(100));
    lv_obj_set_pos(ui->desktop_btn_monitor, 130, 222);
    lv_obj_set_size(ui->desktop_btn_monitor, 180, 64);
    lv_obj_set_style_bg_opa(ui->desktop_btn_monitor, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->desktop_btn_monitor, lv_color_hex(0x8b5cf6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->desktop_btn_monitor, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->desktop_btn_monitor, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->desktop_btn_monitor, 8, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->desktop_btn_monitor, 4, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui->desktop_btn_monitor, lv_color_hex(0x8b5cf6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui->desktop_btn_monitor, 40, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui->desktop_btn_monitor, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_x(ui->desktop_btn_monitor, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(ui->desktop_btn_monitor, 3, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->desktop_btn_monitor, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->desktop_btn_monitor, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->desktop_btn_monitor, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->desktop_btn_monitor, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->desktop_btn_monitor, lv_color_hex(0x713fe0), LV_PART_MAIN|LV_STATE_PRESSED);

    //Write codes desktop_btn_music (音乐应用占位入口)
    ui->desktop_btn_music = lv_btn_create(ui->desktop);
    ui->desktop_btn_music_label = lv_label_create(ui->desktop_btn_music);
    lv_label_set_text(ui->desktop_btn_music_label, "音乐");
    lv_obj_align(ui->desktop_btn_music_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_pos(ui->desktop_btn_music, 130, 298);
    lv_obj_set_size(ui->desktop_btn_music, 86, 64);
    lv_obj_set_style_bg_color(ui->desktop_btn_music, lv_color_hex(0x475569), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->desktop_btn_music, lv_color_hex(0x334155), LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_border_width(ui->desktop_btn_music, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->desktop_btn_music, 8, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->desktop_btn_music, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->desktop_btn_music, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes desktop_btn_update (版本状态/伪 OTA 入口)
    ui->desktop_btn_update = lv_btn_create(ui->desktop);
    ui->desktop_btn_update_label = lv_label_create(ui->desktop_btn_update);
    lv_label_set_text(ui->desktop_btn_update_label, "更新");
    lv_obj_align(ui->desktop_btn_update_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_pos(ui->desktop_btn_update, 224, 298);
    lv_obj_set_size(ui->desktop_btn_update, 86, 64);
    lv_obj_set_style_bg_color(ui->desktop_btn_update, lv_color_hex(0x0f766e), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->desktop_btn_update, lv_color_hex(0x115e59), LV_PART_MAIN|LV_STATE_PRESSED);
    lv_obj_set_style_border_width(ui->desktop_btn_update, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->desktop_btn_update, 8, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->desktop_btn_update, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->desktop_btn_update, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes desktop_label_uptime (开机运行时长)
    ui->desktop_label_uptime = lv_label_create(ui->desktop);
    lv_label_set_text(ui->desktop_label_uptime, "UP 000:00:00");
    lv_obj_set_pos(ui->desktop_label_uptime, 130, 378);
    lv_obj_set_size(ui->desktop_label_uptime, 180, 24);
    lv_obj_set_style_text_align(ui->desktop_label_uptime, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->desktop_label_uptime, &lv_font_montserrat_14, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->desktop_label_uptime, lv_color_hex(0x334155), LV_PART_MAIN|LV_STATE_DEFAULT);

    //The custom code of desktop.


    //Update current screen layout.
    lv_obj_update_layout(ui->desktop);

}
