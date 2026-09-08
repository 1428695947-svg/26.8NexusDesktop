/**
  ******************************************************************************
  * @file    app_monitor.c
  * @brief   系统监控应用实现 (FreeRTOS 运行时状态查看)
  * @author  Doubao
  * @version V1.0
  * @date    2026-09-04
  * @note    屏幕为 LVGL 对象。高频小区域与低频统计分开刷新，避免监控页
  *          周期性大面积重绘阻塞指针响应。
  *          运行于 LVGL 任务上下文, 无需独立 FreeRTOS 任务。
  ******************************************************************************
  */

#include "app_monitor.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "gui_guider.h"
#include "custom.h"
#include "app.h"
#include "app_log.h"
#include "app_storage.h"
#include "app_update.h"
#include "app_health.h"
#include "lv_port_disp.h"

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"

/* ========================= 布局常量 ========================= */
#define MON_W           320
#define MON_H           480
#define MON_BTN_W       70
#define MON_BTN_H       32

/* 最多显示的任务数 (含 IDLE/Timer/日志任务等) */
#define MON_TASK_MAX    12U

/* ========================= 私有变量 ========================= */
static lv_obj_t *s_screen = NULL;              /* 监控屏幕 */
static lv_obj_t *s_content = NULL;             /* 仅正文可纵向滚动 */
static lv_obj_t *s_label_title = NULL;
static lv_obj_t *s_label_live = NULL;
static lv_obj_t *s_label_status = NULL;
static lv_obj_t *s_label_tasks = NULL;
static lv_timer_t *s_timer = NULL;
static uint8_t s_fg = 0;                       /* 监控是否前台 */
static uint8_t s_slow_div = 0U;

/* ========================= 私有函数 ========================= */

/**
  * @brief  向监控文本缓冲区有界追加格式化内容
  * @note   vsnprintf 截断时返回原本需要的长度，不能直接累加指针。
  */
static void monitor_append(char **cursor, char *end, const char *fmt, ...)
{
    size_t remaining;
    int written;
    va_list args;

    if (cursor == NULL || *cursor == NULL || *cursor >= end || fmt == NULL) {
        return;
    }
    remaining = (size_t)(end - *cursor) + 1U;
    va_start(args, fmt);
    written = vsnprintf(*cursor, remaining, fmt, args);
    va_end(args);
    if (written < 0) {
        **cursor = '\0';
    } else if ((size_t)written >= remaining) {
        *cursor = end;
        **cursor = '\0';
    } else {
        *cursor += (size_t)written;
    }
}

/* 返回桌面 */
static void monitor_back_cb(lv_event_t * e)
{
    (void)e;
    App_Monitor_Close();
}

/* 阻止摇杆松手时创建滚动抛掷动画，并取消已经存在的滚动动画。 */
static void monitor_scroll_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_SCROLL_BEGIN) {
        lv_anim_t *anim = lv_event_get_scroll_anim(e);
        if (anim != NULL) anim->time = 0U;
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_coord_t y = lv_obj_get_scroll_y(s_content);
        lv_obj_scroll_to_y(s_content, y, LV_ANIM_OFF);
    }
}

/* 根据标签实际内容高度顺序排布，防止换行或缺字修复后文字互相覆盖。 */
static void monitor_update_layout(void)
{
    lv_coord_t y;

    if (s_content == NULL || s_label_live == NULL ||
        s_label_status == NULL || s_label_tasks == NULL) return;
    lv_obj_update_layout(s_content);
    y = 2;
    lv_obj_set_pos(s_label_live, 10, y);
    y += lv_obj_get_height(s_label_live) + 4;
    lv_obj_set_pos(s_label_status, 10, y);
    y += lv_obj_get_height(s_label_status) + 4;
    lv_obj_set_pos(s_label_tasks, 10, y);
}

/* 刷新界面内容 */
static void monitor_set_text_changed(lv_obj_t *label, const char *text)
{
    const char *old_text;

    if (label == NULL || text == NULL) return;
    old_text = lv_label_get_text(label);
    if (old_text == NULL || strcmp(old_text, text) != 0) {
        lv_label_set_text(label, text);
        monitor_update_layout();
    }
}

/* 仅刷新运行时间和 FPS，小区域每秒更新，不影响指针连续移动。 */
static void monitor_refresh_live(void)
{
    char buf[96];
    char *p = buf;
    char *end = buf + sizeof(buf) - 1;
    uint32_t uptime_ms;
    int sec;

    uptime_ms = (uint32_t)osKernelGetTickCount();
    sec = (int)(uptime_ms / 1000U);
    monitor_append(&p, end, "运行时间: %02d:%02d:%02d\n", sec / 3600, (sec / 60) % 60, sec % 60);
    monitor_append(&p, end, "LVGL: 当前%lu FPS  平均%lu FPS", 
                   (unsigned long)lv_refr_get_fps_current(),
                   (unsigned long)lv_refr_get_fps_avg());
    *p = '\0';
    monitor_set_text_changed(s_label_live, buf);
}

/* 低频刷新系统统计；避免频繁格式化并重绘大块文本。 */
static void monitor_refresh_status(void)
{
    char buf[480];
    char *p = buf;
    char *end = buf + sizeof(buf) - 1;
    uint32_t free_heap;
    uint32_t min_heap;
    uint32_t ev_events = 0, ev_dropped = 0;
    uint8_t key_q_current = 0U, key_q_peak = 0U;
    uint8_t pointer_q_current = 0U, pointer_q_peak = 0U;
    uint32_t pointer_consumed = 0U;
    App_LogStats_t logst;
    App_HealthStats_t health;
    App_DisplayStats_t display;
    free_heap = (uint32_t)xPortGetFreeHeapSize();
    min_heap = (uint32_t)xPortGetMinimumEverFreeHeapSize();

    App_GetInputStats(&ev_events, &ev_dropped);
    App_InputGetQueueStats(&key_q_current, &key_q_peak);
    App_InputGetPointerStats(&pointer_q_current, &pointer_q_peak, &pointer_consumed);
    App_Log_GetStats(&logst);
    App_HealthGetStats(&health);
    App_DisplayGetStats(&display);

    monitor_append(&p, end,
        "RTOS堆: %lu B  最低:%lu B\n",
        (unsigned long)free_heap, (unsigned long)min_heap);
    monitor_append(&p, end,
        "输入事件: %lu  丢弃: %lu\n", (unsigned long)ev_events, (unsigned long)ev_dropped);
    monitor_append(&p, end,
        "错误计数: %lu  日志: %lu 写盘: %lu 丢:%lu\n",
        (unsigned long)logst.error_count,
        (unsigned long)logst.log_events,
        (unsigned long)logst.log_written,
        (unsigned long)logst.log_dropped);
    monitor_append(&p, end, "SD: %s  自检:%s  FR:%u\n",
        logst.sd_ready ? "已挂载" : "未挂载",
        App_StorageGetSelfTestResult() == 1U ? "通过" :
        (App_StorageGetSelfTestResult() == 2U ? "失败" : "未执行"),
        App_StorageGetLastResult());
    monitor_append(&p, end,
        "队列 按键:%u/峰%u  日志:%u/峰%u\n",
        key_q_current, key_q_peak, logst.queue_current, logst.queue_peak);
    monitor_append(&p, end, "指针队列:%u/峰%u 已消费:%lu\n",
                   pointer_q_current, pointer_q_peak,
                   (unsigned long)pointer_consumed);
    monitor_append(&p, end, "固件: v%s  更新:%s\n",
                   APP_FW_VERSION, App_UpdateGetStateText());
    monitor_append(&p, end, "看门狗:0x%02lX 超时:%lu 恢复:%lu\n",
                   (unsigned long)health.fault_mask,
                   (unsigned long)health.timeout_count,
                   (unsigned long)health.recovery_count);
    monitor_append(&p, end, "LCD脏区: %lums/%lupx  历史峰:%lums",
                   (unsigned long)display.last_time_ms,
                   (unsigned long)display.last_pixels,
                   (unsigned long)display.max_time_ms);

    *p = '\0';
    monitor_set_text_changed(s_label_status, buf);
}

/* 任务枚举会短暂挂起调度器，只在低频周期执行。 */
static void monitor_refresh_tasks(void)
{
    char buf[256];
    char *p = buf;
    char *end = buf + sizeof(buf) - 1;
    TaskStatus_t tsk[MON_TASK_MAX];
    uint32_t tsk_cnt;
    uint32_t i;

    memset(tsk, 0, sizeof(tsk));
    tsk_cnt = uxTaskGetSystemState(tsk, MON_TASK_MAX, NULL);
    monitor_append(&p, end, "--- 任务 (栈水位 W) ---\n");
    for (i = 0; i < tsk_cnt && p < end; i++) {
        static const char *st[] = {"运行", "就绪", "阻塞", "挂起", "删除", "无效"};
        const char *stn = (tsk[i].eCurrentState <= 5U) ? st[tsk[i].eCurrentState] : "?";
        monitor_append(&p, end, "%s[%s] 栈余%luW\n",
            tsk[i].pcTaskName, stn, (unsigned long)tsk[i].usStackHighWaterMark);
    }
    *p = '\0';

    monitor_set_text_changed(s_label_tasks, buf);
}

static void monitor_refresh_all(void)
{
    monitor_refresh_live();
    monitor_refresh_status();
    monitor_refresh_tasks();
}

/* 定时器回调 (LVGL 任务上下文执行) */
static void monitor_timer_cb(lv_timer_t * timer)
{
    (void)timer;
    if (!s_fg || s_screen == NULL) {
        return;
    }
    if (lv_scr_act() != s_screen) {
        return;                 /* 非当前屏则不刷新 */
    }
    monitor_refresh_live();
    s_slow_div++;
    if (s_slow_div >= 5U) {
        s_slow_div = 0U;
        monitor_refresh_status();
        monitor_refresh_tasks();
    }
}

/* 创建监控屏幕 (一次性) */
static void monitor_screen_create(void)
{
    lv_obj_t * btn_back;

    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, MON_W, MON_H);
    lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_OFF);
    /* 顶层屏幕固定，保证标题和返回键永远可见。 */
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_MOMENTUM |
                                  LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1e293b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_screen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 顶部: 返回按钮 + 标题 */
    btn_back = lv_btn_create(s_screen);
    lv_obj_set_pos(btn_back, 10, 8);
    lv_obj_set_size(btn_back, MON_BTN_W, MON_BTN_H);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x2195f6), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_back, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_back, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_back, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_back, monitor_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * bl = lv_label_create(btn_back);
    lv_label_set_text(bl, "返回");
    lv_obj_center(bl);

    s_label_title = lv_label_create(s_screen);
    lv_label_set_text(s_label_title, "系统监控");
    lv_obj_set_pos(s_label_title, 90, 10);
    lv_obj_set_style_text_font(s_label_title, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_label_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 只有标题栏下方正文可以滚动，避免整个屏幕位移产生残影。 */
    s_content = lv_obj_create(s_screen);
    lv_obj_set_pos(s_content, 0, 48);
    lv_obj_set_size(s_content, MON_W, MON_H - 48);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);
    lv_obj_add_event_cb(s_content, monitor_scroll_event_cb, LV_EVENT_ALL, NULL);

    s_label_live = lv_label_create(s_content);
    lv_obj_set_pos(s_label_live, 10, 2);
    lv_obj_set_width(s_label_live, MON_W - 20);

    s_label_status = lv_label_create(s_content);
    lv_obj_set_pos(s_label_status, 10, 50);
    lv_obj_set_width(s_label_status, MON_W - 20);

    s_label_tasks = lv_label_create(s_content);
    lv_obj_set_pos(s_label_tasks, 10, 254);
    lv_obj_set_width(s_label_tasks, MON_W - 20);

    lv_obj_set_style_text_font(s_label_live, &lv_font_sourcehan18_custom, 0);
    lv_obj_set_style_text_font(s_label_status, &lv_font_sourcehan18_custom, 0);
    lv_obj_set_style_text_font(s_label_tasks, &lv_font_sourcehan18_custom, 0);
    lv_obj_set_style_text_color(s_label_live, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_color(s_label_status, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_color(s_label_tasks, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_line_space(s_label_status, 2, 0);
    lv_obj_set_style_text_line_space(s_label_tasks, 1, 0);
    lv_label_set_long_mode(s_label_live, LV_LABEL_LONG_WRAP);
    lv_label_set_long_mode(s_label_status, LV_LABEL_LONG_WRAP);
    lv_label_set_long_mode(s_label_tasks, LV_LABEL_LONG_WRAP);

    /* 周期刷新定时器 */
    s_timer = lv_timer_create(monitor_timer_cb, 1000, NULL);
}

/* ========================= 公共接口实现 ========================= */

void App_Monitor_Open(void)
{
    if (s_screen == NULL) {
        monitor_screen_create();
    }
    s_fg = 1;
    s_slow_div = 0U;
    monitor_refresh_all();
    lv_scr_load(s_screen);
}

void App_Monitor_Close(void)
{
    s_fg = 0;
    if (guider_ui.desktop != NULL) {
        lv_scr_load(guider_ui.desktop);
    }
}
