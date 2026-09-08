/**
  ******************************************************************************
  * @file    app_file.h
  * @brief   文件管理应用 (SD 卡 FATFS)
  * @author  Doubao
  * @version V1.0
  * @date    2026-09-04
  * @note    基础功能: 文件列表/新建(重名检测)/打开查看/写入保存/删除。
  *          FatFs 操作由低优先级存储工作线程执行，LVGL 任务只提交请求和显示结果。
  *          FATFS 关闭 LFN, 文件名限 8.3 ASCII。
  ******************************************************************************
  */

#ifndef __APP_FILE_H
#define __APP_FILE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  打开文件管理界面
  */
void App_File_Open(void);

/**
  * @brief  关闭文件管理界面 (返回桌面)
  */
void App_File_Close(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_FILE_H */
