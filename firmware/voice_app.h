// voice_app.h —— 语音对话应用状态机与编排
#pragma once

#include "esp_err.h"
#include "bsp_button.h"

// 初始化应用（创建 UI + 启动 WiFi + 音频 worker + 输入任务）
esp_err_t voice_app_init(void);

// 按键事件入口（bsp_button 回调转发；回调运行于 esp_timer 任务，仅入队）
void voice_app_on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user);
