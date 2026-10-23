// app_main.c —— AI Passport 本地语音对话应用入口（feature/local-voice-chat）
// 流程：BSP 外设初始化 → 创建应用 UI → 启动 WiFi + 音频 worker → 按键驱动对话
#include "voice_app.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_log.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "AI Passport Local Voice Chat (Qwen3-VL via local gateway)");

    bsp_i2c_init();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "Display/LVGL init failed");
        return;
    }
    bsp_display_backlight(100);

    if (bsp_button_init(voice_app_on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Button init failed");
    }

    // 音频 codec 必须在 voice_app_init（其内 voice_audio_start 会设格式）之前就绪
    if (bsp_audio_init() != ESP_OK) {
        ESP_LOGE(TAG, "Audio init failed");
    }

    // 电池初始化失败不阻塞对话
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "Battery init failed (non-fatal)");
    }

    esp_err_t err = voice_app_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Voice app init failed: %s", esp_err_to_name(err));
    }
}
