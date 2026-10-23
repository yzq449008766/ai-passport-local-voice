// voice_http.h —— 调用本机语音网关 /api/chat_wav（body=wav，响应=wav+base64文本头）
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

// 整包模式（voice_http_chat）下的回复 wav 上限；流式模式不受此限制。
#define VOICE_HTTP_MAX_REPLY_BYTES (110 * 1024)  // 整包模式兜底（约 3.4s）

typedef struct {
    char asr_text[256];      // 识别文本（UTF-8）
    char reply_text[384];    // 模型回复（UTF-8）
    uint8_t *reply_wav;      // 整包模式：回复音频（wav 16k/16bit/mono，含 RIFF 头）
    size_t reply_wav_len;    // 流式模式：均为 0/NULL
} voice_http_result_t;

// 整包模式：上传录音 wav，等待完整回复音频（用于 PC 冒烟/兼容路径）。
esp_err_t voice_http_chat(const uint8_t *wav, size_t wav_len, voice_http_result_t *result);

// 流式上传对话（对讲机式：边录边传，录音时长不限）。
// 顺序：begin → (write × N) → finish；录音太短/出错时 abort。
typedef struct voice_stream_req voice_stream_req_t;

// 打开到 /api/chat_stream 的 chunked 上传连接（裸 PCM 16k/16bit/mono）。录音开始前调用。
esp_err_t voice_http_chat_stream_begin(voice_stream_req_t **out);

// 上传一段裸 PCM。录音循环中反复调用；失败返回非 OK（连接断开等）。
esp_err_t voice_http_chat_stream_write(voice_stream_req_t *req, const uint8_t *pcm, size_t bytes);

// 结束上传并等待完整链路：网关 ASR→LLM→TTS 后，响应音频边收边喂流式播放器。
// 返回 ESP_OK 表示链路成功；播放收尾由调用方调 voice_audio_play_stream_end()。
esp_err_t voice_http_chat_stream_finish(voice_stream_req_t *req, voice_http_result_t *result);

// 中止（录音太短/出错时调用，释放连接）。
void voice_http_chat_stream_abort(voice_stream_req_t *req);

void voice_http_free_result(voice_http_result_t *result);
