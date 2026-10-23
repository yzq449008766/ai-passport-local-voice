# 语音网关（PC 侧）

FastAPI 单文件网关：**ASR（sherpa-onnx）→ LLM（千问 DashScope qwen3.8-flash，可切本地）→ TTS（edge-tts 晓晓女声）**，支持天气意图注入实时数据、**语音转备忘录**（Todoist / 企微 webhook / 本地文件）。

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

本机 LLM：可选——本地 Qwen3-VL（llama-server `127.0.0.1:8080`，OpenAI 兼容）或**远程千问**。

## 启动

```bash
# LLM key（Windows 用户级环境变量，勿落盘/入库）
[Environment]::SetEnvironmentVariable('QWEN_API_KEY','sk-...','User')
# 启动时显式注入（后台任务不继承新环境变量）
$env:QWEN_API_KEY=[Environment]::GetEnvironmentVariable('QWEN_API_KEY','User')
python gateway.py    # 0.0.0.0:8000
# 或 start-gateway.ps1（后台 + 日志）
```

自检：`http://127.0.0.1:8000/api/health` 返回 `{"status":"ok","llm":true,"llm_provider":"qwen-dashscope","memo":"todoist|wecom|local-file",...}`。

## 端点

| 端点 | 用途 | body / 响应 |
|---|---|---|
| `POST /api/chat` | PC 冒烟 | base64(wav) → JSON(base64 wav + asr + reply) |
| `POST /api/chat_wav` | 整包（兼容） | 原始 wav → wav + `X-Asr`/`X-Reply`(base64 UTF-8) |
| `POST /api/chat_stream` | **真机（对讲机式）** | 裸 PCM 16k/16bit/mono，chunked 边录边传 → wav + 文本头 |
| `GET /api/health` | 探活 | LLM/ASR/TTS/备忘录通道状态 |

## 关键配置（gateway.py 顶部）

| 变量 | 说明 |
|---|---|
| `LLM_URL` / `LLM_MODEL` | 默认千问 DashScope `qwen3.8-flash`（环境变量可覆盖为 DeepSeek/本地） |
| `QWEN_API_KEY` | 千问 key（用户级环境变量） |
| `TODOIST_TOKEN` / `WECOM_WEBHOOK_URL` | 备忘录落点：Todoist / 企微群机器人 webhook（不配则写本地 `todos/`） |
| `REPLY_MAX_CHARS=24` | 回复截断（约 4.5s 语音） |
| `TTS_VOICE` / `TTS_RATE` | 默认 zh-CN-XiaoxiaoNeural（晓晓女声），+30% 语速 |
| `SYSTEM_PROMPT` | 人设（含 `[TODO]` 备忘录指令） |
| `WEATHER_URL` | wttr.in 城市参数（默认大连） |
| `ASR_MODEL_DIR` / `FFMPEG` | 模型目录 / ffmpeg 路径（环境变量覆盖） |

## 语音转备忘录

说"记一下/提醒我/转到待办 XXX" → LLM 输出 `[TODO]内容` → 网关按 **Todoist → 企微 webhook → 本地 `todos/<日期>.md`** 顺序写入，并播报"已记下：…"。

## 端到端自测

```bash
python test_stream.py    # chunked 上传测试音频 → 打印 ASR/回复/音频大小
```
