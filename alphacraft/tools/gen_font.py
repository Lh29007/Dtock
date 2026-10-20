#!/usr/bin/env python3
"""Génère source/font.h : police bitmap 8x8 (ASCII 32..126) rendue depuis une TTF."""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FONT_CANDIDATES = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
]
font_path = next((p for p in FONT_CANDIDATES if os.path.exists(p)), None)
font = ImageFont.truetype(font_path, 10) if font_path else ImageFont.load_default(size=8)

lines = ["// Généré par tools/gen_font.py : ne pas éditer à la main.",
         "#pragma once", "",
         "// Chaque caractère : 8 octets, un par ligne, bit 7 = pixel le plus à gauche.",
         "static const unsigned char font8x8[95][8] = {"]
for code in range(32, 127):
    ch = chr(code)
    img = Image.new("L", (8, 8), 0)
    d = ImageDraw.Draw(img)
    bbox = d.textbbox((0, 0), ch, font=font)
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    # centre le glyphe dans la cellule 8x8
    ox = (8 - w) // 2 - bbox[0] if w else 0
    oy = (8 - h) // 2 - bbox[1] if h else 0
    d.text((ox, oy), ch, font=font, fill=255)
    rows = []
    for y in range(8):
        v = 0
        for x in range(8):
            if img.getpixel((x, y)) > 110:
                v |= 1 << (7 - x)
        rows.append(v)
    lines.append("    {" + ", ".join(f"0x{r:02X}" for r in rows) + "},  // " + (ch if ch != "\\" else "backslash"))
lines.append("};")
lines.append("")
with open(os.path.join(ROOT, "source", "font.h"), "w") as f:
    f.write("\n".join(lines))
print("font:", font_path)
