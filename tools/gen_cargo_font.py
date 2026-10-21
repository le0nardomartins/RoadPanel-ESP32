"""Gera src/generated/font_cargo.h: a Saira grande (negrito, condensada) do título da tela da
carga ("COLOQUE A CARGA" / "RETIRE A CARGA"), no mesmo formato 4 bpp de fonts.h.

Uso (precisa de Pillow, fontTools e brotli para ler o .woff2 do painel web):
  python tools/gen_cargo_font.py
"""
import io
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
WOFF2 = ROOT.parent / "frontend" / "RoadPanel" / "fonts" / "Saira-Variable-latin.woff2"
OUT = ROOT / "src" / "generated" / "font_cargo.h"

SIZE = 52                       # px: maiúsculas com ~36 px de altura em 480x320
AXES = {"wght": 700, "wdth": 85}  # igual ao título da tela web (font-weight 700, font-stretch 85%)
CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ"


def load_font():
    font = TTFont(str(WOFF2))
    font = instancer.instantiateVariableFont(font, AXES)
    font.flavor = None
    data = io.BytesIO()
    font.save(data)
    data.seek(0)
    return ImageFont.truetype(data, SIZE)


def main():
    font = load_font()
    ascent, descent = font.getmetrics()
    glyphs, bits = [], []
    for ch in CHARS:
        advance = round(font.getlength(ch))
        box = font.getbbox(ch, anchor="ls")   # relativo à origem na linha de base
        if ch == " " or box[2] <= box[0]:
            glyphs.append((ord(ch), len(bits), 0, 0, 0, 0, advance))
            continue
        x0, y0, x1, y1 = box
        w, h = x1 - x0, y1 - y0
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
        offset = len(bits)
        px = img.load()
        for y in range(h):
            row = [round(px[x, y] * 15 / 255) for x in range(w)]
            if w % 2:
                row.append(0)
            bits.extend((row[i] << 4) | row[i + 1] for i in range(0, len(row), 2))
        glyphs.append((ord(ch), offset, w, h, x0, y0, advance))

    lines = [
        "// GERADO por tools/gen_cargo_font.py (Saira, OFL) — não edite à mão.",
        "// Título da tela da carga: só maiúsculas e espaço.",
        "#pragma once",
        '#include "../render/FontTypes.h"',
        "",
        "namespace fonts {",
        "",
        "static const uint8_t cargo_bits[] = {",
    ]
    for i in range(0, len(bits), 24):
        lines.append("  " + ",".join(f"0x{b:02x}" for b in bits[i:i + 24]) + ",")
    lines.append("};")
    lines.append("static const Glyph cargo_glyphs[] = {")
    for g in glyphs:
        lines.append("  {0x%04x, %d, %d, %d, %d, %d, %d}," % g)
    lines.append("};")
    lines.append(f"static const Font cargo = {{cargo_glyphs, {len(glyphs)}, cargo_bits, {ascent}, {descent}}};")
    lines.append("")
    lines.append("}  // namespace fonts")
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    widths = {t: sum(round(font.getlength(c)) for c in t) for t in ("COLOQUE A CARGA", "RETIRE A CARGA")}
    print(f"{OUT.name}: {len(glyphs)} glifos, {len(bits)} bytes, larguras {widths}")


if __name__ == "__main__":
    main()
