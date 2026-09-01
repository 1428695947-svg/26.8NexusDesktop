/**
  ******************************************************************************
  * @file    cal_store.h
  * @brief   触摸校准参数掉电存储 (薄封装)
  * @note    本文件为薄封装, 实际存储由 user_store.c 统一管理 (片内 Flash 扇区7)。
  *          保留 CalStore_* 接口, 供 touch.c / app.c 调用, 行为不变。
  ******************************************************************************
  */
#ifndef __CAL_STORE_H
#define __CAL_STORE_H

#include "main.h"

#define CAL_MAX_RECS   5U    /* 最多保留最近 5 次校准 */

/* 单次校准数据 (与 tp_dev 对应) */
typedef struct
{
    float   xfac;       /* X 校准比例 */
    float   yfac;       /* Y 校准比例 */
    int16_t xoff;       /* X 偏移 */
    int16_t yoff;       /* Y 偏移 */
    uint8_t touchtype;  /* 触屏类型 (X/Y 是否互换) */
} TP_CalData_t;

uint8_t  CalStore_Load(TP_CalData_t *out);      /* 载入最新一条, 1=成功 */
uint8_t  CalStore_Save(const TP_CalData_t *in); /* 追加保存, 自动保留最近5条, 1=成功 */
uint32_t CalStore_GetSeq(void);                 /* 最新记录的序号 (0=无记录) */

#endif
