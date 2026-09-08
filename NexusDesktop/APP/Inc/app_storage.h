/**
  ******************************************************************************
  * @file    app_storage.h
  * @brief   FatFs 共享访问协调层与异步请求接口
  ******************************************************************************
  */

#ifndef __APP_STORAGE_H
#define __APP_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define APP_STORAGE_NAME_MAX  13U

typedef enum {
    APP_STORAGE_REQ_IDLE = 0,
    APP_STORAGE_REQ_PENDING,
    APP_STORAGE_REQ_RUNNING,
    APP_STORAGE_REQ_DONE
} App_StorageRequestState_t;

typedef enum {
    APP_STORAGE_OP_LIST = 0,
    APP_STORAGE_OP_CREATE,
    APP_STORAGE_OP_READ,
    APP_STORAGE_OP_READ_TAIL,
    APP_STORAGE_OP_WRITE,
    APP_STORAGE_OP_DELETE,
    APP_STORAGE_OP_TRUNCATE
} App_StorageOperation_t;

typedef struct {
    char name[APP_STORAGE_NAME_MAX];
    uint32_t size;
    uint8_t attr;
} App_StorageEntry_t;

typedef struct {
    volatile uint8_t state;
    uint8_t operation;
    uint8_t result;
    char path[32];
    char *data;
    uint32_t data_capacity;
    uint32_t data_length;
    uint32_t file_size;
    uint16_t max_lines;
    App_StorageEntry_t *entries;
    uint16_t entry_capacity;
    uint16_t entry_count;
    uint8_t truncated;          /* 列表项超过容量时置 1，调用方需给出明确反馈 */
} App_StorageRequest_t;

void App_StorageInit(void);
uint8_t App_StorageMount(void);
uint8_t App_StorageIsReady(void);
uint8_t App_StorageSelfTest(void);
uint8_t App_StorageGetSelfTestResult(void);
uint8_t App_StorageGetLastResult(void);
uint8_t App_StorageLock(uint32_t timeout_ms);
void App_StorageUnlock(void);
void App_StorageReportError(void);
uint8_t App_StorageSubmit(App_StorageRequest_t *request);
void App_StorageWorkerPoll(void);
void App_StorageRequestReset(App_StorageRequest_t *request);

#ifdef __cplusplus
}
#endif

#endif /* __APP_STORAGE_H */
