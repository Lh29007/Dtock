#!/usr/bin/env python3
"""Génère source/textures.h à partir des PNG 16x16 de textures/."""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
NAMES = ["stone", "dirt", "planks"]

lines = ["// Généré par tools/gen_textures.py : ne pas éditer à la main.",
         "#pragma once", "", "#define TEX_SIZE 16", ""]
for i, name in enumerate(NAMES):
    img = Image.open(os.path.join(ROOT, "textures", name + ".png")).convert("RGB")
    assert img.size == (16, 16), img.size
    data = list(img.getdata())
    flat = []
    for r, g, b in data:
        flat += [r, g, b]
    lines.append(f"static const unsigned char tex_{name}[16 * 16 * 3] = {{")
    for row in range(0, len(flat), 24):
        lines.append("    " + ", ".join(str(v) for v in flat[row:row + 24]) + ",")
    lines.append("};")
    lines.append("")
lines.append("static const unsigned char *const block_textures[4] = {")
lines.append("    0, tex_stone, tex_dirt, tex_planks,")
lines.append("};")
lines.append("")

with open(os.path.join(ROOT, "source", "textures.h"), "w") as f:
    f.write("\n".join(lines))
print("ok")
