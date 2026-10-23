# -*- coding: utf-8 -*-
"""端到端自测：chunked 上传测试音频 → 打印 ASR/回复/音频大小（真机同协议）"""
import base64
import http.client
import io
import wave

WAV = r"<你的测试 wav 路径，如 models/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav>"
HOST, PORT = "127.0.0.1", 8000


def main():
    with wave.open(WAV, "rb") as w:
        pcm = w.readframes(w.getnframes())
    pcm = pcm[:160000]  # 5s（按住 5 秒的场景）
    conn = http.client.HTTPConnection(HOST, PORT, timeout=120)
    conn.request(
        "POST", "/api/chat_stream",
        body=io.BytesIO(pcm),
        headers={"Content-Type": "application/octet-stream",
                 "Transfer-Encoding": "chunked"},
        encode_chunked=True,
    )
    r = conn.getresponse()
    print("status:", r.status, r.reason)
    print("ASR  :", base64.b64decode(r.getheader("X-Asr", "")).decode("utf-8"))
    print("Reply:", base64.b64decode(r.getheader("X-Reply", "")).decode("utf-8"))
    print("audio bytes:", len(r.read()))
    conn.close()


if __name__ == "__main__":
    main()
