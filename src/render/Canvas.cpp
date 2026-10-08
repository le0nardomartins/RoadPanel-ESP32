#include "Canvas.h"

#include <math.h>

#include <algorithm>

const uint8_t BAYER4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

namespace {
inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
}  // namespace

void Canvas::fillRect(int x, int y, int w, int h, Rgb c, int alpha) {
  int y0 = std::max(y, bandY), y1 = std::min(y + h, bandY + BAND_H);
  int x0 = std::max(x, 0), x1 = std::min(x + w, SCREEN_W);
  for (int yy = y0; yy < y1; yy++)
    for (int xx = x0; xx < x1; xx++) blendPx(xx, yy, c, alpha);
}

void Canvas::roundRect(float x, float y, float w, float h, float r, Rgb c, int alpha) {
  int y0 = std::max((int)floorf(y), bandY), y1 = std::min((int)ceilf(y + h), bandY + BAND_H);
  if (y0 >= y1) return;
  int x0 = std::max((int)floorf(x), 0), x1 = std::min((int)ceilf(x + w), SCREEN_W);
  float cx = x + w / 2, cy = y + h / 2, hx = w / 2 - r, hy = h / 2 - r;
  for (int yy = y0; yy < y1; yy++) {
    float py = fabsf(yy + 0.5f - cy) - hy;
    for (int xx = x0; xx < x1; xx++) {
      float px = fabsf(xx + 0.5f - cx) - hx;
      // distância com sinal até a borda do retângulo arredondado
      float ox = std::max(px, 0.0f), oy = std::max(py, 0.0f);
      float d = sqrtf(ox * ox + oy * oy) + std::min(std::max(px, py), 0.0f) - r;
      float cov = clamp01(0.5f - d);
      if (cov > 0) blendPx(xx, yy, c, (int)(alpha * cov));
    }
  }
}

void Canvas::roundRectStroke(float x, float y, float w, float h, float r, float width, Rgb c, int alpha) {
  int y0 = std::max((int)floorf(y - 1), bandY), y1 = std::min((int)ceilf(y + h + 1), bandY + BAND_H);
  if (y0 >= y1) return;
  int x0 = std::max((int)floorf(x - 1), 0), x1 = std::min((int)ceilf(x + w + 1), SCREEN_W);
  float cx = x + w / 2, cy = y + h / 2, hx = w / 2 - r, hy = h / 2 - r;
  for (int yy = y0; yy < y1; yy++) {
    float py = fabsf(yy + 0.5f - cy) - hy;
    for (int xx = x0; xx < x1; xx++) {
      float px = fabsf(xx + 0.5f - cx) - hx;
      float ox = std::max(px, 0.0f), oy = std::max(py, 0.0f);
      float d = sqrtf(ox * ox + oy * oy) + std::min(std::max(px, py), 0.0f) - r;
      float cov = clamp01(0.5f - (fabsf(d + width / 2) - width / 2));
      if (cov > 0) blendPx(xx, yy, c, (int)(alpha * cov));
    }
  }
}

void Canvas::circle(float cx, float cy, float r, Rgb c, int alpha) {
  int y0 = std::max((int)floorf(cy - r - 1), bandY), y1 = std::min((int)ceilf(cy + r + 1), bandY + BAND_H);
  if (y0 >= y1) return;
  int x0 = std::max((int)floorf(cx - r - 1), 0), x1 = std::min((int)ceilf(cx + r + 1), SCREEN_W);
  for (int yy = y0; yy < y1; yy++) {
    float dy = yy + 0.5f - cy;
    for (int xx = x0; xx < x1; xx++) {
      float dx = xx + 0.5f - cx;
      float cov = clamp01(r + 0.5f - sqrtf(dx * dx + dy * dy));
      if (cov > 0) blendPx(xx, yy, c, (int)(alpha * cov));
    }
  }
}

void Canvas::line(float ax, float ay, float bx, float by, float width, Rgb c, int alpha) {
  float hw = width / 2;
  int y0 = std::max((int)floorf(std::min(ay, by) - hw - 1), bandY);
  int y1 = std::min((int)ceilf(std::max(ay, by) + hw + 1), bandY + BAND_H);
  if (y0 >= y1) return;
  int x0 = std::max((int)floorf(std::min(ax, bx) - hw - 1), 0);
  int x1 = std::min((int)ceilf(std::max(ax, bx) + hw + 1), SCREEN_W);
  float vx = bx - ax, vy = by - ay;
  float len2 = vx * vx + vy * vy;
  float inv = len2 > 0 ? 1.0f / len2 : 0;
  for (int yy = y0; yy < y1; yy++) {
    float py = yy + 0.5f - ay;
    for (int xx = x0; xx < x1; xx++) {
      float px = xx + 0.5f - ax;
      float t = clamp01((px * vx + py * vy) * inv);
      float dx = px - vx * t, dy = py - vy * t;
      float cov = clamp01(hw + 0.5f - sqrtf(dx * dx + dy * dy));
      if (cov > 0) blendPx(xx, yy, c, (int)(alpha * cov));
    }
  }
}

void Canvas::convex(const float *xy, int n, Rgb c, int alpha) {
  float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
  float area = 0;
  for (int i = 0; i < n; i++) {
    float x = xy[i * 2], y = xy[i * 2 + 1];
    minX = std::min(minX, x), maxX = std::max(maxX, x), minY = std::min(minY, y), maxY = std::max(maxY, y);
    int j = (i + 1) % n;
    area += x * xy[j * 2 + 1] - xy[j * 2] * y;
  }
  if (fabsf(area) < 1e-3f) return;
  int y0 = std::max((int)floorf(minY), bandY), y1 = std::min((int)ceilf(maxY) + 1, bandY + BAND_H);
  if (y0 >= y1) return;
  int x0 = std::max((int)floorf(minX), 0), x1 = std::min((int)ceilf(maxX) + 1, SCREEN_W);
  // Arestas como retas normalizadas: distância com sinal (positivo = dentro)
  float ex[16], ey[16], ec[16];
  float sgn = area > 0 ? 1.0f : -1.0f;
  n = std::min(n, 16);
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    float dx = xy[j * 2] - xy[i * 2], dy = xy[j * 2 + 1] - xy[i * 2 + 1];
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-6f) len = 1e-6f;
    ex[i] = -dy / len * sgn;
    ey[i] = dx / len * sgn;
    ec[i] = -(ex[i] * xy[i * 2] + ey[i] * xy[i * 2 + 1]);
  }
  for (int yy = y0; yy < y1; yy++) {
    float py = yy + 0.5f;
    for (int xx = x0; xx < x1; xx++) {
      float px = xx + 0.5f;
      float d = 1e9f;
      for (int i = 0; i < n; i++) d = std::min(d, ex[i] * px + ey[i] * py + ec[i]);
      float cov = clamp01(d + 0.5f);
      if (cov > 0) blendPx(xx, yy, c, (int)(alpha * cov));
    }
  }
}

// ── Texto ────────────────────────────────────────────────────────────────
uint32_t nextCodepoint(const char *&p) {
  uint8_t c = (uint8_t)*p++;
  if (c < 0x80) return c;
  int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
  uint32_t code = c & (0x3F >> extra);
  for (int i = 0; i < extra && *p; i++) code = (code << 6) | ((uint8_t)*p++ & 0x3F);
  return code;
}

const Glyph *findGlyph(const Font &font, uint32_t code) {
  int lo = 0, hi = font.count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (font.glyphs[mid].code == code) return &font.glyphs[mid];
    if (font.glyphs[mid].code < code) lo = mid + 1; else hi = mid - 1;
  }
  return nullptr;
}

int Canvas::textWidth(const Font &font, const char *utf8, int tracking) {
  int w = 0;
  for (const char *p = utf8; *p;) {
    const Glyph *g = findGlyph(font, nextCodepoint(p));
    if (g) w += g->advance + tracking;
  }
  return w > 0 ? w - tracking : 0;
}

int Canvas::text(const Font &font, int x, int y, const char *utf8, Rgb c, int alpha, int tracking) {
  int start = x;
  bool visible = y - font.ascent < bandY + BAND_H && y + font.descent > bandY;
  for (const char *p = utf8; *p;) {
    const Glyph *g = findGlyph(font, nextCodepoint(p));
    if (!g) continue;
    if (visible && g->w) {
      int gx = x + g->dx, gy = y + g->dy;
      int r0 = std::max(0, bandY - gy), r1 = std::min((int)g->h, bandY + BAND_H - gy);
      int rowBytes = (g->w + 1) / 2;
      for (int r = r0; r < r1; r++) {
        const uint8_t *src = font.bits + g->offset + r * rowBytes;
        for (int col = 0; col < g->w; col++) {
          int sx = gx + col;
          if (sx < 0 || sx >= SCREEN_W) continue;
          int v = (src[col >> 1] >> ((col & 1) ? 0 : 4)) & 0x0F;
          if (v) blendPx(sx, gy + r, c, alpha * v / 15);
        }
      }
    }
    x += g->advance + tracking;
  }
  return x - start;
}

void Canvas::mask(const Mask &m, int x, int y, Rgb c, int alpha) {
  int r0 = std::max(0, bandY - y), r1 = std::min((int)m.h, bandY + BAND_H - y);
  int rowBytes = (m.w + 1) / 2;
  for (int r = r0; r < r1; r++) {
    const uint8_t *src = m.bits + r * rowBytes;
    for (int col = 0; col < m.w; col++) {
      int sx = x + col;
      if (sx < 0 || sx >= SCREEN_W) continue;
      int v = (src[col >> 1] >> ((col & 1) ? 0 : 4)) & 0x0F;
      if (v) blendPx(sx, y + r, c, alpha * v / 15);
    }
  }
}

void Canvas::sprite(const Sprite &s, float cx, float top, float height, float squeeze, Rgb tint, float fog,
                    Rgb fogColor) {
  float scale = height / s.h;
  float w = s.w * scale * squeeze;
  float left = cx - w / 2;
  int y0 = std::max((int)floorf(top), bandY), y1 = std::min((int)ceilf(top + height), bandY + BAND_H);
  if (y0 >= y1 || w < 1) return;
  int x0 = std::max((int)floorf(left), 0), x1 = std::min((int)ceilf(left + w), SCREEN_W);
  float invX = s.w / w, invY = s.h / height;
  for (int yy = y0; yy < y1; yy++) {
    float v = (yy + 0.5f - top) * invY - 0.5f;
    int iv = (int)floorf(v);
    float fv = v - iv;
    for (int xx = x0; xx < x1; xx++) {
      float u = (xx + 0.5f - left) * invX - 0.5f;
      int iu = (int)floorf(u);
      float fu = u - iu;
      // bilinear em alfa e cor (cor ponderada pelo alfa, sem franjas escuras)
      float a = 0, r = 0, g = 0, b = 0;
      for (int k = 0; k < 4; k++) {
        int sx = iu + (k & 1), sy = iv + (k >> 1);
        if (sx < 0 || sy < 0 || sx >= s.w || sy >= s.h) continue;
        float wgt = ((k & 1) ? fu : 1 - fu) * ((k >> 1) ? fv : 1 - fv);
        int idx = sy * s.w + sx;
        float al = s.alpha[idx] * wgt;
        if (al <= 0) continue;
        uint16_t c = s.rgb[idx];
        a += al;
        r += al * (((c >> 11) & 0x1F) << 3);
        g += al * (((c >> 5) & 0x3F) << 2);
        b += al * ((c & 0x1F) << 3);
      }
      if (a < 1) continue;
      r = r / a * tint.r / 255;
      g = g / a * tint.g / 255;
      b = b / a * tint.b / 255;
      r += (fogColor.r - r) * fog;
      g += (fogColor.g - g) * fog;
      b += (fogColor.b - b) * fog;
      blendPx(xx, yy, {(uint8_t)r, (uint8_t)g, (uint8_t)b}, (int)a);
    }
  }
}
