/**
  ******************************************************************************
  * @file    cal_store.c
  * @brief   触摸校准参数掉电存储 (薄封装)
  * @note    实际存储由 user_store.c 统一管理 (片内 Flash 扇区7)。
  *          保留 CalStore_* 接口, 供 touch.c / app.c 调用, 行为不变:
  *          - CalStore_Save(): 更新校准数据并保存 (cal_seq +1)
  *          - CalStore_Load() : 读取最新校准数据 (0=未校准)
  *          - CalStore_GetSeq(): 校准序号 (0=从未校准)
  ******************************************************************************
  */

#include "cal_store.h"
#include "user_store.h"

/**
  * @brief  载入最新校准记录
  */
uint8_t CalStore_Load(TP_CalData_t *out)
{
    return UserStore_LoadCal(out);
}

/**
  * @brief  保存校准记录
  */
uint8_t CalStore_Save(const TP_CalData_t *in)
{
    return UserStore_SaveCal(in);
}

/**
  * @brief  获取当前校准序号 (0=无记录)
  */
uint32_t CalStore_GetSeq(void)
{
    return UserStore_GetCalSeq();
}
