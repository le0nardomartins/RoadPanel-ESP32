#pragma once
// Formatos dos recursos gerados por tools/gen_assets.py (ficam na flash).

#include <stdint.h>

struct Glyph {
  uint32_t code;     // ponto de código Unicode
  uint32_t offset;   // início do bitmap 4 bpp em Font::bits
  uint8_t w, h;
  int8_t dx, dy;     // canto superior esquerdo relativo à origem na linha de base
  uint8_t advance;
};

struct Font {
  const Glyph *glyphs;   // ordenados por código
  uint16_t count;
  const uint8_t *bits;
  uint8_t ascent, descent;
};

// Máscara de alfa 4 bpp (ícones), colorida na hora de desenhar
struct Mask {
  const uint8_t *bits;
  uint8_t w, h;
};

// Imagem RGB565 com alfa de 8 bits (faces das placas)
struct Sprite {
  const uint16_t *rgb;
  const uint8_t *alpha;
  uint8_t w, h;
};
