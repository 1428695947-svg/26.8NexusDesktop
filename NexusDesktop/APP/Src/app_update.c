/**
  ******************************************************************************
  * @file    app_update.c
  * @brief   系统版本与伪 OTA 演示应用实现
  * @note    当前不改写固件 Flash；用显式状态机演示检查、下载、
  *          校验和就绪流程，始终保持当前版本可运行。
  ******************************************************************************
  */

#include "app_update.h"

#include "lvgl.h"
#include "gui_guider.h"
#include "app_log.h"

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_action_btn = NULL;
static lv_obj_t *s_action_label = NULL;
static lv_obj_t *s_progress = NULL;
static App_UpdateState_t s_state = APP_UPDATE_IDLE;
static uint8_t s_progress_value = 0U;
static uint8_t s_fg = 0U;

static void update_render(void)
{
    const char *status = "当前版本稳定，可检查更新";
    const char *action = "检查更新";
    uint8_t disabled = 0U;

    switch (s_state) {
        case APP_UPDATE_CHECKING:
            status = "正在检查版本...";
            action = "检查中";
            disabled = 1U;
            break;
        case APP_UPDATE_AVAILABLE:
            status = "发现演示版本 " APP_FW_DEMO_VERSION "\n可执行伪 OTA 安全流程";
            action = "模拟更新";
            break;
        case APP_UPDATE_DOWNLOADING:
            status = "正在模拟下载，当前固件仍可用";
            action = "下载中";
            disabled = 1U;
            break;
        case APP_UPDATE_VERIFYING:
            status = "正在校验候选镜像...";
            action = "校验中";
            disabled = 1U;
            break;
        case APP_UPDATE_READY:
            status = "校验通过，伪 OTA 演示完成\n未写固件区，无无效中间态";
            action = "重新演示";
            break;
        default:
            break;
    }
    if (s_status_label != NULL) {
        lv_label_set_text(s_status_label, status);
    }
    if (s_action_label != NULL) {
        lv_label_set_text(s_action_label, action);
    }
    if (s_action_btn != NULL) {
        if (disabled) {
            lv_obj_add_state(s_action_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(s_action_btn, LV_STATE_DISABLED);
        }
    }
    if (s_progress != NULL) {
        lv_bar_set_value(s_progress, s_progress_value, LV_ANIM_OFF);
    }
}

static void update_back_cb(lv_event_t *event)
{
    (void)event;
    App_UpdateClose();
}

static void update_action_cb(lv_event_t *event)
{
    (void)event;
    if (s_state == APP_UPDATE_IDLE || s_state == APP_UPDATE_READY) {
        s_state = APP_UPDATE_CHECKING;
        s_progress_value = 0U;
        App_Log_Event(LOG_LEVEL_INFO, "开始检查更新 current=%s", APP_FW_VERSION);
    } else if (s_state == APP_UPDATE_AVAILABLE) {
        s_state = APP_UPDATE_DOWNLOADING;
        s_progress_value = 0U;
        App_Log_Event(LOG_LEVEL_INFO, "开始伪 OTA target=%s", APP_FW_DEMO_VERSION);
    }
    update_render();
}

static void update_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_fg) {
        return;
    }
    if (s_state == APP_UPDATE_CHECKING) {
        s_progress_value = (uint8_t)(s_progress_value + 5U);
        if (s_progress_value >= 100U) {
            s_progress_value = 100U;
            s_state = APP_UPDATE_AVAILABLE;
            App_Log_Event(LOG_LEVEL_INFO, "发现伪 OTA 演示版本 %s", APP_FW_DEMO_VERSION);
        }
        update_render();
    } else if (s_state == APP_UPDATE_DOWNLOADING) {
        s_progress_value = (uint8_t)(s_progress_value + 4U);
        if (s_progress_value >= 100U) {
            s_progress_value = 0U;
            s_state = APP_UPDATE_VERIFYING;
        }
        update_render();
    } else if (s_state == APP_UPDATE_VERIFYING) {
        s_progress_value = (uint8_t)(s_progress_value + 10U);
        if (s_progress_value >= 100U) {
            s_progress_value = 100U;
            s_state = APP_UPDATE_READY;
            App_Log_Event(LOG_LEVEL_INFO, "伪 OTA 校验通过，未写固件区");
        }
        update_render();
    }
}

static void update_screen_create(void)
{
    lv_obj_t *title;
    lv_obj_t *version;
    lv_obj_t *back;
    lv_obj_t *label;

    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, 320, 480);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x0f172a), LV_PART_MAIN | LV_STATE_DEFAULT);

    back = lv_btn_create(s_screen);
    lv_obj_set_pos(back, 10, 10);
    lv_obj_set_size(back, 70, 34);
    lv_obj_add_event_cb(back, update_back_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(back);
    lv_label_set_text(label, "返回");
    lv_obj_set_style_text_font(label, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label);

    title = lv_label_create(s_screen);
    lv_label_set_text(title, "系统更新");
    lv_obj_set_style_text_font(title, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xf8fafc), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 20, 16);

    version = lv_label_create(s_screen);
    lv_label_set_text(version, "当前版本: " APP_FW_VERSION "\n更新方式: 伪 OTA 状态机");
    lv_obj_set_width(version, 280);
    lv_obj_set_style_text_align(version, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(version, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(version, lv_color_hex(0xcbd5e1), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(version, LV_ALIGN_TOP_MID, 0, 88);

    s_status_label = lv_label_create(s_screen);
    lv_obj_set_width(s_status_label, 280);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_status_label, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xfbbf24), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(s_status_label, LV_ALIGN_CENTER, 0, -45);

    s_progress = lv_bar_create(s_screen);
    lv_obj_set_size(s_progress, 260, 18);
    lv_obj_align(s_progress, LV_ALIGN_CENTER, 0, 30);
    lv_bar_set_range(s_progress, 0, 100);

    s_action_btn = lv_btn_create(s_screen);
    lv_obj_set_size(s_action_btn, 150, 44);
    lv_obj_align(s_action_btn, LV_ALIGN_BOTTOM_MID, 0, -54);
    lv_obj_add_event_cb(s_action_btn, update_action_cb, LV_EVENT_CLICKED, NULL);
    s_action_label = lv_label_create(s_action_btn);
    lv_obj_set_style_text_font(s_action_label, &lv_font_sourcehan18_custom,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(s_action_label);

    (void)lv_timer_create(update_timer_cb, 100U, NULL);
    update_render();
}

void App_UpdateOpen(void)
{
    if (s_screen == NULL) {
        update_screen_create();
    }
    s_fg = 1U;
    update_render();
    lv_scr_load(s_screen);
}

void App_UpdateClose(void)
{
    s_fg = 0U;
    if (guider_ui.desktop != NULL) {
        lv_scr_load(guider_ui.desktop);
    }
}

App_UpdateState_t App_UpdateGetState(void)
{
    return s_state;
}

const char *App_UpdateGetStateText(void)
{
    static const char *const names[] = {
        "空闲", "检查", "可更新", "下载", "校验", "就绪"
    };

    return (s_state <= APP_UPDATE_READY) ? names[s_state] : "异常";
}
