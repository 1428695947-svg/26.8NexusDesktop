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
#include "app_log.h"
#include "app_file.h"
#include "app_logview.h"
#include "app_monitor.h"
#include "app_music.h"
#include "app_update.h"
#include "app_desktop.h"
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

/* 桌面图标拖拽状态。单指针系统同一时刻只会拖动一个对象。 */
static lv_obj_t *s_drag_obj = NULL;
static lv_point_t s_drag_press_point;
static lv_point_t s_drag_obj_origin;
static uint8_t s_drag_moved = 0U;
static lv_obj_t *s_desktop_icons[DESKTOP_ICON_COUNT];

static void desktop_layout_save(void)
{
    DesktopIconLayout_t layout;
    uint32_t i;

    layout.marker = DESKTOP_LAYOUT_MARKER;
    for (i = 0U; i < DESKTOP_ICON_COUNT; i++) {
        layout.x[i] = (int16_t)lv_obj_get_x(s_desktop_icons[i]);
        layout.y[i] = (int16_t)lv_obj_get_y(s_desktop_icons[i]);
    }
    UserStore_SaveDesktopLayout(&layout);
}

static void desktop_layout_restore(void)
{
    const DesktopIconLayout_t *layout = UserStore_GetDesktopLayout();
    uint32_t i;

    if (layout == NULL || layout->marker != DESKTOP_LAYOUT_MARKER) return;
    for (i = 0U; i < DESKTOP_ICON_COUNT; i++) {
        lv_obj_set_pos(s_desktop_icons[i], layout->x[i], layout->y[i]);
    }
}

/* 前置声明 (register_desktop_events 需要引用设置页打开回调) */
static void settings_open_event_cb(lv_event_t * e);
static void desktop_draw_event_cb(lv_event_t * e);
static void desktop_file_event_cb(lv_event_t * e);
static void desktop_log_event_cb(lv_event_t * e);
static void desktop_monitor_event_cb(lv_event_t * e);
static void desktop_music_event_cb(lv_event_t * e);
static void desktop_update_event_cb(lv_event_t * e);

/**
 * 桌面图标拖拽：超过阈值后跟随指针移动；拖动结束产生的 CLICKED 会被拦截，
 * 防止松手时误打开应用。
 */
static void desktop_icon_drag_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_current_target(e);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t point;

    if (indev == NULL) return;
    if (code == LV_EVENT_PRESSED) {
        s_drag_obj = obj;
        s_drag_moved = 0U;
        lv_indev_get_point(indev, &s_drag_press_point);
        s_drag_obj_origin.x = lv_obj_get_x(obj);
        s_drag_obj_origin.y = lv_obj_get_y(obj);
    }
    else if (code == LV_EVENT_PRESSING && s_drag_obj == obj) {
        int32_t dx;
        int32_t dy;
        int32_t x;
        int32_t y;
        int32_t max_x;
        int32_t max_y;

        lv_indev_get_point(indev, &point);
        dx = (int32_t)point.x - s_drag_press_point.x;
        dy = (int32_t)point.y - s_drag_press_point.y;
        if (!s_drag_moved && dx > -6 && dx < 6 && dy > -6 && dy < 6) return;
        s_drag_moved = 1U;
        x = (int32_t)s_drag_obj_origin.x + dx;
        y = (int32_t)s_drag_obj_origin.y + dy;
        max_x = lv_obj_get_width(lv_obj_get_parent(obj)) - lv_obj_get_width(obj);
        max_y = 370 - lv_obj_get_height(obj); /* 保留底部状态栏区域 */
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x > max_x) x = max_x;
        if (y > max_y) y = max_y;
        lv_obj_set_pos(obj, (lv_coord_t)x, (lv_coord_t)y);
    }
    else if (code == LV_EVENT_CLICKED && s_drag_obj == obj) {
        if (s_drag_moved) lv_event_stop_processing(e);
        s_drag_obj = NULL;
        s_drag_moved = 0U;
    }
    else if (code == LV_EVENT_RELEASED && s_drag_obj == obj && s_drag_moved) {
        desktop_layout_save();
    }
    else if (code == LV_EVENT_PRESS_LOST && s_drag_obj == obj) {
        s_drag_obj = NULL;
        s_drag_moved = 0U;
    }
}

static void register_desktop_icon(lv_obj_t *obj, lv_event_cb_t open_cb, lv_ui *ui)
{
    if (obj == NULL) return;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(obj, desktop_icon_drag_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(obj, open_cb, LV_EVENT_CLICKED, ui);
}

/* 密码连续错误锁定 */
#define LOGIN_MAX_FAIL    3                 /* 连续错误次数上限 */
#define LOGIN_LOCK_MS     15000             /* 锁定时长 15 秒 */
static uint32_t    s_lock_until = 0;        /* 锁定到期时刻 (lv_tick_get) */
static lv_timer_t *s_lock_timer = NULL;     /* 锁定倒计时刷新定时器 */

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
    register_desktop_icon(ui->desktop_btn_settings, settings_open_event_cb, ui);
    register_desktop_icon(ui->desktop_btn_draw, desktop_draw_event_cb, ui);
    register_desktop_icon(ui->desktop_btn_file, desktop_file_event_cb, ui);
    register_desktop_icon(ui->desktop_btn_log, desktop_log_event_cb, ui);
    register_desktop_icon(ui->desktop_btn_monitor, desktop_monitor_event_cb, ui);
    register_desktop_icon(ui->desktop_btn_music, desktop_music_event_cb, ui);
    register_desktop_icon(ui->desktop_btn_update, desktop_update_event_cb, ui);
    s_desktop_icons[0] = ui->desktop_btn_settings;
    s_desktop_icons[1] = ui->desktop_btn_draw;
    s_desktop_icons[2] = ui->desktop_btn_file;
    s_desktop_icons[3] = ui->desktop_btn_log;
    s_desktop_icons[4] = ui->desktop_btn_monitor;
    s_desktop_icons[5] = ui->desktop_btn_music;
    s_desktop_icons[6] = ui->desktop_btn_update;
    desktop_layout_restore();
}

static void enter_desktop(lv_ui * ui)
{
    if (ui->desktop == NULL) {
        setup_scr_desktop(ui);
    }
    register_desktop_events(ui);
    /* 桌面本身不滚动，按住图标移动时只允许图标响应拖拽。 */
    lv_obj_clear_flag(ui->desktop, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_MOMENTUM |
                                  LV_OBJ_FLAG_SCROLL_ELASTIC);
    App_DesktopAttachUptimeLabel(ui->desktop_label_uptime);
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
        /* 与开机设置应用保持同一倍率，避免拖动滑条后速度突然下降。 */
        App_SetMouseSpeed(1.5f * (float)val);
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
    else if (slider == ui->settings_slider_volume) {
        g_edit_settings.volume = (uint16_t)val;
        g_settings_dirty = 1;
        lv_snprintf(buf, sizeof(buf), "%d%%", val);
        lv_label_set_text(ui->settings_label_volume_val, buf);
        /* 当前无音频硬件：只更新逻辑设置，保留后续驱动接口。 */
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

/* 桌面"文件管理"按钮 */
static void desktop_file_event_cb(lv_event_t * e)
{
    (void)e;
    App_File_Open();
}

/* 桌面"系统日志"按钮 */
static void desktop_log_event_cb(lv_event_t * e)
{
    (void)e;
    App_LogView_Open();
}

/* 桌面"系统监控"按钮 */
static void desktop_monitor_event_cb(lv_event_t * e)
{
    (void)e;
    App_Monitor_Open();
}

/* 桌面"音乐"按钮: 保留应用入口并提示播放设备未连接。 */
static void desktop_music_event_cb(lv_event_t * e)
{
    (void)e;
    App_MusicOpen();
}

/* 桌面“更新”按钮：显示版本状态并执行伪 OTA 演示。 */
static void desktop_update_event_cb(lv_event_t * e)
{
    (void)e;
    App_UpdateOpen();
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
    if (ui->settings_slider_volume != NULL) {
        lv_obj_add_event_cb(ui->settings_slider_volume, settings_slider_event_cb, LV_EVENT_VALUE_CHANGED, ui);
        lv_obj_add_event_cb(ui->settings_slider_volume, settings_slider_released_cb, LV_EVENT_RELEASED, ui);
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
    if (ui->settings_slider_volume != NULL) {
        char vbuf[16];
        lv_slider_set_value(ui->settings_slider_volume, g_edit_settings.volume, LV_ANIM_OFF);
        lv_snprintf(vbuf, sizeof(vbuf), "%d%%", g_edit_settings.volume);
        lv_label_set_text(ui->settings_label_volume_val, vbuf);
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
    if (g_settings_dirty) {
        if (UserStore_SaveSettings(&g_edit_settings)) {
            g_settings_dirty = 0;
            App_Log_Event(LOG_LEVEL_INFO,
                "设置已保存 sens=%u zoom=%u bright=%u volume=%u sleep=%u",
                (unsigned int)g_edit_settings.sens,
                (unsigned int)g_edit_settings.cursor_zoom,
                (unsigned int)g_edit_settings.brightness,
                (unsigned int)g_edit_settings.volume,
                (unsigned int)g_edit_settings.sleep_sec);
        } else {
            App_Log_Error("系统设置保存失败");
        }
    }
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

/* 锁定倒计时刷新: 更新剩余秒数, 到期后解锁 */
static void login_lock_update_cb(lv_timer_t * t)
{
    (void)t;
    lv_ui * ui = &guider_ui;
    uint32_t now = lv_tick_get();

    if (s_lock_until <= now) {
        /* 解锁 */
        if (ui->login_label_hint != NULL) {
            lv_label_set_text(ui->login_label_hint, "请输入密码登录");
        }
        if (ui->login_btn_login != NULL) {
            lv_obj_clear_state(ui->login_btn_login, LV_STATE_DISABLED);
        }
        if (s_lock_timer != NULL) {
            lv_timer_del(s_lock_timer);
            s_lock_timer = NULL;
        }
        s_lock_until = 0;
        s_error_count = 0;
    }
    else {
        char msg[48];
        uint32_t remain = (s_lock_until - now + 999U) / 1000U;
        if (ui->login_label_hint != NULL) {
            lv_snprintf(msg, sizeof(msg), "错误过多，锁定 %lu 秒", (unsigned long)remain);
            lv_label_set_text(ui->login_label_hint, msg);
        }
    }
}

static void login_event_cb(lv_event_t * e)
{
    lv_ui * ui = (lv_ui *)lv_event_get_user_data(e);
    const char * entered = lv_textarea_get_text(ui->login_ta_password);
    const char * stored = FlashStore_GetPassword();

    /* 锁定期内忽略登录点击 */
    if (s_lock_until > lv_tick_get()) {
        if (ui->login_ta_password != NULL) {
            lv_textarea_set_text(ui->login_ta_password, "");
        }
        return;
    }

    if (stored != NULL && entered != NULL && strcmp(entered, stored) == 0)
    {
        s_error_count = 0;                      /* Correct password: reset counter */
        if (s_lock_timer != NULL) {
            lv_timer_del(s_lock_timer);
            s_lock_timer = NULL;
        }
        s_lock_until = 0;
        if (ui->login_btn_login != NULL) {
            lv_obj_clear_state(ui->login_btn_login, LV_STATE_DISABLED);
        }
        enter_desktop(ui);
        App_Log_Event(LOG_LEVEL_INFO, "密码登录成功");
    }
    else
    {
        /* Wrong password: clear the field and show the number of failed attempts. */
        s_error_count++;
        lv_textarea_set_text(ui->login_ta_password, "");
        if (ui->login_label_hint != NULL) {
            char msg[48];
            if (s_error_count >= LOGIN_MAX_FAIL) {
                /* 连续错误达到上限 -> 锁定一段时间 */
                s_lock_until = lv_tick_get() + LOGIN_LOCK_MS;
                if (s_lock_timer == NULL) {
                    s_lock_timer = lv_timer_create(login_lock_update_cb, 250, NULL);
                }
                if (ui->login_btn_login != NULL) {
                    lv_obj_add_state(ui->login_btn_login, LV_STATE_DISABLED);
                }
                lv_snprintf(msg, sizeof(msg), "密码错误 %d 次，已锁定 15 秒", s_error_count);
                App_Log_Event(LOG_LEVEL_ERROR, "密码错误过多, 登录已锁定");
            }
            else {
                lv_snprintf(msg, sizeof(msg), "密码错误，已输错 %d 次", s_error_count);
                App_Log_Event(LOG_LEVEL_WARN, "密码错误");
            }
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
