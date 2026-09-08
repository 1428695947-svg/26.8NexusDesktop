/**
 * @file lv_port_indev.c
 *
 * 单指针输入设备 (鼠标):
 *  - 坐标/按下状态由应用层 App_MouseUpdate() 维护 (摇杆移动 + 触摸吸附 + PA2 按下)。
 *  - 应用于本工程的猫咪光标 (见 APP/GUI/custom.c), 触发区域为猫咪尾部。
 */

/*Copy this file as "lv_port_indev.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_indev.h"
#include "../../lvgl.h"
#include "app.h"
#include "app_mouse.h"

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void mouse_init(void);
static void mouse_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);

/**********************
 *  STATIC VARIABLES
 **********************/
lv_indev_t * indev_mouse;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;

    /* Initialize the mouse and register it as a pointer device.
     * The pressed/position state comes from the APP layer (App_MouseUpdate). */
    mouse_init();

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = mouse_read;
    indev_mouse = lv_indev_drv_register(&indev_drv);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*Initialize your mouse*/
static void mouse_init(void)
{
    /* Nothing to do: app layer owns the mouse state. */
}

/*Will be called by the library to read the mouse*/
static void mouse_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    int x;
    int y;
    uint8_t pressed;
    uint8_t more;

    (void)indev_drv;
    App_InputReadPointer(&x, &y, &pressed, &more);
    data->point.x = (lv_coord_t)x;
    data->point.y = (lv_coord_t)y;
    data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->continue_reading = more ? 1U : 0U;
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
