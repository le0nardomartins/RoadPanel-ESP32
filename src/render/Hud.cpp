#include "Hud.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>

#include "../generated/fonts.h"
#include "../generated/icons.h"

namespace {

// Paleta (DESIGN.md)
constexpr Rgb INK = hex(0x0e1726);
constexpr Rgb INK2 = hex(0x3d4b62);
constexpr Rgb INK3 = hex(0x5b6a82);
constexpr Rgb ROUTE_STRONG = hex(0x1f5fd8);
constexpr Rgb LIVE = hex(0x0f8a52);
constexpr Rgb LIVE_DOT = hex(0x12b36a);
constexpr Rgb SIM = hex(0xa85f00);
constexpr Rgb STOP = hex(0xc4122f);
constexpr Rgb WHITE = hex(0xffffff);
constexpr Rgb LAMP_RED = hex(0xe5132b);
constexpr Rgb LAMP_YELLOW = hex(0xf2a100);
constexpr Rgb LAMP_GREEN = hex(0x0fae68);
constexpr int PANEL_ALPHA = 255;   // opaco: o chão atrás dele nem é calculado (ver occluders)
constexpr int LINE_ALPHA = 26;     // panel-line: rgba(14,23,38,0.1)

constexpr int GUTTER = 10;
constexpr int PANEL_X = GUTTER, PANEL_Y = 48, PANEL_W = 156;
constexpr int METRICS_H = 40, METRICS_Y = SCREEN_H - GUTTER - METRICS_H;

/** Painel claro translúcido com contorno fino e sombra discreta (cartão do guia). */
void panel(Canvas &c, float x, float y, float w, float h, float r, int alpha = 255) {
  c.roundRect(x, y + 2, w, h, r, INK, 14 * alpha / 255);
  c.roundRect(x, y, w, h, r, WHITE, PANEL_ALPHA * alpha / 255);
  c.roundRectStroke(x, y, w, h, r, 1, INK, LINE_ALPHA * alpha / 255);
}

/** Corta o texto com "…" para caber em maxW. */
std::string fit(const Font &font, const std::string &text, int maxW, int tracking = 0) {
  if (Canvas::textWidth(font, text.c_str(), tracking) <= maxW) return text;
  std::string s = text;
  while (!s.empty()) {
    // remove um caractere UTF-8 inteiro
    size_t i = s.size() - 1;
    while (i > 0 && ((uint8_t)s[i] & 0xC0) == 0x80) i--;
    s.erase(i);
    std::string t = s + "…";
    if (Canvas::textWidth(font, t.c_str(), tracking) <= maxW) return t;
  }
  return "…";
}

/** Quebra em até 2 linhas por palavras. */
int wrap2(const Font &font, const std::string &text, int maxW, std::string &l1, std::string &l2) {
  l1.clear();
  l2.clear();
  size_t pos = 0;
  while (pos < text.size()) {
    size_t sp = text.find(' ', pos);
    std::string word = text.substr(pos, sp == std::string::npos ? std::string::npos : sp - pos);
    pos = sp == std::string::npos ? text.size() : sp + 1;
    std::string &cur = l2.empty() && (l1.empty() || Canvas::textWidth(font, (l1 + " " + word).c_str()) <= maxW)
                           ? l1 : l2;
    cur += (cur.empty() ? "" : " ") + word;
  }
  l2 = fit(font, l2, maxW);
  return l2.empty() ? 1 : 2;
}

/** Maiúsculas em UTF-8 (ASCII e letras acentuadas do latim: ó -> Ó). */
std::string upper(const char *s) {
  std::string out(s);
  for (size_t i = 0; i < out.size(); i++) {
    uint8_t ch = (uint8_t)out[i];
    if (ch == 0xC3 && i + 1 < out.size()) {
      uint8_t next = (uint8_t)out[i + 1];
      if (next >= 0xA0 && next <= 0xBE && next != 0xB7) out[i + 1] = (char)(next - 0x20);
      i++;
    } else if (ch < 0x80) {
      out[i] = (char)toupper(ch);
    }
  }
  return out;
}

inline float easeOut(float t) {
  t = t < 0 ? 0 : (t > 1 ? 1 : t);
  return 1 - (1 - t) * (1 - t) * (1 - t);
}

}  // namespace

void Hud::draw(Canvas &c, const FrameView &view, uint32_t nowMs) {
  const HudView &h = view.hud;
  topBar(c, h);
  guidePanel(c, h, nowMs);
  if (h.gps) gpsCard(c, h);
  if (h.gpsOpacity < 1) metrics(c, h);
  if (h.hazard) hazard(c, h);
  if (fps >= 0) {
    char buf[12];
    snprintf(buf, sizeof buf, "%d fps", fps);
    c.text(fonts::micro, SCREEN_W - GUTTER - Canvas::textWidth(fonts::micro, buf), 46, buf, INK3, 200);
  }
}

// ── Barra de cima: hora, velocidade, semáforo e origem do dado ──────────
void Hud::topBar(Canvas &c, const HudView &h) {
  if (c.bandY > 44) return;
  c.text(fonts::clock, GUTTER + 4, 27, h.clock, INK);

  // Velocidade no centro, com unidade em versalete espaçado
  const char *unit = "M/S";
  int vw = Canvas::textWidth(fonts::speed, h.speed);
  int uw = Canvas::textWidth(fonts::label, unit, 2);
  int x = (SCREEN_W - (vw + 6 + uw)) / 2;
  c.text(fonts::speed, x, 34, h.speed, INK);
  c.text(fonts::label, x + vw + 6, 34, unit, INK2, 255, 2);

  // Origem: Simulação (âmbar, nunca verde) | Ao vivo | Sem sinal...
  const char *text;
  Rgb dot, ink;
  if (h.sim) {
    text = "Simulação", dot = SIM, ink = SIM;
  } else {
    switch (h.link) {
      case LinkStatus::Live: text = "Ao vivo", dot = LIVE_DOT, ink = LIVE; break;
      case LinkStatus::Waiting: text = "Aguardando o carro", dot = SIM, ink = INK2; break;
      case LinkStatus::Stale: text = "Dados parados", dot = SIM, ink = INK2; break;
      case LinkStatus::Offline: text = "Sem sinal", dot = STOP, ink = STOP; break;
      default: text = "Conectando…", dot = INK3, ink = INK2;
    }
  }
  int tw = Canvas::textWidth(fonts::label, text);
  float pw = tw + 30, px = SCREEN_W - GUTTER - pw;
  panel(c, px, 10, pw, 24, 12);
  c.circle(px + 13, 22, 3.5f, dot);
  c.text(fonts::label, (int)px + 22, 26, text, ink);

  // Semáforo detectado: cápsula escura com 3 luzes
  float lx = px - 8 - 50;
  c.roundRect(lx, 11, 50, 22, 11, INK, 235);
  const Rgb lamps[3] = {LAMP_RED, LAMP_YELLOW, LAMP_GREEN};
  for (int i = 0; i < 3; i++) {
    bool on = h.light == i;
    float cx = lx + 12 + i * 13;
    if (on) c.circle(cx, 22, 6.5f, lamps[i], 70);   // halo
    c.circle(cx, 22, 4, on ? lamps[i] : hex(0x3a4352));
  }
}

// ── Guia: próxima manobra + missão ───────────────────────────────────────
/** Altura do cartão do guia: manobra + divisória + cabeçalho + pontos/aviso. */
static int guideHeight(const HudView &h) {
  int points = (int)h.missionAll.size();
  return 84 + 30 + (points ? points * 30 : 22) + (h.warning.empty() ? 0 : 30) + 10;
}

int Hud::occluders(const FrameView &view, Occluder *out) const {
  const HudView &h = view.hud;
  int n = 0;
  out[n++] = {PANEL_X, PANEL_Y, PANEL_W, guideHeight(h), 14};
  int x0 = PANEL_X + PANEL_W + 10, w = SCREEN_W - GUTTER - x0;
  if (h.gps && h.gpsOpacity >= 1) out[n++] = {x0, SCREEN_H - GUTTER - 72, w, 72, 14};
  else if (!h.gps) out[n++] = {x0, METRICS_Y, w, METRICS_H, 12};
  return n;
}

int Hud::guidePanel(Canvas &c, const HudView &h, uint32_t nowMs) {
  int points = (int)h.missionAll.size();
  int height = guideHeight(h);
  if (c.bandY > PANEL_Y + height + 4 || c.bandY + BAND_H < PANEL_Y) return height;
  panel(c, PANEL_X, PANEL_Y, PANEL_W, height, 14);

  // Ícone e distância; troca com um deslize curto (is-changing, ~200 ms)
  float t = easeOut((nowMs - h.maneuverChangedAt) / 220.0f);
  int slide = (int)((1 - t) * 6);
  int alpha = (int)(255 * (0.35f + 0.65f * t));
  const Mask *icon;
  Rgb iconColor = INK;
  switch (h.icon) {
    case ManeuverIcon::Right: icon = &icons::right, iconColor = ROUTE_STRONG; break;
    case ManeuverIcon::Left: icon = &icons::left, iconColor = ROUTE_STRONG; break;
    case ManeuverIcon::Stop: icon = &icons::stop, iconColor = STOP; break;
    case ManeuverIcon::Light: icon = &icons::light; break;
    case ManeuverIcon::Flag: icon = &icons::flag; break;
    case ManeuverIcon::Done: icon = &icons::done, iconColor = LIVE; break;
    case ManeuverIcon::Point: icon = &icons::point, iconColor = ROUTE_STRONG; break;
    default: icon = &icons::straight;
  }
  int ix = PANEL_X + 12, iy = PANEL_Y + 12 + slide;
  c.mask(*icon, ix, iy, iconColor, alpha);
  if (h.icon == ManeuverIcon::Point && h.iconPoint) {
    char letter[2] = {h.iconPoint, 0};
    int lw = Canvas::textWidth(fonts::letter, letter);
    c.text(fonts::letter, ix + 20 - lw / 2, iy + 26, letter, iconColor, alpha);
  }
  std::string dist = fit(fonts::distance, h.maneuverDistance, PANEL_W - 70);
  c.text(fonts::distance, PANEL_X + 60, PANEL_Y + 41 + slide, dist.c_str(), INK, alpha);
  std::string l1, l2;
  wrap2(fonts::body, h.maneuverLabel, PANEL_W - 24, l1, l2);
  c.text(fonts::body, PANEL_X + 12, PANEL_Y + 68, l1.c_str(), INK2);
  if (!l2.empty()) c.text(fonts::body, PANEL_X + 12, PANEL_Y + 83, l2.c_str(), INK2);

  // Missão
  int y = PANEL_Y + 92;
  c.fillRect(PANEL_X + 12, y, PANEL_W - 24, 1, INK, LINE_ALPHA);
  c.text(fonts::micro, PANEL_X + 12, y + 18, "MISSÃO", INK3, 255, 1);
  const char *src = h.sim ? "simulação" : "serial";
  c.text(fonts::micro, PANEL_X + PANEL_W - 12 - Canvas::textWidth(fonts::micro, src), y + 18, src, INK3);
  y += 28;
  if (!points) {
    c.text(fonts::body, PANEL_X + 12, y + 14, h.sim ? "Volta externa" : "Nenhuma missão recebida", INK3);
    y += 22;
  }
  for (int i = 0; i < points; i++) {
    bool done = i < h.missionDone, current = i == h.missionDone;
    float cx = PANEL_X + 12, cy = y + 1;
    char letter[2] = {h.missionAll[i], 0};
    if (current) {
      c.roundRect(cx, cy, 24, 24, 8, ROUTE_STRONG);
      int lw = Canvas::textWidth(fonts::letter, letter);
      c.text(fonts::letter, (int)cx + 12 - lw / 2, (int)cy + 18, letter, WHITE);
    } else if (done) {
      c.roundRect(cx, cy, 24, 24, 8, hex(0xe3f4ea));
      c.mask(icons::check, (int)cx + 4, (int)cy + 4, LIVE);
    } else {
      c.roundRectStroke(cx, cy, 24, 24, 8, 1.2f, INK, 60);
      int lw = Canvas::textWidth(fonts::letter, letter);
      c.text(fonts::letter, (int)cx + 12 - lw / 2, (int)cy + 18, letter, INK2);
    }
    const char *role = roleName(i, points);
    std::string text = done ? std::string(role) + " " + letter : role;
    c.text(fonts::title, (int)cx + 32, (int)cy + 17, text.c_str(), done ? INK3 : INK);
    y += 30;
  }
  if (!h.warning.empty()) {
    std::string w1, w2;
    wrap2(fonts::micro, h.warning, PANEL_W - 24, w1, w2);
    c.text(fonts::micro, PANEL_X + 12, y + 10, w1.c_str(), STOP);
    if (!w2.empty()) c.text(fonts::micro, PANEL_X + 12, y + 22, w2.c_str(), STOP);
  }
  return height;
}

// ── Métricas (rodapé) ────────────────────────────────────────────────────
void Hud::metrics(Canvas &c, const HudView &h) {
  if (c.bandY + BAND_H < METRICS_Y - 16) return;
  int x0 = PANEL_X + PANEL_W + 10, w = SCREEN_W - GUTTER - x0;
  // Posição estimada (modo real): logo acima das métricas
  if (!h.estimate.empty()) {
    std::string e = fit(fonts::micro, h.estimate, w - 8);
    c.text(fonts::micro, SCREEN_W - GUTTER - Canvas::textWidth(fonts::micro, e.c_str()), METRICS_Y - 6, e.c_str(),
           INK2);
  }
  panel(c, x0, METRICS_Y, w, METRICS_H, 12);
  struct Cell {
    const char *label;
    const std::string *value;
  } cells[4] = {{"PWM", &h.pwm}, {"SERVO", &h.servo}, {"BATERIA", &h.battery}, {"MODO", &h.mode}};
  // três métricas curtas e o modo, que é texto: rótulo em cima, valor embaixo
  const int widths[4] = {62, 62, 72, w - 62 - 62 - 72};
  int x = x0;
  for (int i = 0; i < 4; i++) {
    int cw = widths[i];
    if (i) c.fillRect(x, METRICS_Y + 9, 1, METRICS_H - 18, INK, LINE_ALPHA);
    c.text(fonts::micro, x + 12, METRICS_Y + 15, cells[i].label, INK3, 255, 1);
    std::string v = fit(fonts::title, *cells[i].value, cw - 20);
    c.text(fonts::title, x + 12, METRICS_Y + 32, v.c_str(), INK);
    x += cw;
  }
}

// ── Prévia GPS (parado na PARE ou no ponto) ──────────────────────────────
void Hud::gpsCard(Canvas &c, const HudView &h) {
  const int x = PANEL_X + PANEL_W + 10, w = SCREEN_W - GUTTER - x, cardH = 72, y = SCREEN_H - GUTTER - cardH;
  if (c.bandY + BAND_H < y || c.bandY > y + cardH + 2) return;
  int a = (int)(255 * h.gpsOpacity);
  panel(c, x, y, w, cardH, 14, a);
  // Selo (PARE vermelho ou "Coleta A" azul) + situação
  std::string badge = upper(h.gpsBadge.c_str());
  int bw = Canvas::textWidth(fonts::micro, badge.c_str(), 1) + 14;
  c.roundRect(x + 12, y + 10, bw, 18, 6, h.gpsStop ? STOP : ROUTE_STRONG, a);
  c.text(fonts::micro, x + 19, y + 23, badge.c_str(), WHITE, a, 1);
  std::string status = fit(fonts::body, h.gpsStatus, w - bw - 36);
  c.text(fonts::body, x + 20 + bw, y + 24, status.c_str(), INK2, a);
  // Alfinete + próxima placa
  c.mask(icons::pin, x + 10, y + 38, ROUTE_STRONG, a);
  int figW = std::max(Canvas::textWidth(fonts::distance, h.gpsDistance), Canvas::textWidth(fonts::body, h.gpsEta));
  int textW = w - 46 - figW - 12;
  c.text(fonts::micro, x + 34, y + 45, fit(fonts::micro, upper(h.gpsLabel.c_str()), textW, 1).c_str(), INK3, a, 1);
  c.text(fonts::title, x + 34, y + 62, fit(fonts::title, h.gpsTarget, textW).c_str(), INK, a);
  // Distância e tempo estimado à direita
  int dw = Canvas::textWidth(fonts::distance, h.gpsDistance);
  c.text(fonts::distance, x + w - 12 - dw, y + 54, h.gpsDistance, INK, a);
  int ew = Canvas::textWidth(fonts::body, h.gpsEta);
  c.text(fonts::body, x + w - 12 - ew, y + 67, h.gpsEta, INK3, a);
}

// ── Faixa vermelha: pedestre ou obstáculo ────────────────────────────────
void Hud::hazard(Canvas &c, const HudView &h) {
  const int y = 44, hh = 30;
  if (c.bandY > y + hh + 2 || c.bandY + BAND_H < y) return;
  const char *text = h.hazardPerson ? "Pedestre detectado" : "Obstáculo à frente";
  int tw = Canvas::textWidth(fonts::title, text);
  int w = tw + 48;
  int cx = PANEL_X + PANEL_W + (SCREEN_W - (PANEL_X + PANEL_W)) / 2;
  float x = cx - w / 2.0f;
  c.roundRect(x, y + 2, w, hh, hh / 2.0f, INK, 30);
  c.roundRect(x, y, w, hh, hh / 2.0f, STOP);
  c.mask(icons::pedestrian, (int)x + 12, y + 6, WHITE);
  c.text(fonts::title, (int)x + 34, y + 20, text, WHITE);
}
