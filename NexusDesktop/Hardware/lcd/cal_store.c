/**
  ******************************************************************************
  * @file    cal_store.c
  * @brief   触摸校准参数掉电存储实现 (片内 Flash 扇区7, 只保留最近一条)
  * @note    记录格式 (28 字节, 固定写在扇区起始):
  *          [magic 4B][seq 4B][TP_CalData_t 16B][crc32 4B]
  *          - CalStore_Save(): 擦除整个扇区后写入新记录 (每次校准一次,
  *            128KB 擦除期间 CPU 停顿约 1~2 秒, 可接受)
  *          - CalStore_Load(): 读扇区起始记录, 校验 magic+CRC, 载入
  *          - seq 每次校准 +1, 用于确认"数据已更新"
  ******************************************************************************
  */

#include "cal_store.h"
#include <string.h>
#include <stddef.h>

#define CAL_SECTOR_ADDR   0x08060000UL   /* F407VE 最后 128KB 扇区 (扇区7) */
#define CAL_SECTOR_SIZE   (128UL * 1024UL)
#define CAL_MAGIC         0x43414C31UL   /* 'CAL1' */

/* 存储记录: 28 字节 */
typedef struct
{
    uint32_t     magic;
    uint32_t     seq;       /* 校准序号, 每次校准 +1 */
    TP_CalData_t data;
    uint32_t     crc;       /* magic..data 的 CRC32 */
} TP_CalRec_t;

/**
  * @brief  软件 CRC32 (多项式 0xEDB88320)
  */
static uint32_t cal_crc32(const uint8_t *buf, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;
    int b;

    for (i = 0; i < len; i++)
    {
        crc ^= buf[i];
        for (b = 0; b < 8; b++)
        {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
        }
    }
    return ~crc;
}

/**
  * @brief  读取当前存储的记录 (magic+CRC 校验)
  * @retval 1=有效, 0=无记录/损坏
  */
static uint8_t cal_read_rec(TP_CalRec_t *out)
{
    TP_CalRec_t rec;

    if (*(volatile uint32_t *)CAL_SECTOR_ADDR != CAL_MAGIC)
    {
        return 0;
    }
    memcpy(&rec, (const void *)CAL_SECTOR_ADDR, sizeof(rec));
    if (cal_crc32((const uint8_t *)&rec, offsetof(TP_CalRec_t, crc)) != rec.crc)
    {
        return 0;
    }
    *out = rec;
    return 1;
}

/**
  * @brief  擦除校准扇区
  */
static void cal_erase_sector(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t err = 0;

    HAL_FLASH_Unlock();
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = FLASH_SECTOR_7;
    erase.NbSectors = 1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    HAL_FLASHEx_Erase(&erase, &err);
    HAL_FLASH_Lock();
}

/**
  * @brief  编程记录 (按 32 位字写入扇区起始)
  */
static void cal_program_rec(const TP_CalRec_t *rec)
{
    const uint32_t *w = (const uint32_t *)rec;
    uint32_t i;

    HAL_FLASH_Unlock();
    for (i = 0; i < (sizeof(TP_CalRec_t) / 4U); i++)
    {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CAL_SECTOR_ADDR + i * 4U, w[i]);
    }
    HAL_FLASH_Lock();
}

/**
  * @brief  载入最新校准记录
  */
uint8_t CalStore_Load(TP_CalData_t *out)
{
    TP_CalRec_t rec;

    if (!cal_read_rec(&rec))
    {
        return 0;
    }
    *out = rec.data;
    return 1;
}

/**
  * @brief  保存校准记录 (擦除后写入, 只保留最近一条)
  */
uint8_t CalStore_Save(const TP_CalData_t *in)
{
    TP_CalRec_t old;
    TP_CalRec_t rec;
    uint32_t next_seq = 1U;

    if (cal_read_rec(&old))
    {
        next_seq = old.seq + 1U;
    }

    memset(&rec, 0, sizeof(rec));
    rec.magic = CAL_MAGIC;
    rec.seq = next_seq;
    rec.data = *in;
    rec.crc = cal_crc32((const uint8_t *)&rec, offsetof(TP_CalRec_t, crc));

    cal_erase_sector();
    cal_program_rec(&rec);
    return 1;
}

/**
  * @brief  获取当前校准序号 (0=无记录)
  */
uint32_t CalStore_GetSeq(void)
{
    TP_CalRec_t rec;

    if (!cal_read_rec(&rec))
    {
        return 0;
    }
    return rec.seq;
}
