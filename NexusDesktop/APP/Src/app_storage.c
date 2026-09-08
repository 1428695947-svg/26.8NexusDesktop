/**
  ******************************************************************************
  * @file    app_storage.c
  * @brief   FatFs 共享访问协调层 - 统一挂载状态与互斥保护
  ******************************************************************************
  */

#include "app_storage.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "ff.h"
#include "fatfs.h"
#include "shared_bus.h"
#include <stdio.h>
#include <string.h>

static SemaphoreHandle_t s_fs_mutex = NULL;
static volatile uint8_t s_sd_ready = 0U;
static volatile uint8_t s_self_test_result = 0U; /* 0=未执行, 1=通过, 2=失败 */
static volatile uint8_t s_last_result = 0U;
static App_StorageRequest_t *volatile s_pending_request = NULL;

void App_StorageInit(void)
{
    if (s_fs_mutex == NULL) {
        s_fs_mutex = xSemaphoreCreateMutex();
    }
    s_sd_ready = 0U;
    s_self_test_result = 0U;
    s_last_result = (uint8_t)FR_NOT_READY;
    s_pending_request = NULL;
}

uint8_t App_StorageLock(uint32_t timeout_ms)
{
    TickType_t timeout;

    if (s_fs_mutex == NULL) {
        return 0U;
    }
    timeout = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_fs_mutex, timeout) != pdTRUE) {
        return 0U;
    }
    SharedBus_SelectSd();
    return 1U;
}

void App_StorageUnlock(void)
{
    if (s_fs_mutex != NULL) {
        SharedBus_SelectTouch();
        (void)xSemaphoreGive(s_fs_mutex);
    }
}

uint8_t App_StorageMount(void)
{
    FRESULT result;

    if (!App_StorageLock(500U)) {
        return 0U;
    }
    result = f_mount(&SDFatFS, SDPath, 1U);
    s_last_result = (uint8_t)result;
    s_sd_ready = (result == FR_OK) ? 1U : 0U;
    App_StorageUnlock();
    return s_sd_ready;
}

uint8_t App_StorageIsReady(void)
{
    return s_sd_ready;
}

uint8_t App_StorageSelfTest(void)
{
    static const char pattern[] = "NexusDesktop storage self-test\r\n";
    static const char overwrite_pattern[] = "NexusDesktop overwrite self-test\r\n";
    static const char test_dir[] = "0:/CDEXTST";
    FIL file;
    FILINFO info;
    FRESULT result;
    UINT count = 0U;
    char readback[sizeof(overwrite_pattern)];
    char test_file[24];
    uint8_t dir_created = 0U;
    uint8_t slot;

    if (!s_sd_ready || !App_StorageLock(1000U)) {
        s_self_test_result = 2U;
        return 0U;
    }
    result = f_mkdir(test_dir);
    if (result == FR_OK) {
        dir_created = 1U;
    } else if (result != FR_EXIST) {
        goto self_test_done;
    }
    result = FR_EXIST;
    for (slot = 0U; slot < 10U; slot++) {
        (void)snprintf(test_file, sizeof(test_file), "0:/CDEXTST/P%u.TMP", slot);
        if (f_stat(test_file, &info) == FR_NO_FILE) {
            result = f_open(&file, test_file, FA_CREATE_NEW | FA_WRITE);
            break;
        }
    }
    if (result != FR_OK) {
        goto self_test_done;
    }
    result = f_write(&file, pattern, sizeof(pattern) - 1U, &count);
    if (result == FR_OK && count == (sizeof(pattern) - 1U)) {
        result = f_sync(&file);
    }
    (void)f_close(&file);
    if (result != FR_OK || count != (sizeof(pattern) - 1U)) {
        goto self_test_cleanup;
    }

    /* 同名新建必须返回 FR_EXIST，覆盖前先校验元数据。 */
    result = f_open(&file, test_file, FA_CREATE_NEW | FA_WRITE);
    if (result == FR_OK) {
        (void)f_close(&file);
        result = FR_INT_ERR;
        goto self_test_cleanup;
    }
    if (result != FR_EXIST) {
        goto self_test_cleanup;
    }
    result = f_stat(test_file, &info);
    if (result != FR_OK || (uint32_t)info.fsize != (sizeof(pattern) - 1U) ||
        (info.fattrib & AM_DIR) != 0U) {
        result = FR_INT_ERR;
        goto self_test_cleanup;
    }

    result = f_open(&file, test_file, FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK) {
        goto self_test_cleanup;
    }
    result = f_write(&file, overwrite_pattern, sizeof(overwrite_pattern) - 1U, &count);
    if (result == FR_OK && count == (sizeof(overwrite_pattern) - 1U)) {
        result = f_sync(&file);
    }
    (void)f_close(&file);
    if (result != FR_OK || count != (sizeof(overwrite_pattern) - 1U)) {
        goto self_test_cleanup;
    }

    result = f_open(&file, test_file, FA_OPEN_EXISTING | FA_READ);
    if (result != FR_OK) {
        goto self_test_cleanup;
    }
    memset(readback, 0, sizeof(readback));
    result = f_read(&file, readback, sizeof(overwrite_pattern) - 1U, &count);
    (void)f_close(&file);
    if (result != FR_OK || count != (sizeof(overwrite_pattern) - 1U) ||
        memcmp(readback, overwrite_pattern, sizeof(overwrite_pattern) - 1U) != 0) {
        result = FR_DISK_ERR;
        goto self_test_cleanup;
    }
    result = f_unlink(test_file);

self_test_cleanup:
    if (result != FR_OK) {
        (void)f_unlink(test_file);
    }
    if (dir_created) {
        (void)f_unlink(test_dir);
    }
self_test_done:
    App_StorageUnlock();
    s_self_test_result = (result == FR_OK) ? 1U : 2U;
    return (result == FR_OK) ? 1U : 0U;
}

uint8_t App_StorageGetSelfTestResult(void)
{
    return s_self_test_result;
}

uint8_t App_StorageGetLastResult(void)
{
    return s_last_result;
}

void App_StorageReportError(void)
{
    s_sd_ready = 0U;
}

uint8_t App_StorageSubmit(App_StorageRequest_t *request)
{
    uint8_t accepted = 0U;

    if (request == NULL || request->state != APP_STORAGE_REQ_IDLE) {
        return 0U;
    }
    taskENTER_CRITICAL();
    if (s_pending_request == NULL) {
        request->result = (uint8_t)FR_NOT_READY;
        request->entry_count = 0U;
        request->truncated = 0U;
        request->state = APP_STORAGE_REQ_PENDING;
        s_pending_request = request;
        accepted = 1U;
    }
    taskEXIT_CRITICAL();
    return accepted;
}

void App_StorageRequestReset(App_StorageRequest_t *request)
{
    if (request != NULL && request->state == APP_STORAGE_REQ_DONE) {
        request->state = APP_STORAGE_REQ_IDLE;
    }
}

void App_StorageWorkerPoll(void)
{
    App_StorageRequest_t *request;
    FRESULT result = FR_INVALID_PARAMETER;
    FIL file;
    DIR dir;
    FILINFO info;
    UINT count = 0U;
    uint32_t size;
    uint32_t start;
    uint32_t lines;
    uint32_t i;

    taskENTER_CRITICAL();
    request = s_pending_request;
    if (request != NULL && request->state == APP_STORAGE_REQ_PENDING) {
        request->state = APP_STORAGE_REQ_RUNNING;
    } else {
        request = NULL;
    }
    taskEXIT_CRITICAL();
    if (request == NULL) {
        return;
    }

    /* UI 主动发起文件/日志请求时，允许在后台立即尝试一次重新挂载。 */
    if (!App_StorageIsReady() && !App_StorageMount()) {
        result = FR_NOT_READY;
        goto worker_done;
    }
    if (!App_StorageLock(1000U)) {
        result = FR_TIMEOUT;
        goto worker_done;
    }

    switch ((App_StorageOperation_t)request->operation) {
        case APP_STORAGE_OP_LIST:
            if (request->entries == NULL || request->entry_capacity == 0U) {
                result = FR_INVALID_PARAMETER;
                break;
            }
            result = f_opendir(&dir, request->path);
            if (result == FR_OK) {
                while (result == FR_OK && request->entry_count < request->entry_capacity) {
                    result = f_readdir(&dir, &info);
                    if (result != FR_OK || info.fname[0] == '\0') {
                        break;
                    }
                    strncpy(request->entries[request->entry_count].name,
                            info.fname, APP_STORAGE_NAME_MAX - 1U);
                    request->entries[request->entry_count].name[APP_STORAGE_NAME_MAX - 1U] = '\0';
                    request->entries[request->entry_count].size = (uint32_t)info.fsize;
                    request->entries[request->entry_count].attr = info.fattrib;
                    request->entry_count++;
                }
                /* 容量恰好用尽时再探测一项，区分“正好装满”和“已截断”。 */
                if (result == FR_OK && request->entry_count == request->entry_capacity) {
                    result = f_readdir(&dir, &info);
                    if (result == FR_OK && info.fname[0] != '\0') {
                        request->truncated = 1U;
                    }
                }
                (void)f_closedir(&dir);
            }
            break;

        case APP_STORAGE_OP_CREATE:
            result = f_open(&file, request->path, FA_CREATE_NEW | FA_WRITE);
            if (result == FR_OK) {
                result = f_close(&file);
            }
            break;

        case APP_STORAGE_OP_READ:
            result = f_open(&file, request->path, FA_OPEN_EXISTING | FA_READ);
            if (result == FR_OK) {
                if (request->data != NULL && request->data_capacity > 0U) {
                    result = f_read(&file, request->data, (UINT)(request->data_capacity - 1U), &count);
                    request->data[count] = '\0';
                    request->data_length = (uint32_t)count;
                } else {
                    result = FR_INVALID_PARAMETER;
                }
                (void)f_close(&file);
            }
            break;

        case APP_STORAGE_OP_READ_TAIL:
            result = f_open(&file, request->path, FA_OPEN_EXISTING | FA_READ);
            if (result == FR_OK) {
                size = (uint32_t)f_size(&file);
                request->file_size = size;
                if (request->data != NULL && request->data_capacity > 0U) {
                    start = (size > request->data_capacity - 1U) ?
                            size - (request->data_capacity - 1U) : 0U;
                    result = f_lseek(&file, (FSIZE_t)start);
                    if (result == FR_OK) {
                        result = f_read(&file, request->data,
                                        (UINT)(request->data_capacity - 1U), &count);
                    }
                    request->data[count] = '\0';
                    request->data_length = (uint32_t)count;
                    lines = 0U;
                    for (i = request->data_length; i > 0U; i--) {
                        if (request->data[i - 1U] == '\n') {
                            lines++;
                            if (lines > request->max_lines) {
                                uint32_t keep = request->data_length - i;
                                memmove(request->data, request->data + i, keep);
                                request->data[keep] = '\0';
                                request->data_length = keep;
                                break;
                            }
                        }
                    }
                    /* 从文件中部开始读时，丢弃首个不完整行。 */
                    if (start > 0U && lines <= request->max_lines &&
                        request->data_length > 0U) {
                        char *first_newline = strchr(request->data, '\n');

                        if (first_newline != NULL) {
                            uint32_t skip = (uint32_t)(first_newline - request->data) + 1U;
                            uint32_t keep = request->data_length - skip;

                            memmove(request->data, request->data + skip, keep);
                            request->data[keep] = '\0';
                            request->data_length = keep;
                        }
                    }
                } else {
                    result = FR_INVALID_PARAMETER;
                }
                (void)f_close(&file);
            }
            break;

        case APP_STORAGE_OP_WRITE:
            result = f_open(&file, request->path, FA_CREATE_ALWAYS | FA_WRITE);
            if (result == FR_OK) {
                if (request->data != NULL) {
                    result = f_write(&file, request->data, (UINT)request->data_length, &count);
                    if (result == FR_OK && count != request->data_length) {
                        result = FR_DISK_ERR;
                    }
                    if (result == FR_OK) {
                        result = f_sync(&file);
                    }
                } else {
                    result = FR_INVALID_PARAMETER;
                }
                (void)f_close(&file);
            }
            break;

        case APP_STORAGE_OP_DELETE:
            result = f_unlink(request->path);
            break;

        case APP_STORAGE_OP_TRUNCATE:
            result = f_open(&file, request->path, FA_CREATE_ALWAYS | FA_WRITE);
            if (result == FR_OK) {
                result = f_close(&file);
                request->file_size = 0U;
            }
            break;

        default:
            result = FR_INVALID_PARAMETER;
            break;
    }
    App_StorageUnlock();

worker_done:
    request->result = (uint8_t)result;
    s_last_result = (uint8_t)result;
    if (result == FR_DISK_ERR || result == FR_INT_ERR || result == FR_NOT_READY) {
        App_StorageReportError();
    }
    taskENTER_CRITICAL();
    request->state = APP_STORAGE_REQ_DONE;
    s_pending_request = NULL;
    taskEXIT_CRITICAL();
}
