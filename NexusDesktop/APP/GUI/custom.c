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
#include "user_store.h"
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

/* 系统设置运行状态 */
static int          g_cursor_zoom = 100;       /* 光标缩放百分比 100..200 */
static lv_obj_t    *g_bright_overlay = NULL;   /* 亮度遮罩 (lv_layer_top) */
static uint8_t      g_settings_dirty = 0;      /* 1=设置已修改但未写回 Flash */
static SysSettings_t g_edit_settings;          /* 编辑中的设置值 */
static uint8_t      s_cur_brightness = SETTINGS_BRIGHT_DEFAULT; /* 当前生效亮度 */
static volatile uint8_t s_cal_request = 0;     /* 触摸校准请求标志 (LVGL 任务消费) */
static volatile uint8_t s_save_request = 0;    /* 设置写回 Flash 请求标志 (LVGL 任务消费) */

/* 前置声明 (register_desktop_events 需要引用设置页打开回调) */
static void settings_open_event_cb(lv_event_t * e);
static void desktop_draw_event_cb(lv_event_t * e);

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

/* 同步登录/桌面两个界面的鼠标切换按钮: 未连接显示"鼠标未连接"并禁用, 连接后显示当前图案 */
static void gui_update_switch_buttons(uint8_t connected)
{
    lv_obj_t * btns[2] = { guider_ui.login_btn_switch, guider_ui.desktop_btn_switch };
    lv_obj_t * lbls[2] = { guider_ui.login_btn_switch_label, guider_ui.desktop_btn_switch_label };
    int i;

    for (i = 0; i < 2; i++) {
        if (btns[i] != NULL) {
            if (connected) {
                lv_obj_clear_state(btns[i], LV_STATE_DISABLED);
            }
            else {
                lv_obj_add_state(btns[i], LV_STATE_DISABLED);
            }
        }
        if (lbls[i] != NULL) {
            lv_label_set_text(lbls[i], connected ? cursor_name(g_cursor_style) : "鼠标未连接");
        }
    }
}

static void mouse_switch_event_cb(lv_event_t * e)
{
    (void)e;
    if (!App_IsMouseConnected()) {
        return;             /* 鼠标未连接时不允许切换图案 */
    }
    gui_cursor_cycle();
    gui_update_switch_buttons(1);
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
    if (ui->desktop_btn_switch != NULL) {
        lv_obj_add_event_cb(ui->desktop_btn_switch, mouse_switch_event_cb, LV_EVENT_CLICKED,
                            ui->desktop_btn_switch_label);
    }
    if (ui->desktop_btn_settings != NULL) {
        lv_obj_add_event_cb(ui->desktop_btn_settings, settings_open_event_cb, LV_EVENT_CLICKED, ui);
    }
    if (ui->desktop_btn_draw != NULL) {
        lv_obj_add_event_cb(ui->desktop_btn_draw, desktop_draw_event_cb, LV_EVENT_CLICKED, ui);
    }
}

static void enter_desktop(lv_ui * ui)
{
    if (ui->desktop == NULL) {
        setup_scr_desktop(ui);
    }
    register_desktop_events(ui);
    gui_update_switch_buttons(App_IsMouseConnected());   /* 同步桌面按钮的连接/图案状态 */
    lv_scr_load(ui->desktop);
}

static bool settings_evts_done = false;

/* 滑条变化: 立即生效并标记待保存 */
static void settings_slider_event_cb(lv_event_t * e)
{
    lv_obj_t * slider = lv_event_get_target(e);
    lv_ui * ui = &guider_ui;
    int val = (int)lv_slider_get_value(slider);
    char buf[16];

    if (slider == ui->settings_slider_sens) {
        g_edit_settings.sens = (uint16_t)val;
        g_settings_dirty = 1;
        lv_label_set_text(ui->settings_label_sens_val, settings_sens_text(val));
        App_SetMouseSpeed(0.6f * (float)val);
    }
    else if (slider == ui->settings_slider_zoom) {
        g_edit_settings.cursor_zoom = (uint16_t)val;
        g_settings_dirty = 1;
        lv_snprintf(buf, sizeof(buf), "%d%%", val);
        lv_label_set_text(ui->settings_label_zoom_val, buf);
        gui_cursor_set_zoom((uint16_t)val);
    }
    else if (slider == ui->settings_slider_bright) {
        g_edit_settings.brightness = (uint16_t)val;
        g_settings_dirty = 1;
        lv_snprintf(buf, sizeof(buf), "%d%%", val);
        lv_label_set_text(ui->settings_label_bright_val, buf);
        gui_set_brightness((uint8_t)val);
    }
    else if (slider == ui->settings_slider_sleep) {
        g_edit_settings.sleep_sec = settings_sleep_seconds(val);
        g_settings_dirty = 1;
        lv_label_set_text(ui->settings_label_sleep_val, settings_sleep_text(val));
        App_SetSleepSec(g_edit_settings.sleep_sec);
    }
}

/* 滑条松开: 请求把当前设置写回 Flash (由 LVGL 任务执行, 保证断电后保持) */
static void settings_slider_released_cb(lv_event_t * e)
{
    (void)e;
    if (g_settings_dirty) {
        gui_request_save();
    }
}

/* 返回按钮: 写回 Flash 并回到桌面 */
static void settings_back_event_cb(lv_event_t * e)
{
    lv_ui * ui = (lv_ui *)lv_event_get_user_data(e);
    gui_save_settings();
    if (ui->desktop == NULL) {
        setup_scr_desktop(ui);
        register_desktop_events(ui);
    }
    lv_scr_load(ui->desktop);
}

/* 桌面"设置"按钮: 打开系统设置 */
static void settings_open_event_cb(lv_event_t * e)
{
    (void)e;
    gui_open_settings();
}

/* 桌面"画图"按钮: 请求打开画图应用 (LVGL 任务消费请求并切前台) */
static void desktop_draw_event_cb(lv_event_t * e)
{
    (void)e;
    App_RequestDrawOpen();
}

/* 设置页"触摸校准": 置请求标志, 由 App_LvglTask 实际执行
   (避免在校准期间阻塞 LVGL 事件回调) */
static void settings_cal_event_cb(lv_event_t * e)
{
    (void)e;
    gui_request_calibration();
}

/* 注册设置界面事件 (仅一次) */
static void register_settings_events(lv_ui * ui)
{
    if (settings_evts_done) return;
    settings_evts_done = true;

    if (ui->settings_btn_back != NULL) {
        lv_obj_add_event_cb(ui->settings_btn_back, settings_back_event_cb, LV_EVENT_CLICKED, ui);
    }
    if (ui->settings_slider_sens != NULL) {
        lv_obj_add_event_cb(ui->settings_slider_sens, settings_slider_event_cb, LV_EVENT_VALUE_CHANGED, ui);
        lv_obj_add_event_cb(ui->settings_slider_sens, settings_slider_released_cb, LV_EVENT_RELEASED, ui);
    }
    if (ui->settings_slider_zoom != NULL) {
        lv_obj_add_event_cb(ui->settings_slider_zoom, settings_slider_event_cb, LV_EVENT_VALUE_CHANGED, ui);
        lv_obj_add_event_cb(ui->settings_slider_zoom, settings_slider_released_cb, LV_EVENT_RELEASED, ui);
    }
    if (ui->settings_slider_bright != NULL) {
        lv_obj_add_event_cb(ui->settings_slider_bright, settings_slider_event_cb, LV_EVENT_VALUE_CHANGED, ui);
        lv_obj_add_event_cb(ui->settings_slider_bright, settings_slider_released_cb, LV_EVENT_RELEASED, ui);
    }
    if (ui->settings_slider_sleep != NULL) {
        lv_obj_add_event_cb(ui->settings_slider_sleep, settings_slider_event_cb, LV_EVENT_VALUE_CHANGED, ui);
        lv_obj_add_event_cb(ui->settings_slider_sleep, settings_slider_released_cb, LV_EVENT_RELEASED, ui);
    }
    if (ui->settings_btn_cal != NULL) {
        lv_obj_add_event_cb(ui->settings_btn_cal, settings_cal_event_cb, LV_EVENT_CLICKED, ui);
    }
}

/**
 * 打开系统设置界面 (首次创建, 事件只注册一次, 从缓存载入当前值)。
 */
void gui_open_settings(void)
{
    lv_ui * ui = &guider_ui;
    const SysSettings_t *st = UserStore_GetSettings();

    if (ui->settings == NULL) {
        setup_scr_settings(ui);
    }
    register_settings_events(ui);

    /* 载入缓存设置作为编辑基准 */
    if (st != NULL) {
        g_edit_settings = *st;
    }
    /* 同步滑条与数值标签 (避免事件回调误触发) */
    if (ui->settings_slider_sens != NULL) {
        lv_slider_set_value(ui->settings_slider_sens, g_edit_settings.sens, LV_ANIM_OFF);
        lv_label_set_text(ui->settings_label_sens_val, settings_sens_text(g_edit_settings.sens));
    }
    if (ui->settings_slider_zoom != NULL) {
        lv_slider_set_value(ui->settings_slider_zoom, g_edit_settings.cursor_zoom, LV_ANIM_OFF);
        char zbuf[16];
        lv_snprintf(zbuf, sizeof(zbuf), "%d%%", g_edit_settings.cursor_zoom);
        lv_label_set_text(ui->settings_label_zoom_val, zbuf);
    }
    if (ui->settings_slider_bright != NULL) {
        lv_slider_set_value(ui->settings_slider_bright, g_edit_settings.brightness, LV_ANIM_OFF);
        char bbuf[16];
        lv_snprintf(bbuf, sizeof(bbuf), "%d%%", g_edit_settings.brightness);
        lv_label_set_text(ui->settings_label_bright_val, bbuf);
    }
    int sleep_idx = settings_sleep_index(g_edit_settings.sleep_sec);
    if (ui->settings_slider_sleep != NULL) {
        lv_slider_set_value(ui->settings_slider_sleep, sleep_idx, LV_ANIM_OFF);
        lv_label_set_text(ui->settings_label_sleep_val, settings_sleep_text(sleep_idx));
    }

    lv_scr_load(ui->settings);
}

/**
 * 把当前设置写回 Flash (掉电保持)。
 */
void gui_save_settings(void)
{
    UserStore_SaveSettings(&g_edit_settings);
    g_settings_dirty = 0;
    s_save_request = 0;
}

void gui_request_save(void)
{
    s_save_request = 1;
}

uint8_t gui_save_requested(void)
{
    return s_save_request;
}

/**
 * 查询设置是否有未保存修改。
 */
uint8_t gui_settings_is_dirty(void)
{
    return g_settings_dirty;
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
    /* 按光标缩放比换算热点偏移 */
    hx = hx * g_cursor_zoom / 100;
    hy = hy * g_cursor_zoom / 100;

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
 * 设置光标缩放百分比 (100..200)。立即对当前与未显示的 3 个光标生效。
 */
void gui_cursor_set_zoom(uint16_t zoom_percent)
{
    lv_obj_t * imgs[3] = { g_cursor_img, g_tom_img, g_hand_img };
    uint16_t z = zoom_percent;
    uint16_t zz;
    int i;

    if (z < 100) z = 100;
    if (z > 200) z = 200;
    g_cursor_zoom = (int)z;
    zz = (uint16_t)(((uint32_t)z * 256U) / 100U);   /* LVGL zoom: 256 = 1x */

    for (i = 0; i < 3; i++) {
        if (imgs[i] != NULL) {
            lv_img_set_zoom(imgs[i], zz);
        }
    }
    gui_cursor_set_pos(s_cursor_x, s_cursor_y);
}

/**
 * 屏幕亮度 (0=最暗, 100=最亮)。
 * 在顶层创建一个全屏黑色半透明遮罩, 通过对象整体透明度实现统一压暗, 立即生效。
 */
void gui_set_brightness(uint8_t brightness_percent)
{
    uint8_t b = brightness_percent;
    uint8_t dim;

    if (b > 100) b = 100;
    s_cur_brightness = b;
    if (g_bright_overlay == NULL) {
        g_bright_overlay = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(g_bright_overlay);
        lv_obj_set_pos(g_bright_overlay, 0, 0);
        lv_obj_set_size(g_bright_overlay, 320, 480);
        lv_obj_set_style_bg_opa(g_bright_overlay, LV_OPA_COVER, LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(g_bright_overlay, lv_color_hex(0x000000), LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_clear_flag(g_bright_overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(g_bright_overlay, LV_OBJ_FLAG_IGNORE_LAYOUT|LV_OBJ_FLAG_FLOATING);
    }
    /* 亮度 b% -> 遮罩不透明度 (100-b)% */
    dim = (uint8_t)(((uint32_t)(100U - b) * 255U) / 100U);
    lv_obj_set_style_opa(g_bright_overlay, dim, LV_PART_MAIN|LV_STATE_DEFAULT);
}

uint8_t gui_get_brightness(void)
{
    return s_cur_brightness;
}

void gui_request_calibration(void)
{
    s_cal_request = 1;
}

uint8_t gui_cal_requested(void)
{
    return s_cal_request;
}

void gui_cal_request_clear(void)
{
    s_cal_request = 0;
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
    gui_update_switch_buttons(connected);
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

    /* 载入开机保存的系统设置作为编辑基准 (实际生效在 App_ApplySettings) */
    const SysSettings_t *st = UserStore_GetSettings();
    if (st != NULL) {
        g_edit_settings = *st;
    }
}
