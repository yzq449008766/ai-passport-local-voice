# 固件增量（ESP32-C3）

本目录是相对 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport) `main/` 的**增量覆盖**：复制过去即组成语音对话应用。

## 部署步骤

1. 克隆原仓库并构建（ESP-IDF v5.5.3）：
   ```bash
   git clone --depth 1 https://github.com/FoloToy/ai-passport.git
   cd ai-passport
   idf.py set-target esp32c3 && idf.py build   # 先确认原 demo 正常
   ```

2. 覆盖本目录文件到 `main/`：
   ```
   firmware/*.c *.h CMakeLists.txt kconfig.projbuild sdkconfig.defaults partitions.csv
        ↓ 复制到
   ai-passport/main/
   ```
   建议建 feature 分支再改：
   ```bash
   git checkout -b feature/local-voice-chat
   ```

3. 生成全量中文字体 → `main/fonts/lv_font_custom_16.c`（见 `fonts/README.md`）。

4. 填写 `main/kconfig.projbuild` 的 WiFi / 网关地址（3 个占位）。

5. 构建、合并、刷机：
   ```bash
   idf.py build
   idf.py merge-bin
   # 产物 build/merged-binary.bin → 复制为 FoloToy-AI-Passport-full.bin，从 0x0 整体刷入
   python -m esptool --chip esp32c3 -p COM5 -b 460800 write_flash \
     --flash_mode dio --flash_size 8MB --flash_freq 80m \
     0x0 build/FoloToy-AI-Passport-full.bin
   ```

## 文件职责

| 文件 | 职责 |
|---|---|
| `app_main.c` | BSP 初始化 → 挂按键回调 → 启动语音应用 |
| `voice_app.c/h` | 状态机 IDLE→LISTEN→THINK→SPEAK；OK 按住边录边传、松开即发、CLICK 打断 |
| `voice_audio.c/h` | 录音 worker + **流式播放器**（独立任务 + 4×4KB 块队列，feed 阻塞反压） |
| `voice_http.c/h` | `chat_stream` 对讲机式：**自拼 chunked 编码**边录边传；响应剥 RIFF 头流式喂播放器 |
| `voice_ui.c/h` | LVGL 深色自绘界面（状态大字 + 实时录音秒数 + 识别/回复文本） |
| `wifi_sta.c/h` | STA 连接 + 自动重连 |
| `kconfig.projbuild` | WiFi/网关/录音上限/录音下限 编译时配置 |
| `sdkconfig.defaults` | C3 无 PSRAM、LVGL 24KB 堆、全量字体 TXT_LARGE 等 |
| `CMakeLists.txt` | 注册语音模块 + 全量字体 |

## 内存设计（无 PSRAM 关键）

- 空闲堆 ≈ 175KB、最大连续块 ≈ 114KB（实测）。
- **录音**：2KB 栈上缓冲，边录边传（chunked），零堆占用；时长不限（15s 防呆）。
- **播放**：边收边播，队列 ≈16KB；不做整包缓存。
- 整包兼容路径 `voice_http_chat` 上限 110KB（仅 PC 冒烟用）。

## 按键逻辑

| 操作 | 行为 |
|---|---|
| 按住 OK | 开始录音并立即流式上传（屏幕显示「聆听中… X.X 秒」） |
| 松开 OK | 结束上传 → 网关 ASR→LLM→TTS → 响应边下边播 |
| 播放中短按 OK | 打断播放 |

## 调试

```bash
idf.py -p COM5 monitor    # 日志含 voice_app / voice_http / wifi_sta 等 TAG
```
注意：monitor 占用 COM5，再次刷机前需先退出 monitor。
