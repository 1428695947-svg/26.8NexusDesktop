/**
  ******************************************************************************
  * @file    user_store.c
  * @brief   用户配置掉电存储实现 (片内 Flash 扇区7, 追加式记录)
  * @note    记录格式:
  *          [magic 4B][seq 4B][TP_CalData_t 16B][cal_seq 4B][pwd 21B]
  *          [SysSettings_t 10B][settings_marker 1B][crc32 4B]
  *          - UserStore_SaveXxx(): 更新缓存 -> 向下一个空槽追加新记录
  *          - UserStore_Init()   : 扫描记录, 选择序号最新且 CRC 正确的一条;
  *            断电留下的半写记录会被跳过, 上一条有效记录仍可恢复。
  *            首次上电(无记录)则写入默认密码 "kykky" + 默认设置。
  *          - 代码区(分散加载)限制在扇区0~6, 本扇区(7)不会被代码覆盖。
  *          - 128KB 扇区擦除期间 CPU 停顿约 1~2 秒, 保存为低频操作可接受。
  ******************************************************************************
  */

#include "user_store.h"
#include <string.h>
#include <stddef.h>

#define USER_SECTOR_ADDR   0x08060000UL   /* F407VE 片内 Flash 扇区7 起始 */
#define USER_SECTOR_BYTES  (128UL * 1024UL)
#define USER_SECTOR        FLASH_SECTOR_7
#define USER_MAGIC         0x55535231UL   /* 'USR1' */
#define SETTINGS_MARKER    0xA5U          /* 包含 volume 字段的设置版本标记 */

#define PWD_ALIGN          FLASH_PWD_BUF  /* 21 字节 */

/* 旧版 cal_store.c 的存储位置与格式 (迁移用): 记录位于扇区6起始 0x08040000 */
#define LEGACY_CAL_ADDR    0x08040000UL
#define LEGACY_CAL_MAGIC   0x43414C31UL   /* 'CAL1' */

/* 存储记录: 64 字节 (4 字节对齐, 便于按字编程) */
typedef struct
{
    uint32_t     magic;
    uint32_t     seq;         /* 总体保存序号 */
    TP_CalData_t cal;         /* 触摸校准 (16B) */
    uint32_t     cal_seq;     /* 校准序号 (0=从未校准) */
    char         pwd[PWD_ALIGN];  /* 密码 (21B, 含 '\0') */
    SysSettings_t settings;   /* 系统设置 (10B) */
    uint8_t      settings_marker; /* 复用旧 pad[2], 不改变画图数据偏移 */
    DesktopIconLayout_t desktop_layout;
    DrawData_t   drawing;     /* 画图数据 (底色 + 线段表) */
    uint32_t     crc;         /* magic..pad 的 CRC32 */
} UserRec_t;

/* 上一版大记录：尚未包含桌面图标位置，用于无损升级。 */
typedef struct
{
    uint32_t     magic;
    uint32_t     seq;
    TP_CalData_t cal;
    uint32_t     cal_seq;
    char         pwd[PWD_ALIGN];
    SysSettings_t settings;
    uint8_t      settings_marker;
    DrawData_t   drawing;
    uint32_t     crc;
} PreviousUserRec_t;

/* RAM 缓存 */
static UserRec_t    s_rec;
static uint8_t      s_rec_valid = 0;   /* 1=已从 Flash 载入或已初始化 */
static uint32_t     s_saved_addr = USER_SECTOR_ADDR;
static uint32_t     s_next_slot = 0U;

#define USER_SLOT_BYTES    ((uint32_t)sizeof(UserRec_t))
#define USER_SLOT_COUNT    (USER_SECTOR_BYTES / USER_SLOT_BYTES)

/**
  * @brief  旧版校准记录格式 (cal_store.c 早期实现, 28 字节)
  */
typedef struct
{
    uint32_t     magic;
    uint32_t     seq;
    TP_CalData_t data;
    uint32_t     crc;      /* magic..data 的 CRC32 */
} LegacyCalRec_t;

/* 旧版 8 字节设置，仅用于兼容迁移。 */
typedef struct
{
    uint16_t sens;
    uint16_t cursor_zoom;
    uint16_t brightness;
    uint16_t sleep_sec;
} LegacySettings_t;

/* 上一版用户记录 (64B, 未含画图): 升级后用于迁移设置/密码/校准 */
typedef struct
{
    uint32_t     magic;
    uint32_t     seq;
    TP_CalData_t cal;
    uint32_t     cal_seq;
    char         pwd[PWD_ALIGN];
    LegacySettings_t settings;
    uint8_t      pad[3];
    uint32_t     crc;
} LegacyUserRec_t;

/**
  * @brief  软件 CRC32 (多项式 0xEDB88320)
  */
static uint32_t user_crc32(const uint8_t *buf, uint32_t len)
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
  * @retval 1=有效
  */
static uint8_t user_seq_newer(uint32_t lhs, uint32_t rhs)
{
    return ((int32_t)(lhs - rhs) > 0) ? 1U : 0U;
}

static uint8_t user_read_rec(UserRec_t *out)
{
    uint8_t found = 0U;
    uint32_t slot;
    uint32_t best_addr = USER_SECTOR_ADDR;
    uint32_t best_seq = 0U;

    s_next_slot = 0U;
    for (slot = 0U; slot < USER_SLOT_COUNT; slot++) {
        uint32_t addr = USER_SECTOR_ADDR + slot * USER_SLOT_BYTES;
        uint32_t magic = *(volatile uint32_t *)addr;

        if (magic == 0xFFFFFFFFUL) {
            if (s_next_slot == slot) {
                break;
            }
            continue;
        }
        s_next_slot = slot + 1U;
        if (magic != USER_MAGIC) {
            continue;
        }
        if (user_crc32((const uint8_t *)addr, offsetof(UserRec_t, crc)) !=
            *(volatile uint32_t *)(addr + offsetof(UserRec_t, crc))) {
            continue;
        }
        if (!found || user_seq_newer(*(volatile uint32_t *)(addr + offsetof(UserRec_t, seq)), best_seq)) {
            best_seq = *(volatile uint32_t *)(addr + offsetof(UserRec_t, seq));
            best_addr = addr;
            found = 1U;
        }
    }
    if (found) {
        memcpy(out, (const void *)best_addr, sizeof(*out));
        s_saved_addr = best_addr;
    }
    return found;
}

static uint8_t user_import_previous_record(void)
{
    uint8_t found = 0U;
    uint32_t slot;
    uint32_t best_seq = 0U;
    uint32_t best_addr = USER_SECTOR_ADDR;
    uint32_t count = USER_SECTOR_BYTES / (uint32_t)sizeof(PreviousUserRec_t);

    for (slot = 0U; slot < count; slot++) {
        uint32_t addr = USER_SECTOR_ADDR + slot * (uint32_t)sizeof(PreviousUserRec_t);
        const PreviousUserRec_t *rec = (const PreviousUserRec_t *)addr;
        if (rec->magic == 0xFFFFFFFFUL) break;
        if (rec->magic != USER_MAGIC) continue;
        if (user_crc32((const uint8_t *)rec, offsetof(PreviousUserRec_t, crc)) != rec->crc) continue;
        if (!found || user_seq_newer(rec->seq, best_seq)) {
            best_seq = rec->seq;
            best_addr = addr;
            found = 1U;
        }
    }
    if (!found) return 0U;

    /* 旧记录含约 3.2KB 画图数据，绝不能复制到启动栈上的临时结构体。
       只保留最新记录地址，并从 Flash 直接拷贝到全局 RAM 缓存。 */
    {
        const PreviousUserRec_t *best = (const PreviousUserRec_t *)best_addr;
        memset(&s_rec, 0, sizeof(s_rec));
        s_rec.magic = best->magic;
        s_rec.seq = best->seq;
        s_rec.cal = best->cal;
        s_rec.cal_seq = best->cal_seq;
        memcpy(s_rec.pwd, best->pwd, sizeof(s_rec.pwd));
        s_rec.settings = best->settings;
        s_rec.settings_marker = best->settings_marker;
        memcpy(&s_rec.drawing, &best->drawing, sizeof(s_rec.drawing));
        s_rec.desktop_layout.marker = 0U;
    }
    return 1U;
}

/**
  * @brief  校验 Flash 中的记录是否有效 (不复制到 RAM, 无大栈占用)
  */
static uint8_t user_flash_ok(void)
{
    if (*(volatile uint32_t *)s_saved_addr != USER_MAGIC)
    {
        return 0;
    }
    return (user_crc32((const uint8_t *)s_saved_addr, offsetof(UserRec_t, crc)) ==
            *(volatile uint32_t *)(s_saved_addr + offsetof(UserRec_t, crc))) ? 1U : 0U;
}

/**
  * @brief  擦除存储扇区 (扇区7)
  */
static uint8_t user_erase_sector(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t err = 0;

    HAL_FLASH_Unlock();
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = USER_SECTOR;
    erase.NbSectors = 1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    if (HAL_FLASHEx_Erase(&erase, &err) != HAL_OK) {
        HAL_FLASH_Lock();
        return 0U;
    }
    HAL_FLASH_Lock();
    return 1U;
}

/**
  * @brief  编程记录 (按 32 位字写入扇区起始)
  */
static uint8_t user_program_rec(uint32_t addr, const UserRec_t *rec)
{
    const uint32_t *w = (const uint32_t *)rec;
    uint32_t i;

    HAL_FLASH_Unlock();
    for (i = 0; i < (sizeof(UserRec_t) / 4U); i++)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i * 4U, w[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return 0U;
        }
    }
    HAL_FLASH_Lock();
    return 1U;
}

/**
  * @brief  提交当前缓存 (写 CRC -> 擦除 -> 编程)
  * @retval 1=成功
  */
static uint8_t user_commit(void)
{
    uint32_t addr;

    s_rec.seq++;
    s_rec.magic = USER_MAGIC;
    s_rec.crc = user_crc32((const uint8_t *)&s_rec, offsetof(UserRec_t, crc));

    if (s_next_slot >= USER_SLOT_COUNT) {
        if (!user_erase_sector()) {
            return 0U;
        }
        s_next_slot = 0U;
    }
    addr = USER_SECTOR_ADDR + s_next_slot * USER_SLOT_BYTES;
    if (!user_program_rec(addr, &s_rec)) {
        /* 当前槽可能半写; 后续保存跳过它，启动扫描也会回退上一有效槽。 */
        s_next_slot++;
        return 0U;
    }
    if (*(volatile uint32_t *)addr != USER_MAGIC ||
        user_crc32((const uint8_t *)addr, offsetof(UserRec_t, crc)) !=
        *(volatile uint32_t *)(addr + offsetof(UserRec_t, crc))) {
        s_next_slot++;
        return 0U;
    }
    s_saved_addr = addr;
    s_next_slot++;
    s_rec_valid = 1U;
    return 1U;
}

/**
  * @brief  填充默认值 (首次上电)
  */
static void user_set_defaults(void)
{
    memset(&s_rec, 0, sizeof(s_rec));
    s_rec.magic = USER_MAGIC;
    s_rec.seq = 0;
    s_rec.cal_seq = 0;               /* 未校准 */
    strncpy(s_rec.pwd, "kykky", FLASH_PWD_MAX);
    s_rec.pwd[FLASH_PWD_MAX] = '\0';
    s_rec.settings.sens        = SETTINGS_SENS_DEFAULT;
    s_rec.settings.cursor_zoom = SETTINGS_ZOOM_DEFAULT;
    s_rec.settings.brightness  = SETTINGS_BRIGHT_DEFAULT;
    s_rec.settings.sleep_sec   = SETTINGS_SLEEP_DEFAULT;
    s_rec.settings.volume      = SETTINGS_VOLUME_DEFAULT;
    s_rec.settings_marker      = SETTINGS_MARKER;
    s_rec.desktop_layout.marker = 0U;
    s_rec.drawing.bg_color     = 0xFFFF;   /* 默认白底 */
    s_rec.drawing.seg_cnt      = 0;
}

/**
  * @brief  迁移上一版 64B 用户记录 (设置/密码/校准), 保留用户已配置内容
  * @retval 1=成功
  */
static uint8_t user_import_legacy_record(void)
{
    LegacyUserRec_t rec;

    if (*(volatile uint32_t *)USER_SECTOR_ADDR != USER_MAGIC)
    {
        return 0;
    }
    memcpy(&rec, (const void *)USER_SECTOR_ADDR, sizeof(rec));
    if (user_crc32((const uint8_t *)&rec, offsetof(LegacyUserRec_t, crc)) != rec.crc)
    {
        return 0;
    }

    s_rec.cal      = rec.cal;
    s_rec.cal_seq  = rec.cal_seq;
    memcpy(s_rec.pwd, rec.pwd, PWD_ALIGN);
    s_rec.settings.sens = rec.settings.sens;
    s_rec.settings.cursor_zoom = rec.settings.cursor_zoom;
    s_rec.settings.brightness = rec.settings.brightness;
    s_rec.settings.sleep_sec = rec.settings.sleep_sec;
    return 1;
}

/**
  * @brief  迁移旧版校准数据 (位于旧 cal_store 扇区6起始 0x08040000)
  * @note   早期固件把触摸校准单独存在 0x08040000 ('CAL1' 记录)。
  *         升级到统一存储后, 若新记录中还没有校准, 自动导入旧记录,
  *         避免已校准的板子开机被强制重新校准。
  * @retval 1=找到并导入成功
  */
static uint8_t user_import_legacy_cal(void)
{
    LegacyCalRec_t rec;

    if (*(volatile uint32_t *)LEGACY_CAL_ADDR != LEGACY_CAL_MAGIC)
    {
        return 0;
    }
    memcpy(&rec, (const void *)LEGACY_CAL_ADDR, sizeof(rec));
    if (user_crc32((const uint8_t *)&rec, offsetof(LegacyCalRec_t, crc)) != rec.crc)
    {
        return 0;
    }
    /* 旧记录可能是全 0 的无效校准, 过滤掉 */
    if (rec.data.xfac == 0.0f && rec.data.yfac == 0.0f &&
        rec.data.xoff == 0 && rec.data.yoff == 0)
    {
        return 0;
    }
    s_rec.cal = rec.data;
    s_rec.cal_seq = (rec.seq != 0U) ? rec.seq : 1U;
    return 1;
}

/**
  * @brief  初始化 (载入缓存, 首次写入默认值)
  */
void UserStore_Init(void)
{
    if (user_read_rec(&s_rec))
    {
        s_rec_valid = 1;
        /* 旧大记录中这 3 字节原为 pad，布局不变地升级音量字段。 */
        if (s_rec.settings_marker != SETTINGS_MARKER)
        {
            s_rec.settings.volume = SETTINGS_VOLUME_DEFAULT;
            s_rec.settings_marker = SETTINGS_MARKER;
            user_commit();
        }
        /* 新记录存在但尚未校准 -> 尝试迁移旧版校准 */
        if (s_rec.cal_seq == 0U && user_import_legacy_cal())
        {
            user_commit();      /* 迁移结果持久化到新记录 */
        }
    }
    else
    {
        user_set_defaults();
        if (user_import_previous_record()) {
            /* 记录尺寸变化后不能在旧槽位后直接追加；擦除后写入迁移副本。 */
            user_erase_sector();
            s_next_slot = 0U;
        }
        else {
            user_import_legacy_record();    /* 更早的 64B 记录: 保留设置/密码/校准 */
        }
        if (s_rec.cal_seq == 0U)
        {
            user_import_legacy_cal();       /* 更旧的扇区6校准 */
        }
        user_commit();              /* 写入默认密码 + 默认设置 */
    }
}

/**
  * @brief  读取当前缓存的设置
  */
const SysSettings_t *UserStore_GetSettings(void)
{
    return &s_rec.settings;
}

/**
  * @brief  保存设置
  */
uint8_t UserStore_SaveSettings(const SysSettings_t *in)
{
    if (in == NULL) return 0;
    s_rec.settings = *in;
    /* 范围保护 */
    if (s_rec.settings.sens < SETTINGS_SENS_MIN)   s_rec.settings.sens = SETTINGS_SENS_MIN;
    if (s_rec.settings.sens > SETTINGS_SENS_MAX)   s_rec.settings.sens = SETTINGS_SENS_MAX;
    if (s_rec.settings.cursor_zoom < SETTINGS_ZOOM_MIN) s_rec.settings.cursor_zoom = SETTINGS_ZOOM_MIN;
    if (s_rec.settings.cursor_zoom > SETTINGS_ZOOM_MAX) s_rec.settings.cursor_zoom = SETTINGS_ZOOM_MAX;
    if (s_rec.settings.brightness < SETTINGS_BRIGHT_MIN) s_rec.settings.brightness = SETTINGS_BRIGHT_MIN;
    if (s_rec.settings.brightness > SETTINGS_BRIGHT_MAX) s_rec.settings.brightness = SETTINGS_BRIGHT_MAX;
    if (s_rec.settings.volume > SETTINGS_VOLUME_MAX) s_rec.settings.volume = SETTINGS_VOLUME_MAX;
    s_rec.settings_marker = SETTINGS_MARKER;
    return user_commit();
}

/**
  * @brief  读设置 (从 Flash 校验读取)
  */
uint8_t UserStore_LoadSettings(SysSettings_t *out)
{
    if (out == NULL) return 0;
    if (!user_flash_ok()) return 0;
    memcpy(out, (const uint8_t *)s_saved_addr + offsetof(UserRec_t, settings), sizeof(*out));
    return 1;
}

const DesktopIconLayout_t *UserStore_GetDesktopLayout(void)
{
    return &s_rec.desktop_layout;
}

uint8_t UserStore_SaveDesktopLayout(const DesktopIconLayout_t *in)
{
    if (in == NULL || in->marker != DESKTOP_LAYOUT_MARKER) return 0U;
    s_rec.desktop_layout = *in;
    return user_commit();
}

/**
  * @brief  读取缓存校准 (0=未校准)
  */
uint8_t UserStore_LoadCal(TP_CalData_t *out)
{
    if (out == NULL) return 0;
    if (s_rec.cal_seq == 0) return 0;
    *out = s_rec.cal;
    return 1;
}

/**
  * @brief  保存校准 (cal_seq +1)
  */
uint8_t UserStore_SaveCal(const TP_CalData_t *in)
{
    if (in == NULL) return 0;
    s_rec.cal = *in;
    s_rec.cal_seq++;
    return user_commit();
}

/**
  * @brief  获取校准序号
  */
uint32_t UserStore_GetCalSeq(void)
{
    return s_rec.cal_seq;
}

/**
  * @brief  读取缓存密码
  */
const char *UserStore_GetPassword(void)
{
    return s_rec.pwd;
}

/**
  * @brief  读密码 (从 Flash 校验读取)
  */
uint8_t UserStore_LoadPassword(char *out, uint32_t size)
{
    uint32_t n;

    if (out == NULL || size == 0) return 0;
    if (!user_flash_ok()) return 0;
    n = (uint32_t)strlen((const char *)s_saved_addr + offsetof(UserRec_t, pwd));
    if (n >= size) n = size - 1U;
    memcpy(out, (const uint8_t *)s_saved_addr + offsetof(UserRec_t, pwd), n);
    out[n] = '\0';
    return 1;
}

/**
  * @brief  保存密码
  */
uint8_t UserStore_SavePassword(const char *pwd)
{
    uint32_t len;
    if (pwd == NULL) return 0;
    len = (uint32_t)strlen(pwd);
    if (len > FLASH_PWD_MAX) len = FLASH_PWD_MAX;
    memset(s_rec.pwd, 0, sizeof(s_rec.pwd));
    memcpy(s_rec.pwd, pwd, len);
    s_rec.pwd[len] = '\0';
    return user_commit();
}

/**
  * @brief  读取当前缓存的画图数据
  */
const DrawData_t *UserStore_GetDrawing(void)
{
    return &s_rec.drawing;
}

/**
  * @brief  保存画图数据 (随整条用户记录一并写 Flash)
  */
uint8_t UserStore_SaveDrawing(const DrawData_t *in)
{
    if (in == NULL) return 0;
    s_rec.drawing = *in;
    if (s_rec.drawing.seg_cnt > DRAW_SEG_MAX)
    {
        s_rec.drawing.seg_cnt = DRAW_SEG_MAX;
    }
    return user_commit();
}
