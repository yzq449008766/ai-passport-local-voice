# 全量中文字体（lv_font_custom_16）

内置 `lv_font_source_han_sans_sc_16_cjk` 仅 1433 字形，缺大量常用简体字（说/话/连/网/聆/听/按/键……），显示方块。方案：用**思源黑体全量子集**生成 LVGL 位图字体。

## 生成步骤

1. 下载思源黑体简体 Regular（约 8MB）：
   ```
   https://cdn.jsdelivr.net/gh/adobe-fonts/source-han-sans@release/SubsetOTF/CN/SourceHanSansCN-Regular.otf
   ```

2. 用 `lv_font_conv`（Node ≥ 22，npx）生成（bpp4，16px，覆盖全量 CJK + 常用标点/ASCII）：
   ```bash
   npx lv_font_conv --font SourceHanSansCN-Regular.otf --size 16 --bpp 4 \
     --range "0x20-0x7f,0x3000-0x303f,0x4e00-0x9fa5,0xff01-0xff5e" \
     --format c --output lv_font_custom_16.c
   ```

3. 生成器输出的 include 是 `lvgl/lvgl.h`（LVGL9 不存在），改一行：
   ```c
   #include "lvgl.h"        // 原来是 #include "lvgl/lvgl.h"
   ```

4. 放入 `main/fonts/lv_font_custom_16.c`，并在 `main/CMakeLists.txt` 的 `SRCS` 加 `"fonts/lv_font_custom_16.c"`（本仓库已包含）。

5. 必须启用大字体索引（否则编译报 Too large font）：
   - `sdkconfig.defaults`（本仓库已含）：
     ```
     CONFIG_LV_FONT_FMT_TXT_LARGE=y
     ```
   - 若用旧 `sdkconfig` 构建，需把其中的
     `# CONFIG_LV_FONT_FMT_TXT_LARGE is not set` 改为 `CONFIG_LV_FONT_FMT_TXT_LARGE=y`。

## 产物说明

生成源文件约 **17MB C 源码**（2 万+ 汉字位图），不入本仓库。按上述步骤本地生成即可。

## 字形覆盖校验

用正则统计覆盖：`check_font.py` 可核对某字体文件是否含指定 U+ 码位（原始踩坑记录见 docs/PITFALLS.md §字体）。
