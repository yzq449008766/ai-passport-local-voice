// voice_audio.h —— 录音/播放 worker（bsp_audio + 独立任务，仿 demo_audio 模式）
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#define VOICE_SAMPLE_RATE 16000
#define VOICE_CHUNK_BYTES 1024  // 512 samples x 2B

// 启动音频 worker（创建任务 + 初始化 codec 格式）。可重复调用。
esp_err_t voice_audio_start(void);

// 开始录音到 buf（PCM int16），最多 max_ms 毫秒或直到 voice_audio_stop_recording()。
// 返回前会尽量录满；可通过 out_bytes 拿到实际字节数。
esp_err_t voice_audio_record_start(int16_t *buf, size_t max_bytes, size_t *out_bytes);

// 主动停止录音（录音循环内被调用；通常在按键 RELEASE 时从外部调用）。
// 说明：bsp_audio_read 分块阻塞，最长一个 chunk 的延迟后生效。
esp_err_t voice_audio_stop_recording(void);

// 播放 PCM（阻塞直到播完或 cancel）。bytes 为字节数。
esp_err_t voice_audio_play(const int16_t *pcm, size_t bytes);

// ---- 流式播放（边收边播，避免整包缓存，省内存）----

// 启动流式播放器（创建播放任务 + 块队列）。
esp_err_t voice_audio_play_stream_start(void);

// 喂入一段 PCM 播放（阻塞入队：播放器消费完才返回，天然限流）。
// 必须在 play_stream_start() 之后、play_stream_end() 之前调用。
esp_err_t voice_audio_play_stream_feed(const uint8_t *pcm, size_t bytes);

// 结束流式播放：入队结束哨兵并等待全部播完（阻塞）。
esp_err_t voice_audio_play_stream_end(void);

// 打断流式播放：播放器尽快退出，队列清空。
void voice_audio_play_stream_cancel(void);

// 中断当前播放/录音（例如开始新对话）。
void voice_audio_cancel(void);
