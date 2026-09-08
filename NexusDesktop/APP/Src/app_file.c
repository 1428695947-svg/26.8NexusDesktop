/**
  ******************************************************************************
  * @file    app_file.c
  * @brief   文件管理应用实现 (SD 卡 FATFS, 根目录)
  * @author  Doubao
  * @version V1.0
  * @date    2026-09-04
  * @note    文件列表/新建(重名检测 FA_CREATE_NEW)/打开查看/写入保存/删除。
  *          所有 FatFs 操作经 App_StorageSubmit 交给后台存储工作线程。
  *          名称限 8.3 ASCII (FATFS 未开 LFN)。
  ******************************************************************************
  */

#include "app_file.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "gui_guider.h"
#include "custom.h"
#include "widgets_init.h"
#include "app_log.h"
#include "app_storage.h"

#include "ff.h"
#include "diskio.h"

/* ========================= 常量 ========================= */
#define FM_W            320
#define FM_H            480

#define FM_FILE_MAX     32               /* 根目录最多显示文件数 */
#define FM_NAME_MAX     13               /* 8.3 文件名 + 结尾 */
#define FM_CONTENT_MAX  512              /* 读写内容缓冲 */
#define FM_TA_MAX       48               /* 名称输入框最大长度 */

/* ========================= 私有变量 ========================= */
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_label_path = NULL;    /* 路径/状态标签 */
static lv_obj_t *s_list_cont = NULL;     /* 文件列表容器 (可滚动) */
static lv_obj_t *s_label_meta = NULL;    /* 选中项的五项元数据 */
static lv_obj_t *s_label_hint = NULL;    /* 底部提示 */

static App_StorageEntry_t s_entries[FM_FILE_MAX];
static int  s_file_count = 0;                   /* 当前文件数 */
static int  s_selected = -1;                    /* 选中项索引 */
static char s_path[16] = "0:/";                 /* 当前路径 */
static App_StorageRequest_t s_request;
static uint8_t s_fg = 0U;

/* 输入面板 (新建/打开/写入共用) */
static lv_obj_t *s_panel = NULL;         /* 输入面板容器 */
static lv_obj_t *s_ta = NULL;            /* 文本输入框 */
static lv_obj_t *s_ta_title = NULL;
static uint8_t  s_panel_mode = 0;        /* 0=无, 1=新建名, 2=写入内容 */
static char      s_panel_name[FM_NAME_MAX]; /* 写入模式下关联的文件名 */

static char s_content[FM_CONTENT_MAX];   /* 文件内容缓冲 */

/* ========================= 私有函数声明 ========================= */
static void file_refresh_list(void);
static void file_show_hint(const char *msg);
static void file_item_click_cb(lv_event_t * e);
static void file_poll_cb(lv_timer_t *timer);
static void file_panel_hide(void);
static void file_panel_show(uint8_t mode, const char *title, const char *init);

static void file_show_meta(int index, const char *status)
{
    char text[128];
    const char *type;

    if (s_label_meta == NULL) {
        return;
    }
    if (index < 0 || index >= s_file_count) {
        lv_label_set_text(s_label_meta,
            "名称: -  类型: -  大小: -\n位置: 0:/  状态: 未选中");
        return;
    }
    type = (s_entries[index].attr & AM_DIR) ? "目录" : "文件";
    (void)snprintf(text, sizeof(text),
        "名称: %s  类型: %s  大小: %lu B\n位置: %s  状态: %s",
        s_entries[index].name, type, (unsigned long)s_entries[index].size,
        s_path, status != NULL ? status : "正常");
    lv_label_set_text(s_label_meta, text);
}

/* ========================= FATFS 小工具 ========================= */

/* 拼接完整路径到 dst (s_path + name) */
static void file_make_path(char *dst, size_t dstsz, const char *name)
{
    snprintf(dst, dstsz, "%s%s", s_path, name);
}

/* 执行一条命令并返回 FATFS 错误描述 */
static const char *file_fr_desc(FRESULT fr)
{
    switch (fr) {
        case FR_OK:             return "成功";
        case FR_EXIST:          return "重名, 已存在";
        case FR_NO_FILE:        return "文件不存在";
        case FR_NO_PATH:        return "路径不存在";
        case FR_DENIED:         return "被拒绝/权限不足";
        case FR_LOCKED:         return "文件被占用";
        case FR_NOT_ENABLED:    return "未挂载磁盘";
        case FR_NOT_READY:      return "SD 未就绪";
        case FR_NOT_ENOUGH_CORE:return "内存不足";
        case FR_INVALID_NAME:   return "非法文件名 (限8.3)";
        case FR_INVALID_OBJECT: return "非法对象";
        case FR_WRITE_PROTECTED:return "写保护";
        case FR_DISK_ERR:       return "磁盘错误";
        case FR_INT_ERR:        return "内部错误";
        case FR_MKFS_ABORTED:   return "格式化中止";
        case FR_NO_FILESYSTEM:  return "无文件系统";
        case FR_TIMEOUT:        return "超时";
        default:                return "未知错误";
    }
}

/* ========================= 列表 ========================= */

static uint8_t file_submit(App_StorageOperation_t operation, const char *path)
{
    if (s_request.state != APP_STORAGE_REQ_IDLE) {
        file_show_hint("已有存储操作正在执行");
        return 0U;
    }
    memset(&s_request, 0, sizeof(s_request));
    s_request.operation = (uint8_t)operation;
    strncpy(s_request.path, path, sizeof(s_request.path) - 1U);
    s_request.data = s_content;
    s_request.data_capacity = sizeof(s_content);
    s_request.entries = s_entries;
    s_request.entry_capacity = FM_FILE_MAX;
    if (operation == APP_STORAGE_OP_WRITE) {
        s_request.data_length = (uint32_t)strlen(s_content);
    }
    if (!App_StorageSubmit(&s_request)) {
        file_show_hint("存储服务忙");
        return 0U;
    }
    file_show_hint("后台处理中...");
    return 1U;
}

static void file_render_list(void)
{
    char tmp[40];
    int i;

    lv_obj_clean(s_list_cont);
    s_selected = -1;
    file_show_meta(-1, NULL);

    for (i = 0; i < s_file_count; i++) {
        /* 创建列表项按钮 (手动纵向布局, LV_USE_FLEX=0) */
        lv_obj_t * b = lv_btn_create(s_list_cont);
        lv_obj_set_pos(b, 0, i * 34);
        lv_obj_set_size(b, FM_W - 20, 30);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x334155), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(b, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x2195f6), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_text_font(b, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(b, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);

        if (s_entries[i].attr & AM_DIR) {
            snprintf(tmp, sizeof(tmp), "[D] %s", s_entries[i].name);
        } else {
            snprintf(tmp, sizeof(tmp), "%s  %lu B", s_entries[i].name,
                     (unsigned long)s_entries[i].size);
        }
        lv_obj_t * l = lv_label_create(b);
        lv_label_set_text(l, tmp);
        lv_obj_set_pos(l, 8, 4);

        lv_obj_set_user_data(b, (void *)(lv_uintptr_t)i);
        lv_obj_add_event_cb(b, file_item_click_cb, LV_EVENT_CLICKED, NULL);
    }

    if (s_file_count == 0) {
        file_show_hint("目录为空");
    }
    else if (s_request.truncated) {
        file_show_hint("项目过多，仅显示前32项");
        App_Log_Event(LOG_LEVEL_WARN, "文件列表达到上限 path=%s max=%u",
                      s_path, (unsigned int)FM_FILE_MAX);
    }
    else {
        file_show_hint("点击文件选中");
    }
}

static void file_refresh_list(void)
{
    lv_obj_clean(s_list_cont);
    s_file_count = 0;
    s_selected = -1;
    (void)file_submit(APP_STORAGE_OP_LIST, s_path);
}

static void file_poll_cb(lv_timer_t *timer)
{
    App_StorageOperation_t operation;
    FRESULT result;
    char completed_path[sizeof(s_request.path)];

    (void)timer;
    if (s_request.state != APP_STORAGE_REQ_DONE) {
        return;
    }
    operation = (App_StorageOperation_t)s_request.operation;
    result = (FRESULT)s_request.result;
    strncpy(completed_path, s_request.path, sizeof(completed_path) - 1U);
    completed_path[sizeof(completed_path) - 1U] = '\0';
    if (operation == APP_STORAGE_OP_LIST) {
        s_file_count = (int)s_request.entry_count;
    }
    App_StorageRequestReset(&s_request);
    if (!s_fg) {
        return;
    }
    if (result != FR_OK) {
        file_show_hint(file_fr_desc(result));
        App_Log_Error("文件操作失败 op=%u path=%s FR=%u",
                      (unsigned int)operation, completed_path, (unsigned int)result);
        return;
    }

    switch (operation) {
        case APP_STORAGE_OP_LIST:
            file_render_list();
            break;
        case APP_STORAGE_OP_CREATE:
            file_panel_hide();
            App_Log_Event(LOG_LEVEL_INFO, "文件已创建 %s", completed_path);
            file_refresh_list();
            break;
        case APP_STORAGE_OP_READ:
            file_panel_show(2, "文件内容 (确定=覆盖保存)", s_content);
            file_show_hint("读取成功");
            App_Log_Event(LOG_LEVEL_INFO, "文件已读取 %s", completed_path);
            break;
        case APP_STORAGE_OP_WRITE:
            file_panel_hide();
            file_show_hint("保存成功");
            App_Log_Event(LOG_LEVEL_INFO, "文件已写入 %s", completed_path);
            break;
        case APP_STORAGE_OP_DELETE:
            file_show_hint("删除成功");
            App_Log_Event(LOG_LEVEL_WARN, "文件已删除 %s", completed_path);
            file_refresh_list();
            break;
        default:
            break;
    }
}

/* 列表项点击: 选中 */
static void file_item_click_cb(lv_event_t * e)
{
    lv_obj_t * b = lv_event_get_target(e);
    int idx = (int)(lv_uintptr_t)lv_obj_get_user_data(b);
    s_selected = idx;
    file_show_meta(idx, "正常");
    file_show_hint("已选中");
}

/* ========================= 提示 ========================= */

static void file_show_hint(const char *msg)
{
    if (s_label_hint != NULL) {
        lv_label_set_text(s_label_hint, msg);
    }
}

/* ========================= 输入面板 ========================= */

/* 隐藏输入面板并收起键盘 */
static void file_panel_hide(void)
{
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    if (guider_ui.g_kb_top_layer != NULL) {
        lv_obj_add_flag(guider_ui.g_kb_top_layer, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 面板确定按钮处理 */
static void file_panel_ok_cb(lv_event_t * e)
{
    (void)e;
    const char *txt = lv_textarea_get_text(s_ta);
    char full[32];

    if (s_panel_mode == 1) {            /* 新建文件 */
        if (txt == NULL || txt[0] == '\0') {
            file_show_hint("请输入文件名");
            return;
        }
        if (strlen(txt) >= FM_NAME_MAX) {
            file_show_hint("文件名过长 (限8.3)");
            return;
        }
        file_make_path(full, sizeof(full), txt);
        (void)file_submit(APP_STORAGE_OP_CREATE, full);
    }
    else if (s_panel_mode == 2) {       /* 写入内容 */
        if (s_panel_name[0] == '\0') {
            file_show_hint("未选择文件");
            return;
        }
        file_make_path(full, sizeof(full), s_panel_name);
        strncpy(s_content, txt != NULL ? txt : "", sizeof(s_content) - 1U);
        s_content[sizeof(s_content) - 1U] = '\0';
        (void)file_submit(APP_STORAGE_OP_WRITE, full);
    }
}

static void file_panel_cancel_cb(lv_event_t * e)
{
    (void)e;
    file_panel_hide();
}

/* 创建输入面板 (一次) */
static void file_panel_create(void)
{
    lv_obj_t * btn_ok;
    lv_obj_t * btn_cancel;

    s_panel = lv_obj_create(s_screen);
    lv_obj_set_size(s_panel, FM_W, FM_H);
    lv_obj_set_pos(s_panel, 0, 0);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_panel, 220, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(s_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);

    s_ta_title = lv_label_create(s_panel);
    lv_obj_set_pos(s_ta_title, 20, 60);
    lv_obj_set_style_text_font(s_ta_title, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_ta_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);

    s_ta = lv_textarea_create(s_panel);
    lv_obj_set_pos(s_ta, 20, 100);
    lv_obj_set_size(s_ta, FM_W - 40, 120);
    lv_obj_set_style_text_font(s_ta, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_ta, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_ta, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_ta, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_max_length(s_ta, FM_CONTENT_MAX - 1);
    lv_textarea_set_placeholder_text(s_ta, "输入...");
    lv_obj_add_event_cb(s_ta, ta_event_cb, LV_EVENT_ALL,
                        guider_ui.g_kb_top_layer);                 /* 复用全局键盘 */

    btn_ok = lv_btn_create(s_panel);
    lv_obj_set_pos(btn_ok, 40, FM_H - 90);
    lv_obj_set_size(btn_ok, 100, 40);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0x16a34a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_ok, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_ok, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_ok, file_panel_ok_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * ol = lv_label_create(btn_ok);
    lv_label_set_text(ol, "确定");
    lv_obj_center(ol);

    btn_cancel = lv_btn_create(s_panel);
    lv_obj_set_pos(btn_cancel, 180, FM_H - 90);
    lv_obj_set_size(btn_cancel, 100, 40);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x64748b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_cancel, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_cancel, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_cancel, file_panel_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * cl = lv_label_create(btn_cancel);
    lv_label_set_text(cl, "取消");
    lv_obj_center(cl);
}

/* 显示输入面板 */
static void file_panel_show(uint8_t mode, const char *title, const char *init)
{
    if (s_panel == NULL) {
        file_panel_create();
    }
    s_panel_mode = mode;
    lv_label_set_text(s_ta_title, title);
    lv_textarea_set_one_line(s_ta, (mode == 1));   /* 名称单行, 内容多行 */
    lv_textarea_set_text(s_ta, init != NULL ? init : "");
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    /* 文件输入框为运行时创建，主动绑定并弹出顶层键盘，避免必须二次点击。 */
    if (guider_ui.g_kb_top_layer != NULL) {
        lv_keyboard_set_textarea(guider_ui.g_kb_top_layer, s_ta);
        lv_obj_move_foreground(guider_ui.g_kb_top_layer);
        lv_obj_clear_flag(guider_ui.g_kb_top_layer, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ========================= 动作按钮 ========================= */

/* 新建 */
static void file_new_cb(lv_event_t * e)
{
    (void)e;
    file_panel_show(1, "新建文件 - 输入文件名(8.3 ASCII)", "");
}

/* 打开查看 */
static void file_open_cb(lv_event_t * e)
{
    (void)e;
    char full[32];

    if (s_selected < 0 || s_selected >= s_file_count) {
        file_show_hint("请先点击选中文件");
        return;
    }
    if (s_entries[s_selected].attr & AM_DIR) {
        file_show_hint("目录暂不支持打开");
        return;
    }
    strncpy(s_panel_name, s_entries[s_selected].name, FM_NAME_MAX - 1U);
    s_panel_name[FM_NAME_MAX - 1U] = '\0';
    file_make_path(full, sizeof(full), s_panel_name);
    s_content[0] = '\0';
    (void)file_submit(APP_STORAGE_OP_READ, full);
}

/* 删除 */
static void file_del_cb(lv_event_t * e)
{
    (void)e;
    char full[32];

    if (s_selected < 0 || s_selected >= s_file_count) {
        file_show_hint("请先点击选中文件");
        return;
    }
    if (s_entries[s_selected].attr & AM_DIR) {
        file_show_hint("目录暂不支持删除");
        return;
    }
    file_make_path(full, sizeof(full), s_entries[s_selected].name);
    (void)file_submit(APP_STORAGE_OP_DELETE, full);
}

/* 写入(保存当前选中文件) */
static void file_write_cb(lv_event_t * e)
{
    (void)e;
    if (s_selected < 0 || s_selected >= s_file_count) {
        file_show_hint("请先点击选中文件");
        return;
    }
    if (s_entries[s_selected].attr & AM_DIR) {
        file_show_hint("目录暂不支持写入");
        return;
    }
    strncpy(s_panel_name, s_entries[s_selected].name, FM_NAME_MAX - 1);
    s_panel_name[FM_NAME_MAX - 1] = '\0';
    file_panel_show(2, "写入 - 输入内容 (确定=覆盖保存)", "");
}

/* ========================= 屏幕创建 ========================= */

static void file_back_cb(lv_event_t * e)
{
    (void)e;
    App_File_Close();
}

static void file_refresh_cb(lv_event_t * e)
{
    (void)e;
    file_refresh_list();
}

static void file_screen_create(void)
{
    lv_obj_t * btn_back;
    lv_obj_t * btn_refresh;
    lv_obj_t * btn_new;
    lv_obj_t * btn_open;
    lv_obj_t * btn_del;
    lv_obj_t * btn_write;

    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, FM_W, FM_H);
    lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x0f172a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_screen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 顶栏: 返回 + 标题 + 刷新 */
    btn_back = lv_btn_create(s_screen);
    lv_obj_set_pos(btn_back, 10, 8);
    lv_obj_set_size(btn_back, 70, 32);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x2195f6), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_back, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_back, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_back, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_back, file_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * bl = lv_label_create(btn_back);
    lv_label_set_text(bl, "返回");
    lv_obj_center(bl);

    lv_obj_t * title = lv_label_create(s_screen);
    lv_label_set_text(title, "文件管理");
    lv_obj_set_pos(title, 90, 10);
    lv_obj_set_style_text_font(title, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);

    btn_refresh = lv_btn_create(s_screen);
    lv_obj_set_pos(btn_refresh, 240, 8);
    lv_obj_set_size(btn_refresh, 70, 32);
    lv_obj_set_style_bg_color(btn_refresh, lv_color_hex(0x64748b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_refresh, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_refresh, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_refresh, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_refresh, file_refresh_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * rl = lv_label_create(btn_refresh);
    lv_label_set_text(rl, "刷新");
    lv_obj_center(rl);

    s_label_path = lv_label_create(s_screen);
    lv_obj_set_pos(s_label_path, 10, 46);
    lv_obj_set_style_text_font(s_label_path, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_label_path, lv_color_hex(0x94a3b8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_label_path, "路径: 0:/");

    /* 文件列表容器 (手动布局子项, 纵向滚动) */
    s_list_cont = lv_obj_create(s_screen);
    lv_obj_set_pos(s_list_cont, 10, 70);
    lv_obj_set_size(s_list_cont, FM_W - 20, FM_H - 70 - 118);
    lv_obj_set_style_bg_color(s_list_cont, lv_color_hex(0x0f172a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_list_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_list_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_list_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scroll_dir(s_list_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list_cont, LV_SCROLLBAR_MODE_AUTO);

    /* 名称/类型/大小/位置/状态集中展示，方便验收核对。 */
    s_label_meta = lv_label_create(s_screen);
    lv_obj_set_pos(s_label_meta, 10, FM_H - 112);
    lv_obj_set_size(s_label_meta, FM_W - 20, 38);
    lv_obj_set_style_text_font(s_label_meta, &lv_font_sourcehan18_custom,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_label_meta, lv_color_hex(0xcbd5e1),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(s_label_meta, LV_LABEL_LONG_WRAP);
    file_show_meta(-1, NULL);

    /* 底部提示 */
    s_label_hint = lv_label_create(s_screen);
    lv_obj_set_pos(s_label_hint, 10, FM_H - 74);
    lv_obj_set_size(s_label_hint, FM_W - 20, 20);
    lv_obj_set_style_text_font(s_label_hint, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_label_hint, lv_color_hex(0xfbbf24), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_label_hint, "点击文件选中");

    /* 底部动作栏: 新建/打开/删除/写入 */
    btn_new = lv_btn_create(s_screen);
    lv_obj_set_pos(btn_new, 10, FM_H - 50);
    lv_obj_set_size(btn_new, 70, 40);
    lv_obj_set_style_bg_color(btn_new, lv_color_hex(0x16a34a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_new, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_new, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_new, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_new, file_new_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * nl = lv_label_create(btn_new);
    lv_label_set_text(nl, "新建");
    lv_obj_center(nl);

    btn_open = lv_btn_create(s_screen);
    lv_obj_set_pos(btn_open, 88, FM_H - 50);
    lv_obj_set_size(btn_open, 70, 40);
    lv_obj_set_style_bg_color(btn_open, lv_color_hex(0x2195f6), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_open, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_open, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_open, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_open, file_open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * ol2 = lv_label_create(btn_open);
    lv_label_set_text(ol2, "打开");
    lv_obj_center(ol2);

    btn_del = lv_btn_create(s_screen);
    lv_obj_set_pos(btn_del, 166, FM_H - 50);
    lv_obj_set_size(btn_del, 70, 40);
    lv_obj_set_style_bg_color(btn_del, lv_color_hex(0xdc2626), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_del, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_del, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_del, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_del, file_del_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * dl = lv_label_create(btn_del);
    lv_label_set_text(dl, "删除");
    lv_obj_center(dl);

    btn_write = lv_btn_create(s_screen);
    lv_obj_set_pos(btn_write, 244, FM_H - 50);
    lv_obj_set_size(btn_write, 66, 40);
    lv_obj_set_style_bg_color(btn_write, lv_color_hex(0x9333ea), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_write, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_write, &lv_font_sourcehan18_custom, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_write, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_write, file_write_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * wl = lv_label_create(btn_write);
    lv_label_set_text(wl, "写入");
    lv_obj_center(wl);

    (void)lv_timer_create(file_poll_cb, 50U, NULL);
}

/* ========================= 公共接口实现 ========================= */

void App_File_Open(void)
{
    if (s_screen == NULL) {
        file_screen_create();
    }
    s_fg = 1U;
    file_refresh_list();
    lv_scr_load(s_screen);
}

void App_File_Close(void)
{
    s_fg = 0U;
    if (guider_ui.desktop != NULL) {
        lv_scr_load(guider_ui.desktop);
    }
}
