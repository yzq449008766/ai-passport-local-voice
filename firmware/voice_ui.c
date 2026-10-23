// voice_ui.c —— 重新设计的应用 UI（非 demo 菜单）：
//   深色背景 + 顶部状态大字 + 中间中文对话文本 + 底部小字。
//   中文用 LVGL 内置 Source Han Sans SC 16px CJK 子集（需 CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_16_CJK=y）。
#include "voice_ui.h"

#include "bsp_display.h"
#include "bsp_pins.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

#define COLOR_BG     0x0E1420
#define COLOR_PANEL  0x1B2535
#define COLOR_TEXT   0xE8EDF5
#define COLOR_MUTED  0x8FA3BF
#define COLOR_RED    0xFF5C4D
#define COLOR_ORANGE 0xFFB23E
#define COLOR_BLUE   0x3EA6FF
#define COLOR_GREEN  0x5BD65F

static lv_obj_t *s_scr;
static lv_obj_t *s_state_label;   // 顶部大字状态
static lv_obj_t *s_state_dot;     // 状态圆点
static lv_obj_t *s_asr_label;     // "你说：" 识别文本
static lv_obj_t *s_reply_label;   // "它答：" 回复文本
static lv_obj_t *s_note_label;    // 底部小字

static const char *s_state_text = "";

extern const lv_font_t lv_font_custom_16;  // 自定义全量 CJK 字体（思源黑体 16px，约 2 万汉字）

static const lv_font_t *cjk_font(void)
{
    return &lv_font_custom_16;
}

static void set_state_label(void)
{
    lv_label_set_text(s_state_label, s_state_text);
    lv_obj_set_style_text_color(s_state_label, lv_color_hex(COLOR_TEXT), 0);
}

void voice_ui_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_set_size(s_scr, BSP_LCD_W, BSP_LCD_H);

    // 顶部：状态圆点 + 大字状态
    s_state_dot = lv_obj_create(s_scr);
    lv_obj_set_size(s_state_dot, 18, 18);
    lv_obj_set_style_radius(s_state_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_state_dot, 0, 0);
    lv_obj_set_style_bg_color(s_state_dot, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_align(s_state_dot, LV_ALIGN_TOP_LEFT, 18, 22);

    s_state_label = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_state_label, cjk_font(), 0);
    lv_obj_set_style_text_color(s_state_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(s_state_label, LV_ALIGN_TOP_LEFT, 46, 18);
    lv_label_set_text(s_state_label, "BOOT");

    // 标题
    lv_obj_t *title = lv_label_create(s_scr);
    lv_obj_set_style_text_font(title, cjk_font(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_MUTED), 0);
    lv_label_set_text(title, "AI PASSPORT · 本地大脑");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 56);

    // 对话区（中间面板）
    lv_obj_t *panel = lv_obj_create(s_scr);
    lv_obj_set_size(panel, 216, 160);
    lv_obj_set_style_bg_color(panel, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 92);

    s_asr_label = lv_label_create(panel);
    lv_obj_set_style_text_font(s_asr_label, cjk_font(), 0);
    lv_obj_set_style_text_color(s_asr_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_width(s_asr_label, 196);
    lv_label_set_long_mode(s_asr_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_asr_label, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_label_set_text(s_asr_label, "你说：");

    s_reply_label = lv_label_create(panel);
    lv_obj_set_style_text_font(s_reply_label, cjk_font(), 0);
    lv_obj_set_style_text_color(s_reply_label, lv_color_hex(COLOR_BLUE), 0);
    lv_obj_set_width(s_reply_label, 196);
    lv_label_set_long_mode(s_reply_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_reply_label, LV_ALIGN_TOP_LEFT, 10, 66);
    lv_label_set_text(s_reply_label, "它答：");

    // 底部小字
    s_note_label = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_note_label, cjk_font(), 0);
    lv_obj_set_style_text_color(s_note_label, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_width(s_note_label, 216);
    lv_label_set_long_mode(s_note_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_note_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_label_set_text(s_note_label, "按住 OK 说话");

    lv_screen_load(s_scr);
}

void voice_ui_set_state(voice_ui_state_t state)
{
    if (!bsp_lvgl_lock(300)) return;
    const char *text = "";
    uint32_t color = COLOR_MUTED;
    switch (state) {
    case UI_STATE_BOOT:    text = "BOOT";        color = COLOR_MUTED;  break;
    case UI_STATE_IDLE:    text = "READY";       color = COLOR_GREEN;  break;
    case UI_STATE_LISTEN:  text = "LISTENING";   color = COLOR_RED;    break;
    case UI_STATE_THINK:   text = "THINKING";    color = COLOR_ORANGE; break;
    case UI_STATE_SPEAK:   text = "SPEAKING";    color = COLOR_BLUE;   break;
    case UI_STATE_ERROR:   text = "ERROR";       color = COLOR_RED;    break;
    }
    s_state_text = text;
    if (s_state_label) {
        lv_label_set_text(s_state_label, text);
        lv_obj_set_style_text_color(s_state_label, lv_color_hex(color), 0);
    }
    if (s_state_dot) {
        lv_obj_set_style_bg_color(s_state_dot, lv_color_hex(color), 0);
    }
    bsp_lvgl_unlock();
}

void voice_ui_set_texts(const char *asr, const char *reply)
{
    if (!bsp_lvgl_lock(300)) return;
    if (s_asr_label) {
        char buf[200];
        snprintf(buf, sizeof(buf), "你说：%s", asr ? asr : "");
        lv_label_set_text(s_asr_label, buf);
    }
    if (s_reply_label) {
        char buf[320];
        snprintf(buf, sizeof(buf), "它答：%s", reply ? reply : "");
        lv_label_set_text(s_reply_label, buf);
    }
    bsp_lvgl_unlock();
}

void voice_ui_set_note(const char *note)
{
    if (!bsp_lvgl_lock(300)) return;
    if (s_note_label) lv_label_set_text(s_note_label, note ? note : "");
    bsp_lvgl_unlock();
}

void voice_ui_destroy(void)
{
    if (!bsp_lvgl_lock(300)) return;
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_state_label = s_state_dot = NULL;
        s_asr_label = s_reply_label = s_note_label = NULL;
    }
    bsp_lvgl_unlock();
}
