#!/usr/bin/env python3
"""生成 main/font_data.c：12px 点阵字库，只收录源码里实际用到的汉字 + 可打印 ASCII。

用法：python3 tools/gen_font.py [字体文件]
默认使用文泉驿正黑（自带 12px 点阵，GPLv2 带字体嵌入例外）。
改了任何界面文字后重新运行一次即可。
"""
import glob
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc"
SIZE = 12

chars = set(chr(c) for c in range(0x20, 0x7F))
for path in glob.glob(os.path.join(ROOT, "main", "*.c")):
    if path.endswith("font_data.c"):
        continue
    for ch in open(path, encoding="utf-8").read():
        if ord(ch) > 0x7F:
            chars.add(ch)
chars = sorted(chars)

font = ImageFont.truetype(FONT, SIZE)
ascent, _ = font.getmetrics()
rows_out = []
for ch in chars:
    img = Image.new("1", (16, 16), 0)
    ImageDraw.Draw(img).text((0, 0), ch, font=font, fill=1)
    px = img.load()
    # 字形整体上移，让 12 行刚好容纳 12px 的字
    top = max(0, ascent - 10)
    rows = []
    for y in range(SIZE):
        v = 0
        for x in range(SIZE):
            if px[x, y + top]:
                v |= 1 << (11 - x)
        rows.append(v)
    rows_out.append((ord(ch), rows))

with open(os.path.join(ROOT, "main", "font_data.c"), "w", encoding="utf-8") as f:
    f.write("/* 由 tools/gen_font.py 自动生成，请勿手改。字形来自文泉驿正黑 12px。 */\n")
    f.write('#include "font.h"\n\n')
    f.write(f"const uint16_t font_count = {len(chars)};\n")
    f.write("const uint16_t font_cp[] = {\n")
    for i in range(0, len(rows_out), 12):
        f.write("    " + ", ".join(f"0x{cp:04X}" for cp, _ in rows_out[i:i + 12]) + ",\n")
    f.write("};\n\nconst uint16_t font_rows[][FONT_H] = {\n")
    for cp, rows in rows_out:
        label = chr(cp) if cp not in (0x5C, 0x2F, 0x2A) else "?"
        f.write("    {" + ",".join(f"0x{v:03X}" for v in rows) + f"}}, /* {label} */\n")
    f.write("};\n")
print(f"{len(chars)} glyphs, {len(chars) * 24} bytes of bitmap")
