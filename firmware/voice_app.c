// voice_app.c —— 状态机：IDLE → LISTEN(录音) → THINK(HTTP) → SPEAK(播放) → IDLE
//   按键：OK 按住开始录音，松开停止并上传；播放期间 OK 短按可打断。
#include "voice_app.h"

#include "voice_audio.h"
#include "voice_http.h"
#include "voice_ui.h"
#include "wifi_sta.h"

#include "bsp_audio.h"
#include "bsp_display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "voice_app";

typedef enum {
    APP_IDLE,
    APP_LISTEN,
    APP_THINK,
    APP_SPEAK,
} app_state_t;

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t ev;
} app_key_event_t;

#define KEY_QUEUE_DEPTH 8
// 流式上传后录音不再本地缓存；此上限仅为防呆（按住过久自动截止）
#define RECORD_MAX_BYTES ((size_t)(VOICE_SAMPLE_RATE * 2 * CONFIG_AIPASSPORT_RECORD_MAX_MS / 1000))
#define RECORD_MIN_BYTES ((size_t)(VOICE_SAMPLE_RATE * 2 * CONFIG_AIPASSPORT_RECORD_MIN_MS / 1000))

static app_state_t s_state = APP_IDLE;
static QueueHandle_t s_key_queue;
static TaskHandle_t s_app_task;
static char s_asr_text[256];
static char s_reply_text[384];
static volatile bool s_chat_active;
static volatile bool s_capture_stop;   // OK 松开时置位，录音循环据此停止

static void do_chat_wrapper(void *arg);

static void set_state(app_state_t st)
{
    s_state = st;
    switch (st) {
    case APP_IDLE:   voice_ui_set_state(UI_STATE_IDLE);   break;
    case APP_LISTEN: voice_ui_set_state(UI_STATE_LISTEN); break;
    case APP_THINK:  voice_ui_set_state(UI_STATE_THINK);  break;
    case APP_SPEAK:  voice_ui_set_state(UI_STATE_SPEAK);  break;
    }
}

static void do_chat(void)
{
    // 1) 打开流式上传连接（对讲机式：边录边传，按住多久录多久）
    voice_ui_set_texts("", "");
    voice_ui_set_note("按住说话…");
    set_state(APP_LISTEN);

    voice_stream_req_t *req = NULL;
    esp_err_t err = voice_http_chat_stream_begin(&req);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "stream begin failed: %s", esp_err_to_name(err));
        voice_ui_set_note("连接失败，重试");
        s_chat_active = false;
        set_state(APP_IDLE);
        return;
    }

    // 2) 边录边传（录音零堆分配：chunk 栈上，立即上传）
    int16_t chunk[1024];        // 2KB/块 ≈ 64ms
    size_t total = 0;
    size_t last_note_ms = 0;
    s_capture_stop = false;
    err = ESP_OK;
    while (!s_capture_stop && total < RECORD_MAX_BYTES) {
        err = bsp_audio_read((uint8_t *)chunk, sizeof(chunk));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "audio read failed: %s", esp_err_to_name(err));
            break;
        }
        err = voice_http_chat_stream_write(req, (const uint8_t *)chunk, sizeof(chunk));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "stream write failed: %s", esp_err_to_name(err));
            break;
        }
        total += sizeof(chunk);
        // 实时时长显示（约每秒刷新一次）
        size_t ms = total * 1000 / (VOICE_SAMPLE_RATE * 2);
        if (ms - last_note_ms >= 1000) {
            last_note_ms = ms;
            char note[48];
            snprintf(note, sizeof(note), "聆听中… %lu.%lu 秒",
                     (unsigned long)(ms / 1000), (unsigned long)((ms % 1000) / 100));
            voice_ui_set_note(note);
        }
    }
    if (err != ESP_OK) {
        voice_http_chat_stream_abort(req);
        voice_ui_set_note("录音失败，重试");
        s_chat_active = false;
        set_state(APP_IDLE);
        return;
    }
    if (total < RECORD_MIN_BYTES) {
        voice_http_chat_stream_abort(req);
        voice_ui_set_note("太短了，按住再说一次");
        s_chat_active = false;
        set_state(APP_IDLE);
        return;
    }
    ESP_LOGI(TAG, "captured %u bytes (%lu ms)",
             (unsigned)total, (unsigned long)(total * 1000 / (VOICE_SAMPLE_RATE * 2)));

    // 3) 收尾上传 → 网关处理 → 响应边收边播
    voice_ui_set_note("本机模型思考中…");
    set_state(APP_THINK);
    voice_http_result_t resp = { 0 };
    err = voice_http_chat_stream_finish(req, &resp);
    if (err != ESP_OK) {
        voice_audio_play_stream_cancel();
        voice_audio_play_stream_end();
        voice_ui_set_note("网关无响应，检查电脑");
        s_chat_active = false;
        set_state(APP_IDLE);
        return;
    }

    strncpy(s_asr_text, resp.asr_text, sizeof(s_asr_text) - 1);
    s_asr_text[sizeof(s_asr_text) - 1] = '\0';
    strncpy(s_reply_text, resp.reply_text, sizeof(s_reply_text) - 1);
    s_reply_text[sizeof(s_reply_text) - 1] = '\0';
    ESP_LOGI(TAG, "ASR: %s | Reply: %s", s_asr_text, s_reply_text);

    // 4) 等播放完成（播放器已在 HTTP 响应期间边收边播）
    voice_ui_set_texts(s_asr_text, s_reply_text);
    voice_ui_set_note("播放中…");
    set_state(APP_SPEAK);
    voice_audio_play_stream_end();
    s_chat_active = false;

    char note[80];
    snprintf(note, sizeof(note), "按住 OK 说话 ｜ %s", wifi_sta_status_text());
    voice_ui_set_note(note);
    set_state(APP_IDLE);
}

static void app_task(void *arg)
{
    (void)arg;
    app_key_event_t ev;
    for (;;) {
        if (xQueueReceive(s_key_queue, &ev, portMAX_DELAY) != pdTRUE) continue;

        if (ev.btn != BSP_BTN_OK) continue;

        if (ev.ev == BSP_BTN_PRESS && s_state == APP_IDLE && !s_chat_active) {
            s_chat_active = true;
            if (xTaskCreate(do_chat_wrapper, "voice_chat", 8192, NULL, 4, NULL) != pdPASS) {
                s_chat_active = false;
                voice_ui_set_note("任务创建失败");
            }
        } else if (ev.ev == BSP_BTN_RELEASE && s_state == APP_LISTEN) {
            s_capture_stop = true;  // 松手：录音循环停止，进入发送
        } else if (ev.ev == BSP_BTN_CLICK && s_state == APP_SPEAK) {
            voice_audio_play_stream_cancel();  // 打断播放（HTTP 侧 feed 随即失败退出）
        }
    }
}

static void do_chat_wrapper(void *arg)
{
    (void)arg;
    do_chat();
    vTaskDelete(NULL);
}

// 按键回调运行于 esp_timer 任务：只入队
void voice_app_on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!s_key_queue) return;
    app_key_event_t e = { .btn = btn, .ev = ev };
    (void)xQueueSend(s_key_queue, &e, 0);
}

esp_err_t voice_app_init(void)
{
    ESP_LOGI(TAG, "init: free heap=%u, max block=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    s_key_queue = xQueueCreate(KEY_QUEUE_DEPTH, sizeof(app_key_event_t));
    if (!s_key_queue) return ESP_ERR_NO_MEM;

    if (xTaskCreate(app_task, "voice_app", 4096, NULL, 5, &s_app_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    voice_ui_create();
    voice_ui_set_state(UI_STATE_BOOT);
    voice_ui_set_note("WiFi 连接中…");

    esp_err_t err = voice_audio_start();
    if (err != ESP_OK) {
        voice_ui_set_note("音频初始化失败");
    }
    err = wifi_sta_start();
    if (err != ESP_OK) {
        voice_ui_set_state(UI_STATE_ERROR);
        voice_ui_set_note("WiFi 启动失败");
        return err;
    }

    // 等待 WiFi 就绪（最多 15s），期间 UI 显示连接状态
    for (int i = 0; i < 75; i++) {
        if (wifi_sta_is_connected()) break;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (wifi_sta_is_connected()) {
        char note[96];
        snprintf(note, sizeof(note), "按住 OK 说话 ｜ %s", wifi_sta_status_text());
        voice_ui_set_note(note);
        voice_ui_set_state(UI_STATE_IDLE);
    } else {
        voice_ui_set_state(UI_STATE_ERROR);
        voice_ui_set_note("WiFi 连接失败，检查热点");
    }
    return ESP_OK;
}
