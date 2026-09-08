/**
  ******************************************************************************
  * @file    app_boot.c
  * @brief   启动界面控制 - 调度器启动后短暂展示并进入登录页
  ******************************************************************************
  */

#include "app_boot.h"

#include "lvgl.h"
#include "gui_guider.h"
#include "custom.h"
#include "app_mouse.h"

#define BOOT_SHOW_MS 1200U

static lv_obj_t *s_boot_screen = NULL;
static lv_timer_t *s_boot_timer = NULL;

static void boot_finish_cb(lv_timer_t *timer)
{
    (void)timer;

    if (guider_ui.login != NULL) {
        lv_scr_load(guider_ui.login);
    }
    if (App_IsMouseConnected()) {
        gui_cursor_set_pos(g_mouse_x, g_mouse_y);
    }
    if (s_boot_screen != NULL) {
        lv_obj_del_async(s_boot_screen);
        s_boot_screen = NULL;
    }
    s_boot_timer = NULL;
}

void App_BootStart(void)
{
    lv_obj_t *title;
    lv_obj_t *subtitle;
    lv_obj_t *bar;

    s_boot_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_boot_screen, 320, 480);
    lv_obj_set_style_bg_color(s_boot_screen, lv_color_hex(0x0f172a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_boot_screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    title = lv_label_create(s_boot_screen);
    lv_label_set_text(title, "NEXUS DESKTOP");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xf8fafc), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -42);

    subtitle = lv_label_create(s_boot_screen);
    lv_label_set_text(subtitle, "FreeRTOS starting...");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x94a3b8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 2);

    bar = lv_obj_create(s_boot_screen);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 190, 4);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x38bdf8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 38);

    gui_cursor_hide();
    lv_scr_load(s_boot_screen);
    s_boot_timer = lv_timer_create(boot_finish_cb, BOOT_SHOW_MS, NULL);
    if (s_boot_timer != NULL) {
        lv_timer_set_repeat_count(s_boot_timer, 1);
    }
}
