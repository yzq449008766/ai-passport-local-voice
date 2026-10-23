# 本地语音网关（PC 侧）

FastAPI 单文件网关：**ASR（sherpa-onnx）→ LLM（本地 Qwen3-VL）→ TTS（edge-tts）**，并支持天气意图自动注入实时数据。

## 依赖

```bash
pip install -r requirements.txt
```

模型下载（约 232MB）：
```bash
# sherpa-onnx paraformer 中文 int8
# 官方 release: https://github.com/k2-fsa/sherpa-onnx/releases
# 解压到 models/sherpa-onnx-paraformer-zh-2023-09-14/（model.int8.onnx + tokens.txt）
```

本机 LLM：llama-server 起 Qwen3-VL-8B Q4，监听 `127.0.0.1:8080`（OpenAI 兼容）。

## 启动

```bash
python gateway.py    # 0.0.0.0:8000
# 或 start-gateway.ps1（后台 + 日志）
```

自检：`http://127.0.0.1:8000/api/health` 返回 `{"status":"ok","llm":true,...}`。

## 端点

| 端点 | 用途 | body / 响应 |
|---|---|---|
| `POST /api/chat` | PC 冒烟 | base64(wav) → JSON(base64 wav + asr + reply) |
| `POST /api/chat_wav` | 整包（兼容） | 原始 wav → wav + `X-Asr`/`X-Reply`(base64 UTF-8) |
| `POST /api/chat_stream` | **真机（对讲机式）** | 裸 PCM 16k/16bit/mono，chunked 边录边传 → wav + 文本头 |
| `GET /api/health` | 探活 | LLM/ASR/TTS 状态 |

## 关键配置（gateway.py 顶部）

| 变量 | 说明 |
|---|---|
| `LLM_URL` / `LLM_MODEL` | 本地大模型地址（可用环境变量覆盖） |
| `REPLY_MAX_CHARS=24` | 回复截断（约 4.5s 语音） |
| `TTS_VOICE` / `TTS_RATE` | zh-CN-YunxiNeural，+30% 语速 |
| `SYSTEM_PROMPT` | 人设（可改成你想要的 AI 分身） |
| `WEATHER_URL` | wttr.in 城市参数（默认大连） |
| `ASR_MODEL_DIR` / `FFMPEG` | 模型目录 / ffmpeg 路径（环境变量覆盖） |

## 端到端自测

```bash
python test_stream.py    # chunked 上传测试音频 → 打印 ASR/回复/音频大小
```
