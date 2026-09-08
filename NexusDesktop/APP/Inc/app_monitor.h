/**
  ******************************************************************************
  * @file    app_monitor.h
  * @brief   系统监控应用 (FreeRTOS 运行时状态查看)
  * @author  Doubao
  * @version V1.0
  * @date    2026-09-04
  * @note    显示: 运行时间 / 剩余堆内存 / 任务状态与栈水位 / 输入事件统计 / 错误计数。
  *          界面为 LVGL 屏幕, 由 App_Monitor_Open() 打开, 周期刷新由 LVGL
  *          定时器驱动 (运行于 LVGL 任务上下文, 无需独立任务)。
  ******************************************************************************
  */

#ifndef __APP_MONITOR_H
#define __APP_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  打开系统监控界面 (首次创建屏幕, 之后复用)
  */
void App_Monitor_Open(void);

/**
  * @brief  关闭系统监控界面 (返回桌面)
  */
void App_Monitor_Close(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_MONITOR_H */
