#include "Logo.h"

#include <algorithm>

#include "../generated/logo.h"

namespace logo {
namespace {

uint8_t bits[BYTES];   // máscara 4 bpp, 2 pixels por byte

int sextet(char ch) {
  if (ch >= 'A' && ch <= 'Z') return ch - 'A';
  if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
  if (ch >= '0' && ch <= '9') return ch - '0' + 52;
  if (ch == '+') return 62;
  if (ch == '/') return 63;
  return -1;   // '=' e o fim
}

/** Base64 -> bits (para ao fim do texto ou do buffer). */
void decode() {
  uint32_t acc = 0;
  int have = 0, out = 0;
  for (const char *p = BASE64; *p && out < BYTES; p++) {
    int v = sextet(*p);
    if (v < 0) break;
    acc = (acc << 6) | (uint32_t)v;
    have += 6;
    if (have >= 8) {
      have -= 8;
      bits[out++] = (uint8_t)(acc >> have);
    }
  }
}

struct Decoder {
  Decoder() { decode(); }
} decoder;

}  // namespace

int width() { return W; }
int height() { return H; }

void draw(Canvas &c, int x, int y, Rgb color, int alpha) {
  if (alpha <= 0) return;
  const int rowBytes = (W + 1) / 2;
  int r0 = std::max(0, c.bandY - y), r1 = std::min(H, c.bandY + BAND_H - y);
  int col0 = std::max(0, -x), col1 = std::min(W, SCREEN_W - x);
  for (int r = r0; r < r1; r++) {
    const uint8_t *src = bits + r * rowBytes;
    for (int col = col0; col < col1; col++) {
      int v = (src[col >> 1] >> ((col & 1) ? 0 : 4)) & 0x0F;
      if (v) c.blendPx(x + col, y + r, color, alpha * v / 15);
    }
  }
}

}  // namespace logo
