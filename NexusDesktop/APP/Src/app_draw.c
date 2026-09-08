/**
  ******************************************************************************
  * @file    app_draw.c
  * @brief   画图应用模块 - 原生 LCD 绘制与持久化
  * @note    画图前台期间独占 LCD，LVGL 任务通过公开状态接口暂停刷新。
  ******************************************************************************
  */

#include "app_draw.h"

#include "cmsis_os.h"
#include "lcd.h"
#include "gui.h"
#include "delay.h"
#include "user_store.h"
#include "app_mouse.h"
#include "touch.h"
#include "app_health.h"
#include "app_log.h"

/* 触摸画图状态: 上一笔坐标用于连续连线。 */
static uint16_t s_draw_prev_x = 0U;
static uint16_t s_draw_prev_y = 0U;
static uint8_t  s_draw_prev_valid = 0U;

/**
  * @brief  触摸画图 (应用层)
  * @note   前置条件: 调用前需先执行 TP_UpdateDebug()/TP_Scan(0) 刷新 tp_dev。
  *         - 触摸按下: 在触摸点画一个蓝色点
  *         - 持续触摸拖动: 与上一点连线 (蓝色)
  *         - 松开后: 重置笔画, 下一次按下重新起笔
  * @note   未校准时 tp_dev.x/y 是 XPT2046 原始 AD 值 (0~4095), 超出屏幕范围,
  *         这里做线性映射到 320x480 并做边界保护; 完成 TP_Adjust() 校准后
  *         xfac/xoff 生效, 自动使用精确屏幕坐标。
  */
void App_TouchDraw(void)
{
    uint16_t x, y;

    if (dbg_tp_pressed == 0)
    {
        s_draw_prev_valid = 0;      /* 松开: 下一笔重新起笔 */
        return;
    }

    /* 每周期重新双读校验 (两次读数偏差 < ERR_RANGE 才算有效):
       失败说明本次读数抖动/不可靠, 直接跳过本周期, 绝不用上一笔的陈旧坐标,
       避免出现"有时准、有时偏"的跳变 */
    if (TP_Read_XY2(&x, &y) == 0)
    {
        return;
    }

    if (tp_dev.xfac == 0.0f)
    {
        /* 未校准: 原始 AD 线性映射到屏幕坐标。
           注意: 本屏 XPT2046 原始 AD 的 X 方向与屏幕 X 相反 (实测左右镜像),
           因此 X 坐标取反后再映射; Y 方向一致, 直接映射。
           线性映射只是近似, 建议调用 App_TouchCalibrate() 校准后更准。 */
        x = (uint16_t)((uint32_t)(LCD_W - 1U) - (((uint32_t)x * LCD_W) / 4096U));
        y = (uint16_t)(((uint32_t)y * LCD_H) / 4096U);
    }
    else
    {
        /* 已校准: 使用校准后的屏幕坐标 */
        x = (uint16_t)(tp_dev.xfac * x + tp_dev.xoff);
        y = (uint16_t)(tp_dev.yfac * y + tp_dev.yoff);
    }

    /* 边界保护, 防止越界写窗口 */
    if (x >= LCD_W) { x = (uint16_t)(LCD_W - 1); }
    if (y >= LCD_H) { y = (uint16_t)(LCD_H - 1); }

    POINT_COLOR = BLUE;

    if (s_draw_prev_valid)
    {
        /* 连续触摸: 与上一点连线 */
        LCD_DrawLine(s_draw_prev_x, s_draw_prev_y, x, y);
    }
    else
    {
        /* 按下处画一个蓝色点 */
        LCD_DrawPoint(x, y);
    }

    s_draw_prev_x = x;
    s_draw_prev_y = y;
    s_draw_prev_valid = 1;
}


static volatile uint8_t s_paint_fg = 0U;
static volatile uint8_t s_paint_open_req = 0U;

void App_DrawEnterForeground(void)
{
    s_paint_fg = 1U;
}

uint8_t App_DrawIsForeground(void)
{
    return s_paint_fg;
}

/* =========================================================================
 * 画图应用 (原生 LCD 绘制; 独立 FreeRTOS 任务; 摇杆+PA2 鼠标操控)
 * 说明: 本模块封装画图状态与原生 LCD 绘制, freertos.c 只调用任务入口。
 *       - 输入只读 PA2 + 摇杆(不读触摸), 避免触摸误锁导致"画着画着就点不动";
 *       - 画线仿 App_TouchDraw: 按下期间每帧从上一点连到当前点(连续实线);
 *       - 按钮/色块动作在"松开沿"触发一次, 一次点击只生效一次。
 * ========================================================================= */
#define PAINT_CANVAS_Y   118U
#define PAINT_BTN_Y0     4U
#define PAINT_BTN_H      32U
#define PAINT_BTN_W      78U
#define PAINT_BTN_STEP   80U
#define PAINT_SW_BG_Y    42U
#define PAINT_SW_BG_H    30U
#define PAINT_SW_BG_STEP 38U
#define PAINT_BG_NUM     4U
#define PAINT_SW_PN_Y    78U
#define PAINT_SW_PN_H    24U
#define PAINT_SW_PN_STEP 30U
#define PAINT_PN_NUM     6U
#define PAINT_PANEL_BG   0xEF5B
#define PAINT_RING       0xF81F
#define PAINT_CUR_HALF   4U
#define PAINT_JOY_SPEED  4.0f
#define PAINT_X_MASK     0x01FFU
#define PAINT_WIDTH_SHIFT 13U

static const uint16_t s_paint_bg[PAINT_BG_NUM] = { WHITE, BLACK, YELLOW, CYAN };
static const uint16_t s_paint_pn[PAINT_PN_NUM]  = { BLACK, RED, GREEN, BLUE, YELLOW, MAGENTA };
static const char *const s_paint_btn_txt[4] = { "Clear", "Save", "Min", "Exit" };

static DrawData_t s_paint_work;                   /* 画布工作副本 */
static uint16_t   s_paint_pen = BLACK;            /* 当前笔刷 */
static uint8_t    s_paint_eraser = 0U;             /* 1=使用当前背景色擦除 */
static uint8_t    s_paint_width = 1U;              /* 笔画宽度: 1/3/5 px */
static uint8_t    s_tools_hidden = 0U;              /* 1=收起颜色/工具选择区 */
static uint8_t    s_paint_session = 0;            /* 会话有效(最小化保留) */
static uint8_t    s_capacity_warned = 0U;          /* 每次满容量只提示/记录一次 */

static uint16_t s_paint_cur_x = 160;
static uint16_t s_paint_cur_y = 240;
static uint16_t s_paint_drawn_x = 160;
static uint16_t s_paint_drawn_y = 240;
static uint8_t  s_paint_cur_on = 0;

/* ---- 基础绘制 ---- */
static void paint_fill(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t c)
{
    if (x2 < x1) { uint16_t t = x1; x1 = x2; x2 = t; }
    if (y2 < y1) { uint16_t t = y1; y1 = y2; y2 = t; }
    LCD_Fill(x1, y1, x2, y2, c);
}

static void paint_btn(uint16_t x, const char *txt)
{
    uint16_t tw = 0;
    const char *p;

    paint_fill(x, PAINT_BTN_Y0, x + PAINT_BTN_W - 1, PAINT_BTN_Y0 + PAINT_BTN_H - 1, WHITE);
    POINT_COLOR = BLACK;
    LCD_DrawRectangle(x, PAINT_BTN_Y0, x + PAINT_BTN_W - 1, PAINT_BTN_Y0 + PAINT_BTN_H - 1);
    for (p = txt; *p; p++) tw += 8U;
    POINT_COLOR = BLACK;
    LCD_ShowString((uint16_t)(x + (PAINT_BTN_W - tw) / 2U), (uint16_t)(PAINT_BTN_Y0 + 8U), 16,
                   (char *)txt, 1);
}

static void paint_sel_ring(uint16_t x, uint16_t y, uint16_t w)
{
    POINT_COLOR = PAINT_RING;
    LCD_DrawRectangle((uint16_t)(x - 2U), (uint16_t)(y - 2U),
                      (uint16_t)(x + w + 1U), (uint16_t)(y + w + 1U));
    LCD_DrawRectangle((uint16_t)(x - 3U), (uint16_t)(y - 3U),
                      (uint16_t)(x + w + 2U), (uint16_t)(y + w + 2U));
}

static void paint_bg_swatch(uint16_t idx)
{
    uint16_t x = (uint16_t)(44U + idx * PAINT_SW_BG_STEP);
    paint_fill(x, PAINT_SW_BG_Y, x + PAINT_SW_BG_H - 1, PAINT_SW_BG_Y + PAINT_SW_BG_H - 1,
               s_paint_bg[idx]);
    POINT_COLOR = BLACK;
    LCD_DrawRectangle(x, PAINT_SW_BG_Y, x + PAINT_SW_BG_H - 1, PAINT_SW_BG_Y + PAINT_SW_BG_H - 1);
    if (s_paint_work.bg_color == s_paint_bg[idx])
    {
        paint_sel_ring(x, PAINT_SW_BG_Y, PAINT_SW_BG_H);
    }
}

static void paint_pn_swatch(uint16_t idx)
{
    uint16_t x = (uint16_t)(40U + idx * PAINT_SW_PN_STEP);
    paint_fill(x, PAINT_SW_PN_Y, x + PAINT_SW_PN_H - 1, PAINT_SW_PN_Y + PAINT_SW_PN_H - 1,
               s_paint_pn[idx]);
    POINT_COLOR = BLACK;
    LCD_DrawRectangle(x, PAINT_SW_PN_Y, x + PAINT_SW_PN_H - 1, PAINT_SW_PN_Y + PAINT_SW_PN_H - 1);
    if (!s_paint_eraser && s_paint_pen == s_paint_pn[idx])
    {
        paint_sel_ring(x, PAINT_SW_PN_Y, PAINT_SW_PN_H);
    }
}

static void paint_tool_buttons(void)
{
    static const uint8_t widths[3] = {1U, 3U, 5U};
    uint16_t i;

    paint_fill(220, PAINT_SW_PN_Y, 246, PAINT_SW_PN_Y + PAINT_SW_PN_H - 1U, WHITE);
    POINT_COLOR = BLACK;
    LCD_DrawRectangle(220, PAINT_SW_PN_Y, 246, PAINT_SW_PN_Y + PAINT_SW_PN_H - 1U);
    LCD_ShowString(229, PAINT_SW_PN_Y + 4U, 16, "E", 1);
    if (s_paint_eraser) paint_sel_ring(220, PAINT_SW_PN_Y, PAINT_SW_PN_H);

    for (i = 0U; i < 3U; i++) {
        uint16_t x = (uint16_t)(250U + i * 23U);
        paint_fill(x, PAINT_SW_PN_Y, x + 20U, PAINT_SW_PN_Y + PAINT_SW_PN_H - 1U, WHITE);
        POINT_COLOR = BLACK;
        LCD_DrawRectangle(x, PAINT_SW_PN_Y, x + 20U, PAINT_SW_PN_Y + PAINT_SW_PN_H - 1U);
        LCD_ShowNum(x + 6U, PAINT_SW_PN_Y + 4U, widths[i], 1U, 16U);
        if (s_paint_width == widths[i]) paint_sel_ring(x, PAINT_SW_PN_Y, 20U);
    }
}

static void paint_tool_toggle(void)
{
    paint_fill(210, PAINT_SW_BG_Y, 315, PAINT_SW_BG_Y + PAINT_SW_BG_H - 1U, WHITE);
    POINT_COLOR = BLACK;
    LCD_DrawRectangle(210, PAINT_SW_BG_Y, 315, PAINT_SW_BG_Y + PAINT_SW_BG_H - 1U);
    LCD_ShowString(s_tools_hidden ? 227U : 219U, PAINT_SW_BG_Y + 7U, 16,
                   s_tools_hidden ? "Show Tools" : "Hide Tools", 1);
}

static void paint_tool_status(void)
{
    BACK_COLOR = PAINT_PANEL_BG;
    POINT_COLOR = s_paint_eraser ? RED : BLACK;
    LCD_ShowString(4, 104, 12, s_paint_eraser ? "MODE:ERASER" : "MODE:PEN", 0);
    LCD_ShowString(118, 104, 12, "SIZE:", 0);
    LCD_ShowNum(154, 104, s_paint_width, 1U, 12U);
    LCD_ShowString(164, 104, 12, "PX", 0);
}

static uint16_t paint_active_color(void)
{
    return s_paint_eraser ? s_paint_work.bg_color : s_paint_pen;
}

static uint8_t paint_width_code(uint8_t width)
{
    return (width >= 5U) ? 2U : ((width >= 3U) ? 1U : 0U);
}

static uint8_t paint_seg_width(const DrawSeg_t *seg)
{
    uint8_t code = (uint8_t)((seg->x1 >> PAINT_WIDTH_SHIFT) & 0x03U);
    return (code == 2U) ? 5U : ((code == 1U) ? 3U : 1U);
}

static uint16_t paint_seg_x1(const DrawSeg_t *seg)
{
    return (uint16_t)(seg->x1 & PAINT_X_MASK);
}

static void paint_render_line(uint16_t color, uint16_t x1, uint16_t y1,
                              uint16_t x2, uint16_t y2, uint8_t width)
{
    int32_t x = x1, y = y1;
    int32_t dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
    int32_t sx = (x1 < x2) ? 1 : -1;
    int32_t dy = -((y2 > y1) ? (y2 - y1) : (y1 - y2));
    int32_t sy = (y1 < y2) ? 1 : -1;
    int32_t err = dx + dy;
    int32_t r = width / 2U;

    for (;;) {
        int32_t xa = x - r, xb = x + r, ya = y - r, yb = y + r;
        if (xa < 0) xa = 0; if (xb >= LCD_W) xb = LCD_W - 1;
        if (ya < PAINT_CANVAS_Y) ya = PAINT_CANVAS_Y; if (yb >= LCD_H) yb = LCD_H - 1;
        if (ya <= yb) paint_fill((uint16_t)xa, (uint16_t)ya, (uint16_t)xb, (uint16_t)yb, color);
        if (x == x2 && y == y2) break;
        {
            int32_t e2 = 2 * err;
            if (e2 >= dy) { err += dy; x += sx; }
            if (e2 <= dx) { err += dx; y += sy; }
        }
    }
}

static void paint_seg_write(DrawSeg_t *seg, uint16_t color, uint16_t x1, uint16_t y1,
                            uint16_t x2, uint16_t y2, uint8_t width)
{
    seg->color = color;
    seg->x1 = (uint16_t)((x1 & PAINT_X_MASK) |
              ((uint16_t)paint_width_code(width) << PAINT_WIDTH_SHIFT));
    seg->y1 = y1;
    seg->x2 = x2;
    seg->y2 = y2;
}

/* 容量到顶时合并同一笔画中相邻的两段，释放约一半槽位。 */
static void paint_compact_segments(void)
{
    uint16_t rd = 0U, wr = 0U;

    while (rd < s_paint_work.seg_cnt) {
        DrawSeg_t *a = &s_paint_work.segs[rd];
        if (rd + 1U < s_paint_work.seg_cnt) {
            DrawSeg_t *b = &s_paint_work.segs[rd + 1U];
            if (a->color == b->color && paint_seg_width(a) == paint_seg_width(b) &&
                a->x2 == paint_seg_x1(b) && a->y2 == b->y1) {
                paint_seg_write(&s_paint_work.segs[wr++], a->color, paint_seg_x1(a), a->y1,
                                b->x2, b->y2, paint_seg_width(a));
                rd += 2U;
                continue;
            }
        }
        if (wr != rd) s_paint_work.segs[wr] = s_paint_work.segs[rd];
        wr++;
        rd++;
    }
    s_paint_work.seg_cnt = wr;
    if (wr < DRAW_SEG_MAX) s_capacity_warned = 0U;
}

/* 面板局部恢复: 只重画与矩形相交的按钮/色块/标签 */
static void paint_panel_restore(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    int i;
    uint8_t bg_lbl = 0;
    uint8_t pn_lbl = 0;

    paint_fill(x0, y0, x1, y1, PAINT_PANEL_BG);
    for (i = 0; i < 4; i++)
    {
        uint16_t bx = (uint16_t)(1 + i * PAINT_BTN_STEP);
        if (x1 >= bx && x0 <= (uint16_t)(bx + PAINT_BTN_W - 1U) &&
            y1 >= PAINT_BTN_Y0 && y0 <= (uint16_t)(PAINT_BTN_Y0 + PAINT_BTN_H - 1U))
        {
            paint_btn(bx, s_paint_btn_txt[i]);
        }
    }
    if (!s_tools_hidden) for (i = 0; i < (int)PAINT_BG_NUM; i++)
    {
        uint16_t sx = (uint16_t)(44 + i * PAINT_SW_BG_STEP);
        if (x1 >= (uint16_t)(sx - 3) && x0 <= (uint16_t)(sx + PAINT_SW_BG_H + 2U) &&
            y1 >= (uint16_t)(PAINT_SW_BG_Y - 3) && y0 <= (uint16_t)(PAINT_SW_BG_Y + PAINT_SW_BG_H + 2U))
        {
            paint_bg_swatch((uint16_t)i);
        }
    }
    if (!s_tools_hidden) for (i = 0; i < (int)PAINT_PN_NUM; i++)
    {
        uint16_t sx = (uint16_t)(40 + i * PAINT_SW_PN_STEP);
        if (x1 >= (uint16_t)(sx - 3) && x0 <= (uint16_t)(sx + PAINT_SW_PN_H + 2U) &&
            y1 >= (uint16_t)(PAINT_SW_PN_Y - 3) && y0 <= (uint16_t)(PAINT_SW_PN_Y + PAINT_SW_PN_H + 2U))
        {
            paint_pn_swatch((uint16_t)i);
        }
    }
    if (!s_tools_hidden && y1 >= PAINT_SW_PN_Y && y0 <= (uint16_t)(PAINT_SW_PN_Y + PAINT_SW_PN_H + 3U) && x1 >= 217U)
        paint_tool_buttons();
    if (x1 >= 4 && x0 <= 40 && y1 >= (uint16_t)(PAINT_SW_BG_Y + 4) &&
        y0 <= (uint16_t)(PAINT_SW_BG_Y + 20)) bg_lbl = 1;
    if (x1 >= 4 && x0 <= 40 && y1 >= (uint16_t)(PAINT_SW_PN_Y + 2) &&
        y0 <= (uint16_t)(PAINT_SW_PN_Y + 18)) pn_lbl = 1;
    BACK_COLOR = PAINT_PANEL_BG;
    POINT_COLOR = BLACK;
    if (!s_tools_hidden && bg_lbl) LCD_ShowString(4, PAINT_SW_BG_Y + 6, 16, "BG:", 0);
    if (!s_tools_hidden && pn_lbl) LCD_ShowString(4, PAINT_SW_PN_Y + 4, 16, "PN:", 0);
    paint_tool_toggle();
    if (!s_tools_hidden) paint_tool_status();
}

static void paint_ui_panel(void)
{
    uint16_t i;

    paint_fill(0, 0, 319, PAINT_CANVAS_Y - 1, PAINT_PANEL_BG);
    for (i = 0; i < 4; i++)
    {
        paint_btn((uint16_t)(1 + i * PAINT_BTN_STEP), s_paint_btn_txt[i]);
    }
    BACK_COLOR = PAINT_PANEL_BG;
    POINT_COLOR = BLACK;
    if (!s_tools_hidden) {
        LCD_ShowString(4, PAINT_SW_BG_Y + 6, 16, "BG:", 0);
        for (i = 0; i < PAINT_BG_NUM; i++) paint_bg_swatch(i);
        POINT_COLOR = BLACK;
        LCD_ShowString(4, PAINT_SW_PN_Y + 4, 16, "PN:", 0);
        for (i = 0; i < PAINT_PN_NUM; i++) paint_pn_swatch(i);
        paint_tool_buttons();
        paint_tool_status();
    }
    paint_tool_toggle();
}

/**
  * @brief  画图持久化容量达到上限时给出可见且可追踪的反馈
  */
static void paint_capacity_warn(void)
{
    if (s_capacity_warned) {
        return;
    }
    s_capacity_warned = 1U;
    /* 极端情况下无法继续压缩时只记录诊断，不在画布上留下 FULL 字样。 */
    App_Log_Error("画图线段达到上限 max=%u", (unsigned int)DRAW_SEG_MAX);
}

static void paint_canvas(void)
{
    uint16_t i;

    paint_fill(0, PAINT_CANVAS_Y, 319, 479, s_paint_work.bg_color);
    for (i = 0; i < s_paint_work.seg_cnt; i++)
    {
        DrawSeg_t *seg = &s_paint_work.segs[i];
        paint_render_line(seg->color, paint_seg_x1(seg), seg->y1,
                          seg->x2, seg->y2, paint_seg_width(seg));
    }
}

static void paint_redraw_all(void)
{
    paint_ui_panel();
    paint_canvas();
}

/* ---- 光标 (仅用于非笔画状态瞄准) ---- */
static void paint_restore_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint16_t i;
    uint16_t cy0 = (y0 > PAINT_CANVAS_Y) ? y0 : PAINT_CANVAS_Y;

    if (cy0 > y1) return;
    paint_fill(x0, cy0, x1, y1, s_paint_work.bg_color);
    for (i = 0; i < s_paint_work.seg_cnt; i++)
    {
        DrawSeg_t *s = &s_paint_work.segs[i];
        uint16_t sx1 = paint_seg_x1(s);
        uint16_t min_x = (sx1 < s->x2) ? sx1 : s->x2;
        uint16_t max_x = (sx1 > s->x2) ? sx1 : s->x2;
        uint16_t min_y = (s->y1 < s->y2) ? s->y1 : s->y2;
        uint16_t max_y = (s->y1 > s->y2) ? s->y1 : s->y2;
        uint16_t margin = paint_seg_width(s) / 2U;
        if (min_x <= x1 + margin && max_x + margin >= x0 &&
            min_y <= y1 + margin && max_y + margin >= cy0)
        {
            paint_render_line(s->color, sx1, s->y1, s->x2, s->y2, paint_seg_width(s));
        }
    }
}

static void paint_cursor_erase(void)
{
    uint16_t x0, y0, x1, y1;

    if (!s_paint_cur_on) return;
    x0 = (s_paint_drawn_x > PAINT_CUR_HALF + 1U)
         ? (uint16_t)(s_paint_drawn_x - PAINT_CUR_HALF - 1U) : 0U;
    y0 = (s_paint_drawn_y > PAINT_CUR_HALF + 1U)
         ? (uint16_t)(s_paint_drawn_y - PAINT_CUR_HALF - 1U) : 0U;
    x1 = (uint16_t)(s_paint_drawn_x + PAINT_CUR_HALF + 1U);
    if (x1 >= LCD_W) x1 = (uint16_t)(LCD_W - 1U);
    y1 = (uint16_t)(s_paint_drawn_y + PAINT_CUR_HALF + 1U);
    if (y1 >= LCD_H) y1 = (uint16_t)(LCD_H - 1U);

    if (y0 < PAINT_CANVAS_Y)
    {
        uint16_t py1 = (y1 < PAINT_CANVAS_Y) ? y1 : (uint16_t)(PAINT_CANVAS_Y - 1U);
        paint_panel_restore(x0, y0, x1, py1);
        y0 = PAINT_CANVAS_Y;
    }
    if (y0 <= y1) paint_restore_rect(x0, y0, x1, y1);
    s_paint_cur_on = 0;
}

static void paint_cursor_draw(void)
{
    uint16_t x = s_paint_cur_x;
    uint16_t y = s_paint_cur_y;

    if (x < PAINT_CUR_HALF) x = PAINT_CUR_HALF;
    if (x > (uint16_t)(LCD_W - 1U - PAINT_CUR_HALF)) x = (uint16_t)(LCD_W - 1U - PAINT_CUR_HALF);
    if (y < PAINT_CUR_HALF) y = PAINT_CUR_HALF;
    if (y > (uint16_t)(LCD_H - 1U - PAINT_CUR_HALF)) y = (uint16_t)(LCD_H - 1U - PAINT_CUR_HALF);
    s_paint_cur_x = x;
    s_paint_cur_y = y;

    paint_fill((uint16_t)(x - PAINT_CUR_HALF), (uint16_t)(y - PAINT_CUR_HALF),
               (uint16_t)(x + PAINT_CUR_HALF), (uint16_t)(y + PAINT_CUR_HALF), BLACK);
    paint_fill((uint16_t)(x - 1U), (uint16_t)(y - 1U),
               (uint16_t)(x + 1U), (uint16_t)(y + 1U), WHITE);
    s_paint_drawn_x = x;
    s_paint_drawn_y = y;
    s_paint_cur_on = 1;
}

/* ---- 命中测试 / 动作 ---- */
static int paint_hit(int x, int y)
{
    int i;

    if (y >= PAINT_BTN_Y0 && y < (int)(PAINT_BTN_Y0 + PAINT_BTN_H))
    {
        for (i = 0; i < 4; i++)
        {
            int bx = (int)(1 + i * PAINT_BTN_STEP);
            if (x >= bx && x < bx + (int)PAINT_BTN_W) return 1 + i;
        }
    }
    if (y >= PAINT_SW_BG_Y && y < (int)(PAINT_SW_BG_Y + PAINT_SW_BG_H) &&
        x >= 210 && x <= 315) return 31;
    if (!s_tools_hidden && y >= PAINT_SW_BG_Y && y < (int)(PAINT_SW_BG_Y + PAINT_SW_BG_H))
    {
        for (i = 0; i < (int)PAINT_BG_NUM; i++)
        {
            int sx = (int)(44 + i * PAINT_SW_BG_STEP);
            if (x >= sx && x < sx + (int)PAINT_SW_BG_H) return 10 + i;
        }
    }
    if (!s_tools_hidden && y >= PAINT_SW_PN_Y && y < (int)(PAINT_SW_PN_Y + PAINT_SW_PN_H))
    {
        for (i = 0; i < (int)PAINT_PN_NUM; i++)
        {
            int sx = (int)(40 + i * PAINT_SW_PN_STEP);
            if (x >= sx && x < sx + (int)PAINT_SW_PN_H) return 20 + i;
        }
        if (x >= 217 && x <= 247) return 30;
        if (x >= 248 && x < 320) return 40 + (x - 248) / 23;
    }
    if (y >= (int)PAINT_CANVAS_Y) return 100;
    return 0;
}

static void paint_clear(void)
{
    s_paint_work.seg_cnt = 0;
    s_capacity_warned = 0U;
    paint_canvas();
}

static void paint_save(void)
{
    paint_fill(100, PAINT_CANVAS_Y + 4, 220, PAINT_CANVAS_Y + 24, WHITE);
    POINT_COLOR = BLACK;
    LCD_DrawRectangle(100, PAINT_CANVAS_Y + 4, 220, PAINT_CANVAS_Y + 24);
    LCD_ShowString(124, PAINT_CANVAS_Y + 8, 16, "SAVED", 1);
    UserStore_SaveDrawing(&s_paint_work);
    delay_ms(500);
    paint_canvas();
}

static void paint_set_bg(uint8_t idx)
{
    if (idx >= PAINT_BG_NUM) return;
    s_paint_work.bg_color = s_paint_bg[idx];
    paint_canvas();
    paint_ui_panel();
}

/* 摇杆移动光标 (画图输入只用摇杆+PA2, 不读触摸, 防止触摸误锁) */
static void paint_joy_move(void)
{
    float joy_x = 0.0f;
    float joy_y = 0.0f;

    App_MouseGetVector(&joy_x, &joy_y);
    if (joy_x != 0.0f || joy_y != 0.0f)
    {
        int dx = (int)(joy_x * PAINT_JOY_SPEED);
        int dy = (int)(joy_y * PAINT_JOY_SPEED);
        if (dx != 0)
        {
            int nx = (int)s_paint_cur_x + dx;
            s_paint_cur_x = (nx < 0) ? 0U
                           : (uint16_t)((nx > (int)(LCD_W - 1U)) ? (int)(LCD_W - 1U) : nx);
        }
        if (dy != 0)
        {
            int ny = (int)s_paint_cur_y + dy;
            s_paint_cur_y = (ny < 0) ? 0U
                           : (uint16_t)((ny > (int)(LCD_H - 1U)) ? (int)(LCD_H - 1U) : ny);
        }
    }
}

/**
  * @brief  读取一次画图触摸坐标
  * @note   使用物理坐标扫描，避免未校准时 TP_Scan(0) 将坐标乘以零系数。
  */
static uint8_t paint_touch_read(uint16_t *screen_x, uint16_t *screen_y)
{
    static uint16_t last_x = 0U, last_y = 0U;
    static uint16_t candidate_x = 0U, candidate_y = 0U;
    static uint8_t confirmed = 0U;
    static uint8_t candidate_count = 0U;
    static uint8_t miss_count = 0U;
    uint16_t raw_x;
    uint16_t raw_y;
    int32_t x;
    int32_t y;

    if (screen_x == NULL || screen_y == NULL) return 0U;
    if (TP_Scan(1) == 0U || TP_Read_XY2(&raw_x, &raw_y) == 0U) {
        /* 单次采样丢失不立刻抬笔，连续 4 次失败才确认释放。 */
        if (confirmed && miss_count++ < 3U) {
            *screen_x = last_x; *screen_y = last_y;
            return 1U;
        }
        confirmed = 0U; candidate_count = 0U; miss_count = 0U;
        return 0U;
    }

    /* 松开瞬间或总线受扰可能返回接近 0/4095 的满量程值；直接丢弃，
       不能钳位到屏幕边角，否则会被误识别为右下角点击。 */
    if (raw_x < 80U || raw_x > 4015U || raw_y < 80U || raw_y > 4015U) {
        if (confirmed && miss_count++ < 3U) {
            *screen_x = last_x; *screen_y = last_y;
            return 1U;
        }
        confirmed = 0U; candidate_count = 0U; miss_count = 0U;
        return 0U;
    }

    if (tp_dev.xfac == 0.0f || tp_dev.yfac == 0.0f) {
        x = (int32_t)(LCD_W - 1U) - (int32_t)(((uint32_t)raw_x * LCD_W) / 4096U);
        y = (int32_t)(((uint32_t)raw_y * LCD_H) / 4096U);
    }
    else {
        x = (int32_t)(tp_dev.xfac * raw_x + tp_dev.xoff);
        y = (int32_t)(tp_dev.yfac * raw_y + tp_dev.yoff);
    }
    if (x < -12 || y < -12 || x > (int32_t)LCD_W + 11 || y > (int32_t)LCD_H + 11) return 0U;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (int32_t)LCD_W) x = (int32_t)LCD_W - 1;
    if (y >= (int32_t)LCD_H) y = (int32_t)LCD_H - 1;
    /* 新按压需两个相邻有效样本确认；笔画中拒绝单帧超过 70px 的瞬移。 */
    if (!confirmed) {
        uint16_t nx = (uint16_t)x, ny = (uint16_t)y;
        uint16_t dx = (nx > candidate_x) ? (nx - candidate_x) : (candidate_x - nx);
        uint16_t dy = (ny > candidate_y) ? (ny - candidate_y) : (candidate_y - ny);
        if (candidate_count == 0U || dx > 35U || dy > 35U) {
            candidate_x = nx; candidate_y = ny; candidate_count = 1U;
            return 0U;
        }
        confirmed = 1U;
    }
    else {
        uint16_t dx = ((uint16_t)x > last_x) ? ((uint16_t)x - last_x) : (last_x - (uint16_t)x);
        uint16_t dy = ((uint16_t)y > last_y) ? ((uint16_t)y - last_y) : (last_y - (uint16_t)y);
        if (dx > 70U || dy > 70U) {
            if (miss_count++ < 3U) { *screen_x = last_x; *screen_y = last_y; return 1U; }
            confirmed = 0U; candidate_count = 0U; miss_count = 0U;
            return 0U;
        }
    }
    miss_count = 0U;
    last_x = (uint16_t)x; last_y = (uint16_t)y;
    *screen_x = last_x;
    *screen_y = last_y;
    return 1U;
}

/* 前台运行: 按下画布=起笔并连续画线; 松开画布=抬笔; 按钮/色块=松开沿单击 */
static void paint_run(void)
{
    uint16_t pen_x = 0, pen_y = 0;      /* 笔画上一屏幕点 */
    uint16_t pers_x = 0, pers_y = 0;    /* 持久化节流锚点 */
    uint8_t stroking = 0;
    uint8_t btn_prev = 0;
    uint8_t touch_stroking = 0U;
    uint8_t touch_panel_pressed = 0U;
    uint16_t touch_panel_x = 0U, touch_panel_y = 0U;
    uint8_t quit = 0;
    uint32_t stroke_last = 0;
    uint32_t t0;

    paint_redraw_all();

    s_paint_cur_x = (g_mouse_x > 0) ? (uint16_t)g_mouse_x : 0U;
    s_paint_cur_y = (g_mouse_y > 0) ? (uint16_t)g_mouse_y : 0U;
    if (s_paint_cur_x >= LCD_W) s_paint_cur_x = (uint16_t)(LCD_W - 1U);
    if (s_paint_cur_y >= LCD_H) s_paint_cur_y = (uint16_t)(LCD_H - 1U);
    s_paint_cur_on = 0;
    s_paint_drawn_x = s_paint_cur_x;
    s_paint_drawn_y = s_paint_cur_y;

    /* 等待打开应用的那一下按键松开 */
    t0 = HAL_GetTick();
    while (App_MouseBtnDown() && ((HAL_GetTick() - t0) < 500U)) osDelay(5);
    btn_prev = App_MouseBtnDown();

    while (!quit)
    {
        App_HealthBeat(APP_HEALTH_DRAW);
        uint8_t down;
        uint8_t moved;
        uint8_t action_redraw = 0;
        uint16_t touch_x = 0U;
        uint16_t touch_y = 0U;
        uint8_t touch_down = paint_touch_read(&touch_x, &touch_y);

        /* 原生画布前台独立接管触摸；画布内按住并移动即可连续作画。 */
        if (touch_down && touch_y >= PAINT_CANVAS_Y)
        {
            paint_cursor_erase();
            s_paint_cur_x = touch_x;
            s_paint_cur_y = touch_y;
            if (touch_stroking == 0U)
            {
                if (s_paint_work.seg_cnt >= DRAW_SEG_MAX) paint_compact_segments();
                if (s_paint_work.seg_cnt < DRAW_SEG_MAX) {
                    DrawSeg_t *sg = &s_paint_work.segs[s_paint_work.seg_cnt++];
                    paint_seg_write(sg, paint_active_color(), touch_x, touch_y,
                                    touch_x, touch_y, s_paint_width);
                }
                else paint_capacity_warn();
                paint_render_line(paint_active_color(), touch_x, touch_y,
                                  touch_x, touch_y, s_paint_width);
                pen_x = touch_x; pen_y = touch_y;
                pers_x = touch_x; pers_y = touch_y;
                touch_stroking = 1U;
            }
            else if (touch_x != pen_x || touch_y != pen_y)
            {
                uint16_t ddx = (touch_x > pers_x) ? (touch_x - pers_x) : (pers_x - touch_x);
                uint16_t ddy = (touch_y > pers_y) ? (touch_y - pers_y) : (pers_y - touch_y);
                paint_render_line(paint_active_color(), pen_x, pen_y,
                                  touch_x, touch_y, s_paint_width);
                pen_x = touch_x; pen_y = touch_y;
                if (ddx >= 8U || ddy >= 8U) {
                    if (s_paint_work.seg_cnt >= DRAW_SEG_MAX) paint_compact_segments();
                    if (s_paint_work.seg_cnt < DRAW_SEG_MAX) {
                        DrawSeg_t *sg = &s_paint_work.segs[s_paint_work.seg_cnt++];
                        paint_seg_write(sg, paint_active_color(), pers_x, pers_y,
                                        touch_x, touch_y, s_paint_width);
                        pers_x = touch_x; pers_y = touch_y;
                    }
                    else paint_capacity_warn();
                }
            }
            /* 画图触摸只控制画笔，不改写桌面摇杆鼠标坐标。 */
            btn_prev = App_MouseBtnDown() ? 1U : 0U;
            osDelay(5);
            continue;
        }
        if (touch_down && touch_y < PAINT_CANVAS_Y)
        {
            paint_cursor_erase();
            touch_panel_pressed = 1U;
            touch_panel_x = touch_x;
            touch_panel_y = touch_y;
            osDelay(5);
            continue;
        }
        if (!touch_down && touch_panel_pressed)
        {
            int act = paint_hit((int)touch_panel_x, (int)touch_panel_y);
            touch_panel_pressed = 0U;
            if (act >= 1 && act <= 4) {
                if (act == 1) paint_clear();
                else if (act == 2) paint_save();
                else if (act == 3) { s_paint_session = 1; quit = 1; }
                else { s_paint_session = 0; quit = 1; }
            }
            else if (act >= 10 && act < 10 + (int)PAINT_BG_NUM) paint_set_bg((uint8_t)(act - 10));
            else if (act >= 20 && act < 20 + (int)PAINT_PN_NUM) {
                s_paint_pen = s_paint_pn[act - 20]; s_paint_eraser = 0U; paint_ui_panel();
            }
            else if (act == 30) { s_paint_eraser = 1U; paint_ui_panel(); }
            else if (act == 31) { s_tools_hidden = (uint8_t)!s_tools_hidden; paint_ui_panel(); }
            else if (act >= 40 && act <= 42) {
                static const uint8_t widths[3] = {1U, 3U, 5U};
                s_paint_width = widths[act - 40]; paint_ui_panel();
            }
            action_redraw = 1U;
        }
        if (touch_stroking != 0U) {
            touch_stroking = 0U;
            s_paint_cur_on = 0U;
        }

        paint_joy_move();
        down = App_MouseBtnDown() ? 1U : 0U;
        moved = (s_paint_cur_x != s_paint_drawn_x) || (s_paint_cur_y != s_paint_drawn_y);

        if (stroking)
        {
            /* 笔画中: 光标隐藏, 每帧从上一点连到当前点 (连续实线) */
            if (moved)
            {
                uint16_t x = s_paint_cur_x, y = s_paint_cur_y;
                if (y < PAINT_CANVAS_Y) y = PAINT_CANVAS_Y;
                if (x >= LCD_W) x = (uint16_t)(LCD_W - 1U);
                paint_render_line(paint_active_color(), pen_x, pen_y, x, y, s_paint_width);
                pen_x = x;
                pen_y = y;
                stroke_last = HAL_GetTick();
                {
                    uint16_t ddx = (x > pers_x) ? (x - pers_x) : (pers_x - x);
                    uint16_t ddy = (y > pers_y) ? (y - pers_y) : (pers_y - y);
                    if ((ddx >= 8U || ddy >= 8U) && s_paint_work.seg_cnt >= DRAW_SEG_MAX)
                        paint_compact_segments();
                    if ((ddx >= 8U || ddy >= 8U) && s_paint_work.seg_cnt < DRAW_SEG_MAX)
                    {
                        DrawSeg_t *sg = &s_paint_work.segs[s_paint_work.seg_cnt];
                        paint_seg_write(sg, paint_active_color(), pers_x, pers_y,
                                        x, y, s_paint_width);
                        s_paint_work.seg_cnt++;
                        pers_x = x;
                        pers_y = y;
                    }
                    else if ((ddx >= 8U || ddy >= 8U) && s_paint_work.seg_cnt >= DRAW_SEG_MAX)
                    {
                        paint_capacity_warn();
                    }
                }
            }
            if (!down || ((HAL_GetTick() - stroke_last) > 3000U))
            {
                stroking = 0;       /* 抬笔或看门狗超时: 结束笔画, 恢复光标 */
            }
        }
        else
        {
            if (moved) paint_cursor_erase();

            if (down && !btn_prev)          /* 按下沿 */
            {
                if (paint_hit((int)s_paint_cur_x, (int)s_paint_cur_y) == 100)
                {
                    uint16_t x = s_paint_cur_x, y = s_paint_cur_y;
                    if (y < PAINT_CANVAS_Y) y = PAINT_CANVAS_Y;
                    if (x >= LCD_W) x = (uint16_t)(LCD_W - 1U);
                    paint_cursor_erase();   /* 隐藏方块光标, 进入连续画线 */
                    if (s_paint_work.seg_cnt >= DRAW_SEG_MAX) paint_compact_segments();
                    if (s_paint_work.seg_cnt < DRAW_SEG_MAX)
                    {
                        DrawSeg_t *sg = &s_paint_work.segs[s_paint_work.seg_cnt];
                        paint_seg_write(sg, paint_active_color(), x, y, x, y, s_paint_width);
                        s_paint_work.seg_cnt++;
                    }
                    else
                    {
                        paint_capacity_warn();
                    }
                    paint_render_line(paint_active_color(), x, y, x, y, s_paint_width);
                    pen_x = x; pen_y = y;
                    pers_x = x; pers_y = y;
                    stroking = 1;
                    stroke_last = HAL_GetTick();
                }
            }
            else if (!down && btn_prev)     /* 松开沿: 一次单击一次动作 */
            {
                int act = paint_hit((int)s_paint_cur_x, (int)s_paint_cur_y);
                if (act >= 1 && act <= 4)
                {
                    if (act == 1)      { paint_clear(); action_redraw = 1; }
                    else if (act == 2) { paint_save(); action_redraw = 1; }
                    else if (act == 3) { s_paint_session = 1; quit = 1; }   /* 最小化 */
                    else               { s_paint_session = 0; quit = 1; }   /* 退出 */
                }
                else if (act >= 10 && act < 10 + (int)PAINT_BG_NUM)
                {
                    paint_set_bg((uint8_t)(act - 10));
                    action_redraw = 1;
                }
                else if (act >= 20 && act < 20 + (int)PAINT_PN_NUM)
                {
                    s_paint_pen = s_paint_pn[act - 20];
                    s_paint_eraser = 0U;
                    paint_ui_panel();
                    action_redraw = 1;
                }
                else if (act == 30)
                {
                    s_paint_eraser = 1U;
                    paint_ui_panel();
                    action_redraw = 1;
                }
                else if (act == 31)
                {
                    s_tools_hidden = (uint8_t)!s_tools_hidden;
                    paint_ui_panel();
                    action_redraw = 1;
                }
                else if (act >= 40 && act <= 42)
                {
                    static const uint8_t widths[3] = {1U, 3U, 5U};
                    s_paint_width = widths[act - 40];
                    paint_ui_panel();
                    action_redraw = 1;
                }
            }
        }

        g_mouse_x = (int)s_paint_cur_x;     /* 同步桌面鼠标位置 */
        g_mouse_y = (int)s_paint_cur_y;

        if (!quit && !stroking && (moved || action_redraw || !s_paint_cur_on))
        {
            paint_cursor_draw();
        }
        btn_prev = down;
        osDelay(5);
    }

    /* 退出/最小化: 等按键松开再交还, 防桌面重复触发 */
    t0 = HAL_GetTick();
    while (App_MouseBtnDown() && ((HAL_GetTick() - t0) < 1000U)) {
        App_HealthBeat(APP_HEALTH_DRAW);
        osDelay(5);
    }
    paint_cursor_erase();
    g_mouse_x = (int)s_paint_cur_x;
    g_mouse_y = (int)s_paint_cur_y;
}

/**
  * @brief  画图应用 FreeRTOS 任务 (freertos.c 直接调用本函数)
  */
void App_DrawTask(void *argument)
{
    (void)argument;
    for (;;)
    {
        App_HealthBeat(APP_HEALTH_DRAW);
        while (!s_paint_fg) {
            App_HealthBeat(APP_HEALTH_DRAW);
            osDelay(10);
        }

        /* 新会话(开机/退出后)载入已保存绘图; 最小化恢复沿用 RAM 画布 */
        if (!s_paint_session)
        {
            s_paint_work = *UserStore_GetDrawing();
            if (s_paint_work.seg_cnt > DRAW_SEG_MAX) s_paint_work.seg_cnt = DRAW_SEG_MAX;
            s_capacity_warned = (s_paint_work.seg_cnt >= DRAW_SEG_MAX) ? 1U : 0U;
            s_paint_session = 1;
        }
        paint_run();
        s_paint_fg = 0;
        osDelay(20);
    }
}

/**
  * @brief  桌面"画图"按钮请求打开应用 (由 custom.c 事件调用)
  */
void App_RequestDrawOpen(void)
{
    s_paint_open_req = 1;
}

/**
  * @brief  消费打开请求 (LVGL 任务轮询)
  */
uint8_t App_ConsumeDrawOpenRequest(void)
{
    if (s_paint_open_req)
    {
        s_paint_open_req = 0;
        return 1;
    }
    return 0;
}
