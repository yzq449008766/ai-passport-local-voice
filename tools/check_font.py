# -*- coding: utf-8 -*-
"""字体字形覆盖校验：检查 C 字体源/OTF 是否含指定 U+ 码位（缺字排查）"""
import re
import sys


def check_font_source_c(path: str, chars: str) -> dict:
    """从 lv_font_conv 生成的 .c 中提取所有 0xXXXX 码位，统计覆盖"""
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    glyphs = set(int(m, 16) for m in re.findall(r"0x([0-9a-fA-F]{4,5})\b", src))
    return {c: (ord(c) in glyphs) for c in chars}


def check_otf(path: str, chars: str) -> dict:
    try:
        from fontTools.ttLib import TTFont
    except ImportError:
        sys.exit("需要 fonttools: pip install fonttools")
    f = TTFont(path)
    cmap = f.getBestCmap()
    return {c: (ord(c) in cmap) for c in chars}


if __name__ == "__main__":
    path = sys.argv[1]
    test_chars = "说话题联网聆听按键内存不足WiFi网关思考播放识别回复天气温度下雨"
    result = check_font_source_c(path, test_chars) if path.endswith(".c") else check_otf(path, test_chars)
    missing = [c for c, ok in result.items() if not ok]
    print(f"检查 {len(result)} 字：缺失 {len(missing)} 个 -> {''.join(missing) if missing else '无'}")
