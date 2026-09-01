/**
  ******************************************************************************
  * @file    joystick.c
  * @brief   PS2双轴摇杆模块驱动（STM32F407VET6 + Keil MDK + HAL库）
  * @author  Embedded Expert
  * @version V1.3
  * @date    2026-08-22
  ******************************************************************************
  * @attention
  * 1. 依赖 CubeMX 生成的 hadc1（ADC1/IN0/IN1），PA0/PA1 需配置为 ADC 模拟输入
  * 2. 摇杆Z轴按键由 key 模块处理（PA2 输入上拉），本模块不涉及按键扫描
  * 3. F407 的 ADC 无硬件校准功能，HAL_ADCEx_Calibration_Start 是 F1/F3/L4 系列接口，
  *    因此本模块只做摇杆物理中心的软件校准
  * 4. 模块建议 3.3V 供电：5V 供电会超出 ADC 量程（读数饱和 4095）并超过引脚额定值
  *
  * 【公有接口】（仅 3 个，供应用层调用）
  *   void JOY_Init(Joystick_HandleTypeDef *hjoy)    初始化+中心校准（上电调用一次）
  *   void JOY_Update(Joystick_HandleTypeDef *hjoy)  一帧完整处理（周期任务调用）
  *   uint8_t JOY_IsConnected(Joystick_HandleTypeDef *hjoy)  摇杆连接检测（1=已连接，0=未连接）
  * 其余函数均为 static 私有实现，外部不可见
  *
  * 【调用流程】
  *   1. 初始化（main -> App_Init）
  *        Key_Init() -> JOY_Init(&hjoy) -> 注册按键回调 -> HAL_TIM_Base_Start_IT(&htim5)
  *   2. 周期任务（FreeRTOS，建议 10ms/100Hz）
  *        App_JoystickTask() 循环调用 JOY_Update(&hjoy)
  *        内部流程：读原始值 -> 归一化 -> 平方曲线
  *        结果存放在 hjoy.x_norm / hjoy.y_norm（-1.0~1.0），由应用层直接读取使用
  *   3. 按键（TIM5 1ms 中断）
  *        HAL_TIM_PeriodElapsedCallback -> App_Tick1ms() -> Key_ScanHandler()
  *        Z轴按键事件由 key 模块回调上报，具体功能在应用层实现
  *
  * 【注意事项 / 移植要点】
  * 1. 移植时修改 joystick.h 顶部的 JOY_ADC_HANDLE / JOY_X_ADC_CHANNEL / JOY_Y_ADC_CHANNEL，
  *    并确认 CubeMX 已生成对应 ADC 初始化代码；把本文件加入 Keil 工程 Hardware 组
  * 2. 校准在 JOY_Init 中自动完成（每轴 20 次平均），开机时摇杆必须处于中位，
  *    否则 center_x/center_y 偏移会导致归一化异常
  * 3. 3.3V 供电时中心约为 2048；5V 供电时中心约 3100，且高段行程饱和在 4095、
  *    正向归一化最大仅约 0.32，控制不对称，不推荐
  * 4. JOY_ReadADC 每次 Stop->ConfigChannel->Start->Poll->Stop，为阻塞式轮询，
  *    不要在中断上下文中调用 JOY_Update
 * 5. 连接检测不依赖“断开时读数接近0”，而是综合窗口内波动、校准中心合理性和
 *    静止读数贴近中心来判断；参数见 joystick.h 的 JOY_CONNECT_*。
 *    持续按住对角极限位置可能被判为未连接（与旧版同样的取舍）
  ******************************************************************************
  */

#include "joystick.h"

/* ========================= 私有函数声明 ========================= */
static void JOY_Calibrate(Joystick_HandleTypeDef *hjoy);
static uint16_t JOY_ReadADC(uint32_t channel);
static void JOY_GetNormalized(Joystick_HandleTypeDef *hjoy);
static void JOY_ApplyCurve(Joystick_HandleTypeDef *hjoy);
static void JOY_ReadRaw(Joystick_HandleTypeDef *hjoy);
static void JOY_UpdateConnWindow(Joystick_HandleTypeDef *hjoy);

/* ========================= 连接检测滑动窗口 =========================
 * 记录最近 JOY_CONNECT_WINDOW 次原始值的最小/最大, 用于判断摇杆是否有
 * 实际信号(摇杆在动)。窗口内波动超过 JOY_CONNECT_RANGE 视为有信号。 */
static uint16_t s_conn_min_x = 0;
static uint16_t s_conn_max_x = 0;
static uint16_t s_conn_min_y = 0;
static uint16_t s_conn_max_y = 0;
static uint8_t  s_conn_window_cnt = 0;
static uint8_t  s_conn_bad_cnt = 0;    /* 连续未连接判定计数 */

/* ========================= 公有函数实现 ========================= */

/**
  * @brief  摇杆模块初始化
  * @param  hjoy: 摇杆句柄指针
  * @retval None
  * @note   写入默认算法参数后调用私有 JOY_Calibrate() 完成中心校准
  * @note   说明：STM32F407 的 ADC 硬件不提供校准功能（ADC_CR2 无 CAL 位），
  *         HAL_ADCEx_Calibration_Start 是 F1/F3/L4 等其他系列的接口，
  *         F4 的 HAL 库中不存在该函数，因此这里不做 ADC 硬件校准，
  *         只做摇杆物理中心的软件校准
  */
void JOY_Init(Joystick_HandleTypeDef *hjoy)
{
    if (hjoy == NULL) {
        return;
    }

    /* 写入默认算法参数 */
    hjoy->dead_zone = JOY_DEAD_ZONE;
    hjoy->sensitivity = JOY_SENSITIVITY;
    hjoy->x_raw = 0;
    hjoy->y_raw = 0;
    hjoy->x_norm = 0.0f;
    hjoy->y_norm = 0.0f;

    /* 校准摇杆物理中心 */
    JOY_Calibrate(hjoy);
}

/**
  * @brief  摇杆一帧完整处理
  * @param  hjoy: 摇杆句柄指针
  * @retval None
  * @note   内部流程：JOY_ReadRaw -> JOY_GetNormalized -> JOY_ApplyCurve
  * @note   应在周期任务中调用（建议 10ms/100Hz），不要在中断上下文中调用
  */
void JOY_Update(Joystick_HandleTypeDef *hjoy)
{
    if (hjoy == NULL) {
        return;
    }

    /* 读取X/Y轴原始值 */
    JOY_ReadRaw(hjoy);

    /* 更新连接检测滑动窗口 */
    JOY_UpdateConnWindow(hjoy);

    /* 原始值 -> 归一化值 */
    JOY_GetNormalized(hjoy);

    /* 平方曲线映射 */
    JOY_ApplyCurve(hjoy);
}

/**
  * @brief  检测摇杆是否连接
  * @param  hjoy: 摇杆句柄指针
  * @retval 1=已连接，0=未连接
  * @note   判定条件（不依赖"断开时读数接近0"）：
  *         1. 最近窗口内原始值有明显波动（摇杆在动）-> 已连接
  *         2. 校准中心合理（说明校准时摇杆在位）且静止读数贴近中心 -> 已连接
  *         3. 连续 JOY_CONNECT_CONFIRM_N 次未通过上述判定才上报断开，避免抖动误判
  * @note   在 JOY_Update 之后调用即可（滑动窗口在 JOY_Update 内维护）
  */
uint8_t JOY_IsConnected(Joystick_HandleTypeDef *hjoy)
{
    uint16_t dx;
    uint16_t dy;
    uint8_t  center_ok;
    uint8_t  mid_ok;
    uint8_t  moved;

    if (hjoy == NULL) {
        return 0;
    }

    /* 计算判定信号 */
    center_ok = ((hjoy->center_x >= JOY_CONNECT_CENTER_MIN) && (hjoy->center_x <= JOY_CONNECT_CENTER_MAX) &&
                 (hjoy->center_y >= JOY_CONNECT_CENTER_MIN) && (hjoy->center_y <= JOY_CONNECT_CENTER_MAX)) ? 1U : 0U;
    mid_ok = (((hjoy->x_raw >= JOY_CONNECT_MID_MIN) && (hjoy->x_raw <= JOY_CONNECT_MID_MAX)) ||
              ((hjoy->y_raw >= JOY_CONNECT_MID_MIN) && (hjoy->y_raw <= JOY_CONNECT_MID_MAX))) ? 1U : 0U;
    moved = (((uint16_t)(s_conn_max_x - s_conn_min_x) > JOY_CONNECT_RANGE) ||
             ((uint16_t)(s_conn_max_y - s_conn_min_y) > JOY_CONNECT_RANGE)) ? 1U : 0U;

    /* 任一强信号即认为连接:
     * - mid_ok   : 至少一轴处于合理中段读数(摇杆在位, 与校准中心无关, 插上即有效)
     * - moved    : 窗口内波动大(真实摇动; 断开噪声通常远小于该阈值)
     * - center   : 校准中心合理且静止读数贴近中心(校准时在位)
     */
    if (mid_ok || moved) {
        s_conn_bad_cnt = 0;
        return 1;
    }
    if (center_ok) {
        dx = (hjoy->x_raw > hjoy->center_x) ? (uint16_t)(hjoy->x_raw - hjoy->center_x)
                                            : (uint16_t)(hjoy->center_x - hjoy->x_raw);
        dy = (hjoy->y_raw > hjoy->center_y) ? (uint16_t)(hjoy->y_raw - hjoy->center_y)
                                            : (uint16_t)(hjoy->center_y - hjoy->y_raw);
        if ((dx <= JOY_CONNECT_REST_BAND) || (dy <= JOY_CONNECT_REST_BAND)) {
            s_conn_bad_cnt = 0;
            return 1;
        }
    }

    /* 4) 连续多次未通过才判定断开, 降低瞬时误判 */
    if (++s_conn_bad_cnt >= JOY_CONNECT_CONFIRM_N) {
        return 0;
    }
    return 1;
}

/* ========================= 私有函数实现 ========================= */

/**
  * @brief  更新连接检测滑动窗口 (原始值 min/max)
  * @param  hjoy: 摇杆句柄指针
  * @retval None
  * @note   每 JOY_CONNECT_WINDOW 个样本重置一次窗口
  */
static void JOY_UpdateConnWindow(Joystick_HandleTypeDef *hjoy)
{
    if (s_conn_window_cnt == 0U) {
        s_conn_min_x = hjoy->x_raw;
        s_conn_max_x = hjoy->x_raw;
        s_conn_min_y = hjoy->y_raw;
        s_conn_max_y = hjoy->y_raw;
    }
    else {
        if (hjoy->x_raw < s_conn_min_x) s_conn_min_x = hjoy->x_raw;
        if (hjoy->x_raw > s_conn_max_x) s_conn_max_x = hjoy->x_raw;
        if (hjoy->y_raw < s_conn_min_y) s_conn_min_y = hjoy->y_raw;
        if (hjoy->y_raw > s_conn_max_y) s_conn_max_y = hjoy->y_raw;
    }

    if (++s_conn_window_cnt >= JOY_CONNECT_WINDOW) {
        s_conn_window_cnt = 0;
    }
}

/**
  * @brief  摇杆中心校准
  * @param  hjoy: 摇杆句柄指针
  * @retval None
  * @note   X/Y轴各采样 JOY_SAMPLE_COUNT 次取平均值，存入 center_x / center_y
  * @note   校准时请保持摇杆处于物理中位
  */
static void JOY_Calibrate(Joystick_HandleTypeDef *hjoy)
{
    uint32_t sum_x = 0;
    uint32_t sum_y = 0;
    uint8_t i;

    if (hjoy == NULL) {
        return;
    }

    /* X轴连续采样 JOY_SAMPLE_COUNT 次求平均 */
    for (i = 0; i < JOY_SAMPLE_COUNT; i++) {
        sum_x += JOY_ReadADC(JOY_X_ADC_CHANNEL);
    }
    hjoy->center_x = (uint16_t)(sum_x / JOY_SAMPLE_COUNT);

    /* Y轴连续采样 JOY_SAMPLE_COUNT 次求平均 */
    for (i = 0; i < JOY_SAMPLE_COUNT; i++) {
        sum_y += JOY_ReadADC(JOY_Y_ADC_CHANNEL);
    }
    hjoy->center_y = (uint16_t)(sum_y / JOY_SAMPLE_COUNT);
}

/**
  * @brief  读取指定ADC通道的原始值
  * @param  channel: ADC通道号（如 ADC_CHANNEL_0 / ADC_CHANNEL_1）
  * @retval 12位ADC值（0~4095），失败返回0
  * @note   转换流程：HAL_ADC_Start -> HAL_ADC_PollForConversion
  *                   -> HAL_ADC_GetValue -> HAL_ADC_Stop
  * @note   ADC1按IN0/IN1双通道扫描方式配置时，若只改序列第1位，
  *         序列长度仍是2，读到的会是序列末通道的值。
  *         因此这里临时把序列长度改为1（清SQR1的L位），
  *         保证每次读到的都是指定通道，读完后恢复原序列长度
  */
static uint16_t JOY_ReadADC(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    uint32_t sqr1_backup;
    HAL_StatusTypeDef status;
    uint16_t value = 0;

    /* 确保ADC空闲（若正在转换则先停止） */
    HAL_ADC_Stop(JOY_ADC_HANDLE);

    /* 备份SQR1（含序列长度位L），临时改为单次转换 */
    sqr1_backup = JOY_ADC_HANDLE->Instance->SQR1;
    JOY_ADC_HANDLE->Instance->SQR1 &= ~ADC_SQR1_L;

    /* 将目标通道配置到规则序列第1位 */
    sConfig.Channel = channel;
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;

    status = HAL_ADC_ConfigChannel(JOY_ADC_HANDLE, &sConfig);
    if (status == HAL_OK) {
        status = HAL_ADC_Start(JOY_ADC_HANDLE);
    }

    if (status == HAL_OK) {
        /* 等待转换完成并读取结果 */
        if (HAL_ADC_PollForConversion(JOY_ADC_HANDLE, 10) == HAL_OK) {
            value = (uint16_t)HAL_ADC_GetValue(JOY_ADC_HANDLE);
        }
        HAL_ADC_Stop(JOY_ADC_HANDLE);
    }

    /* 恢复原序列长度 */
    JOY_ADC_HANDLE->Instance->SQR1 = sqr1_backup;

    return value;
}

/**
  * @brief  原始值归一化并应用死区
  * @param  hjoy: 摇杆句柄指针
  * @retval None
  * @note   公式：norm = (raw - center) / center，结果限幅到 [-1.0, 1.0]
  * @note   死区：若 |norm| < dead_zone / center，则归零
  */
static void JOY_GetNormalized(Joystick_HandleTypeDef *hjoy)
{
    float dead_x;
    float dead_y;

    if (hjoy == NULL) {
        return;
    }

    /* 防止中心值为0导致除零错误 */
    if (hjoy->center_x == 0) {
        hjoy->center_x = 1;
    }
    if (hjoy->center_y == 0) {
        hjoy->center_y = 1;
    }

    /* X轴归一化 */
    hjoy->x_norm = ((float)hjoy->x_raw - (float)hjoy->center_x) / (float)hjoy->center_x;

    /* Y轴归一化 */
    hjoy->y_norm = ((float)hjoy->y_raw - (float)hjoy->center_y) / (float)hjoy->center_y;

    /* X轴死区处理 */
    dead_x = (float)hjoy->dead_zone / (float)hjoy->center_x;
    if ((hjoy->x_norm > -dead_x) && (hjoy->x_norm < dead_x)) {
        hjoy->x_norm = 0.0f;
    }

    /* Y轴死区处理 */
    dead_y = (float)hjoy->dead_zone / (float)hjoy->center_y;
    if ((hjoy->y_norm > -dead_y) && (hjoy->y_norm < dead_y)) {
        hjoy->y_norm = 0.0f;
    }

    /* 限幅到 [-1.0, 1.0]，防止中心偏移导致归一化值超界 */
    if (hjoy->x_norm > 1.0f) {
        hjoy->x_norm = 1.0f;
    } else if (hjoy->x_norm < -1.0f) {
        hjoy->x_norm = -1.0f;
    }

    if (hjoy->y_norm > 1.0f) {
        hjoy->y_norm = 1.0f;
    } else if (hjoy->y_norm < -1.0f) {
        hjoy->y_norm = -1.0f;
    }
}

/**
  * @brief  对归一化值应用平方曲线
  * @param  hjoy: 摇杆句柄指针
  * @retval None
  * @note   公式：y = sign(x) * x^2，结果直接覆盖原 x_norm / y_norm
  *         小角度输出更小、大角度输出更饱满，提高低速控制精度
  */
static void JOY_ApplyCurve(Joystick_HandleTypeDef *hjoy)
{
    float x;
    float y;

    if (hjoy == NULL) {
        return;
    }

    x = hjoy->x_norm;
    y = hjoy->y_norm;

    if (x < 0.0f) {
        hjoy->x_norm = -(x * x);
    } else {
        hjoy->x_norm = (x * x);
    }

    if (y < 0.0f) {
        hjoy->y_norm = -(y * y);
    } else {
        hjoy->y_norm = (y * y);
    }
}

/**
  * @brief  读取摇杆一帧原始数据
  * @param  hjoy: 摇杆句柄指针
  * @retval None
  * @note   刷新 x_raw / y_raw 两个成员
  */
static void JOY_ReadRaw(Joystick_HandleTypeDef *hjoy)
{
    if (hjoy == NULL) {
        return;
    }

    hjoy->x_raw = JOY_ReadADC(JOY_X_ADC_CHANNEL);
    hjoy->y_raw = JOY_ReadADC(JOY_Y_ADC_CHANNEL);
}
