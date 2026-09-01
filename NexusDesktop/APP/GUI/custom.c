/*
* Copyright 2023 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

/*
 * Adapted for the STM32 embedded target:
 *  - Removed the SDL2 dependency (SDL_ShowCursor) used by the simulator.
 *  - The cat cursor is positioned by the APP input layer (joystick/touch).
 *  - The login button now verifies the password held in flash before entering
 *    the desktop, and locks (returns to the login screen) clear the password.
 */


/*********************
 *      INCLUDES
 *********************/
#include "lvgl.h"
#include "custom.h"
#include "cursor_hotspot.h"
#include "tom_hotspot.h"
#include "hand_hotspot.h"
#include "flash_store.h"
#include "app.h"
#include <string.h>

/**********************
 *  STATIC VARIABLES
 **********************/
static bool desktop_evts_done = false;
static lv_obj_t * g_cursor_img = NULL;
static lv_obj_t * g_tom_img = NULL;
static lv_obj_t * g_hand_img = NULL;
static int      g_cursor_style = 0;       /* 0 = 小猫, 1 = 汤姆猫, 2 = 手指手 */
static int      s_cursor_x = 160;
static int      s_cursor_y = 240;
static int      s_error_count = 0;

/* The custom mouse cursor image (see img_cat_cursor.c). */
extern const lv_img_dsc_t img_cat_cursor;
extern const lv_img_dsc_t img_tom_cursor;
extern const lv_img_dsc_t img_hand_cursor;

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Return to the lock (login) screen and wipe the entered password.
 */
static void lock_screen(lv_ui * ui)
{
    if (ui->login_ta_password) {
        lv_textarea_set_text(ui->login_ta_password, "");
        lv_textarea_set_password_mode(ui->login_ta_password, true);
    }
    if (ui->login_cb_show_pwd) {
        lv_obj_clear_state(ui->login_cb_show_pwd, LV_STATE_CHECKED);
    }
    if (ui->g_kb_top_layer) {
        lv_obj_add_flag(ui->g_kb_top_layer, LV_OBJ_FLAG_HIDDEN);
    }
    /* Clear the wrong-password hint and counter. */
    if (ui->login_label_hint) {
        lv_label_set_text(ui->login_label_hint, "请输入密码登录");
    }
    s_error_count = 0;
    /* Collapse the desktop menu (hide the lock/shutdown options). */
    if (ui->desktop_btn_lock) {
        lv_obj_add_flag(ui->desktop_btn_lock, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui->desktop_btn_shutdown) {
        lv_obj_add_flag(ui->desktop_btn_shutdown, LV_OBJ_FLAG_HIDDEN);
    }
    lv_scr_load(ui->login);
}

static void show_pwd_event_cb(lv_event_t * e)
{
    lv_obj_t * ta = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_t * cb = lv_event_get_current_target(e);
    bool checked = (lv_obj_get_state(cb) & LV_STATE_CHECKED) != 0;

    /* checked -> show plain text, unchecked -> hide as bullets */
    lv_textarea_set_password_mode(ta, !checked);
}

static void menu_btn_event_cb(lv_event_t * e)
{
    lv_ui * ui = (lv_ui *)lv_event_get_user_data(e);
    bool hidden = lv_obj_has_flag(ui->desktop_btn_lock, LV_OBJ_FLAG_HIDDEN);
    if (hidden) {
        lv_obj_clear_flag(ui->desktop_btn_lock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui->desktop_btn_shutdown, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(ui->desktop_btn_lock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->desktop_btn_shutdown, LV_OBJ_FLAG_HIDDEN);
    }
}

static void lock_btn_event_cb(lv_event_t * e)
{
    lock_screen((lv_ui *)lv_event_get_user_data(e));
}

static void shutdown_btn_event_cb(lv_event_t * e)
{
    (void)e;
    App_EnterPowerOff();    /* 关机 -> 熄屏, 摇杆移动或按键按下唤醒 */
}

static const char * cursor_name(int style)
{
    return (style == 0) ? "鼠标:猫"
         : (style == 1) ? "鼠标:汤姆"
         : "鼠标:手";
}

static void mouse_switch_event_cb(lv_event_t * e)
{
    lv_obj_t * lbl = (lv_obj_t *)lv_event_get_user_data(e);
    if (!App_IsMouseConnected()) {
        return;             /* 鼠标未连接时不允许切换图案 */
    }
    gui_cursor_cycle();
    if (lbl != NULL) {
        lv_label_set_text(lbl, cursor_name(g_cursor_style));
    }
}

static void register_desktop_events(lv_ui * ui)
{
    if (desktop_evts_done) return;
    desktop_evts_done = true;

    if (ui->desktop_btn_menu != NULL) {
        lv_obj_add_event_cb(ui->desktop_btn_menu, menu_btn_event_cb, LV_EVENT_CLICKED, ui);
    }
    if (ui->desktop_btn_lock != NULL) {
        lv_obj_add_event_cb(ui->desktop_btn_lock, lock_btn_event_cb, LV_EVENT_CLICKED, ui);
    }
    if (ui->desktop_btn_shutdown != NULL) {
        lv_obj_add_event_cb(ui->desktop_btn_shutdown, shutdown_btn_event_cb, LV_EVENT_CLICKED, ui);
    }
}

static void enter_desktop(lv_ui * ui)
{
    if (ui->desktop == NULL) {
        setup_scr_desktop(ui);
    }
    register_desktop_events(ui);
    lv_scr_load(ui->desktop);
}

static void login_event_cb(lv_event_t * e)
{
    lv_ui * ui = (lv_ui *)lv_event_get_user_data(e);
    const char * entered = lv_textarea_get_text(ui->login_ta_password);
    const char * stored = FlashStore_GetPassword();

    if (stored != NULL && entered != NULL && strcmp(entered, stored) == 0)
    {
        s_error_count = 0;                      /* Correct password: reset counter */
        enter_desktop(ui);
    }
    else
    {
        /* Wrong password: clear the field and show the number of failed attempts. */
        s_error_count++;
        lv_textarea_set_text(ui->login_ta_password, "");
        if (ui->login_label_hint != NULL) {
            char msg[40];
            lv_snprintf(msg, sizeof(msg), "密码错误，已输错 %d 次", s_error_count);
            lv_label_set_text(ui->login_label_hint, msg);
        }
    }
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Create both mouse cursors (cat / Tom) on the system layer (hidden until positioned).
 */
void gui_cursor_init(void)
{
    if (g_cursor_img == NULL) {
        g_cursor_img = lv_img_create(lv_layer_sys());
        lv_img_set_src(g_cursor_img, &img_cat_cursor);
        lv_obj_clear_flag(g_cursor_img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(g_cursor_img,
                        LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN);
    }
    if (g_tom_img == NULL) {
        g_tom_img = lv_img_create(lv_layer_sys());
        lv_img_set_src(g_tom_img, &img_tom_cursor);
        lv_obj_clear_flag(g_tom_img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(g_tom_img,
                        LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN);
    }
    if (g_hand_img == NULL) {
        g_hand_img = lv_img_create(lv_layer_sys());
        lv_img_set_src(g_hand_img, &img_hand_cursor);
        lv_obj_clear_flag(g_hand_img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(g_hand_img,
                        LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN);
    }
    g_cursor_style = 0;
}

/**
 * Public interface: place the active cursor so its hotspot lands on (x, y).
 * Call this from your own input handling whenever the cursor should move.
 */
void gui_cursor_set_pos(int x, int y)
{
    lv_obj_t * imgs[3] = { g_cursor_img, g_tom_img, g_hand_img };
    int hx = CURSOR_HOTSPOT_X;
    int hy = CURSOR_HOTSPOT_Y;
    int i;

    s_cursor_x = x;
    s_cursor_y = y;

    switch (g_cursor_style) {
        case 1: hx = TOM_HOTSPOT_X; hy = TOM_HOTSPOT_Y; break;
        case 2: hx = HAND_HOTSPOT_X; hy = HAND_HOTSPOT_Y; break;
        default: break;
    }

    for (i = 0; i < 3; i++) {
        if (imgs[i] == NULL) continue;
        if (i == g_cursor_style) {
            lv_obj_clear_flag(imgs[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(imgs[i], x - hx, y - hy);
        }
        else {
            lv_obj_add_flag(imgs[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/**
 * Cycle the mouse cursor style (cat <-> Tom) and re-position at the current spot.
 */
void gui_cursor_cycle(void)
{
    g_cursor_style = (g_cursor_style + 1) % 3;
    gui_cursor_set_pos(s_cursor_x, s_cursor_y);
}

/**
 * Hide both cursors when they should not be shown.
 */
void gui_cursor_hide(void)
{
    if (g_cursor_img == NULL) return;
    lv_obj_add_flag(g_cursor_img, LV_OBJ_FLAG_HIDDEN);
    if (g_tom_img != NULL) {
        lv_obj_add_flag(g_tom_img, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_hand_img != NULL) {
        lv_obj_add_flag(g_hand_img, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * 返回登录界面并清空密码 (唤醒/锁定后需重新输入密码)。
 */
void gui_lock_screen(void)
{
    lock_screen(&guider_ui);
}

/**
 * 根据摇杆(鼠标)连接状态更新左上角切换按钮。
 * 未连接: 显示"鼠标未连接"并禁用按钮; 已连接: 显示当前图案并启用。
 */
void gui_update_mouse_conn(uint8_t connected)
{
    lv_obj_t * btn = guider_ui.login_btn_switch;
    lv_obj_t * lbl = guider_ui.login_btn_switch_label;

    if (connected) {
        if (btn != NULL) {
            lv_obj_clear_state(btn, LV_STATE_DISABLED);
        }
        if (lbl != NULL) {
            lv_label_set_text(lbl, cursor_name(g_cursor_style));
        }
    }
    else {
        if (btn != NULL) {
            lv_obj_add_state(btn, LV_STATE_DISABLED);
        }
        if (lbl != NULL) {
            lv_label_set_text(lbl, "鼠标未连接");
        }
    }
}

void custom_init(lv_ui *ui)
{
    /* Add your codes here */
    gui_cursor_init();

    if (ui->login_cb_show_pwd != NULL && ui->login_ta_password != NULL) {
        lv_obj_add_event_cb(ui->login_cb_show_pwd, show_pwd_event_cb, LV_EVENT_VALUE_CHANGED, ui->login_ta_password);
    }

    if (ui->login_btn_login != NULL) {
        lv_obj_add_event_cb(ui->login_btn_login, login_event_cb, LV_EVENT_CLICKED, ui);
    }

    /* 鼠标样式切换: 绑定左上角按钮, 点击时切换 小猫 <-> 汤姆猫 */
    if (ui->login_btn_switch != NULL) {
        lv_obj_add_event_cb(ui->login_btn_switch, mouse_switch_event_cb, LV_EVENT_CLICKED,
                            ui->login_btn_switch_label);
    }
}
