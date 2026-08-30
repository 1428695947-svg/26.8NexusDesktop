/**
  ******************************************************************************
  * @file    flash_store.c
  * @brief   掉电保存驱动实现 - 密码配置项的片内 Flash 持久化存储
  * @note    记录格式 (固定写在扇区起始):
  *          [magic 4B][seq 4B][len 4B][password 21B][pad 3B][crc32 4B]
  *          - FlashStore_SavePassword(): 擦除整个扇区后写入新记录
  *          - FlashStore_LoadPassword() : 读扇区起始记录, 校验 magic+CRC, 载入
 *          - 使用片内 Flash 最末尾 (扇区7 尾部 0x0807FF00), 避免后期程序增长覆盖
  ******************************************************************************
  */

#include "flash_store.h"
#include <string.h>
#include <stddef.h>

#define FLASH_SECTOR_ADDR    0x0807FF00UL   /* F407VE 片内 Flash 最末尾 (扇区7 尾部) */
#define FLASH_SECTOR_NUM     FLASH_SECTOR_7
#define FLASH_MAGIC          0x464C5331UL   /* 'FLS1' */

#define PWD_LEN_MAX          FLASH_PWD_BUF  /* 21 字节 (20 字符 + '\0') */
#define PWD_ALIGN            21U            /* 密码数据实际占用 (fls 记录按 4 字节对齐填充) */

/* 存储记录 */
typedef struct
{
    uint32_t magic;
    uint32_t seq;                 /* 保存序号, 每次保存 +1 */
    uint32_t len;                 /* 密码字符串长度 (不含 '\0') */
    uint8_t  pwd[PWD_ALIGN];      /* 密码数据 (含 '\0') */
    uint8_t  pad[3];              /* 对齐到 4 字节 */
    uint32_t crc;                 /* magic..pad 的 CRC32 */
} FlashRec_t;

/* RAM 缓存 */
static char s_pwd_cache[FLASH_PWD_BUF] = "kykky";

/**
  * @brief  软件 CRC32 (多项式 0xEDB88320)
  */
static uint32_t flash_crc32(const uint8_t *buf, uint32_t len)
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
  * @brief  读取当前存储记录 (magic+CRC 校验)
  */
static uint8_t flash_read_rec(FlashRec_t *out)
{
    FlashRec_t rec;

    if (*(volatile uint32_t *)FLASH_SECTOR_ADDR != FLASH_MAGIC)
    {
        return 0;
    }
    memcpy(&rec, (const void *)FLASH_SECTOR_ADDR, sizeof(rec));
    if (flash_crc32((const uint8_t *)&rec, offsetof(FlashRec_t, crc)) != rec.crc)
    {
        return 0;
    }
    *out = rec;
    return 1;
}

/**
  * @brief  擦除存储扇区
  */
static void flash_erase_sector(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t err = 0;

    HAL_FLASH_Unlock();
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = FLASH_SECTOR_NUM;
    erase.NbSectors = 1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    HAL_FLASHEx_Erase(&erase, &err);
    HAL_FLASH_Lock();
}

/**
  * @brief  编程记录 (按 32 位字写入扇区起始)
  */
static void flash_program_rec(const FlashRec_t *rec)
{
    const uint32_t *w = (const uint32_t *)rec;
    uint32_t i;

    HAL_FLASH_Unlock();
    for (i = 0; i < (sizeof(FlashRec_t) / 4U); i++)
    {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_SECTOR_ADDR + i * 4U, w[i]);
    }
    HAL_FLASH_Lock();
}

/**
  * @brief  初始化 (载入缓存, 首次写入默认密码)
  */
void FlashStore_Init(void)
{
    FlashRec_t rec;

    if (flash_read_rec(&rec))
    {
        if (rec.len < FLASH_PWD_BUF)
        {
            memcpy(s_pwd_cache, rec.pwd, rec.len);
            s_pwd_cache[rec.len] = '\0';
        }
    }
    else
    {
        FlashStore_SavePassword("kykky");
    }
}

/**
  * @brief  读取密码
  */
uint8_t FlashStore_LoadPassword(char *out, uint32_t size)
{
    FlashRec_t rec;
    uint32_t n;

    if (out == NULL || size == 0)
    {
        return 0;
    }
    if (!flash_read_rec(&rec))
    {
        return 0;
    }
    n = rec.len;
    if (n >= size)
    {
        n = size - 1U;
    }
    memcpy(out, rec.pwd, n);
    out[n] = '\0';
    return 1;
}

/**
  * @brief  保存密码
  */
uint8_t FlashStore_SavePassword(const char *pwd)
{
    FlashRec_t old;
    FlashRec_t rec;
    uint32_t next_seq = 1U;
    uint32_t len;

    if (pwd == NULL)
    {
        return 0;
    }
    len = (uint32_t)strlen(pwd);
    if (len > FLASH_PWD_MAX)
    {
        len = FLASH_PWD_MAX;
    }

    if (flash_read_rec(&old))
    {
        next_seq = old.seq + 1U;
    }

    memset(&rec, 0, sizeof(rec));
    rec.magic = FLASH_MAGIC;
    rec.seq = next_seq;
    rec.len = len;
    memcpy(rec.pwd, pwd, len);
    rec.pwd[len] = '\0';
    rec.crc = flash_crc32((const uint8_t *)&rec, offsetof(FlashRec_t, crc));

    flash_erase_sector();
    flash_program_rec(&rec);

    memcpy(s_pwd_cache, rec.pwd, len);
    s_pwd_cache[len] = '\0';
    return 1;
}

/**
  * @brief  获取当前缓存的密码
  */
const char *FlashStore_GetPassword(void)
{
    return s_pwd_cache;
}
