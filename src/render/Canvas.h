#pragma once
// Desenho em faixas: a tela de 480x320 é montada em faixas horizontais (480 x BAND_H) que vão
// para o display por DMA enquanto a próxima é desenhada. Assim cabe na RAM do ESP32 sem PSRAM
// e ainda dá para misturar transparência, antisserrilhado e texto suave como no navegador.
//
// O buffer guarda RGB565 com os bytes trocados (ordem do SPI), pronto para o DMA.
// Coordenadas de desenho são sempre da TELA; o canvas recorta para a faixa atual.

#include <stdint.h>

#include "FontTypes.h"

constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 320;
constexpr int BAND_H = 32;
constexpr int BAND_COUNT = SCREEN_H / BAND_H;

struct Rgb {
  uint8_t r, g, b;
};

constexpr Rgb hex(uint32_t v) { return {(uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; }

inline uint16_t swap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
inline uint16_t pack565(int r, int g, int b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
/** Cor -> pixel do buffer (565 com bytes trocados). */
inline uint16_t toPixel(Rgb c) { return swap16(pack565(c.r, c.g, c.b)); }
inline Rgb fromPixel(uint16_t px) {
  uint16_t v = swap16(px);
  int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
  return {(uint8_t)((r << 3) | (r >> 2)), (uint8_t)((g << 2) | (g >> 4)), (uint8_t)((b << 3) | (b >> 2))};
}

/** Pontilhado ordenado 4x4 (0..15): tira o degrau do RGB565 nos degradês claros. */
extern const uint8_t BAYER4[16];
inline uint16_t toPixelDither(float r, float g, float b, int x, int y) {
  float t = BAYER4[((y & 3) << 2) | (x & 3)] * (1.0f / 16.0f);
  int ri = (int)(r + t * 8), gi = (int)(g + t * 4), bi = (int)(b + t * 8);
  ri = ri > 255 ? 255 : ri;
  gi = gi > 255 ? 255 : gi;
  bi = bi > 255 ? 255 : bi;
  return swap16(pack565(ri, gi, bi));
}

class Canvas {
 public:
  void begin(uint16_t *buffer, int y0) {
    buf = buffer;
    bandY = y0;
  }

  uint16_t *row(int y) { return buf + (y - bandY) * SCREEN_W; }
  bool rowInBand(int y) const { return y >= bandY && y < bandY + BAND_H; }

  /** Mistura uma cor com alfa 0..255 num pixel (coordenada de tela, já dentro da faixa). */
  inline void blendPx(int x, int y, Rgb c, int a) {
    if (a <= 0) return;
    uint16_t &px = buf[(y - bandY) * SCREEN_W + x];
    if (a >= 255) {
      px = toPixel(c);
      return;
    }
    Rgb d = fromPixel(px);
    int ia = 255 - a;
    px = toPixel({(uint8_t)((c.r * a + d.r * ia) / 255), (uint8_t)((c.g * a + d.g * ia) / 255),
                  (uint8_t)((c.b * a + d.b * ia) / 255)});
  }

  void fillRect(int x, int y, int w, int h, Rgb c, int alpha = 255);
  /** Retângulo arredondado com bordas suaves (coordenadas em px, podem ser fracionárias). */
  void roundRect(float x, float y, float w, float h, float r, Rgb c, int alpha = 255);
  /** Contorno de retângulo arredondado (borda de 1 px suave). */
  void roundRectStroke(float x, float y, float w, float h, float r, float width, Rgb c, int alpha = 255);
  void circle(float cx, float cy, float r, Rgb c, int alpha = 255);
  /** Traço reto com pontas arredondadas e bordas suaves. */
  void line(float x0, float y0, float x1, float y1, float width, Rgb c, int alpha = 255);
  /** Polígono convexo com bordas suaves (pontos em sentido qualquer). */
  void convex(const float *xy, int n, Rgb c, int alpha = 255);

  /** Texto UTF-8 com a linha de base em y. Devolve a largura. */
  int text(const Font &font, int x, int y, const char *utf8, Rgb c, int alpha = 255, int tracking = 0);
  static int textWidth(const Font &font, const char *utf8, int tracking = 0);
  /** Máscara 4 bpp colorida (ícones). */
  void mask(const Mask &m, int x, int y, Rgb c, int alpha = 255);
  /** Placa: imagem com alfa, escalada (bilinear), centrada em (cx, top). squeeze < 1 estreita (vista de lado). */
  void sprite(const Sprite &s, float cx, float top, float height, float squeeze, Rgb tint, float fog, Rgb fogColor);
  /** Imagem com alfa sob uma transformação afim: o centro da imagem vai para (cx, cy) e um passo de
   *  1 px na imagem anda (ax, ay) na tela em x e (bx, by) em y. Serve para girar e achatar. */
  void spriteAffine(const Sprite &s, float cx, float cy, float ax, float ay, float bx, float by,
                    Rgb tint = {255, 255, 255}, float fog = 0, Rgb fogColor = {0, 0, 0});

  uint16_t *buf = nullptr;
  int bandY = 0;
};

const Glyph *findGlyph(const Font &font, uint32_t code);
/** Decodifica um caractere UTF-8 e avança o ponteiro. */
uint32_t nextCodepoint(const char *&p);
