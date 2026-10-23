# -*- coding: utf-8 -*-
"""
AI Passport 语音对话网关 v1 —— ASR(sherpa-onnx) → LLM(llama-server qwen3vl) → TTS(edge-tts)
启动：python gateway.py  （监听 127.0.0.1:8000，PC 冒烟阶段）
ESP32 接入时：改 --host 0.0.0.0 + 防火墙放行 + 加 token
"""
import base64
import io
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.request
import wave

sys.stdout.reconfigure(encoding="utf-8")

import numpy as np
import sherpa_onnx
import edge_tts
from fastapi import FastAPI, HTTPException, Request, Response
from pydantic import BaseModel

# ---------- 配置 ----------
_BASE = os.path.dirname(os.path.abspath(__file__))
MODEL_DIR = os.environ.get(
    "ASR_MODEL_DIR",
    os.path.join(_BASE, "models", "sherpa-onnx-paraformer-zh-2023-09-14"),
)
# 远程商用 LLM（P0：本地 Qwen3-VL → DeepSeek API；环境变量可覆盖回本地/其他）
LLM_URL = os.environ.get("LLM_URL", "https://api.deepseek.com/v1/chat/completions")
LLM_MODEL = os.environ.get("LLM_MODEL", "deepseek-v4-flash")
LLM_API_KEY = os.environ.get("DEEPSEEK_API_KEY", "")
LLM_TIMEOUT = 60
FFMPEG = os.environ.get("FFMPEG", "ffmpeg")
TTS_VOICE = "zh-CN-YunxiNeural"
TTS_RATE = "+30%"

SYSTEM_PROMPT = (
    "你是戴在于经理胸前的 AI 工牌助理，称呼用户为“于经理”。"
    "回答简短、直接、口语化，不超过 40 个字。"
)

# 多轮会话历史（内存版，单用户冒烟够用；ESP32 阶段按 session_id 分桶）
HISTORY = [{"role": "system", "content": SYSTEM_PROMPT}]

# ---------- ASR ----------
_recognizer = None


def get_recognizer():
    global _recognizer
    if _recognizer is None:
        _recognizer = sherpa_onnx.OfflineRecognizer.from_paraformer(
            paraformer=os.path.join(MODEL_DIR, "model.int8.onnx"),
            tokens=os.path.join(MODEL_DIR, "tokens.txt"),
            num_threads=4,
            sample_rate=16000,
            feature_dim=80,
        )
    return _recognizer


def wav_bytes_to_np(data: bytes):
    """wav bytes(16kHz/16bit/mono) -> (sr, float32 samples)"""
    with wave.open(io.BytesIO(data), "rb") as w:
        sr = w.getframerate()
        raw = w.readframes(w.getnframes())
    samples = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
    return sr, samples


def asr_text(wav_bytes: bytes) -> str:
    sr, samples = wav_bytes_to_np(wav_bytes)
    rec = get_recognizer()
    stream = rec.create_stream()
    stream.accept_waveform(sr, samples)
    rec.decode_stream(stream)
    return stream.result.text.strip()


def asr_raw_pcm(pcm_bytes: bytes) -> str:
    """裸 PCM(16k/16bit/mono, 无 wav 头) 识别 —— 对讲机流式上传用"""
    samples = np.frombuffer(pcm_bytes, dtype=np.int16).astype(np.float32) / 32768.0
    rec = get_recognizer()
    stream = rec.create_stream()
    stream.accept_waveform(16000, samples)
    rec.decode_stream(stream)
    return stream.result.text.strip()


# ---------- LLM ----------
REPLY_MAX_CHARS = 24  # 回复截断（约 4.5s 语音，适配 ESP32 端缓冲）

# 实时天气（免费无 key；大连为默认城市，可改为居住地）
WEATHER_URL = "https://wttr.in/Dalian?format=j1"
WEATHER_KEYWORDS = ("天气", "温度", "气温", "冷不冷", "热不热", "下雨", "下雪", "刮风", "湿度", "穿什么", "出门")


def get_weather() -> str | None:
    try:
        with urllib.request.urlopen(WEATHER_URL, timeout=8) as r:
            d = json.loads(r.read().decode("utf-8"))
        c = d["current_condition"][0]
        desc = c["weatherDesc"][0]["value"]
        temp = c["temp_C"]
        feels = c.get("FeelsLikeC", temp)
        hum = c.get("humidity", "?")
        return f"大连当前 {temp}°C（体感 {feels}°C），{desc}，湿度 {hum}%"
    except Exception as e:
        print(f"[weather] fetch failed: {e}")
        return None


def is_weather_question(text: str) -> bool:
    return any(k in text for k in WEATHER_KEYWORDS)


def llm_chat(messages) -> str:
    body = json.dumps({"model": LLM_MODEL, "messages": messages, "max_tokens": 100, "temperature": 0.8}, ensure_ascii=False).encode("utf-8")
    headers = {"Content-Type": "application/json"}
    if LLM_API_KEY:
        headers["Authorization"] = "Bearer " + LLM_API_KEY
    req = urllib.request.Request(
        LLM_URL,
        data=body,
        headers=headers,
    )
    with urllib.request.urlopen(req, timeout=LLM_TIMEOUT) as r:
        data = json.loads(r.read().decode("utf-8"))
    reply = data["choices"][0]["message"]["content"].strip()
    # 人设要求 ≤40 字；超长时按字符边界截断（Python str 切片安全，不会劈开 UTF-8）
    if len(reply) > REPLY_MAX_CHARS:
        reply = reply[:REPLY_MAX_CHARS] + "…"
    return reply


# ---------- TTS ----------
async def tts_wav(text: str, sample_rate: int = 22050) -> bytes:
    """edge-tts 中文合成 -> ffmpeg 转指定采样率/16bit/mono wav bytes"""
    fd, tmp_mp3 = tempfile.mkstemp(suffix=".mp3")
    os.close(fd)
    tmp_wav = tmp_mp3[:-4] + ".wav"
    try:
        c = edge_tts.Communicate(text, TTS_VOICE, rate=TTS_RATE)
        await c.save(tmp_mp3)
        subprocess.run(
            [FFMPEG, "-y", "-loglevel", "error", "-i", tmp_mp3, "-ar", str(sample_rate), "-ac", "1", tmp_wav],
            check=True,
        )
        with open(tmp_wav, "rb") as f:
            return f.read()
    finally:
        for p in (tmp_mp3, tmp_wav):
            try:
                os.remove(p)
            except OSError:
                pass


# ---------- API ----------
app = FastAPI(title="AI Passport Voice Gateway", version="1.0")


class ChatReq(BaseModel):
    audio: str  # base64(wav 16k/16bit/mono)
    session_id: str | None = None


class ChatResp(BaseModel):
    asr_text: str
    reply_text: str
    audio: str  # base64(wav 22.05k/16bit/mono)
    latency_ms: int


@app.get("/api/health")
async def health():
    return {"status": "ok", "llm": bool(LLM_API_KEY), "llm_provider": "deepseek" if LLM_API_KEY else "local", "asr": True, "tts": True}


# ---------- 统一对话流水线 ----------
async def process_voice(text: str) -> tuple[str, bytes]:
    """ASR 文本 → LLM（天气意图自动注入实时数据）→ TTS。返回 (reply, wav16k)"""
    HISTORY.append({"role": "user", "content": text})
    msgs = HISTORY[-8:]
    if is_weather_question(text):
        w = get_weather()
        if w:
            msgs = msgs + [{"role": "system",
                            "content": f"[实时数据] {w} —— 据此播报天气，一句话不超过 24 个字，俏皮自然。"}]
    reply = llm_chat(msgs)
    HISTORY.append({"role": "assistant", "content": reply})
    audio = await tts_wav(reply, sample_rate=16000)
    return reply, audio


@app.post("/api/chat")
async def chat(req: ChatReq):
    t0 = time.time()
    try:
        wav_bytes = base64.b64decode(req.audio)
    except Exception:
        raise HTTPException(status_code=400, detail="audio base64 解码失败")

    # 1) ASR
    try:
        text = asr_text(wav_bytes)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"ASR 失败: {e}")
    if not text:
        raise HTTPException(status_code=422, detail="未识别到语音，请再试一次")

    # 2) LLM（含天气注入）+ 3) TTS
    try:
        reply, audio = await process_voice(text)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"LLM/TTS 失败: {e}")

    latency_ms = int((time.time() - t0) * 1000)
    return ChatResp(
        asr_text=text,
        reply_text=reply,
        audio=base64.b64encode(audio).decode("ascii"),
        latency_ms=latency_ms,
    )


@app.post("/api/chat_wav")
async def chat_wav(request: Request):
    """ESP32 整包端点：body = 原始 wav(16k/16bit/mono)；响应 = wav(16k) + X-Asr/X-Reply(base64)"""
    t0 = time.time()
    wav_bytes = await request.body()
    if not wav_bytes:
        raise HTTPException(status_code=400, detail="empty body")

    try:
        text = asr_text(wav_bytes)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"ASR 失败: {e}")
    if not text:
        raise HTTPException(status_code=422, detail="未识别到语音")

    try:
        reply, audio = await process_voice(text)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"LLM/TTS 失败: {e}")

    headers = {
        "X-Asr": base64.b64encode(text.encode("utf-8")).decode("ascii"),
        "X-Reply": base64.b64encode(reply.encode("utf-8")).decode("ascii"),
        "X-Latency-Ms": str(int((time.time() - t0) * 1000)),
    }
    return Response(content=audio, media_type="audio/wav", headers=headers)


@app.post("/api/chat_stream")
async def chat_stream(request: Request):
    """ESP32 对讲机式端点：body = 裸 PCM(16k/16bit/mono, chunked 流式上传，时长不限)；
    响应 = wav(16k) + X-Asr/X-Reply(base64 UTF-8)"""
    t0 = time.time()
    body = await request.body()
    if len(body) < 4000:  # < 0.125s
        raise HTTPException(status_code=422, detail="音频太短")

    try:
        text = asr_raw_pcm(body)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"ASR 失败: {e}")
    if not text:
        raise HTTPException(status_code=422, detail="未识别到语音")

    try:
        reply, audio = await process_voice(text)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"LLM/TTS 失败: {e}")

    headers = {
        "X-Asr": base64.b64encode(text.encode("utf-8")).decode("ascii"),
        "X-Reply": base64.b64encode(reply.encode("utf-8")).decode("ascii"),
        "X-Latency-Ms": str(int((time.time() - t0) * 1000)),
    }
    return Response(content=audio, media_type="audio/wav", headers=headers)


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(app, host="0.0.0.0", port=8000, log_level="info")
