// voice_ui.h —— 语音对话应用 UI（状态 + 识别/回复文本）
#pragma once

// UI 状态
typedef enum {
    UI_STATE_BOOT,      // 启动中
    UI_STATE_IDLE,      // 待机（WiFi 已连接，等待按键）
    UI_STATE_LISTEN,    // 聆听中
    UI_STATE_THINK,     // 思考中
    UI_STATE_SPEAK,     // 回复中
    UI_STATE_ERROR,     // 错误（显示提示）
} voice_ui_state_t;

// 创建并加载主界面（须持 LVGL 锁或在 LVGL 任务中调用）
void voice_ui_create(void);

// 切换状态（可在任意任务调用，内部加锁）
void voice_ui_set_state(voice_ui_state_t state);

// 设置识别文本与回复文本（UTF-8 中文，可在任意任务调用）
void voice_ui_set_texts(const char *asr, const char *reply);

// 设置状态栏小字（如 WiFi 状态、时延）
void voice_ui_set_note(const char *note);

// 删除界面（退出用）
void voice_ui_destroy(void);
