# AI Passport · 本地语音对话（对讲机式）

> 把 **FoloToy AI Passport**（ESP32-C3，无 PSRAM）改造成一台**完全离线、本地推理**的语音对讲机：
> 按住 OK 说话 → 设备经 WiFi **边录边传**到本机网关 →（sherpa-onnx ASR → 本地 Qwen3-VL → edge-tts TTS）→ 设备**边下边播** + 屏幕实时显示状态与文本。

![链路](docs/ARCHITECTURE.md)

一句话：**一碰就开口、完全离线、本地跑**。问天气还会真报天气（网关自动注入实时数据）。

---

## 为什么值得做

- **对讲机式体验**：按住 OK 想讲多久讲多久（15s 防呆），松手即发；回复实时播放，随时可短按打断。
- **无 PSRAM 也能跑**：两条流式设计（录音 chunked 边录边传、回复边收边播）让内存峰值降到 ≈20KB，彻底告别"内存不足"。
- **本地大模型**：Qwen3-VL-8B 跑在你自己的电脑上，数据不出局域网；零 API 费用、零云端依赖（TTS 除外，edge-tts 需外网）。
- **中文显示完整**：全量思源黑体（2 万+汉字），不再缺字方块。
- **真天气**：网关检测天气意图 → 拉取免费实时天气 → 俏皮播报。

## 硬件

| 项目 | 说明 |
|---|---|
| 设备 | FoloToy AI Passport（官方开源，MIT）：ESP32-C3 QFN32 · 8MB Flash · **无 PSRAM** · 240×320 TFT · 原生 USB-Serial/JTAG |
| 电脑 | 本机网关运行处，需 2.4GHz WiFi 局域网可达 |
| 本地 LLM | llama-server 跑 Qwen3-VL-8B（Q4），OpenAI 兼容 `/v1/chat/completions` |

## 仓库结构

```
├── gateway/          PC 侧语音网关（FastAPI：ASR→LLM→TTS + 天气注入）
├── firmware/         固件增量（覆盖原仓库 main/ 的文件，含全量中文字体方案）
├── tools/            构建 / 合并 / 刷机脚本
└── docs/             架构说明、踩坑记录
```

## 快速开始（3 步）

### 0. 准备
- 克隆并构建 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport)（ESP-IDF **v5.5.3**，`idf.py set-target esp32c3`），确认原 demo 可刷可跑。
- PC 装 Python 3.12。

### 1. 部署网关（PC 侧）
```bash
cd gateway
pip install -r requirements.txt
# 模型：sherpa-onnx paraformer 中文 int8（232MB），见 gateway/README.md
python gateway.py          # 监听 0.0.0.0:8000
```
自检：`Invoke-RestMethod http://127.0.0.1:8000/api/health`（三层全绿）。

### 2. 部署固件（设备侧）
1. 把 `firmware/` 下文件复制到 `ai-passport/main/` 覆盖（保留 demo 无妨，app_main 已切到语音应用）。
2. 生成全量中文字体并放入 `main/fonts/`（见 `firmware/fonts/README.md`；产物约 17MB C 源，不入库）。
3. 修改 `main/kconfig.projbuild` 里的 `YOUR_WIFI_SSID / YOUR_WIFI_PASSWORD / 192.168.1.100`（网关电脑的局域网 IP）。
4. 构建 + 合并 + 刷机：
```bash
idf.py build
idf.py merge-bin          # 产出 build/merged-binary.bin
# 复制为 FoloToy-AI-Passport-full.bin，从 0x0 整体刷入（含 bootloader/分区表）
python -m esptool --chip esp32c3 -p COM5 -b 460800 write_flash \
  --flash_mode dio --flash_size 8MB --flash_freq 80m 0x0 build/FoloToy-AI-Passport-full.bin
```
5. 开机 → 屏幕显示 WiFi 已连 → **按住 OK 说话，松手，听回复**。

### 3. 玩
- 问天气：「今天天气咋样？」→ 播报大连实时天气（改 `gateway.py` 的 `WEATHER_URL` 换城市）。
- 长句子：「按住 OK 讲一段完整的话，松手发送」。
- 打断：播放时短按 OK。

## 配置速查

| 项 | 位置 | 说明 |
|---|---|---|
| WiFi / 网关地址 / 录音上限 | `firmware/kconfig.projbuild` | 编译时配置 |
| 网关地址 | 同上 `AIPASSPORT_GATEWAY_HOST` | 电脑局域网 IP（勿用虚拟网卡地址） |
| 回复长度 / 人设 / 天气城市 | `gateway/gateway.py` | `REPLY_MAX_CHARS`、`SYSTEM_PROMPT`、`WEATHER_URL` |

## 已知边界

- edge-tts 合成需外网；完全断网时可用本地 TTS 替换（见 docs/PITFALLS.md §TTS）。
- ESP32-C3 仅 2.4GHz WiFi。
- 天气为"意图关键词 + 免费源"，非万能工具调用。
- 整包兼容路径（`voice_http_chat`）保留给 PC 冒烟，真机走流式。

## 致谢 / License

- 基于 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport)（MIT）二次开发，BSP 与构建框架均来自原项目。
- 本仓库代码：MIT（见 [LICENSE](LICENSE)）。派生代码保留原项目版权声明。
- ASR：[sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx)（Apache-2.0）；TTS：[edge-tts](https://github.com/rany2/edge-tts)；天气：[wttr.in](https://wttr.in)。

## 实战记录

完整踩坑（无 PSRAM 内存、chunked 协议、中文字体、IDF 版本差异、刷机流程）见 [docs/PITFALLS.md](docs/PITFALLS.md)。
