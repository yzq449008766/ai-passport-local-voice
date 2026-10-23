// voice_http.c —— esp_http_client 调网关 /api/chat_wav（事件回调收集响应 body）
#include "voice_http.h"

#include "voice_audio.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "mbedtls/base64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "voice_http";

typedef struct {
    uint8_t *buf;
    size_t len;
    size_t cap;
    bool failed;
} resp_buf_t;

// 流式上传请求句柄（chunked POST /api/chat_stream）
struct voice_stream_req {
    esp_http_client_handle_t client;
};

static void resp_buf_append(resp_buf_t *rb, const char *data, int len)
{
    if (rb->failed) return;
    if (rb->len + (size_t)len > rb->cap) {
        size_t new_cap = rb->cap ? rb->cap * 2 : 4096;
        while (new_cap < rb->len + (size_t)len) new_cap *= 2;
        uint8_t *nb = realloc(rb->buf, new_cap);
        if (!nb) {
            rb->failed = true;
            return;
        }
        rb->buf = nb;
        rb->cap = new_cap;
    }
    memcpy(rb->buf + rb->len, data, (size_t)len);
    rb->len += (size_t)len;
}

static int decode_b64_header(const char *b64, char *out, size_t out_size)
{
    if (!b64 || !*b64) return -1;
    size_t olen = 0;
    unsigned char tmp[512];
    int rc = mbedtls_base64_decode(tmp, sizeof(tmp), &olen,
                                   (const unsigned char *)b64, strlen(b64));
    if (rc != 0) return -1;
    size_t n = olen < out_size - 1 ? olen : out_size - 1;
    memcpy(out, tmp, n);
    out[n] = '\0';
    return 0;
}

static esp_err_t on_data(esp_http_client_event_t *ev)
{
    resp_buf_t *rb = (resp_buf_t *)ev->user_data;
    if (ev->data_len > 0) {
        resp_buf_append(rb, (const char *)ev->data, ev->data_len);
    }
    return ESP_OK;
}

esp_err_t voice_http_chat(const uint8_t *wav, size_t wav_len, voice_http_result_t *result)
{
    if (!wav || !result) return ESP_ERR_INVALID_ARG;
    memset(result, 0, sizeof(*result));

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/api/chat_wav",
             CONFIG_AIPASSPORT_GATEWAY_HOST, CONFIG_AIPASSPORT_GATEWAY_PORT);

    resp_buf_t rb = { 0 };
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000,
        .buffer_size = 4096,
        .user_data = &rb,
        .event_handler = on_data,
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "Content-Type", "audio/wav");
    esp_err_t err = esp_http_client_set_post_field(client, (const char *)wav, wav_len);
    if (err != ESP_OK) goto done;

    err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP perform failed: %s", esp_err_to_name(err));
        goto done;
    }

    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "Gateway status %d", status);
        err = ESP_FAIL;
        goto done;
    }

    char *asr_b64 = NULL;
    char *reply_b64 = NULL;
    esp_http_client_get_header(client, "X-Asr", &asr_b64);
    esp_http_client_get_header(client, "X-Reply", &reply_b64);
    if (asr_b64) decode_b64_header(asr_b64, result->asr_text, sizeof(result->asr_text));
    if (reply_b64) decode_b64_header(reply_b64, result->reply_text, sizeof(result->reply_text));

    if (rb.failed || rb.len == 0) {
        ESP_LOGE(TAG, "Reply body empty/failed");
        err = ESP_FAIL;
        goto done;
    }
    if (rb.len > VOICE_HTTP_MAX_REPLY_BYTES) {
        ESP_LOGE(TAG, "Reply too large: %u", (unsigned)rb.len);
        err = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    result->reply_wav = rb.buf;
    result->reply_wav_len = rb.len;
    rb.buf = NULL;  // 所有权移交
    err = ESP_OK;

done:
    if (rb.buf) free(rb.buf);
    esp_http_client_cleanup(client);
    if (err != ESP_OK && result->reply_wav) {
        free(result->reply_wav);
        result->reply_wav = NULL;
        result->reply_wav_len = 0;
    }
    return err;
}

void voice_http_free_result(voice_http_result_t *result)
{
    if (result && result->reply_wav) {
        free(result->reply_wav);
        result->reply_wav = NULL;
        result->reply_wav_len = 0;
    }
}

esp_err_t voice_http_chat_stream_begin(voice_stream_req_t **out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/api/chat_stream",
             CONFIG_AIPASSPORT_GATEWAY_HOST, CONFIG_AIPASSPORT_GATEWAY_PORT);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 120000,
        .buffer_size = 2048,
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    // write_len = -1 → Transfer-Encoding: chunked（边录边传，长度未知）
    esp_err_t err = esp_http_client_open(client, -1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    voice_stream_req_t *req = calloc(1, sizeof(*req));
    if (!req) {
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    req->client = client;
    *out = req;
    return ESP_OK;
}

esp_err_t voice_http_chat_stream_write(voice_stream_req_t *req, const uint8_t *pcm, size_t bytes)
{
    if (!req || !req->client || bytes == 0) return ESP_ERR_INVALID_STATE;
    // esp_http_client 声明了 Transfer-Encoding: chunked，但不会自动编码：
    // 必须由调用方按 chunked 格式发送（size\r\n + data + \r\n）。
    char hdr[16];
    int hlen = snprintf(hdr, sizeof(hdr), "%x\r\n", (unsigned)bytes);
    int wrote = esp_http_client_write(req->client, hdr, hlen);
    if (wrote != hlen) {
        ESP_LOGE(TAG, "stream write chunk hdr failed: %d/%d", wrote, hlen);
        return ESP_FAIL;
    }
    wrote = esp_http_client_write(req->client, (const char *)pcm, (int)bytes);
    if (wrote != (int)bytes) {
        ESP_LOGE(TAG, "stream write chunk data failed: %d/%u", wrote, (unsigned)bytes);
        return ESP_FAIL;
    }
    wrote = esp_http_client_write(req->client, "\r\n", 2);
    if (wrote != 2) {
        ESP_LOGE(TAG, "stream write chunk trailer failed: %d", wrote);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t voice_http_chat_stream_finish(voice_stream_req_t *req, voice_http_result_t *result)
{
    if (!req || !req->client || !result) return ESP_ERR_INVALID_STATE;
    memset(result, 0, sizeof(*result));

    esp_err_t err = ESP_OK;
    // 结束 chunked body（0 长度终止块），网关才开始处理并回响应
    if (esp_http_client_write(req->client, "0\r\n\r\n", 5) != 5) {
        ESP_LOGE(TAG, "chunk terminator write failed");
        err = ESP_FAIL;
        goto done;
    }
    // 阻塞等待网关处理完（ASR→LLM→TTS）返回响应头
    int hlen = esp_http_client_fetch_headers(req->client);
    if (hlen < 0) {
        ESP_LOGE(TAG, "fetch headers failed");
        err = ESP_FAIL;
        goto done;
    }
    int status = esp_http_client_get_status_code(req->client);
    if (status != 200) {
        ESP_LOGE(TAG, "Gateway status %d", status);
        err = ESP_FAIL;
        goto done;
    }

    char *asr_b64 = NULL;
    char *reply_b64 = NULL;
    esp_http_client_get_header(req->client, "X-Asr", &asr_b64);
    esp_http_client_get_header(req->client, "X-Reply", &reply_b64);
    if (asr_b64) decode_b64_header(asr_b64, result->asr_text, sizeof(result->asr_text));
    if (reply_b64) decode_b64_header(reply_b64, result->reply_text, sizeof(result->reply_text));

    // 流式读回响应体：剥 RIFF 头 → 喂流式播放器（边收边播，不缓存）
    voice_audio_play_stream_start();
    uint8_t buf[2048];
    size_t header_remain = 44;
    int n;
    while ((n = esp_http_client_read(req->client, (char *)buf, sizeof(buf))) > 0) {
        const uint8_t *data = buf;
        int len = n;
        if (header_remain > 0) {
            size_t skip = header_remain < (size_t)len ? header_remain : (size_t)len;
            data += skip;
            len -= (int)skip;
            header_remain -= skip;
        }
        if (len > 0) {
            esp_err_t e = voice_audio_play_stream_feed(data, (size_t)len);
            if (e != ESP_OK) {
                // 播放被打断（CLICK）或异常：停止读取，视为正常结束
                ESP_LOGW(TAG, "feed interrupted: %s", esp_err_to_name(e));
                break;
            }
        }
    }
    err = ESP_OK;

done:
    esp_http_client_cleanup(req->client);
    req->client = NULL;
    free(req);
    return err;
}

void voice_http_chat_stream_abort(voice_stream_req_t *req)
{
    if (!req) return;
    if (req->client) {
        esp_http_client_close(req->client);
        esp_http_client_cleanup(req->client);
        req->client = NULL;
    }
    free(req);
}
