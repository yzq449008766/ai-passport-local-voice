# 踩坑记录（实战复盘）

全部来自真机联调实测。每一条都踩过、修过、验证过。

## 1. 无 PSRAM 内存不足（最核心）

**症状**：按 OK 后屏幕报「内存不足」。启动日志：`voice_app: init: free heap=175852, max block=114688`（空闲堆 175KB、最大连续块仅 114KB）。

**根因**：原实现"录 4s=128KB + 整包回复 176KB"——录音缓冲超过最大连续块必然 malloc 失败；且 sdkconfig 残留 `RECORD_MAX_MS=4000` 旧值（与 kconfig 不一致，是分配失败的隐藏元凶之一）。

**修复**：
- 录音改为 **chunked 流式上传**（2KB 栈缓冲，零堆，时长不限）
- 播放改为 **流式播放**（块队列 4×4KB，feed 阻塞反压，不整包缓存）
- 统一 sdkconfig 与 kconfig 的录音上限

## 2. ESP-IDF esp_http_client 的 chunked 坑（协议级）

**症状**：对讲机版按住 OK 约 2 秒后报错。设备日志：`transport_base: poll_write select error 104, errno = Connection reset by peer`。PC 侧同样 chunked 请求却成功。

**根因**：`esp_http_client_open(client, -1)` 只在请求头声明 `Transfer-Encoding: chunked`，但 **`esp_http_client_write` 只是裸 TCP 发送、不自动做 chunked 编码**（查 v5.5.3 源码确认）。裸发 PCM 后，服务端 h11 按 chunked 解析二进制 → 立即解析失败 → RST。

**修复**：调用方自拼标准 chunked 格式：
```
每个数据块:  <size-hex>\r\n  <data>  \r\n
结束:        "0\r\n\r\n"（写终止块后再 fetch_headers）
```
与 Python `http.client(encode_chunked=True)` 逐字节一致，网关即可正常接收。

## 3. 中文字体方块

**症状**：屏幕中文全方块。内置 `lv_font_source_han_sans_sc_16_cjk` 实际仅 1433 字形（"说/话/连/网/聆/听/按/键"等常用字全缺）。

**修复**：思源黑体全量子集（U+20-7F、U+3000-303F、U+4E00-9FA5、U+FF01-FF5E）经 `lv_font_conv`（bpp4/16px）生成 17MB C 源。三个配套坑：
- 生成器输出 `#include "lvgl/lvgl.h"` 在 LVGL9 不存在 → 改 `#include "lvgl.h"`
- 编译报 "Too large font" → `CONFIG_LV_FONT_FMT_TXT_LARGE=y`
- sdkconfig 里该选项是注释形式 `# CONFIG_... is not set`，需文本替换为 `CONFIG_LV_FONT_FMT_TXT_LARGE=y`

## 4. ESP-IDF 工具链

- **版本锁定 v5.5.3**（官方要求）。
- **全量 `git clone --recursive` 卡死** → 必须浅克隆 + 浅子模块。
- `export.ps1` 激活后直接 `idf.py` 报 `No module named 'esp_idf_monitor'`（python 被沙箱/环境劫持）→ 显式用 venv python 调：
  ```
  C:\Users\Win11\.espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe E:\esp\esp-idf-v5.5.3\tools\idf.py build
  ```
- **IDF 5.5 API 差异**三处：
  - `esp_ip4addr_ntoa(addr, buf, buflen)` 三参
  - `esp_http_client_get_header(client, name, &value)` 出参风格
  - `esp_http_client_open(client, int write_len)` 只有两参（无 path 参数），路径走 cfg.url
  - `esp_http_client_read(client, char *buf, int len)` 要 char*，uint8_t* 需强转

## 5. 刷机 / 串口

- 设备 PnP status Unknown / `Could not open COM5` → **拔插 USB 即恢复**。
- `idf.py monitor` 占用 COM5 → 再次刷机前必须先停 monitor（否则 PermissionError）。
- 交付物是 `build/merged-binary.bin` → 复制为 `FoloToy-AI-Passport-full.bin`，**从 0x0 整体刷**（含 bootloader/分区表），勿只刷 app。
- 归档验证：`python tools/archive_firmware.py verify build/firmware/<sha256目录>`。

## 6. 网关（PC 侧）

- 系统 Python 与沙箱 Python 隔离：依赖装在哪个解释器就用哪个启动（`python -c "import sys; print(sys.executable)"` 先定位）。
- 局域网访问：网关必须 `0.0.0.0:8000`；电脑防火墙入站 TCP 8000 需放行（UAC 提权 netsh 在无人值守时会失败，需用户在场确认）。
- LLM 与 ComfyUI 争抢资源：ComfyUI 生成期间不得启动本机 LLM（llama-server 约 3.9GB 内存+显存）。

## 7. 沙箱环境录音不可用

沙箱内 sounddevice/ffmpeg DirectShow 无法访问真实麦克风/扬声器（input 全 -9996）。PC 端冒烟用测试音频代替真实麦克风。

## 8. 回复长度与体验调优

- 回复截断 24 字 + TTS +30% 语速 → 音频约 146KB / 4.6s @16k（整包时代参数；流式后仅影响时延）。
- 流式后回复长度不再受设备内存限制，24 字只是时延/体验取舍。

## 9. 天气问答"答非所问"

本地模型无实时数据，问天气会瞎编。修复：网关关键词检测 + 免费源（wttr.in，无 key）注入实时数据 → LLM 俏皮播报（"冷得像老板的咖啡…"）。

## 10. 后台任务不继承新设的用户级环境变量

**症状**：`[Environment]::SetEnvironmentVariable('QWEN_API_KEY',...,'User')` 后重启网关，`/api/health` 仍显示 `llm:false`、冒烟 500。

**根因**：后台任务进程的环境在创建时已快照，新设的用户级变量不会自动进入已运行/同会话派生的进程。

**修复**：启动命令里显式注入：
```powershell
$env:QWEN_API_KEY=[Environment]::GetEnvironmentVariable('QWEN_API_KEY','User'); python gateway.py
```

## 11. 备忘录 [TODO] 分支不能走通用截断

**症状**：`llm_chat` 里 `REPLY_MAX_CHARS=24` 截断会把 LLM 输出的 `[TODO]明天上午十点给王总回电话` 截成半截。

**修复**：`llm_chat` 对 `[TODO]` 开头直接返回不截断；待办内容由 `memo_sink` 落点后，播报文本再用 `short_text` 单独截断（避免长内容撑爆语音缓冲）。
