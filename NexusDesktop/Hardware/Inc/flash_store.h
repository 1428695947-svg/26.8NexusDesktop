/**
  ******************************************************************************
  * @file    flash_store.h
  * @brief   掉电保存驱动 - 密码等配置项的片内 Flash 持久化存储
  * @note    与 key 模块放置规则一致: 头文件在 Hardware/Inc, 源文件在 Hardware/Src。
  *          使用 STM32F407VET6 片内 Flash 扇区6 (0x08040000, 128KB), 掉电不丢失。
  ******************************************************************************
  */
#ifndef __FLASH_STORE_H
#define __FLASH_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 密码最大长度 (含结尾 '\0' 占位, 不超过 Login 代码中的 max_length) */
#define FLASH_PWD_MAX      20U
#define FLASH_PWD_BUF      (FLASH_PWD_MAX + 1U)

/**
  * @brief  初始化掉电保存驱动
  * @note   尝试从 Flash 载入最近一次保存的密码到 RAM 缓存;
  *         若 Flash 无记录 (首次上电), 则写入默认密码 "kykky"。
  * @retval None
  */
void FlashStore_Init(void);

/**
  * @brief  读取掉电保存的密码
  * @param  out:  输出缓冲区
  * @param  size: 输出缓冲区大小 (至少 FLASH_PWD_BUF)
  * @retval 1=成功(已带 '\0' 结尾), 0=失败
  */
uint8_t FlashStore_LoadPassword(char *out, uint32_t size);

/**
  * @brief  保存密码到 Flash (掉电保持)
  * @param  pwd: 以 '\0' 结尾的密码字符串
  * @retval 1=成功, 0=失败
  */
uint8_t FlashStore_SavePassword(const char *pwd);

/**
  * @brief  获取当前缓存的密码 (只读, 生命周期为整个程序)
  * @retval 密码字符串指针, 永不为 NULL
  */
const char *FlashStore_GetPassword(void);

#ifdef __cplusplus
}
#endif

#endif /* __FLASH_STORE_H */
