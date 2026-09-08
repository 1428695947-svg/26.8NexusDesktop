/**
  ******************************************************************************
  * @file    app_logview.h
  * @brief   日志查看应用 (显示 SD 持久化的系统日志)
  * @author  Doubao
  * @version V1.0
  * @date    2026-09-04
  ******************************************************************************
  */

#ifndef __APP_LOGVIEW_H
#define __APP_LOGVIEW_H

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  打开日志查看界面
  */
void App_LogView_Open(void);

/**
  * @brief  关闭日志查看界面 (返回桌面)
  */
void App_LogView_Close(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_LOGVIEW_H */
