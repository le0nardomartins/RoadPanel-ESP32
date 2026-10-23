"""Gera src/generated/logo.h: a logo do time (assets/logo.png) em base64, para a tela sem dados.

A logo é preta sobre fundo transparente, então só o alfa importa: a imagem é recortada na área
desenhada, redimensionada para caber em MAX_W x MAX_H (tela de 480x320) e guardada como máscara de
alfa 4 bpp (2 pixels por byte, como os ícones), codificada em base64. O firmware decodifica uma vez
ao ligar (render/Logo.cpp) e pinta a máscara na cor da tinta.

Uso (precisa de Pillow):
  python tools/gen_logo.py
"""
import base64
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "logo.png"
OUT = ROOT / "src" / "generated" / "logo.h"

MAX_W, MAX_H = 352, 132   # px: larga e baixa, com folga nas laterais da tela de 480


def main():
    img = Image.open(SRC).convert("RGBA")
    # Cobertura = alfa x quanto o pixel é escuro (um halo claro na borda, se houver, não vira tinta)
    dark = Image.eval(img.convert("RGB").convert("L"), lambda v: 255 - v)
    alpha = Image.frombytes("L", img.size, bytes(a * d // 255 for a, d in zip(img.getchannel("A").tobytes(),
                                                                                dark.tobytes())))
    box = alpha.getbbox()
    alpha = alpha.crop(box)
    scale = min(MAX_W / alpha.width, MAX_H / alpha.height)
    w, h = round(alpha.width * scale), round(alpha.height * scale)
    alpha = alpha.resize((w, h), Image.LANCZOS)

    bits = bytearray()
    px = alpha.load()
    for y in range(h):
        row = [round(px[x, y] * 15 / 255) for x in range(w)]
        if w % 2:
            row.append(0)
        bits.extend((row[i] << 4) | row[i + 1] for i in range(0, len(row), 2))
    b64 = base64.b64encode(bytes(bits)).decode("ascii")

    lines = [
        "// GERADO por tools/gen_logo.py a partir de assets/logo.png — não edite à mão.",
        f"// Logo do time: máscara de alfa 4 bpp {w}x{h} (2 pixels por byte, linha a linha) em base64.",
        "#pragma once",
        "",
        "namespace logo {",
        "",
        f"constexpr int W = {w}, H = {h};",
        f"constexpr int BYTES = {len(bits)};   // (W + 1) / 2 * H",
        "static const char BASE64[] =",
    ]
    for i in range(0, len(b64), 100):
        lines.append(f'    "{b64[i:i + 100]}"')
    lines[-1] += ";"
    lines += ["", "}  // namespace logo", ""]
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"{OUT.name}: {w}x{h} (recorte {box}), {len(bits)} bytes -> {len(b64)} caracteres base64")


if __name__ == "__main__":
    main()
