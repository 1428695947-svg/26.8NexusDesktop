/**
  ******************************************************************************
  * @file    flash_store.c
  * @brief   登录密码掉电保存 (薄封装)
  * @note    实际存储由 user_store.c 统一管理 (片内 Flash 扇区7)。
  *          保留 FlashStore_* 接口, 供 app.c / custom.c 调用, 行为不变:
  *          - FlashStore_Init(): 载入(或首次写入默认)密码
  *          - FlashStore_LoadPassword() / SavePassword() / GetPassword()
  ******************************************************************************
  */

#include "flash_store.h"
#include "user_store.h"

/**
  * @brief  初始化 (载入缓存, 首次写入默认值)
  */
void FlashStore_Init(void)
{
    UserStore_Init();
}

/**
  * @brief  读取掉电保存的密码
  */
uint8_t FlashStore_LoadPassword(char *out, uint32_t size)
{
    return UserStore_LoadPassword(out, size);
}

/**
  * @brief  保存密码到 Flash (掉电保持)
  */
uint8_t FlashStore_SavePassword(const char *pwd)
{
    return UserStore_SavePassword(pwd);
}

/**
  * @brief  获取当前缓存的密码
  */
const char *FlashStore_GetPassword(void)
{
    return UserStore_GetPassword();
}
