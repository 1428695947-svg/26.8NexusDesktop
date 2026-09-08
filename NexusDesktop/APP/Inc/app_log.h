/**
  ******************************************************************************
  * @file    app_log.h
  * @brief   系统日志模块 (SD 持久化 + 日志查看应用数据源)
  * @author  Doubao
  * @version V1.0
  * @date    2026-09-04
  * @note    - 日志事件由各任务通过 App_Log_Event() 投递, 经 FreeRTOS 队列交给
  *            独立日志任务 (App_LogTask) 写入 SD 卡文件 0:/SYSLOG.LOG。
  *          - 写盘在独立任务中完成, 不阻塞 UI/输入任务。
  *          - 记录内容: 登录失败/成功、文件操作、设置修改、输入设备断开恢复、
  *            熄屏唤醒、错误事件等。
  ******************************************************************************
  */

#ifndef __APP_LOG_H
#define __APP_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ========================= 日志级别 ========================= */
#define LOG_LEVEL_INFO      0U
#define LOG_LEVEL_WARN      1U
#define LOG_LEVEL_ERROR     2U

/* ========================= 常量 ========================= */
#define LOG_MSG_MAX         96U     /* 单条日志文本最大长度 */
#define LOG_QUEUE_LEN       16U     /* 日志队列深度 (条) */
#define LOG_FILE_PATH       "0:/SYSLOG.LOG"

/* ========================= 运行统计 (供系统监控应用读取) ========================= */
typedef struct {
    uint32_t log_events;        /* 已产生的日志事件总数 */
    uint32_t log_written;       /* 成功写入 SD 的日志条数 */
    uint32_t log_dropped;       /* 丢弃的日志条数 (队列满) */
    uint32_t sd_write_fail;     /* SD 写入失败次数 */
    uint32_t error_count;       /* 错误事件计数 */
    uint8_t  queue_current;     /* 当前待写日志数 */
    uint8_t  queue_peak;        /* 启动以来队列峰值 */
    uint8_t  sd_ready;          /* SD 卡是否挂载成功 (1=成功) */
} App_LogStats_t;

/* ========================= 公共接口 ========================= */

/**
  * @brief  初始化日志模块 (创建队列, 必须在调度器启动前调用一次)
  * @note   在 App_Init() 中调用 (main 启动调度器之前)
  */
void App_Log_Init(void);

/**
  * @brief  日志任务入口 (由 freertos.c 创建)
  * @note   任务内: 挂载 SD -> 打开日志文件 -> 循环消费队列写盘
  */
void App_LogTask(void *argument);

/**
  * @brief  记录一条日志事件 (任务上下文调用, printf 风格格式化)
  * @param  level: LOG_LEVEL_INFO / WARN / ERROR
  * @param  fmt:   格式化字符串 (同 printf)
  */
void App_Log_Event(uint8_t level, const char *fmt, ...);

/**
  * @brief  记录一条错误日志 (同时累加错误计数, 供系统监控显示)
  */
void App_Log_Error(const char *fmt, ...);

/**
  * @brief  查询 SD 卡是否挂载成功
  */
uint8_t App_Log_IsSdReady(void);

/**
  * @brief  读取日志运行统计 (供系统监控应用)
  */
void App_Log_GetStats(App_LogStats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __APP_LOG_H */
