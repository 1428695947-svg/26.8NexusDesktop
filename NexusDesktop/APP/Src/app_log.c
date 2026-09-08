/**
  ******************************************************************************
  * @file    app_log.c
  * @brief   异步系统日志 - 通过共享存储层持久化到 SD 卡
  ******************************************************************************
  */

#include "app_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "cmsis_os.h"
#include "ff.h"
#include "app_storage.h"
#include "app_health.h"

typedef struct {
    uint8_t level;
    char text[LOG_MSG_MAX];
} LogMsg_t;

static QueueHandle_t s_log_queue = NULL;
static volatile App_LogStats_t s_stats;
static volatile uint32_t s_log_task_heartbeat = 0U;

static void log_update_queue_stats(void)
{
    UBaseType_t current;

    if (s_log_queue == NULL) {
        return;
    }
    current = uxQueueMessagesWaiting(s_log_queue);
    s_stats.queue_current = (uint8_t)current;
    if (current > s_stats.queue_peak) {
        s_stats.queue_peak = (uint8_t)current;
    }
}

static void log_format_line(char *out, uint32_t out_size, uint8_t level, const char *text)
{
    const char *level_text = "I";

    if (level == LOG_LEVEL_WARN) {
        level_text = "W";
    } else if (level == LOG_LEVEL_ERROR) {
        level_text = "E";
    }
    (void)snprintf(out, out_size, "[%s][%lu] %s\r\n", level_text,
                   (unsigned long)osKernelGetTickCount(), text);
}

static void log_file_write(const LogMsg_t *msg)
{
    FIL file;
    FRESULT result;
    UINT written = 0U;
    uint8_t opened = 0U;
    char line[LOG_MSG_MAX + 24U];

    if (!App_StorageLock(500U)) {
        s_stats.sd_write_fail++;
        return;
    }
    result = f_open(&file, LOG_FILE_PATH, FA_OPEN_ALWAYS | FA_WRITE);
    if (result == FR_OK) {
        opened = 1U;
        result = f_lseek(&file, f_size(&file));
    }
    if (result == FR_OK) {
        log_format_line(line, sizeof(line), msg->level, msg->text);
        result = f_write(&file, line, (UINT)strlen(line), &written);
    }
    if (result == FR_OK) {
        result = f_sync(&file);
    }
    if (opened) {
        (void)f_close(&file);
    }
    App_StorageUnlock();

    if (result == FR_OK && written > 0U) {
        s_stats.log_written++;
    } else {
        s_stats.sd_write_fail++;
        App_StorageReportError();
    }
}

void App_Log_Init(void)
{
    if (s_log_queue == NULL) {
        s_log_queue = xQueueCreate(LOG_QUEUE_LEN, sizeof(LogMsg_t));
    }
    memset((void *)&s_stats, 0, sizeof(s_stats));
}

void App_LogTask(void *argument)
{
    LogMsg_t msg;
    uint32_t last_mount_try = 0U;
    uint32_t tick;

    (void)argument;
    s_log_task_heartbeat++;
    s_stats.sd_ready = App_StorageMount();
    last_mount_try = (uint32_t)osKernelGetTickCount();
    if (s_stats.sd_ready) {
        if (App_StorageSelfTest()) {
            App_Log_Event(LOG_LEVEL_INFO, "SD 读写自检通过");
        } else {
            App_Log_Error("SD 读写自检失败");
        }
    }
    for (;;) {
        App_HealthBeat(APP_HEALTH_LOG);
        s_log_task_heartbeat++;
        App_StorageWorkerPoll();
        tick = (uint32_t)osKernelGetTickCount();
        if (!App_StorageIsReady() && (tick - last_mount_try) >= 30000U) {
            s_stats.sd_ready = App_StorageMount();
            last_mount_try = (uint32_t)osKernelGetTickCount();
        }
        if (xQueueReceive(s_log_queue, &msg, pdMS_TO_TICKS(50U)) == pdTRUE) {
            if (App_StorageIsReady()) {
                log_file_write(&msg);
            } else {
                s_stats.log_dropped++;
            }
            log_update_queue_stats();
        }
        s_stats.sd_ready = App_StorageIsReady();
    }
}

static void log_enqueue(uint8_t level, const char *fmt, va_list args)
{
    LogMsg_t msg;

    if (s_log_queue == NULL || fmt == NULL) {
        return;
    }
    memset(&msg, 0, sizeof(msg));
    msg.level = (level <= LOG_LEVEL_ERROR) ? level : LOG_LEVEL_INFO;
    (void)vsnprintf(msg.text, sizeof(msg.text), fmt, args);
    s_stats.log_events++;
    if (xQueueSend(s_log_queue, &msg, 0U) != pdTRUE) {
        s_stats.log_dropped++;
    }
    log_update_queue_stats();
}

void App_Log_Event(uint8_t level, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    log_enqueue(level, fmt, args);
    va_end(args);
}

void App_Log_Error(const char *fmt, ...)
{
    va_list args;

    s_stats.error_count++;
    va_start(args, fmt);
    log_enqueue(LOG_LEVEL_ERROR, fmt, args);
    va_end(args);
}

uint8_t App_Log_IsSdReady(void)
{
    return App_StorageIsReady();
}

void App_Log_GetStats(App_LogStats_t *out)
{
    if (out != NULL) {
        memcpy(out, (const void *)&s_stats, sizeof(*out));
        out->sd_ready = App_StorageIsReady();
    }
}
