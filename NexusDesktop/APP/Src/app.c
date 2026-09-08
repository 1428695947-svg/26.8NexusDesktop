/**
  ******************************************************************************
  * @file    app.c
  * @brief   应用层模块 - 模块初始化装配与按键回调
  * @author  Embedded Expert
  * @version V1.0
  * @date    2026-08-22
  ******************************************************************************
  * @attention
  * 1. 应用层只负责装配：按键/摇杆/定时器各驱动模块互不依赖
  * 2. 按键回调（按下/单击/双击/释放）运行在TIM5中断上下文，
  *    通过 FreeRTOS 队列（xQueueSendFromISR）投递 KeyEventMsg_t 给任务处理
  * 3. 集中在此处初始化，避免CubeMX重新生成main.c时中文注释乱码
  ******************************************************************************
  */

#include "app.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "touch.h"
#include "lcd.h"
#include "gui.h"
#include "cal_store.h"
#include "delay.h"
#include "flash_store.h"
#include "user_store.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "gui_guider.h"
#include "custom.h"
#include "app_log.h"
#include "app_mouse.h"
#include "app_power.h"
#include "app_draw.h"
#include "app_touch.h"
#include "app_boot.h"
#include "app_storage.h"
#include "app_health.h"



/* ========================= 公共函数实现 ========================= */

/**
  * @brief  应用层初始化（在main中调用）
  * @retval None
  * @note   完成各驱动模块初始化、按键回调注册与TIM5中断启动
  */
void App_Init(void)
{
    App_StorageInit();
    /* 驱动模块初始化 */
    App_MouseInit();
    App_PowerInit();
    App_Log_Init();             /* 创建日志队列 (调度器启动前, 供各任务/UI 记录事件) */
    App_HealthInit();           /* 初始化任务心跳基线, 调度器启动后由健康任务检查 */

    /* ==================== ILI9488 LCD + XPT2046 触摸初始化 ====================
     * 说明: 原实现写在 main.c 的 USER CODE 区, CubeMX 重新生成 main.c 时中文
     *       注释容易乱码, 故集中放在应用层 App_Init() 中 (app.c 不会被重新生成)。
     * 1. delay_init(): 初始化 DWT 周期计数器 (delay_us 依赖)
     * 2. LCD_Init()  : SPI1(10.5MHz) + LCD GPIO + ILI9488 初始化序列
     * 3. 显示红色背景 + 字符串, 用于物理观察与 Debug 验证
     * 4. 触摸按需启动; 无存储校准时自动校准一次 (9 点, 结果写 Flash)
     * ======================================================================== */
    delay_init();
    LCD_Init();
    LCD_Clear(RED);
   POINT_COLOR = BLUE;
   BACK_COLOR = BLACK;

   UserStore_Init();               /* 提前载入校准/密码/设置 (供校准判定与开机生效使用) */

   TP_Enable();                /* 按需启动触摸 (首次自动 TP_Init) */
   /* 不再开机阻塞校准: 已校准(含旧版迁移)直接使用; 未校准先从设置页"触摸校准"手动校准,
      期间触摸按未校准的线性映射近似使用, 摇杆鼠标不受影响。 */

    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    /* ==================== GUI Guider 界面初始化 ====================
     * 加载登录/桌面界面、密码 Flash 存储、小猫光标。
     * 之后的输入由摇杆/触摸/PA2 控制鼠标, 见 App_MouseUpdate()。
     */
    App_GuiInit();
}

/**
  * @brief  1ms节拍处理（在TIM5中断回调中调用）
  * @retval None
  */
void App_Tick1ms(void)
{
    /* PA2 由 key.c (V2.0) 在 TIM5 中断内完成消抖/单击/双击判定并回调入队,
     * 这里不再直接读取 GPIO, 只驱动按键扫描与 LVGL 时钟。 */
    App_InputTick1ms();
    lv_tick_inc(1);
}


/**
  * @brief  图形化界面刷新
  * @note   NONE
  */
void App_LvglTask(void)
{
    uint8_t was_draw = 0;       /* 1=刚从画图应用切回, 需要重绘桌面 */

    for(;;)
    {
        App_HealthBeat(APP_HEALTH_LVGL);
        /* 画图应用前台: 暂停 LVGL (不刷新不处理输入), 避免覆盖画布/状态互踩 */
        if (App_DrawIsForeground())
        {
            was_draw = 1;
            osDelay(10);        /* 10ms */
            continue;
        }

        /* 画图退出/最小化后切回: 强制 LVGL 重绘当前界面 (画图直接写过 LCD) */
        if (was_draw)
        {
            was_draw = 0;
            /* 复位鼠标输入状态: 画图里那一下"按下-松开"不能在桌面重复触发 */
            App_InputFlush();
            lv_indev_reset(NULL, NULL);       /* 清 LVGL 输入设备内部按下状态 */
            if (lv_scr_act() != NULL)
            {
                lv_obj_invalidate(lv_scr_act());
            }
        }

        App_MouseUpdate();      /* 根据摇杆/触摸/PA2 更新鼠标坐标与按下状态 */
        App_ProcessInputEvents();/* 消费输入事件队列, 驱动 LVGL 点击 (含长按重复) */
        lv_task_handler();

        /* 桌面入口: 请求打开画图应用 -> 置前台, 由画图任务接管屏幕 */
        if (App_ConsumeDrawOpenRequest())
        {
            App_DrawEnterForeground();
            continue;           /* 下一轮进入暂停分支 */
        }

        /* 处理设置页发起的"写回 Flash"请求 (滑条松开即保存, 掉电后保持) */
        if (gui_save_requested()) {
            gui_save_settings();
        }
        /* 处理设置页发起的触摸校准请求 (在 LVGL 任务循环里执行, 避免事件回调内长阻塞) */
        if (gui_cal_requested()) {
            gui_cal_request_clear();    /* 先清除, 防止校准(含超时)退出后反复进入 */
            App_TouchCalibrate();
        }
        App_PowerPoll();         /* 自动熄屏状态机 */
        osDelay(5);  /* 200Hz */
    }
}

/**
  * @brief  GUI 界面初始化 (应用层装配)
  * @note   载入掉电保存的密码, 创建登录/桌面界面与小猫光标。
  *         必须在 lv_init/lv_port_disp_init/lv_port_indev_init 之后调用。
  */
void App_GuiInit(void)
{
    FlashStore_Init();          /* 载入(或首次写入默认)密码到 Flash */
    setup_ui(&guider_ui);       /* GUI Guider 生成的界面装配 */
    custom_init(&guider_ui);    /* 自定义回调: 显示密码/登录/小猫光标 */
    gui_cursor_set_pos(g_mouse_x, g_mouse_y);
    App_ApplySettings();        /* 应用开机保存的系统设置 */
    App_BootStart();            /* 启动页结束后自动进入登录界面 */
}


/**
  * @brief  应用开机/修改后的系统设置
  * @note   灵敏度 -> 摇杆鼠标速度; 光标大小 -> LVGL 缩放; 亮度 -> 顶层遮罩;
  *         熄屏时间 -> 空闲计时阈值。
  */
void App_ApplySettings(void)
{
    const SysSettings_t *st = UserStore_GetSettings();

    if (st == NULL) {
        return;
    }
    App_SetMouseSpeed(1.5f * (float)st->sens);   /* 默认灵敏度5 -> 7.5，提升跨屏移动速度 */
    App_SetSleepSec(st->sleep_sec);
    gui_cursor_set_zoom(st->cursor_zoom);
    gui_set_brightness(st->brightness);
    App_PowerRecordActivity();
}



