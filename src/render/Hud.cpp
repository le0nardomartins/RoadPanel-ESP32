#include "Hud.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>

#include "../generated/font_cargo.h"
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
constexpr Rgb WAIT = hex(0xffb21a);       // --sim-dot: parada travada esperando o botão RUN
constexpr Rgb CHARGE = hex(0x12d97a);     // onda de carga
constexpr Rgb LOW = hex(0xff3b30);
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
  // Tela da carga aberta cobre tudo: nem desenha o HUD por baixo
  if (h.cargo && h.cargoOpen >= 1) {
    cargo(c, h, nowMs);
    return;
  }
  topBar(c, h);
  guidePanel(c, h, nowMs);
  if (h.gps) gpsCard(c, h);
  if (h.gpsOpacity < 1) metrics(c, h);
  if (h.hazard) hazard(c, h);
  if (h.cargo) cargo(c, h, nowMs);
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
  if (h.cargo && h.cargoOpen >= 1) {
    out[n++] = {0, 0, SCREEN_W, SCREEN_H, 0};   // tela da carga opaca: a cena inteira fica de fora
    return n;
  }
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
    // Parada consumida com o carro ainda travado nela (point_stop_latched até o botão RUN)
    bool waiting = i == h.missionWaitingAt;
    bool done = i < h.missionDone && !waiting, current = i == h.missionDone && h.missionWaitingAt < 0;
    float cx = PANEL_X + 12, cy = y + 1;
    char letter[2] = {h.missionAll[i], 0};
    if (waiting) {
      c.roundRect(cx - 3, cy - 3, 30, 30, 11, WAIT, 70);   // halo
      c.roundRect(cx, cy, 24, 24, 8, WAIT);
      int lw = Canvas::textWidth(fonts::letter, letter);
      c.text(fonts::letter, (int)cx + 12 - lw / 2, (int)cy + 18, letter, INK);
    } else if (current) {
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
    const char *role = roleName(i < (int)h.missionRoles.size() ? h.missionRoles[i] : 0);
    if (waiting) {
      // Papel em cima, "aguardando" embaixo (o cartão é estreito)
      c.text(fonts::title, (int)cx + 32, (int)cy + 11, role, INK);
      c.text(fonts::micro, (int)cx + 32, (int)cy + 24, "AGUARDANDO RUN", SIM, 255, 1);
    } else {
      std::string text = done ? std::string(role) + " " + letter : role;
      c.text(fonts::title, (int)cx + 32, (int)cy + 17, text.c_str(), done ? INK3 : INK);
    }
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
    int vx = x + 12;
    Rgb ink = INK;
    if (i == 2) {
      // Bateria: raio verde carregando/carregada, vermelho com bateria baixa (car.telemetry.battery_state)
      bool charge = h.batteryState == 1 || h.batteryState == 2;
      if (charge) {
        // Raio: dois triângulos (metade de cima e de baixo)
        float bx = (float)vx, by = METRICS_Y + 19;
        const float top[6] = {bx + 7, by, bx + 0.5f, by + 8, bx + 6, by + 8};
        const float bottom[6] = {bx + 3, by + 14, bx + 9.5f, by + 6, bx + 4, by + 6};
        c.convex(top, 3, CHARGE);
        c.convex(bottom, 3, CHARGE);
        vx += 13;
        ink = LIVE;
      } else if (h.batteryState == 4) {
        ink = LOW;
      }
    }
    std::string v = fit(fonts::title, *cells[i].value, cw - 20 - (vx - x - 12));
    c.text(fonts::title, vx, METRICS_Y + 32, v.c_str(), ink);
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

// ── Tela da carga (parado no ponto esperando o botão RUN) ───────────────
// Porte da .cargo-takeover do painel web: céu claro, malha de pontos, anéis finos, caixa 3D caindo
// na caçamba (coleta) ou saindo dela (entrega), setas, botão azul (o RUN do carro) apertado por um toque e o
// título grande no centro. Mesmos tempos (loop de 4 s) e curvas do CSS.
namespace ct {

inline float clamp01f(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

constexpr Rgb SKY = hex(0xf3f5f9);
// Botão RUN do carro: azul (--route do painel), parede escura e brilho claro
constexpr Rgb KEY = hex(0x2f7bff);
constexpr Rgb KEY_WALL = hex(0x123f95);
constexpr Rgb KEY_LIGHT = hex(0x8ab6ff);
constexpr Rgb BASE = hex(0xeef1f6);
constexpr float LOOP_MS = 4000;
constexpr float CX = SCREEN_W / 2.0f;
constexpr float DROP = 1.7f;   // altura da queda em alturas da caixa (a web usa 2,3; aqui a tela é baixa)

/** cubic-bezier(x1, y1, x2, y2) do CSS: resolve x(t) = p por Newton e devolve y(t). */
float bezier(float p, float x1, float y1, float x2, float y2) {
  p = p < 0 ? 0 : (p > 1 ? 1 : p);
  float t = p;
  for (int i = 0; i < 6; i++) {
    float it = 1 - t;
    float x = 3 * it * it * t * x1 + 3 * it * t * t * x2 + t * t * t - p;
    float dx = 3 * it * it * x1 + 6 * it * t * (x2 - x1) + 3 * t * t * (1 - x2);
    if (fabsf(dx) < 1e-5f) break;
    t -= x / dx;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
  }
  float it = 1 - t;
  return 3 * it * it * t * y1 + 3 * it * t * t * y2 + t * t * t;
}
float easeOutStrong(float p) { return bezier(p, 0.23f, 1, 0.32f, 1); }      // cubic-bezier(0.23, 1, 0.32, 1)
float easeInOutStrong(float p) { return bezier(p, 0.77f, 0, 0.175f, 1); }   // cubic-bezier(0.77, 0, 0.175, 1)
float easePanel(float p) { return bezier(p, 0.16f, 1, 0.3f, 1); }           // --ease-out do painel
float ease(float p) { return bezier(p, 0.25f, 0.1f, 0.25f, 1); }            // ease (padrão do CSS)

/** Fração de p entre a e b (0..1). */
float span(float p, float a, float b) { return clamp01f((p - a) / (b - a)); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float phase(uint32_t elapsed, float periodMs, float delayMs = 0) {
  float e = elapsed - delayMs;
  if (e < 0) e += periodMs * (1 + (int)(-e / periodMs));
  return fmodf(e, periodMs) / periodMs;
}
int alphaOf(float a) { return (int)(255 * clamp01f(a)); }

// Projeção da caixa: gira em y (órbita de 16 s), inclina 24° para mostrar o topo e põe perspectiva
struct Projector {
  float cx, cy, cosY, sinY, cosX, sinX;
  static constexpr float D = 520;   // distância da câmera (px): perspectiva leve como o perspective: 900px
  /** Gira o ponto (vista da câmera: x, y para baixo, z para longe) sem a perspectiva. */
  void view(float x, float y, float z, float out[3]) const {
    float x1 = x * cosY + z * sinY, z1 = -x * sinY + z * cosY;
    out[0] = x1;
    out[1] = y * cosX - z1 * sinX;
    out[2] = y * sinX + z1 * cosX;
  }
  void project(float x, float y, float z, float &sx, float &sy, float &sz) const {
    float p[3];
    view(x, y, z, p);
    float k = D / (D + p[2]);
    sx = cx + p[0] * k;
    sy = cy + p[1] * k;
    sz = p[2];
  }
};

/** Caçamba (alvo tracejado), sombra e caixa com contorno fino, no chão y = 0 do palco. */
void drawBox(Canvas &c, float cx, float cy, float size, uint32_t elapsed, bool delivery, Rgb accent, float a) {
  float orbit = (25.0f + 360.0f * phase(elapsed, 16000)) * (float)M_PI / 180;
  const float tilt = 24.0f * (float)M_PI / 180;
  Projector pr{cx, cy, cosf(orbit), sinf(orbit), cosf(tilt), sinf(tilt)};
  float p = phase(elapsed, LOOP_MS);

  // Animação (mesmos keyframes do CSS): deslocamento em alturas da caixa, achatamento e opacidade
  float lift = 0, sxz = 1, sy = 1, fade = 1, shadow = 1, shadowScale = 1.25f;
  if (!delivery) {
    if (p < 0.30f) lift = lerp(-DROP, 0, easeInOutStrong(span(p, 0, 0.30f)));
    else if (p < 0.35f) {
      float t = easeOutStrong(span(p, 0.30f, 0.35f));
      lift = lerp(0, 0.05f, t), sxz = lerp(1, 1.05f, t), sy = lerp(1, 0.9f, t);
    } else if (p < 0.44f) {
      float t = ease(span(p, 0.35f, 0.44f));
      lift = lerp(0.05f, 0, t), sxz = lerp(1.05f, 1, t), sy = lerp(0.9f, 1, t);
    }
    fade = p < 0.08f ? span(p, 0, 0.08f) : 1 - span(p, 0.88f, 0.97f);
    float t = span(p, 0, 0.30f);
    shadow = p < 0.30f ? t : 1 - span(p, 0.88f, 0.97f);
    shadowScale = lerp(0.3f, 1.25f, t);
  } else {
    if (p >= 0.06f && p < 0.14f) {
      float t = ease(span(p, 0.06f, 0.14f));
      lift = lerp(0, -0.05f, t), sxz = lerp(1, 1.04f, t), sy = lerp(1, 0.94f, t);
    } else if (p >= 0.14f) {
      float t = easeInOutStrong(span(p, 0.14f, 0.40f));
      lift = lerp(-0.05f, -DROP, t), sxz = lerp(1.04f, 1, t), sy = lerp(0.94f, 1, t);
    }
    fade = p < 0.06f ? span(p, 0, 0.06f) : 1 - span(p, 0.40f, 0.46f);
    float t = span(p, 0.14f, 0.40f);
    shadow = p < 0.06f ? span(p, 0, 0.06f) : 1 - t;
    shadowScale = lerp(1.25f, 0.3f, t);
  }

  // Caçamba: quadrado deitado (1,8x a caixa) com contorno na cor do papel e alvo tracejado
  float bed[8], h = size * 0.9f, z;
  const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
  for (int i = 0; i < 4; i++) pr.project(corners[i][0] * h, 0, corners[i][1] * h, bed[i * 2], bed[i * 2 + 1], z);
  c.convex(bed, 4, WHITE, alphaOf(0.7f * a));
  for (int i = 0; i < 4; i++) {
    int j = (i + 1) % 4;
    c.line(bed[i * 2], bed[i * 2 + 1], bed[j * 2], bed[j * 2 + 1], 1.5f, accent, alphaOf(a));
  }
  float inner = size * 0.58f;
  for (int i = 0; i < 4; i++) {
    int j = (i + 1) % 4;
    for (int d = 0; d < 5; d++) {   // tracejado: 5 traços por lado
      float t0 = (d + 0.15f) / 5, t1 = (d + 0.65f) / 5;
      float ax, ay, bx, by;
      pr.project(lerp(corners[i][0], corners[j][0], t0) * inner, 0, lerp(corners[i][1], corners[j][1], t0) * inner, ax, ay, z);
      pr.project(lerp(corners[i][0], corners[j][0], t1) * inner, 0, lerp(corners[i][1], corners[j][1], t1) * inner, bx, by, z);
      c.line(ax, ay, bx, by, 1.2f, accent, alphaOf(0.7f * a));
    }
  }

  // Sombra: disco no chão, mais escuro no centro
  if (shadow > 0) {
    for (int ring = 0; ring < 2; ring++) {
      float r = size * 0.5f * shadowScale * (ring ? 0.6f : 1.0f);
      float pts[32];
      for (int i = 0; i < 16; i++) {
        float ang = i * (float)M_PI / 8;
        pr.project(cosf(ang) * r, 0, sinf(ang) * r, pts[i * 2], pts[i * 2 + 1], z);
      }
      c.convex(pts, 16, INK, alphaOf(0.08f * shadow * a));
    }
  }

  // Caixa: base no chão, achatada a partir do chão
  float boxA = fade * a;
  if (boxA <= 0.01f) return;
  float half = size / 2;
  float v[8][3], w[8][3];   // tela (x, y, z) e vista da câmera antes da perspectiva
  for (int i = 0; i < 8; i++) {
    float x = (i & 1 ? half : -half) * sxz;
    float y = ((i & 2 ? 0 : -size) * sy) + lift * size;
    float zz = (i & 4 ? half : -half) * sxz;
    pr.project(x, y, zz, v[i][0], v[i][1], v[i][2]);
    pr.view(x, y, zz, w[i]);
  }
  float mid[3] = {0, 0, 0};   // centro da caixa
  for (int i = 0; i < 8; i++)
    for (int k = 0; k < 3; k++) mid[k] += w[i][k] / 8;
  // Faces (vértices em volta) e tons do CSS: frente/topo brancos, laterais em cinza-azulado
  struct Face { int idx[4]; Rgb fill; bool top; };
  const Face faces[6] = {
      {{0, 1, 3, 2}, WHITE, false},                 // z-: frente
      {{5, 4, 6, 7}, hex(0xf1f4f9), false},         // z+: fundo
      {{1, 5, 7, 3}, hex(0xe8edf5), false},         // x+: direita
      {{4, 0, 2, 6}, hex(0xdfe5ef), false},         // x-: esquerda
      {{4, 5, 1, 0}, WHITE, true},                  // y-: topo
      {{2, 3, 7, 6}, hex(0xd5dce8), false},         // y+: base
  };
  for (const Face &f : faces) {
    // Face virada para a câmera: normal para fora (centro da face - centro da caixa) contra o
    // raio câmera -> face (a câmera fica em z = -D); as de trás não aparecem
    float fc[3] = {0, 0, 0};
    for (int k = 0; k < 4; k++)
      for (int j = 0; j < 3; j++) fc[j] += w[f.idx[k]][j] / 4;
    float facing = (fc[0] - mid[0]) * fc[0] + (fc[1] - mid[1]) * fc[1] + (fc[2] - mid[2]) * (fc[2] + Projector::D);
    if (facing >= 0) continue;
    float quad[8];
    for (int k = 0; k < 4; k++) quad[k * 2] = v[f.idx[k]][0], quad[k * 2 + 1] = v[f.idx[k]][1];
    c.convex(quad, 4, f.fill, alphaOf(boxA));
    if (f.top) {
      // Faixa no topo (12% no meio), na cor do papel
      float s[8];
      const float u[2] = {0.44f, 0.56f};
      const float *a0 = v[f.idx[0]], *a1 = v[f.idx[1]], *a2 = v[f.idx[2]], *a3 = v[f.idx[3]];
      for (int k = 0; k < 2; k++) {
        s[k * 2] = lerp(a0[0], a1[0], u[k]), s[k * 2 + 1] = lerp(a0[1], a1[1], u[k]);
        s[6 - k * 2] = lerp(a3[0], a2[0], u[k]), s[7 - k * 2] = lerp(a3[1], a2[1], u[k]);
      }
      c.convex(s, 4, accent, alphaOf(boxA));
    }
    for (int k = 0; k < 4; k++) {
      const float *e0 = v[f.idx[k]], *e1 = v[f.idx[(k + 1) % 4]];
      c.line(e0[0], e0[1], e1[0], e1[1], 1.5f, INK, alphaOf(boxA));
    }
  }
}

/** Botão azul: base branca com contorno, parede escura e tampa que desce no aperto. */
void drawButton(Canvas &c, float bx, float by, uint32_t elapsed, float a) {
  float p = phase(elapsed, LOOP_MS);
  float press = 0;
  if (p >= 0.54f && p < 0.60f) press = ease(span(p, 0.54f, 0.60f));
  else if (p >= 0.60f && p < 0.64f) press = 1;
  else if (p >= 0.64f && p < 0.72f) press = 1 - ease(span(p, 0.64f, 0.72f));
  const float rx = 48, ry = 21, capRx = 32, capRy = 12, lift = 13, travel = 8;

  // Ondas azuis no aperto
  for (int k = 0; k < 2; k++) {
    float q = phase(elapsed, LOOP_MS, k * 180.0f);
    if (q < 0.59f) continue;
    float op, sc;
    if (q < 0.61f) op = lerp(0, 0.8f, span(q, 0.59f, 0.61f)), sc = lerp(0.8f, 0.85f, span(q, 0.59f, 0.61f));
    else {
      float t = ease(span(q, 0.61f, 0.90f));
      op = lerp(0.8f, 0, t), sc = lerp(0.85f, 1.6f, t);
    }
    c.ellipse(bx, by - 4, rx * sc, ry * sc, KEY, alphaOf(op * a), 1.5f);
  }

  // Sombra, base (contorno em tinta) e parede da tampa
  c.ellipse(bx, by + 8, rx, ry, INK, alphaOf(0.1f * a));
  c.ellipse(bx, by, rx + 1.5f, ry + 1.5f, INK, alphaOf(a));
  c.ellipse(bx, by, rx, ry, BASE, alphaOf(a));
  float capY = by - lift + press * travel;
  float wall[8] = {bx - capRx, capY, bx + capRx, capY, bx + capRx, by - 2, bx - capRx, by - 2};
  c.ellipse(bx, by - 2, capRx, capRy, KEY_WALL, alphaOf(a));
  c.convex(wall, 4, KEY_WALL, alphaOf(a));
  // Tampa com brilho no canto de cima
  c.ellipse(bx, capY, capRx, capRy, KEY, alphaOf(a));
  c.ellipse(bx - 6, capY - 3, capRx * 0.55f, capRy * 0.45f, KEY_LIGHT, alphaOf(0.55f * a));

  // Toque: anel que desce, comprime no aperto e sobe
  float op = 0, ty = -0.5f, sc = 1.15f;
  if (p >= 0.40f && p < 0.50f) {
    float t = ease(span(p, 0.40f, 0.50f));
    op = t, ty = lerp(-0.5f, 0, t), sc = lerp(1.15f, 1, t);
  } else if (p >= 0.50f && p < 0.60f) {
    float t = easeInOutStrong(span(p, 0.50f, 0.60f));
    op = 1, ty = lerp(0, 0.3f, t), sc = lerp(1, 0.8f, t);
  } else if (p >= 0.60f && p < 0.64f) {
    op = 1, ty = 0.3f, sc = 0.8f;
  } else if (p >= 0.64f && p < 0.72f) {
    float t = ease(span(p, 0.64f, 0.72f));
    op = 1, ty = lerp(0.3f, 0, t), sc = lerp(0.8f, 1, t);
  } else if (p >= 0.72f && p < 0.82f) {
    float t = ease(span(p, 0.72f, 0.82f));
    op = 1 - t, ty = lerp(0, -0.5f, t), sc = lerp(1, 1.15f, t);
  }
  if (op > 0.01f) {
    const float tapR = 13;
    float ty0 = capY - 34 + ty * 2 * tapR;
    c.circle(bx, ty0 + 3, tapR * sc, INK, alphaOf(0.12f * op * a));
    c.circle(bx, ty0, tapR * sc, WHITE, alphaOf(0.9f * op * a));
    c.ellipse(bx, ty0, tapR * sc, tapR * sc, INK, alphaOf(op * a), 2);
  }
}

}  // namespace ct

void Hud::cargo(Canvas &c, const HudView &h, uint32_t nowMs) {
  using namespace ct;
  float open = easePanel(h.cargoOpen);
  float a = open;
  uint32_t elapsed = nowMs - h.cargoSince;
  bool delivery = h.cargoRole == 'E';
  Rgb accent = delivery ? LIVE : ROUTE_STRONG;

  // Céu do painel (opaco quando aberta: no TFT não há desfoque, então a cena some por baixo)
  c.fillRect(0, c.bandY, SCREEN_W, BAND_H, SKY, alphaOf(a));

  // Malha de pontos azul, mais forte no centro (elipse 60% x 55%, some em 75%)
  const int gap = 16;
  int gy0 = ((c.bandY - 2) / gap) * gap + gap / 2;
  for (int y = gy0; y < c.bandY + BAND_H + 2; y += gap) {
    for (int x = gap / 2; x < SCREEN_W; x += gap) {
      float nx = (x - CX) / (SCREEN_W * 0.6f), ny = (y - SCREEN_H / 2.0f) / (SCREEN_H * 0.55f);
      float fall = 1 - clamp01f((sqrtf(nx * nx + ny * ny) - 0.15f) / 0.6f);
      if (fall > 0) c.circle((float)x, (float)y, 1.1f, ROUTE_STRONG, alphaOf(0.32f * fall * a));
    }
  }

  // Anéis finos saindo do centro (3,6 s, a cada 1,2 s)
  for (int k = 0; k < 3; k++) {
    float q = easeOutStrong(phase(elapsed, 3600, k * 1200.0f));
    float r = 104 * lerp(0.4f, 1.45f, q);
    c.ellipse(CX, SCREEN_H / 2.0f, r, r, accent, alphaOf(0.35f * (1 - q) * a), 1);
  }

  // Palco: caixa 3D, setas e botão
  const float stageY = 104;
  drawBox(c, 156, stageY + 18, 40, elapsed, delivery, accent, a);
  for (int i = 0; i < 3; i++) {
    float q = phase(elapsed, 1200, i * 200.0f);
    float t = q < 0.3f ? q / 0.3f : 1 - (q - 0.3f) / 0.7f;
    float x = 222 + i * 13 + 4 * t, y = stageY - 6;
    int al = alphaOf(lerp(0.25f, 1, t) * a);
    c.line(x, y - 6, x + 6, y, 2, INK3, al);
    c.line(x + 6, y, x, y + 6, 2, INK3, al);
  }
  drawButton(c, 326, stageY + 6, elapsed, a);

  // Textos: entram subindo 14 px (o título 80 ms antes da linha de baixo), como na web
  float rise1 = easePanel(clamp01f((elapsed - 80.0f) / 300)), rise2 = easePanel(clamp01f((elapsed - 160.0f) / 300));
  char kicker[32];
  if (h.cargoPoint) snprintf(kicker, sizeof kicker, "%s · PONTO %c", delivery ? "ENTREGA" : "COLETA", h.cargoPoint);
  else snprintf(kicker, sizeof kicker, "%s", delivery ? "ENTREGA" : "COLETA");
  int kw = Canvas::textWidth(fonts::micro, kicker, 2);
  float pulse = 0.725f + 0.275f * cosf(phase(elapsed, 1600) * 2 * (float)M_PI);   // bolt-pulse: 1 -> 0,45 -> 1
  int kx = (int)(CX - (kw + 12) / 2.0f);
  c.circle(kx + 3.5f, 172, 3.5f, accent, alphaOf(pulse * a));
  c.text(fonts::micro, kx + 12, 176, kicker, accent, alphaOf(a), 2);

  const char *title = delivery ? "RETIRE A CARGA" : "COLOQUE A CARGA";
  int tw = Canvas::textWidth(fonts::cargo, title);
  c.text(fonts::cargo, (int)(CX - tw / 2.0f), 226 + (int)(14 * (1 - rise1)), title, INK, alphaOf(a * rise1));

  // "E APERTE O BOTÃO ● AZUL": a bolinha azul cresce junto com o aperto
  const char *left = "E APERTE O BOTÃO", *right = "AZUL";
  const int track = 3, dotGap = 28;
  int lw = Canvas::textWidth(fonts::label, left, track), rw = Canvas::textWidth(fonts::label, right, track);
  int sx = (int)(CX - (lw + dotGap + rw) / 2.0f), sy = 252 + (int)(14 * (1 - rise2));
  int subA = alphaOf(a * rise2);
  c.text(fonts::label, sx, sy, left, INK2, subA, track);
  float p = phase(elapsed, LOOP_MS), dot = 1;
  if (p >= 0.56f && p < 0.61f) dot = lerp(1, 1.25f, ease(span(p, 0.56f, 0.61f)));
  else if (p >= 0.61f && p < 0.82f) dot = lerp(1.25f, 1, ease(span(p, 0.61f, 0.82f)));
  c.circle(sx + lw + dotGap / 2.0f, sy - 5, 5 * dot, KEY, subA);
  c.text(fonts::label, sx + lw + dotGap, sy, right, INK2, subA, track);

  // Simulação: o botão RUN é o BOOT da placa
  if (h.cargoSim) {
    const char *hint = "Simulação · aperte o botão BOOT da placa";
    int hw = Canvas::textWidth(fonts::body, hint) + 28;
    float hx = CX - hw / 2.0f, hy = 274;
    c.roundRect(hx, hy, hw, 26, 13, WHITE, subA);
    c.roundRectStroke(hx, hy, hw, 26, 13, 1, INK, LINE_ALPHA * subA / 255);
    c.text(fonts::body, (int)hx + 14, (int)hy + 18, hint, INK2, subA);
  }
}
