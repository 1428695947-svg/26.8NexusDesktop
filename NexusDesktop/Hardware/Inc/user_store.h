/**
  ******************************************************************************
  * @file    user_store.h
  * @brief   用户配置掉电存储 (片内 Flash 扇区7, 追加式记录)
  * @note    把 触摸校准 + 登录密码 + 系统设置 + 画图数据 合并为一条记录, 存放在 STM32F407VET6
  *          片内 Flash 最后一个 128KB 扇区 (扇区7, 0x08060000), 掉电不丢失。
  *          - 追加记录 + CRC => 正常保存不擦扇区; 半写记录不会覆盖上一份有效数据。
  *          - 代码区在分散加载文件中被限制在扇区0~6 (0x08000000~0x0805FFFF),
  *            该存储扇区(扇区7)永远不会被代码覆盖。
  *          - 单条记录格式:
  *            [magic 4B][seq 4B][TP_CalData_t 16B][cal_seq 4B][pwd 21B][SysSettings_t 10B]
  *            [settings_marker 1B][DrawData_t 3204B][crc32 4B]
  ******************************************************************************
  */
#ifndef __USER_STORE_H
#define __USER_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cal_store.h"      /* TP_CalData_t */
#include "flash_store.h"    /* FLASH_PWD_MAX / FLASH_PWD_BUF */

/* ========================= 系统设置数据结构 ========================= */
typedef struct
{
    uint16_t sens;          /* 光标灵敏度 1..10 (映射到摇杆鼠标速度) */
    uint16_t cursor_zoom;   /* 光标缩放百分比 100..200 (256=100%) */
    uint16_t brightness;    /* 屏幕亮度 10..100 (%) */
    uint16_t sleep_sec;     /* 熄屏时间(秒), 0=从不自动熄屏 */
    uint16_t volume;        /* 逻辑音量 0..100 (%), 音频硬件接入后直接复用 */
} SysSettings_t;

/* ========================= 桌面图标布局 ========================= */
#define DESKTOP_ICON_COUNT 7U
#define DESKTOP_LAYOUT_MARKER 0x49434F4EUL /* 'ICON' */
typedef struct
{
    uint32_t marker;
    int16_t x[DESKTOP_ICON_COUNT];
    int16_t y[DESKTOP_ICON_COUNT];
} DesktopIconLayout_t;

/* ==================== 画图数据 (随用户记录一并掉电保存) ==================== */
#define DRAW_SEG_MAX   320U      /* 最多保存的线段数 (每段 12B) */

/* 一条线段 (RGB565 颜色 + 两个端点), 12 字节对齐 */
typedef struct
{
    uint16_t color;              /* RGB565 */
    uint16_t x1;
    uint16_t y1;
    uint16_t x2;
    uint16_t y2;
} DrawSeg_t;

/* 一张画布: 底色 + 线段表 */
typedef struct
{
    uint16_t  bg_color;          /* 画布底色 (RGB565) */
    uint16_t  seg_cnt;           /* 有效线段数 (<= DRAW_SEG_MAX) */
    DrawSeg_t segs[DRAW_SEG_MAX];
} DrawData_t;

/* 设置取值范围 */
#define SETTINGS_SENS_MIN        1
#define SETTINGS_SENS_MAX        10
#define SETTINGS_ZOOM_MIN        100
#define SETTINGS_ZOOM_MAX        200
#define SETTINGS_BRIGHT_MIN      10
#define SETTINGS_BRIGHT_MAX      100
#define SETTINGS_VOLUME_MIN      0
#define SETTINGS_VOLUME_MAX      100
#define SETTINGS_SLEEP_NEVER     0

/* 默认值 */
#define SETTINGS_SENS_DEFAULT    5       /* -> speed = 1.0*5 = 5.0 px/frame */
#define SETTINGS_ZOOM_DEFAULT    100
#define SETTINGS_BRIGHT_DEFAULT  80
#define SETTINGS_SLEEP_DEFAULT   60      /* 60s */
#define SETTINGS_VOLUME_DEFAULT  50

/**
  * @brief  初始化掉电保存 (载入最新记录到 RAM 缓存; 首次上电写入默认密码+默认设置)
  */
void UserStore_Init(void);

/**
  * @brief  读取当前缓存的系统设置 (只读, 生命周期整个程序, 永不为 NULL)
  */
const SysSettings_t *UserStore_GetSettings(void);

/**
  * @brief  更新系统设置并保存到 Flash (掉电保持)
  * @param  in: 新设置
  * @retval 1=成功, 0=失败
  */
uint8_t UserStore_SaveSettings(const SysSettings_t *in);

/**
  * @brief  读最新设置 (从 Flash 校验读取)
  * @retval 1=成功, 0=失败
  */
uint8_t UserStore_LoadSettings(SysSettings_t *out);

const DesktopIconLayout_t *UserStore_GetDesktopLayout(void);
uint8_t UserStore_SaveDesktopLayout(const DesktopIconLayout_t *in);

/**
  * @brief  读取当前缓存的校准数据 (1=已校准且有效, 0=未校准)
  */
uint8_t UserStore_LoadCal(TP_CalData_t *out);

/**
  * @brief  保存校准数据 (cal_seq 自动 +1)
  * @retval 1=成功
  */
uint8_t UserStore_SaveCal(const TP_CalData_t *in);

/**
  * @brief  获取校准序号 (0=从未校准)
  */
uint32_t UserStore_GetCalSeq(void);

/**
  * @brief  读取缓存密码 (带 '\0' 结尾, 永不为 NULL)
  */
const char *UserStore_GetPassword(void);

/**
  * @brief  读取密码 (从 Flash 校验读取)
  * @retval 1=成功
  */
uint8_t UserStore_LoadPassword(char *out, uint32_t size);

/**
  * @brief  保存密码 (掉电保持)
  * @retval 1=成功
  */
uint8_t UserStore_SavePassword(const char *pwd);

/**
  * @brief  读取当前缓存的画图数据 (只读, 生命周期整个程序, 永不为 NULL)
  */
const DrawData_t *UserStore_GetDrawing(void);

/**
  * @brief  保存画图数据到 Flash (掉电保持)
  * @retval 1=成功
  */
uint8_t UserStore_SaveDrawing(const DrawData_t *in);

#ifdef __cplusplus
}
#endif

#endif /* __USER_STORE_H */
