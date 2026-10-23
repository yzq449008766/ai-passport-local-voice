// wifi_sta.h —— 连接本地 WiFi（STA），供语音对话应用使用
#pragma once

#include "esp_err.h"
#include <stdbool.h>

// 启动 STA 连接。返回后异步连接；结果通过 wifi_sta_is_connected() 查询。
esp_err_t wifi_sta_start(void);

// 是否已获得 IP（连接成功）。
bool wifi_sta_is_connected(void);

// 最近一次连接状态文本（英文短句，供 UI 显示）。
const char *wifi_sta_status_text(void);
