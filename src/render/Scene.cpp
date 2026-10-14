#include "Scene.h"

#include <algorithm>

#include "../generated/car_sprites.h"
#include "../generated/sprites.h"
#include "../generated/track_sdf.h"

namespace {

constexpr float PX = 50.0f;            // px da pista por metro
constexpr float INV_PX = 0.02f;

// Cores (trackMeshes.js, routeRibbon.js, gpsPreview.js)
constexpr Rgb FIELD = hex(0xeef1f5);
constexpr Rgb LANE = hex(0xdde2e9);
constexpr Rgb ISLAND = hex(0xfdfdfe);
constexpr Rgb LINE = hex(0x0e131b);
constexpr Rgb DOT = hex(0x2f7bff);
constexpr Rgb SKY_TOP = hex(0xf7f9fc);
constexpr Rgb ROUTE = hex(0x2f7bff);
constexpr Rgb ROUTE_EDGE = hex(0x1546b8);
constexpr Rgb GPS_BLUE = hex(0x1f5fd8);
constexpr Rgb POST = hex(0x1b2028);
constexpr Rgb SIGN_BACK = hex(0x3a414d);
constexpr Rgb WHITE = hex(0xffffff);

constexpr float LINE_HALF = 8 * 0.72f / 2;   // LINE_DRAW_PX / 2 (px da pista)
constexpr float LINE_OPACITY = 0.58f;
constexpr float SHADOW_HALF = 0.42f * PX / 2; // sombra de contato das ilhas
constexpr float RIBBON_HALF = 0.66f * PX / 2;
constexpr float GPS_HALF = 0.44f * PX / 2;
constexpr float RIBBON_FADE_M = 34;
constexpr float STRIP_STEP = 5;               // px entre amostras da fita (perto do chão)
constexpr float STRIP_STEP_MAX = 24;          // px: na vista de cima da pista inteira
// Carro (car.js: CAR_SIZE; o modelo 3D é escalado para 0,8 m de largura e 1,2 m de comprimento)
constexpr float CAR_LEN = 1.2f, CAR_WID = 0.8f;
constexpr float RAD2DEG = 180.0f / (float)M_PI;
constexpr float TOP_FROM_DEG = 64;   // acima disso a câmera vê o carro "de cima"
// Placas (signs.js)
constexpr float SIGN_SIZE = 0.6f, POST_HEIGHT = 0.9f;
constexpr float SIGN_NEAR_M = 1.2f;   // mais perto que isso da câmera a placa não é desenhada

inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline float smoothstep(float a, float b, float x) {
  float t = clamp01((x - a) / (b - a));
  return t * t * (3 - 2 * t);
}
inline float fractf(float v) { return v - floorf(v); }
inline void mixTo(float &r, float &g, float &b, Rgb c, float k) {
  r += (c.r - r) * k;
  g += (c.g - g) * k;
  b += (c.b - b) * k;
}
/** Seno rápido (parábola com correção), suficiente para as faixas de luz do campo. */
inline float fastSin(float x) {
  const float inv2pi = 0.15915494f;
  x = x * inv2pi;
  x = x - floorf(x + 0.5f);          // -0.5..0.5 volta
  x *= 6.2831853f;                   // -pi..pi
  float y = 1.2732395f * x - 0.4052847f * x * fabsf(x);
  return 0.225f * (y * fabsf(y) - y) + y;
}
/** Cobertura de [-hw, hw] por um pixel de meia largura aa centrado em d (filtro caixa). */
inline float bandCoverage(float d, float hw, float aa) {
  float lo = std::max(-hw, d - aa), hi = std::min(hw, d + aa);
  return hi > lo ? (hi - lo) / (2 * aa) : 0;
}

}  // namespace

Scene::Projected Scene::project(float X, float Y, float Z) const {
  float dx = X - px_, dy = Y - py_, dz = Z - pz_;
  float z = dx * fx_ + dy * fy_ + dz * fz_;
  if (z < 0.08f) return {0, 0, z, false};
  float sx = dx * rx_ + dz * rz_;
  float sy = dx * ux_ + dy * uy_ + dz * uz_;
  return {SCREEN_W / 2.0f + sx / z * fpx_, SCREEN_H / 2.0f - sy / z * fpx_, z, true};
}

void Scene::prepare(const FrameView &view) {
  view_ = &view;
  // ── Câmera ──
  px_ = view.camPos.x, py_ = view.camPos.y, pz_ = view.camPos.z;
  fx_ = view.camLook.x - px_, fy_ = view.camLook.y - py_, fz_ = view.camLook.z - pz_;
  float fl = sqrtf(fx_ * fx_ + fy_ * fy_ + fz_ * fz_);
  fx_ /= fl, fy_ /= fl, fz_ /= fl;
  // direita: horizontal, à direita de quem olha (y da pista para baixo)
  rx_ = -fz_, rz_ = fx_;
  float rl = sqrtf(rx_ * rx_ + rz_ * rz_);
  if (rl < 1e-5f) { rx_ = 1, rz_ = 0; } else { rx_ /= rl, rz_ /= rl; }
  // cima = direita x frente, com y positivo
  ux_ = fy_ * rz_ - fz_ * 0;
  uy_ = fz_ * rx_ - fx_ * rz_;
  uz_ = fx_ * 0 - fy_ * rx_;
  if (uy_ < 0) ux_ = -ux_, uy_ = -uy_, uz_ = -uz_;
  fpx_ = (SCREEN_H / 2.0f) / tanf(view.fovDeg * (float)M_PI / 360.0f);

  // ── Linhas do chão ──
  horizonY_ = -1;
  float fogSpan = std::max(1.0f, view.fogFar - view.fogNear);
  for (int y = 0; y < SCREEN_H; y++) {
    Row &row = rows_[y];
    float v = (y + 0.5f - SCREEN_H / 2.0f) / fpx_;
    float h0 = (0.5f - SCREEN_W / 2.0f) / fpx_;
    // raio do pixel (0, y): frente - cima*v + direita*h0
    float ax = fx_ - ux_ * v + rx_ * h0;
    float ay = fy_ - uy_ * v;
    float az = fz_ - uz_ * v + rz_ * h0;
    row.ground = ay < -1e-4f;
    if (!row.ground) {
      horizonY_ = (float)y;
      continue;
    }
    float t = -py_ / ay;   // profundidade (m), igual à distância ao longo da frente
    row.wx0 = (px_ + t * ax) * PX;
    row.wy0 = (pz_ + t * az) * PX;
    row.dwx = t * rx_ / fpx_ * PX;
    row.dwy = t * rz_ / fpx_ * PX;
    row.fog = clamp01((t - view.fogNear) / fogSpan);
    // pegada do pixel no chão: na linha e entre linhas (no centro da tela)
    float v1 = (y + 1.5f - SCREEN_H / 2.0f) / fpx_;
    float by = fy_ - uy_ * v1;
    float alongRow = fabsf(t / fpx_) * PX;
    float acrossRow = alongRow;
    if (by < -1e-4f) {
      float t1 = -py_ / by;
      float cx0 = t * (fx_ - ux_ * v), cz0 = t * (fz_ - uz_ * v);
      float cx1 = t1 * (fx_ - ux_ * v1), cz1 = t1 * (fz_ - uz_ * v1);
      acrossRow = sqrtf((cx1 - cx0) * (cx1 - cx0) + (cz1 - cz0) * (cz1 - cz0)) * PX;
    }
    row.aa = 0.5f * std::max(alongRow, sqrtf(alongRow * acrossRow)) + 0.05f;
  }

  // ── Fita da missão e rota GPS ──
  ribbon_.count = 0;
  gps_.count = 0;
  if (view.plan && view.plan->length > 0) {
    const Polyline &line = view.plan->line;
    // Câmera alta (vista da pista inteira): 5 px da pista ficam menores que 1 px na tela, então o
    // passo cresce com a altura (perseguição: câmera a ~3,7 m, passo de 5 px)
    float step = std::min(STRIP_STEP_MAX, std::max(STRIP_STEP, STRIP_STEP * py_ / 3.7f * 0.6f));
    float s0 = std::max(0.0f, view.s - 15);
    float s1 = std::min({line.length, view.reveal, view.s + RIBBON_FADE_M * PX});
    if (s1 > s0) buildStrips(ribbon_, line, s0, s1, RIBBON_HALF, step);
    if (view.gpsVisible) {
      float g1 = view.gpsS0 + (view.gpsS1 - view.gpsS0) * view.gpsProgress;
      if (g1 > view.gpsS0 + 1) buildStrips(gps_, line, view.gpsS0, std::min(g1, line.length), GPS_HALF, step);
      pin_ = project(view.gpsPin.x * INV_PX, 0, view.gpsPin.y * INV_PX);
    }
  }
  prepareSigns(view);
  prepareCar(view);
}

void Scene::buildStrips(StripList &out, const Polyline &line, float s0, float s1, float half, float step) {
  // Nunca passa da capacidade fixa: trecho longo demais alarga o passo
  step = std::max(step, (s1 - s0) / (MAX_STRIPS - 1));
  int count = std::max(2, (int)ceilf((s1 - s0) / step) + 1);
  auto at = [&](int i) { return std::min(s1, s0 + i * step); };
  // três amostras por vez (atual, próxima, a seguinte), sem vetores temporários
  Vec2 pi = line.sample(at(0)).pos, pn = line.sample(at(1)).pos;
  Vec2 prevDir{0, 0};
  for (int i = 0; i + 1 < count && out.count < MAX_STRIPS; i++) {
    Vec2 pnn = i + 2 < count ? line.sample(at(i + 2)).pos : pn;
    Vec2 pts[2] = {pi, pn};
    float si = at(i);
    pi = pn, pn = pnn;
    Vec2 d = pts[1] - pts[0];
    float len = length(d);
    if (len < 0.01f) continue;
    Vec2 dir{d.x / len, d.y / len};
    Vec2 nextDir = dir;
    if (i + 2 < count) {
      Vec2 dn = pnn - pts[1];
      float ln = length(dn);
      if (ln > 0.01f) nextDir = {dn.x / ln, dn.y / ln};
    }
    Vec2 m0 = out.count == 0 || (prevDir.x == 0 && prevDir.y == 0) ? dir : unitBetween({0, 0}, prevDir + dir);
    Vec2 m1 = unitBetween({0, 0}, dir + nextDir);
    prevDir = dir;
    // caixa na tela pelos 4 cantos (com folga para a ponta da junção)
    Vec2 n{-dir.y * half * 1.15f, dir.x * half * 1.15f};
    Vec2 corners[4] = {pts[0] + n, pts[0] - n, pts[1] + n, pts[1] - n};
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    bool ok = true;
    for (Vec2 c : corners) {
      Projected p = project(c.x * INV_PX, 0, c.y * INV_PX);
      if (!p.ok) { ok = false; break; }
      minX = std::min(minX, p.x), maxX = std::max(maxX, p.x), minY = std::min(minY, p.y), maxY = std::max(maxY, p.y);
    }
    if (!ok || maxX < 0 || minX >= SCREEN_W || maxY < 0 || minY >= SCREEN_H) continue;
    Strip &s = out.items[out.count];
    s.p = pts[0];
    s.dir = dir;
    s.m0 = m0;
    s.m1 = m1;
    s.len = len;
    s.s0 = si;
    s.x0 = std::max(0, (int)floorf(minX) - 2);
    s.x1 = std::min(SCREEN_W, (int)ceilf(maxX) + 3);
    s.y0 = std::max(0, (int)floorf(minY) - 2);
    s.y1 = std::min(SCREEN_H, (int)ceilf(maxY) + 3);
    out.count++;
  }
  if (&out == &ribbon_) ribbonHalf_ = half; else gpsHalf_ = half;
}

void Scene::prepareSigns(const FrameView &view) {
  signs_.clear();
  signs_.reserve(trackdata::SIGN_COUNT);   // só aloca no 1º quadro, depois reaproveita
  for (int i = 0; i < trackdata::SIGN_COUNT; i++) {
    const SignDef &sign = trackdata::SIGNS[i];
    float X = sign.at.x * INV_PX, Z = sign.at.y * INV_PX;
    bool light = sign.kind == SignKind::TrafficLight;
    // signs.js: poste de 0,9 m (0,72 m no semáforo) e a placa encaixada no topo
    float postTop = light ? POST_HEIGHT * 0.8f : POST_HEIGHT;
    float center = light ? postTop + 0.2f : POST_HEIGHT + SIGN_SIZE / 2 - 0.04f;
    float size = light ? 0.44f / 0.88f : SIGN_SIZE;   // a caixa do semáforo ocupa 88% da imagem
    Projected c = project(X, center, Z);
    Projected ground = project(X, 0, Z);
    Projected postEnd = project(X, postTop, Z);
    Projected bottom = project(X, center - size / 2, Z);
    if (!c.ok || !ground.ok || !postEnd.ok || !bottom.ok) continue;
    if (c.depth > view.fogFar + 2) continue;
    // Eixo horizontal da placa no mundo. Quem olha a frente lê da esquerda para a direita;
    // por trás a face também fica legível (como a face traseira girada do JS), só que escura.
    // O semáforo é uma caixa: sempre de frente para a câmera.
    float vx = px_ - X, vz = pz_ - Z;
    float vl = sqrtf(vx * vx + vz * vz);
    float facingDot = vl > 1e-4f ? (sign.facing.x * vx + sign.facing.y * vz) / vl : 1;
    float hx, hz;
    if (light) {
      hx = rx_, hz = rz_;
    } else {
      hx = sign.facing.y, hz = -sign.facing.x;
      if (facingDot < 0) hx = -hx, hz = -hz;
    }
    Projected right = project(X + hx * size / 2, center, Z + hz * size / 2);
    Projected left = project(X - hx * size / 2, center, Z - hz * size / 2);
    if (!right.ok || !left.ok) continue;
    // Placa encostada na câmera (o carro passando rente a ela, ou a câmera indo para a vista de
    // cima): a projeção estoura e ela vira uma faixa cortando a tela. Some antes disso.
    float nearest = std::min(std::min(std::min(c.depth, ground.depth), std::min(postEnd.depth, bottom.depth)),
                             std::min(right.depth, left.depth));
    if (nearest < SIGN_NEAR_M) continue;
    if (fabsf(right.x - left.x) > SCREEN_W / 3.0f || fabsf(ground.y - c.y) > SCREEN_H) continue;
    SignDraw d;
    switch (sign.kind) {
      case SignKind::Point:
        d.sprite = sign.point == 'A' ? &sprites::point_A : sign.point == 'B' ? &sprites::point_B : &sprites::point_C;
        break;
      case SignKind::Stop: d.sprite = &sprites::stop; break;
      case SignKind::Detour: d.sprite = &sprites::detour; break;
      default:
        d.sprite = view.light == 0 ? &sprites::light_red : view.light == 1 ? &sprites::light_yellow
                   : view.light == 2 ? &sprites::light_green : &sprites::light_off;
    }
    float halfW = d.sprite->w / 2.0f, halfH = d.sprite->h / 2.0f;
    d.cx = c.x, d.cy = c.y;
    d.ax = (right.x - c.x) / halfW, d.ay = (right.y - c.y) / halfW;
    d.bx = (bottom.x - c.x) / halfH, d.by = (bottom.y - c.y) / halfH;
    if (fabsf(d.by) * d.sprite->h < 2) continue;
    d.tint = (!light && sign.kind != SignKind::Point && facingDot < 0) ? SIGN_BACK : WHITE;
    d.depth = ground.depth;
    d.fog = clamp01((ground.depth - view.fogNear) / std::max(1.0f, view.fogFar - view.fogNear));
    d.postX0 = ground.x, d.postY0 = ground.y;
    d.postX1 = postEnd.x, d.postY1 = postEnd.y;
    d.postWidth = std::max(1.2f, 0.048f * fpx_ / ground.depth);
    signs_.push_back(d);
  }
  std::sort(signs_.begin(), signs_.end(), [](const SignDraw &a, const SignDraw &b) { return a.depth > b.depth; });
}

void Scene::prepareCar(const FrameView &view) {
  car_ = CarDraw{};
  float cx = view.carX * INV_PX, cz = view.carY * INV_PX;
  float fx = view.carDirX, fz = view.carDirY;   // frente
  float rx = -fz, rz = fx;                       // direita
  const float cy = carsprites::CENTER_HEIGHT;

  // Sombra suave no chão: faixa de linhas da tela em que ela aparece
  shadowY0_ = SCREEN_H, shadowY1_ = -1;
  for (int k = 0; k < 4; k++) {
    float a = (k & 1 ? 1 : -1) * CAR_LEN * 0.85f, b = (k & 2 ? 1 : -1) * CAR_WID * 1.05f;
    Projected p = project(cx + fx * a + rx * b, 0, cz + fz * a + rz * b);
    if (!p.ok) continue;
    shadowY0_ = std::min(shadowY0_, (int)p.y - 1);
    shadowY1_ = std::max(shadowY1_, (int)p.y + 2);
  }

  Projected c = project(cx, cy, cz);
  if (!c.ok) return;
  car_.visible = true;
  car_.cx = c.x;
  car_.cy = c.y;
  car_.depth = c.depth;

  // De onde a câmera vê o carro: elevação e azimute (0 = bem atrás, + = pela direita)
  float dx = px_ - cx, dy = py_ - cy, dz = pz_ - cz;
  float horiz = sqrtf(dx * dx + dz * dz);
  float elev = atan2f(dy, horiz) * RAD2DEG;
  float az = atan2f(dx * rx + dz * rz, -(dx * fx + dz * fz)) * RAD2DEG;

  if (elev >= TOP_FROM_DEG) {
    // De cima: projeta os eixos do carro e monta a afim (1 px da imagem = 1/PPM m)
    float ppmScreen = fpx_ / c.depth;
    const bool small = ppmScreen < 0.6f * carsprites::TOP_PPM;
    car_.sprite = small ? &carsprites::TOP_SMALL : &carsprites::TOP;
    float step = 1.0f / (small ? carsprites::TOP_SMALL_PPM : carsprites::TOP_PPM);
    const float L = 0.5f;
    Projected pr = project(cx + rx * L, cy, cz + rz * L);   // +x da imagem = direita do carro
    Projected pb = project(cx - fx * L, cy, cz - fz * L);   // +y da imagem = para trás
    if (!pr.ok || !pb.ok) { car_.visible = false; return; }
    car_.affine = true;
    car_.ax = (pr.x - c.x) / L * step, car_.ay = (pr.y - c.y) / L * step;
    car_.bx = (pb.x - c.x) / L * step, car_.by = (pb.y - c.y) / L * step;
    return;
  }
  // Perseguição (ou intermediária): imagem do ângulo mais próximo, em escala
  bool mid = elev > (carsprites::CHASE_ELEVATION + carsprites::MID_ELEVATION) / 2;
  const Sprite *set = mid ? carsprites::MID : carsprites::CHASE;
  int count = mid ? carsprites::MID_COUNT : carsprites::CHASE_COUNT;
  float az0 = mid ? carsprites::MID_AZ0 : carsprites::CHASE_AZ0;
  float stepDeg = mid ? carsprites::MID_AZ_STEP : carsprites::CHASE_AZ_STEP;
  float ppm = mid ? carsprites::MID_PPM : carsprites::CHASE_PPM;
  int i = (int)lroundf((az - az0) / stepDeg);
  i = std::max(0, std::min(count - 1, i));
  car_.sprite = &set[i];
  car_.height = set[i].h * (fpx_ / c.depth) / ppm;
}

void Scene::drawCar(Canvas &c) const {
  if (!car_.visible) return;
  if (car_.affine) {
    c.spriteAffine(*car_.sprite, car_.cx, car_.cy, car_.ax, car_.ay, car_.bx, car_.by);
  } else {
    c.sprite(*car_.sprite, car_.cx, car_.cy - car_.height / 2, car_.height, 1.0f, WHITE, 0, FOG);
  }
}

// ── Chão ─────────────────────────────────────────────────────────────────
void Scene::shadeGround(float wx, float wy, float aa, bool carShadow, float &r, float &g, float &b) const {
  using namespace tracksdf;
  float gx = (wx - X0) * (1.0f / CELL), gy = (wy - Y0) * (1.0f / CELL);
  int ix = (int)floorf(gx), iy = (int)floorf(gy);
  float d = -32;   // fora da grade: campo, longe de qualquer linha
  int cls = 0;
  if (ix >= 0 && iy >= 0 && ix < W - 1 && iy < H - 1) {
    float fx = gx - ix, fy = gy - iy;
    const uint16_t *c0 = CELLS + iy * W + ix;
    const uint16_t *c1 = c0 + W;
    float d00 = (int8_t)(c0[0] & 0xFF), d10 = (int8_t)(c0[1] & 0xFF);
    float d01 = (int8_t)(c1[0] & 0xFF), d11 = (int8_t)(c1[1] & 0xFF);
    d = ((d00 + (d10 - d00) * fx) + ((d01 + (d11 - d01) * fx) - (d00 + (d10 - d00) * fx)) * fy) * (1.0f / UNITS_PER_PX);
    const uint16_t *near = (fy < 0.5f ? c0 : c1) + (fx < 0.5f ? 0 : 1);
    cls = *near >> 8;
  }
  int region = cls & 3;
  bool nearIsland = cls & 4;
  // Cor de base: asfalto de um lado da linha, ilha/campo do outro (misturados sob a linha)
  Rgb off = region == 2 || (region == 1 && nearIsland) ? ISLAND : FIELD;
  if (region == 0) off = FIELD;
  float laneK = clamp01((d + aa) / (2 * aa));
  r = off.r + (LANE.r - off.r) * laneK;
  g = off.g + (LANE.g - off.g) * laneK;
  b = off.b + (LANE.b - off.b) * laneK;

  // Campo: malha de pontos azuis com faixas de luz passando
  if (region == 0 && d < -aa) {
    float aaM = aa * 2 * INV_PX * 1.2f;
    if (aaM < 0.05f) {   // longe, os pontos somem antes de virar moiré
      float mx = wx * INV_PX, my = wy * INV_PX;
      float cx = fractf(mx * (1 / 0.46f)) - 0.5f, cy = fractf(my * (1 / 0.46f)) - 0.5f;
      float dist2 = (cx * cx + cy * cy) * (0.46f * 0.46f);
      float outer = 0.035f + aaM;
      if (dist2 < outer * outer) {
        float dotMask = 1 - smoothstep(0.035f - aaM, outer, sqrtf(dist2));
        float t = view_->time;
        float w1 = smoothstep(0.82f, 1, fastSin(mx * 0.21f + my * 0.08f - t * 0.9f));
        float w2 = smoothstep(0.86f, 1, fastSin(-mx * 0.06f + my * 0.19f - t * 0.6f + 1.7f));
        float ex = mx - 11, ey = my - 6;
        float w3 = smoothstep(0.9f, 1, fastSin(sqrtf(ex * ex + ey * ey) * 0.55f - t * 1.1f));
        float glow = clamp01(w1 + w2 * 0.8f + w3 * 0.7f);
        float far = 1 - smoothstep(0.018f, 0.05f, aaM);
        mixTo(r, g, b, DOT, dotMask * (0.22f + 0.78f * glow) * far);
      }
    }
  }
  // Sombra de contato das ilhas no asfalto
  if (nearIsland && d > 0 && d < SHADOW_HALF) {
    float a = 1 - d / SHADOW_HALF;
    mixTo(r, g, b, {15, 23, 41}, a * a * 0.16f);
  }
  // Linha preta translúcida
  float ad = fabsf(d);
  if (ad < LINE_HALF + aa) mixTo(r, g, b, LINE, LINE_OPACITY * bandCoverage(ad, LINE_HALF, aa));

  // Largada quadriculada (sólida)
  using namespace trackdata;
  float lx = wx - FINISH_AT.x, ly = wy - FINISH_AT.y;
  float u = lx * FINISH_DIR.x + ly * FINISH_DIR.y;
  float v = -lx * FINISH_DIR.y + ly * FINISH_DIR.x;
  if (fabsf(u) < FINISH_ALONG / 2 + aa && fabsf(v) < FINISH_ACROSS / 2 + aa) {
    // 2x2 amostras dentro do pixel: bordas do quadriculado suaves
    float black = 0;
    for (int k = 0; k < 4; k++) {
      float su = u + ((k & 1) ? 0.5f : -0.5f) * aa, sv = v + ((k & 2) ? 0.5f : -0.5f) * aa;
      if (fabsf(su) >= FINISH_ALONG / 2 || fabsf(sv) >= FINISH_ACROSS / 2) continue;
      int col = (int)((su + FINISH_ALONG / 2) / (FINISH_ALONG / FINISH_COLS));
      int row = (int)((sv + FINISH_ACROSS / 2) / (FINISH_ACROSS / FINISH_ROWS));
      if (((col + row) & 1) == 0) black += 0.25f;
    }
    mixTo(r, g, b, LINE, black);
  }
  // Sombra do carro (gradiente radial)
  if (!carShadow) return;
  float cx = wx - view_->carX, cy = wy - view_->carY;
  float cu = (cx * view_->carDirX + cy * view_->carDirY) / (CAR_LEN * 0.85f * PX);
  float cv = (-cx * view_->carDirY + cy * view_->carDirX) / (CAR_WID * 1.05f * PX);
  float rr = cu * cu + cv * cv;
  if (rr < 1) mixTo(r, g, b, {15, 22, 35}, 0.42f * (1 - sqrtf(rr)));
}

namespace {
// Shaders da fita e da rota GPS, separados do laço de pixels: além de legível, evita um erro
// interno do GCC 8 do xtensa (ESP32) com muitos floats vivos numa função só.

/** routeRibbon.js (fragmentShader). Devolve alfa 0..1 (0 = não desenha). */
__attribute__((noinline)) float ribbonShade(float t, float side, float aaSide, float carS, float reveal,
                                            float time, float &r, float &g, float &b) {
  float ahead = t - carS;
  if (ahead < -0.3f || t > reveal) return 0;
  float edge = smoothstep(0.84f - aaSide, 0.84f + aaSide, side) * (1 - smoothstep(1 - aaSide, 1, side));
  float phase = fractf((t + side * 0.22f) / 0.85f - time * 0.55f);
  float chevron = smoothstep(0, 0.06f, phase) * (1 - smoothstep(0.16f, 0.24f, phase));
  chevron *= 1 - smoothstep(0.55f, 0.7f, side);
  float fade = 1 - smoothstep(RIBBON_FADE_M * 0.35f, RIBBON_FADE_M, ahead);
  float nearK = smoothstep(-0.3f, 0.9f, ahead);
  r = ROUTE.r, g = ROUTE.g, b = ROUTE.b;
  mixTo(r, g, b, ROUTE_EDGE, edge * 0.7f);
  mixTo(r, g, b, WHITE, chevron * 0.55f);
  return std::min(0.92f, (0.2f + edge * 0.7f + chevron * 0.45f) * fade * nearK);
}

/** gpsPreview.js (routeFragment): contorno branco, miolo azul, luzes correndo. */
__attribute__((noinline)) float gpsShade(float vt, float side, float aaSide, float progress, float lenM,
                                         float time, float opacity, float &r, float &g, float &b) {
  if (vt > progress) return 0;
  float casing = smoothstep(0.62f - aaSide, 0.62f + aaSide, side);
  r = GPS_BLUE.r, g = GPS_BLUE.g, b = GPS_BLUE.b;
  mixTo(r, g, b, WHITE, casing);
  float run = fractf(vt * lenM / 0.7f - time * 0.9f);
  float dash = (1 - smoothstep(0, 0.18f, run)) * (1 - casing);
  mixTo(r, g, b, {199, 224, 255}, dash * 0.7f);
  float head = 1 - smoothstep(0, 0.05f, progress - vt);
  if (progress < 0.999f) mixTo(r, g, b, WHITE, head * 0.5f);
  return (1 - smoothstep(1 - aaSide, 1, side)) * opacity;
}

}  // namespace

void Scene::drawStrips(Canvas &c, const StripList &strips, bool gps) {
  const FrameView &v = *view_;
  float half = gps ? gpsHalf_ : ribbonHalf_;
  float carS = v.s * INV_PX, reveal = v.reveal * INV_PX;
  float gpsSpan = std::max(1.0f, v.gpsS1 - v.gpsS0);
  float gpsLenM = gpsSpan * INV_PX;
  int y0 = c.bandY, y1 = c.bandY + BAND_H;
  for (const Strip &s : strips) {
    if (s.y1 <= y0 || s.y0 >= y1) continue;
    Vec2 e = s.p + s.dir * s.len;
    for (int y = std::max(s.y0, y0); y < std::min(s.y1, y1); y++) {
      const Row &row = rows_[y];
      if (!row.ground) continue;
      float aaSide = row.aa * 2 / half * 1.5f;
      for (int x = s.x0; x < s.x1; x++) {
        float wx = row.wx0 + x * row.dwx, wy = row.wy0 + x * row.dwy;
        float qx = wx - s.p.x, qy = wy - s.p.y;
        // dentro do trecho: depois da junção de entrada e antes da de saída (sem frestas)
        if (qx * s.m0.x + qy * s.m0.y < 0) continue;
        if ((wx - e.x) * s.m1.x + (wy - e.y) * s.m1.y >= 0) continue;
        float side = fabsf(-qx * s.dir.y + qy * s.dir.x) / half;
        if (side > 1 + aaSide) continue;
        float travel = s.s0 + qx * s.dir.x + qy * s.dir.y;   // px
        float r, g, b;
        float alpha = gps ? gpsShade((travel - v.gpsS0) / gpsSpan, side, aaSide, v.gpsProgress, gpsLenM, v.time,
                                     v.gpsOpacity, r, g, b)
                          : ribbonShade(travel * INV_PX, side, aaSide, carS, reveal, v.time, r, g, b);
        if (alpha <= 0.004f) continue;
        // névoa como no resto da cena
        mixTo(r, g, b, FOG, row.fog);
        c.blendPx(x, y, {(uint8_t)r, (uint8_t)g, (uint8_t)b}, (int)(alpha * 255));
      }
    }
  }
}

void Scene::drawBand(Canvas &c) {
  const FrameView &v = *view_;
  // ── Céu e chão ──
  for (int y = c.bandY; y < c.bandY + BAND_H; y++) {
    uint16_t *dst = c.row(y);
    const Row &row = rows_[y];
    if (!row.ground) {
      // céu em degradê: claro no alto, cor da névoa no horizonte
      float k = horizonY_ > 0 ? clamp01((horizonY_ - y) / (SCREEN_H * 0.45f)) : 1;
      float r = FOG.r + (SKY_TOP.r - FOG.r) * k, g = FOG.g + (SKY_TOP.g - FOG.g) * k,
            b = FOG.b + (SKY_TOP.b - FOG.b) * k;
      for (int x = 0; x < SCREEN_W; x++) dst[x] = toPixelDither(r, g, b, x, y);
      continue;
    }
    bool shadowRow = y >= shadowY0_ && y <= shadowY1_;
    // trechos cobertos por painéis opacos do HUD: não calcula o chão ali
    int skip0[MAX_OCCLUDERS], skip1[MAX_OCCLUDERS], skips = 0;
    for (int k = 0; k < occluderCount_; k++) {
      const Occluder &o = occluders_[k];
      if (y < o.y + 1 || y >= o.y + o.h - 1) continue;
      int inset = (y < o.y + o.r || y >= o.y + o.h - o.r) ? o.r : 1;
      skip0[skips] = o.x + inset;
      skip1[skips++] = o.x + o.w - inset;
    }
    float wx = row.wx0, wy = row.wy0;
    for (int x = 0; x < SCREEN_W; x++, wx += row.dwx, wy += row.dwy) {
      bool hidden = false;
      for (int k = 0; k < skips; k++) {
        if (x >= skip0[k] && x < skip1[k]) {
          wx += row.dwx * (skip1[k] - 1 - x);
          wy += row.dwy * (skip1[k] - 1 - x);
          x = skip1[k] - 1;
          hidden = true;
          break;
        }
      }
      if (hidden) continue;
      float r, g, b;
      shadeGround(wx, wy, row.aa, shadowRow, r, g, b);
      if (row.fog > 0) mixTo(r, g, b, FOG, row.fog);
      dst[x] = toPixelDither(r, g, b, x, y);
    }
  }
  // ── Fita da missão e rota GPS ──
  drawStrips(c, ribbon_, false);
  if (v.gpsVisible) {
    drawStrips(c, gps_, true);
  }
  // ── Placas e carro, de longe para perto ──
  bool carDrawn = false;
  for (const SignDraw &s : signs_) {
    if (!carDrawn && s.depth < car_.depth) {
      drawCar(c);
      carDrawn = true;
    }
    float spanY = fabsf(s.by) * s.sprite->h / 2 + fabsf(s.ay) * s.sprite->w / 2;
    if (s.postY0 < c.bandY - 2 && s.cy + spanY < c.bandY - 2) continue;
    Rgb post = POST;
    post.r += (uint8_t)((FOG.r - post.r) * s.fog);
    post.g += (uint8_t)((FOG.g - post.g) * s.fog);
    post.b += (uint8_t)((FOG.b - post.b) * s.fog);
    c.line(s.postX0, s.postY0, s.postX1, s.postY1, s.postWidth, post);
    c.spriteAffine(*s.sprite, s.cx, s.cy, s.ax, s.ay, s.bx, s.by, s.tint, s.fog, FOG);
  }
  if (!carDrawn) drawCar(c);
  // Prévia GPS: anel pulsando em volta do carro (gpsPreview.js: carRing)
  if (v.gpsVisible && car_.visible) {
    float pulse = fractf(v.time * 0.8f);
    float r = 9 + pulse * 10;
    int a = (int)(255 * v.gpsOpacity * (1 - pulse));
    c.roundRectStroke(car_.cx - r, car_.cy - r, 2 * r, 2 * r, r, 2.0f, GPS_BLUE, a);
    c.roundRectStroke(car_.cx - 9, car_.cy - 9, 18, 18, 9, 1.6f, GPS_BLUE, (int)(200 * v.gpsOpacity));
  }
  // ── Alfinete da prévia GPS ──
  if (v.gpsVisible && pin_.ok) {
    int a = (int)(255 * v.gpsOpacity);
    c.circle(pin_.x, pin_.y - 13, 11, WHITE, a);
    c.circle(pin_.x, pin_.y - 13, 9, GPS_BLUE, a);
    c.circle(pin_.x, pin_.y - 13, 3.2f, WHITE, a);
    float tri[6] = {pin_.x - 6, pin_.y - 8, pin_.x + 6, pin_.y - 8, pin_.x, pin_.y};
    c.convex(tri, 3, GPS_BLUE, a);
  }
}
