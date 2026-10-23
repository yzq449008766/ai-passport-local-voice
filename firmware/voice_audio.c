// voice_audio.c —— 录音/播放 worker 任务
#include "voice_audio.h"

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "voice_audio";

enum {
    CMD_RECORD_START = 1,
    CMD_RECORD_STOP,
    CMD_PLAY,
    CMD_CANCEL,
};

typedef struct {
    int16_t *buf;
    size_t max_bytes;
    size_t *out_bytes;
    const int16_t *play_pcm;
    size_t play_bytes;
} cmd_payload_t;

static TaskHandle_t s_task;
static SemaphoreHandle_t s_ready;   // 命令完成信号
static volatile bool s_cancel;

static esp_err_t do_record(cmd_payload_t *p)
{
    size_t got = 0;
    while (got < p->max_bytes && !s_cancel) {
        size_t n = (p->max_bytes - got) < VOICE_CHUNK_BYTES
                       ? (p->max_bytes - got) : VOICE_CHUNK_BYTES;
        esp_err_t err = bsp_audio_read((uint8_t *)p->buf + got, n);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "audio read failed: %s", esp_err_to_name(err));
            if (p->out_bytes) *p->out_bytes = got;
            return err;
        }
        got += n;
    }
    if (p->out_bytes) *p->out_bytes = got;
    return ESP_OK;
}

static esp_err_t do_play(cmd_payload_t *p)
{
    size_t played = 0;
    while (played < p->play_bytes && !s_cancel) {
        size_t n = (p->play_bytes - played) < VOICE_CHUNK_BYTES
                       ? (p->play_bytes - played) : VOICE_CHUNK_BYTES;
        esp_err_t err = bsp_audio_write((const uint8_t *)p->play_pcm + played, n);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "audio write failed: %s", esp_err_to_name(err));
            return err;
        }
        played += n;
    }
    return ESP_OK;
}

static void audio_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t value = 0;
        if (xTaskNotifyWait(0, UINT32_MAX, &value, portMAX_DELAY) != pdTRUE) continue;
        cmd_payload_t *cmd = (cmd_payload_t *)value;
        if (!cmd || (cmd->buf == NULL && cmd->play_pcm == NULL)) continue;
        if (s_cancel) {
            if (cmd->out_bytes) *cmd->out_bytes = 0;
            xSemaphoreGive(s_ready);
            continue;
        }
        if (cmd->play_pcm) {
            do_play(cmd);
        } else {
            do_record(cmd);
        }
        xSemaphoreGive(s_ready);
    }
}

esp_err_t voice_audio_start(void)
{
    if (s_task) return ESP_OK;

    esp_err_t err = bsp_audio_set_format(VOICE_SAMPLE_RATE, 16, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set format failed: %s", esp_err_to_name(err));
        return err;
    }
    bsp_audio_set_volume(85);

    if (!s_ready) s_ready = xSemaphoreCreateBinary();
    if (!s_ready) return ESP_ERR_NO_MEM;

    s_cancel = false;
    if (xTaskCreate(audio_task, "voice_audio", 4096, NULL, 5, &s_task) != pdPASS) {
        vSemaphoreDelete(s_ready);
        s_ready = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t voice_audio_record_start(int16_t *buf, size_t max_bytes, size_t *out_bytes)
{
    if (!buf || !s_task) return ESP_ERR_INVALID_STATE;
    cmd_payload_t cmd = { .buf = buf, .max_bytes = max_bytes, .out_bytes = out_bytes };
    s_cancel = false;
    xSemaphoreTake(s_ready, 0);
    xTaskNotify(s_task, (uint32_t)&cmd, eSetValueWithOverwrite);
    if (xSemaphoreTake(s_ready, pdMS_TO_TICKS(60000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    return ESP_OK;
}

esp_err_t voice_audio_stop_recording(void)
{
    // 录音循环以 chunk 为粒度，设置 cancel 即可在下一个 chunk 前停止。
    s_cancel = true;
    return ESP_OK;
}

esp_err_t voice_audio_play(const int16_t *pcm, size_t bytes)
{
    if (!pcm || !s_task) return ESP_ERR_INVALID_STATE;
    cmd_payload_t cmd = { .play_pcm = pcm, .play_bytes = bytes };
    s_cancel = false;
    xSemaphoreTake(s_ready, 0);
    xTaskNotify(s_task, (uint32_t)&cmd, eSetValueWithOverwrite);
    if (xSemaphoreTake(s_ready, pdMS_TO_TICKS(60000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    return ESP_OK;
}

void voice_audio_cancel(void)
{
    s_cancel = true;
}

// ---------------- 流式播放 ----------------
// 独立播放任务 + 块队列：HTTP 回调把 PCM 块喂进队列，播放任务实时写 I2S。
// 队列容量小 + feed 阻塞入队，形成反压：下载速度不会超过播放速度，内存占用≈队列容量。

#define STREAM_Q_DEPTH 4
#define STREAM_Q_SLOT (4 * 1024)   // 每块最大字节（配合 esp_http_client buffer_size）

typedef struct {
    uint8_t *data;
    size_t len;
} play_chunk_t;

static QueueHandle_t s_play_q;
static TaskHandle_t s_play_task;
static volatile bool s_play_cancel;

static void play_stream_task(void *arg)
{
    (void)arg;
    play_chunk_t c;
    for (;;) {
        if (xQueueReceive(s_play_q, &c, portMAX_DELAY) != pdTRUE) continue;
        if (c.data == NULL) {          // 结束哨兵
            s_play_task = NULL;
            vTaskDelete(NULL);
            return;
        }
        size_t off = 0;
        while (off < c.len && !s_play_cancel) {
            size_t n = (c.len - off) < VOICE_CHUNK_BYTES
                           ? (c.len - off) : VOICE_CHUNK_BYTES;
            esp_err_t err = bsp_audio_write(c.data + off, n);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "stream write failed: %s", esp_err_to_name(err));
                break;
            }
            off += n;
        }
        free(c.data);
        if (s_play_cancel) {           // 打断：丢弃队列并退出
            while (xQueueReceive(s_play_q, &c, 0) == pdTRUE) {
                if (c.data) free(c.data);
            }
            s_play_task = NULL;
            vTaskDelete(NULL);
            return;
        }
    }
}

esp_err_t voice_audio_play_stream_start(void)
{
    if (s_play_q) return ESP_OK;       // 已启动
    s_play_q = xQueueCreate(STREAM_Q_DEPTH, sizeof(play_chunk_t));
    if (!s_play_q) return ESP_ERR_NO_MEM;
    s_play_cancel = false;
    if (xTaskCreate(play_stream_task, "voice_play", 4096, NULL, 6, &s_play_task) != pdPASS) {
        vQueueDelete(s_play_q);
        s_play_q = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t voice_audio_play_stream_feed(const uint8_t *pcm, size_t bytes)
{
    if (!s_play_q || bytes == 0) return ESP_ERR_INVALID_STATE;
    if (s_play_cancel) return ESP_FAIL;
    uint8_t *buf = malloc(bytes);
    if (!buf) return ESP_ERR_NO_MEM;
    memcpy(buf, pcm, bytes);
    play_chunk_t c = { buf, bytes };
    if (xQueueSend(s_play_q, &c, portMAX_DELAY) != pdTRUE) {
        free(buf);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t voice_audio_play_stream_end(void)
{
    if (!s_play_q) return ESP_ERR_INVALID_STATE;
    play_chunk_t end = { NULL, 0 };
    // 等队列能入队（播放器消费完所有块）后放结束哨兵，再等任务退出
    if (xQueueSend(s_play_q, &end, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    while (s_play_task != NULL) vTaskDelay(pdMS_TO_TICKS(10));
    vQueueDelete(s_play_q);
    s_play_q = NULL;
    s_play_cancel = false;
    return ESP_OK;
}

void voice_audio_play_stream_cancel(void)
{
    s_play_cancel = true;
}
