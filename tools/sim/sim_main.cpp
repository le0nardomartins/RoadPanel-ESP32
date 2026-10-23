// Ambiente de testes no PC: roda o App, a Scene e o Hud do firmware (os mesmos .cpp do ESP32)
// em tempo virtual, sem display nem serial. Um carro falso manda os dados como o main.py pela
// serial (o painel não tem mais modo simulação) e o teste confere:
//
//  - sem dados, só a logo do time; ela sai quando os dados chegam e volta quando eles param;
//  - cada missão (AB, AC, BA, BC, CA, CB) coleta no 1º ponto e entrega no último, na ordem;
//  - o carro só anda no sentido da pista (mão única): trajetória ligada nó a nó e sempre a favor
//    do sentido de cada trecho;
//  - ao parar numa placa (ponto ou PARE) a câmera abre e mostra a pista inteira, segura essa
//    vista enquanto o carro espera e volta para trás do carro quando ele segue;
//  - trocar os pontos (missão nova) recomeça na partida, sem somar os pontos antigos.
//
// Salva quadros em PNG (480x320, como no TFT) na pasta de saída. Uso:
//   tools\simuild.bat      (compila a DLL e roda pelo run.py; quadros em tools\sim\out)

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <new>
#include <string>
#include <vector>

#include "../../src/app/App.h"
#include "../../src/render/Hud.h"
#include "../../src/render/Scene.h"

// Conta alocações (new) feitas pelo firmware: no ESP32 sem PSRAM um new que falha aborta e reinicia
// a placa (foi o que reiniciava a simulação ao abrir o zoom no ponto de coleta).
static long allocCount = 0;
static size_t allocBiggest = 0;
void *operator new(size_t n) {
  allocCount++;
  allocBiggest = std::max(allocBiggest, n);
  if (void *p = malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }

namespace {

uint32_t STEP_MS = 33;   // ~30 fps (o caso de quadros lentos usa 166)
constexpr float PX_TO_M = trackdata::METERS_PER_PX;

std::string outDir = "tools/sim/out";
int failures = 0;

void check(bool ok, const char *fmt, const char *a = "", double b = 0) {
  if (ok) return;
  failures++;
  printf("  FALHOU: ");
  printf(fmt, a, b);
  printf("\n");
}

// ── PNG sem compressão (deflate "stored"), suficiente para conferir os quadros ──
uint32_t crcTable[256];
void initCrc() {
  for (uint32_t n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    crcTable[n] = c;
  }
}
uint32_t crc(const uint8_t *p, size_t n, uint32_t c = 0xFFFFFFFFu) {
  for (size_t i = 0; i < n; i++) c = crcTable[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return c;
}
void be32(std::vector<uint8_t> &v, uint32_t x) {
  for (int s = 24; s >= 0; s -= 8) v.push_back((uint8_t)(x >> s));
}
void chunk(FILE *f, const char *type, const std::vector<uint8_t> &data) {
  std::vector<uint8_t> out;
  be32(out, (uint32_t)data.size());
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  be32(out, crc(out.data() + 4, out.size() - 4) ^ 0xFFFFFFFFu);
  fwrite(out.data(), 1, out.size(), f);
}
void writePng(const std::string &path, const uint16_t *frame) {
  std::vector<uint8_t> raw;
  for (int y = 0; y < SCREEN_H; y++) {
    raw.push_back(0);
    for (int x = 0; x < SCREEN_W; x++) {
      Rgb c = fromPixel(frame[y * SCREEN_W + x]);
      raw.push_back(c.r), raw.push_back(c.g), raw.push_back(c.b);
    }
  }
  std::vector<uint8_t> z{0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) a = (a + v) % 65521, b = (b + a) % 65521;
  for (size_t i = 0; i < raw.size(); i += 65535) {
    size_t n = std::min<size_t>(65535, raw.size() - i);
    z.push_back(i + n == raw.size());
    z.push_back((uint8_t)n), z.push_back((uint8_t)(n >> 8));
    z.push_back((uint8_t)~n), z.push_back((uint8_t)(~n >> 8));
    z.insert(z.end(), raw.begin() + i, raw.begin() + i + n);
  }
  be32(z, (b << 16) | a);
  std::vector<uint8_t> ihdr;
  be32(ihdr, SCREEN_W), be32(ihdr, SCREEN_H);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) { printf("  não abriu %s\n", path.c_str()); return; }
  const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  fwrite(sig, 1, 8, f);
  chunk(f, "IHDR", ihdr);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", {});
  fclose(f);
}

// ── Quadro inteiro, faixa a faixa, como no main.cpp ──
Scene scene;
Hud hud;
std::vector<uint16_t> frame(SCREEN_W * SCREEN_H);

void drawFrame(const FrameView &view, uint32_t now) {
  scene.prepare(view);
  Occluder occ[MAX_OCCLUDERS];
  scene.setOccluders(occ, hud.occluders(view, occ));
  for (int band = 0; band < BAND_COUNT; band++) {
    Canvas c;
    c.begin(frame.data() + band * BAND_H * SCREEN_W, band * BAND_H);
    scene.drawBand(c);
    hud.draw(c, view, now);
  }
}

void render(const FrameView &view, uint32_t now, const std::string &name) {
  drawFrame(view, now);
  writePng(outDir + "/" + name + ".png", frame.data());
}

/** Pixels escuros do quadro. Normal: ~6 mil (carro, linhas, textos). Uma placa encostada na câmera
 *  virava uma faixa escura de dezenas de milhares cortando a tela. */
int darkPixels() {
  int n = 0;
  for (uint16_t px : frame) {
    Rgb c = fromPixel(px);
    n += c.r * 299 + c.g * 587 + c.b * 114 < 90 * 1000;
  }
  return n;
}

// ── Projeção igual à da Scene (para conferir o enquadramento) ──
struct ScreenPt {
  float x, y;
  bool ok;
};
ScreenPt projectGround(const FrameView &v, Vec2 trackPx) {
  float fx = v.camLook.x - v.camPos.x, fy = v.camLook.y - v.camPos.y, fz = v.camLook.z - v.camPos.z;
  float fl = sqrtf(fx * fx + fy * fy + fz * fz);
  fx /= fl, fy /= fl, fz /= fl;
  float rx = -fz, rz = fx, rl = sqrtf(rx * rx + rz * rz);
  if (rl < 1e-5f) rx = 1, rz = 0; else rx /= rl, rz /= rl;
  float ux = fy * rz, uy = fz * rx - fx * rz, uz = -fy * rx;
  if (uy < 0) ux = -ux, uy = -uy, uz = -uz;
  float fpx = (SCREEN_H / 2.0f) / tanf(v.fovDeg * (float)M_PI / 360.0f);
  float dx = trackPx.x * PX_TO_M - v.camPos.x, dy = -v.camPos.y, dz = trackPx.y * PX_TO_M - v.camPos.z;
  float z = dx * fx + dy * fy + dz * fz;
  if (z < 0.05f) return {0, 0, false};
  return {SCREEN_W / 2.0f + (dx * rx + dz * rz) / z * fpx, SCREEN_H / 2.0f - (dx * ux + dy * uy + dz * uz) / z * fpx,
          true};
}

/** A pista inteira aparece na tela, fora dos cartões do HUD? Devolve a fração de pontos visíveis. */
float trackVisible(const TrackNetwork &net, const FrameView &v, const Occluder *occ, int occCount) {
  int total = 0, seen = 0;
  for (int e = 0; e < net.edgeCount(); e++) {
    const Polyline &line = net.edge(e).line;
    for (float s = 0; s <= line.length; s += 10) {
      total++;
      ScreenPt p = projectGround(v, line.sample(s).pos);
      if (!p.ok || p.x < 0 || p.y < 0 || p.x >= SCREEN_W || p.y >= SCREEN_H) continue;
      bool hidden = false;
      for (int k = 0; k < occCount; k++) {
        const Occluder &o = occ[k];
        hidden |= p.x >= o.x && p.x < o.x + o.w && p.y >= o.y && p.y < o.y + o.h;
      }
      if (!hidden) seen++;
    }
  }
  return total ? (float)seen / total : 0;
}

/** Mão única: trechos ligados nó a nó e o carro sempre no sentido do trecho em que está. */
void checkOneWay(const TrackNetwork &net, const Plan &plan, float s, int &wrongWay) {
  int index = -1;
  for (size_t i = 0; i < plan.ranges.size(); i++) {
    if (s >= plan.ranges[i].s0 && s <= plan.ranges[i].s1) { index = (int)i; break; }
  }
  if (index < 0) return;
  Sample p = plan.line.sample(s);
  const Polyline &edge = net.edge(plan.edges[index]).line;
  Projection hit = edge.project(p.pos);
  if (hit.d > 30) return;   // na curva da junção, entre dois trechos
  if (dot(p.dir, edge.sample(hit.s).dir) < -0.3f) wrongWay++;
}

/**
 * Carro "verdadeiro" mandando dados pela serial, como o main.py + tools/serial_bridge.py: anda na
 * trajetória da missão, para 3 s em cada PARE (stop_active), vê semáforos e desvios, tira o ponto
 * de nav.route ao chegar nele e trava ali (point_stop_latched) até alguém apertar o botão RUN.
 * reportScale > 1 faz a telemetria mentir a velocidade (a estimativa do painel passa à frente).
 */
constexpr uint32_t CARGO_PRESS_MS = 3000;   // quem colocou/retirou a carga aperta o RUN 3 s depois
struct FakeCar {
  TrackNetwork net;
  Plan truth;
  std::string route;
  float sTrue = 0, speedMs = 0.40f, reportScale = 1.0f;
  float overshootPx = 15;   // depois da linha de chegada anda mais um pouco e para
  uint32_t stopUntil = 0, lightUntil = 0, detourUntil = 0, latchedAt = 0;
  size_t nextEvent = 0;
  bool latched = false, autoPress = true;
  int presses = 0, confirmed = 0;
  double ts = 0;
  float battery = 84;
  int batteryState = 3;   // descarregando
  int clockMinutes = 14 * 60 + 32;

  explicit FakeCar(const std::string &mission)
      : truth(buildPlan(net, {trackdata::CAR_START_EDGE, trackdata::CAR_START_S}, mission, PlanMode::Shortest)),
        route(mission) {}

  bool parked() const { return sTrue >= truth.length + overshootPx; }
  void pressRun() {
    if (!latched) return;
    latched = false;
    presses++;   // telemetry.run_button_presses
  }
  /** Um passo do carro; manda uma linha (~10 por segundo) e devolve se mandou. */
  bool step(App &app, uint32_t now, uint32_t dtMs) {
    if (latched && autoPress && now - latchedAt >= CARGO_PRESS_MS) pressRun();
    bool stopped = now < stopUntil || latched || parked();
    if (!stopped) sTrue = std::min(truth.length + overshootPx, sTrue + speedMs / PX_TO_M * dtMs / 1000.0f);
    // Marcos que o carro verdadeiro vê ao passar (com a mesma antecedência do Localizer)
    while (nextEvent < truth.events.size()) {
      const PlanEvent &e = truth.events[nextEvent];
      float lead = e.type == EventType::Detour ? 40 : e.type == EventType::TrafficLight ? 90
                   : e.type == EventType::Stop ? 30 : 0;
      if (sTrue < e.s - lead) break;
      if (e.type == EventType::Stop) stopUntil = now + 3000;
      if (e.type == EventType::TrafficLight) lightUntil = now + 1500;
      if (e.type == EventType::Detour) detourUntil = now + 600;
      if (e.type == EventType::Point && e.target && !route.empty() && route[0] == e.point) {
        route.erase(0, 1);   // o carro confirmou o ponto: nav.route encolhe
        confirmed++;
        latched = true;      // e trava ali até o botão RUN
        latchedAt = now;
      }
      nextEvent++;
    }
    stopped = now < stopUntil || latched || parked();
    if (now % 99 >= dtMs && dtMs < 99) return false;
    VehicleData d;
    ts += 0.1;
    d.ts = ts;
    d.route = route;
    d.routeText = route;
    d.running = true;
    d.speed = stopped ? 0 : speedMs * reportScale;
    d.pwm = stopped ? 0 : 120;
    d.runPresses = presses;
    d.battery = battery;
    d.batteryState = batteryState;
    d.controlMode = "AUTOMÁTICO";
    d.clockMinutes = clockMinutes;
    d.stopActive = now < stopUntil;   // placa PARE (a trava no ponto não liga stop_active)
    d.trafficLight = now < lightUntil ? 2 : -1;
    d.rightDetour = now < detourUntil;
    app.onVehicleData(d, now);
    return true;
  }
};

struct Arrival {
  std::string badge;
  uint32_t at;
  Vec2 pos;
  int done;
  bool stop;
};

struct RunResult {
  std::vector<Arrival> arrivals;
  bool finished = false;
  int wrongWay = 0;
  int badLinks = 0;
  float maxJumpPx = 0;
  float worstZoomVisible = 1;
  int zoomChecks = 0;
  int stuckZoom = 0;     // vista de cima que não voltou para trás do carro quando ele andou
  int movedInZoom = 0;   // carro andando na tela com o carro de verdade parado na placa
  uint32_t end = 0;      // relógio no fim da rodada (para continuar com o mesmo App)
};

/**
 * Roda o App com o carro falso por até maxMs. Fotografa cada chegada (já com a vista da pista
 * inteira) e confere enquadramento, sentido e saltos. stopAfterFinish: para 1 s depois de
 * "Percurso finalizado".
 */
RunResult run(App &app, FakeCar &car, const std::string &tag, uint32_t maxMs, bool stopAfterFinish, bool shots,
              uint32_t start = 1000) {
  TrackNetwork net;
  RunResult r;
  FrameView view;
  bool prevGps = false;
  uint32_t lastPlanSerial = 0, finishedAt = 0;
  float lastX = NAN, lastY = NAN;
  uint32_t zoomShotAt = 0, chaseShotAt = 0;
  std::string zoomName;
  uint32_t movingSince = 0;
  for (uint32_t now = start; now < start + maxMs; now += STEP_MS) {
    r.end = now;
    car.step(app, now, STEP_MS);
    app.update(now, view);
    const HudView &h = view.hud;
    const Plan &plan = *view.plan;

    if (plan.serial != lastPlanSerial) {
      lastPlanSerial = plan.serial;
      for (size_t i = 1; i < plan.edges.size(); i++) {
        std::vector<int> next = net.successors(plan.edges[i - 1]);
        bool linked = std::find(next.begin(), next.end(), plan.edges[i]) != next.end();
        if (!linked) r.badLinks++;
      }
    }
    checkOneWay(net, plan, view.s, r.wrongWay);

    // O carro nunca "pula" (reinício da missão apareceria aqui)
    if (!isnan(lastX)) r.maxJumpPx = std::max(r.maxJumpPx, hypotf(view.carX - lastX, view.carY - lastY));
    lastX = view.carX, lastY = view.carY;

    // Chegada numa placa: a prévia GPS abre
    bool gps = h.gps && h.gpsOpacity >= 1;
    if (gps && !prevGps) {
      Arrival a{h.gpsStop ? "PARE" : h.gpsBadge, now, {view.carX, view.carY}, h.missionDone, h.gpsStop};
      r.arrivals.push_back(a);
      zoomShotAt = now + 2500;   // câmera já assentada na vista de cima
      char name[64];
      snprintf(name, sizeof name, "%s_%02d_%s", tag.c_str(), (int)r.arrivals.size(),
               a.stop ? "pare" : (a.badge.substr(0, a.badge.find(' ')) + "_" + a.badge.back()).c_str());
      zoomName = name;
      for (char &c : zoomName) c = c == ' ' ? '_' : c;
    }
    if (zoomShotAt && now >= zoomShotAt) {
      zoomShotAt = 0;
      // A tela da carga cobre a pista de propósito: o enquadramento é conferido sem ela
      FrameView bare = view;
      bare.hud.cargo = false;
      Occluder occ[MAX_OCCLUDERS];
      int n = hud.occluders(bare, occ);
      float visible = trackVisible(net, bare, occ, n);
      r.worstZoomVisible = std::min(r.worstZoomVisible, visible);
      r.zoomChecks++;
      // Parado esperando: no máximo o fim da frenagem (~0,25 s) depois de a prévia abrir
      const Arrival &a = r.arrivals.back();
      if (hypotf(view.carX - a.pos.x, view.carY - a.pos.y) > 8) r.movedInZoom++;
      if (shots) render(view, now, zoomName);
    }
    // Depois de seguir: a câmera volta para trás do carro (perseguição, perto do chão)
    bool moving = strcmp(h.speed, "0,00") != 0;
    if (moving && !h.gps) {
      if (!movingSince) movingSince = now;
      if (now - movingSince > 2000 && view.camPos.y > 6) r.stuckZoom++;
      if (!r.arrivals.empty() && chaseShotAt == 0 && now - r.arrivals.back().at > 6000 && shots) {
        chaseShotAt = now;
        render(view, now, tag + "_seguindo");
      }
    } else {
      movingSince = 0;
    }
    prevGps = gps;

    // (o HUD atualiza a cada 120 ms: logo depois de uma missão nova ele ainda pode mostrar "Fim")
    if (h.icon == ManeuverIcon::Done && !r.finished && now - start > 500) {
      r.finished = true;
      finishedAt = now;
      if (shots) render(view, now, tag + "_finalizado");
    }
    if (stopAfterFinish && r.finished && now - finishedAt > 1000) break;
  }
  return r;
}

}  // namespace

/** Grava um quadro a cada everyMs de uma missão inteira (para procurar defeitos de desenho). */
extern "C" __declspec(dllexport) int roadpanel_frames(const char *out, const char *route, int everyMs, int stepMs) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  if (out && *out) outDir = out;
  initCrc();
  App *app = new App();
  FakeCar car(route);
  FrameView view;
  uint32_t nextShot = 0, finishedAt = 0;
  int count = 0;
  for (uint32_t now = 1000; now < 1000 + 300000; now += stepMs) {
    car.step(*app, now, stepMs);
    app->update(now, view);
    if (now >= nextShot) {
      nextShot = now + everyMs;
      char name[64];
      snprintf(name, sizeof name, "q_%s_%06u", route, now - 1000);
      render(view, now, name);
      count++;
    }
    if (view.hud.icon == ManeuverIcon::Done && !finishedAt && now > 2000) finishedAt = now;
    if (finishedAt && now - finishedAt > 2000) break;
  }
  printf("%d quadros em %s\n", count, outDir.c_str());
  delete app;
  return 0;
}

// Compilado como DLL e chamado pelo run.py: o Smart App Control do Windows bloqueia .exe novos
// sem assinatura, mas deixa o Python (assinado) carregar a DLL.
extern "C" __declspec(dllexport) int roadpanel_sim(const char *out) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  if (out && *out) outDir = out;
  initCrc();
  failures = 0;

  // ── 0) Sem dados: só a logo do time; some quando os dados chegam e volta quando eles param ──
  {
    printf("Tela da logo sem dados\n");
    App *app = new App();
    FrameView view;
    uint32_t now = 1000;
    bool logoAtBoot = true, closed = false, backOffline = false, backStale = false;
    for (; now < 3000; now += STEP_MS) {
      app->update(now, view);
      logoAtBoot &= view.hud.logo && view.hud.logoOpen >= 1;
      if (now >= 2000 && now < 2000 + STEP_MS) {
        render(view, now, "tela_01_sem_dados_logo_do_time");
        Occluder occ[MAX_OCCLUDERS];
        check(hud.occluders(view, occ) == 1 && occ[0].w == SCREEN_W, "%stela da logo não cobre a cena", "");
      }
    }
    FakeCar car("AC");
    for (; now < 9000; now += STEP_MS) {   // dados chegando
      car.step(*app, now, STEP_MS);
      app->update(now, view);
      if (now >= 3000 + 130 && now < 3000 + 130 + STEP_MS) render(view, now, "tela_02_dados_chegaram_logo_saindo");
      if (now >= 3000 + CARGO_FADE_MS + 2 * STEP_MS && !view.hud.logo) closed = true;
    }
    for (; now < 13500; now += STEP_MS) {   // cabo solto: sem linhas por mais de 3 s
      app->update(now, view);
      if (view.hud.logo && view.hud.logoOpen >= 1) backOffline = true;
    }
    render(view, now, "tela_03_sem_sinal_logo_de_volta");
    for (; now < 16000; now += STEP_MS) car.step(*app, now, STEP_MS), app->update(now, view);   // volta
    check(!view.hud.logo, "%slogo não saiu com os dados de volta", "");
    for (; now < 21000; now += STEP_MS) {   // main.py travado: linhas chegam com o mesmo _ts
      if (now % 99 < STEP_MS) {
        VehicleData d;
        d.ts = 42;
        d.route = "C";
        app->onVehicleData(d, now);
      }
      app->update(now, view);
      if (view.hud.logo && view.hud.logoOpen >= 1) backStale = true;
    }
    printf("  logo ao ligar: %s, fechou com dados: %s, voltou sem sinal: %s, com dados parados: %s\n",
           logoAtBoot ? "sim" : "não", closed ? "sim" : "não", backOffline ? "sim" : "não", backStale ? "sim" : "não");
    check(logoAtBoot, "%slogo não aparece ao ligar sem dados", "");
    check(closed, "%slogo não saiu quando os dados chegaram", "");
    check(backOffline, "%slogo não voltou sem sinal", "");
    check(backStale, "%slogo não voltou com os dados parados", "");
    delete app;
  }

  // ── 1) Cada missão de 2 pontos: coleta no 1º, entrega no 2º ──
  const char *missions[] = {"AB", "AC", "BA", "BC", "CA", "CB"};
  for (const char *mission : missions) {
    App *app = new App();   // App tem a trajetória inteira; no heap para não estourar a pilha
    FakeCar car(mission);
    printf("Missão %c -> %c\n", mission[0], mission[1]);
    RunResult r = run(*app, car, std::string("missao_") + mission, 400000, true, true);
    std::vector<const Arrival *> points;
    for (const Arrival &a : r.arrivals) {
      printf("  %6.1f s  %-12s carro em (%.0f, %.0f)\n", (a.at - 1000) / 1000.0, a.badge.c_str(), a.pos.x, a.pos.y);
      if (!a.stop) points.push_back(&a);
    }
    std::string pickup = std::string("Coleta ") + mission[0], delivery = std::string("Entrega ") + mission[1];
    check(points.size() == 2, "%sesperava 2 chegadas em pontos, teve %.0f", "", (double)points.size());
    if (points.size() == 2) {
      check(points[0]->badge == pickup, "1ª parada devia ser '%s'", pickup.c_str());
      check(points[1]->badge == delivery, "2ª parada devia ser '%s'", delivery.c_str());
      check(points[1]->done > points[0]->done, "%smissão não avançou entre coleta e entrega", "");
    }
    check(r.finished, "%smissão não terminou", "");
    check(r.badLinks == 0, "%strajetória pulou entre trechos não ligados (%.0f)", "", r.badLinks);
    check(r.wrongWay == 0, "%scarro andou na contramão em %.0f quadros", "", r.wrongWay);
    check(r.maxJumpPx < 6, "%scarro saltou %.1f px (reinício?)", "", r.maxJumpPx);
    check(r.zoomChecks > 0 && r.worstZoomVisible > 0.97f, "%spista inteira não coube no zoom (%.0f%% visível)", "",
          r.worstZoomVisible * 100);
    check(r.stuckZoom == 0, "%scâmera não voltou para trás do carro (%.0f quadros)", "", r.stuckZoom);
    check(r.movedInZoom == 0, "%scarro andou na tela parado na placa (%.0f vezes)", "", r.movedInZoom);
    printf("  pista visível no zoom: %.0f%%, maior salto: %.1f px\n", r.worstZoomVisible * 100, r.maxJumpPx);
    delete app;
  }

  // ── 1c) Desenho: nenhum quadro com faixa/mancha escura cortando a tela ──
  for (const char *mission : {"AC", "BC", "CA"}) {
    App *app = new App();
    FakeCar car(mission);
    FrameView view;
    int worst = 0;
    uint32_t worstAt = 0, finishedAt = 0;
    long drawAllocs = 0, frames = 0;
    size_t biggest = 0;
    bool firstFrame = true;
    for (uint32_t now = 1000; now < 1000 + 300000; now += STEP_MS) {
      car.step(*app, now, STEP_MS);
      app->update(now, view);
      long before = allocCount;
      allocBiggest = 0;
      drawFrame(view, now);
      if (!firstFrame) {   // o 1º quadro reserva a lista de placas
        drawAllocs += allocCount - before;
        frames++;
        biggest = std::max(biggest, allocBiggest);
      }
      firstFrame = false;
      int dark = view.hud.logo ? 0 : darkPixels();   // a logo é preta de propósito
      if (dark > worst) worst = dark, worstAt = now;
      if (view.hud.icon == ManeuverIcon::Done && !finishedAt && now > 2000) finishedAt = now;
      if (finishedAt && now - finishedAt > 1000) break;
    }
    printf("Desenho %c -> %c: pior quadro com %d pixels escuros (%.2f s)\n", mission[0], mission[1], worst,
           (worstAt - 1000) / 1000.0);
    if (worst > 12000) {
      delete app;
      app = new App();   // refaz até o pior quadro e grava para conferir
      FakeCar again(mission);
      for (uint32_t now = 1000; now <= worstAt; now += STEP_MS) again.step(*app, now, STEP_MS), app->update(now, view);
      render(view, worstAt, std::string("defeito_") + mission);
    }
    check(worst <= 12000, "%smancha escura cortando a tela (%.0f pixels escuros)", "", worst);
    printf("  desenho: %.1f alocações por quadro, maior bloco %zu bytes\n",
           frames ? (double)drawAllocs / frames : 0.0, biggest);
    // Só textos curtos do HUD podem alocar; a fita e a rota GPS usam memória fixa (Scene::StripList)
    check(biggest <= 256, "%sdesenho pediu um bloco de %.0f bytes num quadro (no ESP32 pode faltar e reiniciar)", "",
          (double)biggest);
    delete app;
  }

  // ── 1b) Quadros lentos (~6 fps, como no ESP32): o painel não perde a PARE nem os pontos ──
  {
    STEP_MS = 166;
    App *app = new App();
    FakeCar car("BC");
    printf("Missão B -> C a ~6 fps\n");
    RunResult r = run(*app, car, "lento", 400000, true, false);
    std::string seen;
    int stops = 0;
    for (const Arrival &a : r.arrivals) {
      if (a.stop) stops++; else seen += a.badge + "; ";
    }
    printf("  paradas: %s%d PARE, terminou: %s\n", seen.c_str(), stops, r.finished ? "sim" : "não");
    check(seen == "Coleta B; Entrega C; ", "%sa 6 fps não coletou em B e entregou em C", "");
    check(stops == 2, "%sa 6 fps parou %.0f vez(es) na PARE (esperava 2)", "", stops);
    check(r.finished, "%sa 6 fps o percurso não terminou", "");
    STEP_MS = 33;
    delete app;
  }

  // ── 2) Fim do percurso e missão nova pela serial: recomeça na partida ──
  {
    App *app = new App();
    FakeCar car("AC");
    printf("Percurso completo A -> C\n");
    RunResult r = run(*app, car, "fim", 400000, true, false);
    check(r.finished, "%spercurso não terminou", "");
    printf("Nova missão B -> A pela serial, recomeçando na partida\n");
    FakeCar next("BA");
    next.ts = car.ts;
    FrameView a, b;
    {
      App fresh;
      fresh.update(1000, a);
    }
    next.step(*app, r.end + STEP_MS - (r.end + STEP_MS) % 99, STEP_MS);   // a primeira linha já com B -> A
    app->update(r.end + STEP_MS, b);
    float off = hypotf(a.carX - b.carX, a.carY - b.carY);
    check(off < 2, "%snova missão não voltou para a partida (%.1f px dela)", "", off);
    RunResult r2 = run(*app, next, "fim_nova", 400000, true, false, r.end + 2 * STEP_MS);
    std::string seen;
    for (const Arrival &x : r2.arrivals) if (!x.stop) seen += x.badge + "; ";
    printf("  paradas: %s maior salto: %.1f px\n", seen.c_str(), r2.maxJumpPx);
    check(seen == "Coleta B; Entrega A; ", "%snova missão não coletou em B e entregou em A", "");
    check(r2.finished, "%snova missão não terminou", "");
    delete app;
  }

  // ── 3) Telemetria mentindo: marcos corrigem a estimativa, mas o carro nunca dá ré ──
  // O carro verdadeiro anda a 0,40 m/s e a telemetria diz 0,50 m/s: a estimativa do painel passa à
  // frente e cada marco (desvio, semáforo, PARE) manda recuar.
  for (const char *mission : {"AC", "BA", "CB"}) {
    printf("Ao vivo %c -> %c (telemetria 25%% acima da real)\n", mission[0], mission[1]);
    TrackNetwork net;
    App *app = new App();
    FakeCar car(mission);
    car.reportScale = 1.25f;
    car.autoPress = false;
    FrameView view;
    float lastS = 0;
    uint32_t lastSerial = 0;
    int backward = 0, wrongWay = 0, waitSeen = 0, latchMoving = 0;
    bool renderedWait = false;
    // Capturas para a pasta screenshots (só na missão A -> C)
    const bool shots = strcmp(mission, "AC") == 0;
    bool shotGoing = false, shotLeaving = false, shotWaitC = false;
    uint32_t releasedAt = 0;
    float lastX = NAN, lastY = NAN;
    for (uint32_t now = 1000; now < 1000 + 400000 && !car.parked(); now += STEP_MS) {
      if (car.latched && now - car.latchedAt >= 2000) car.pressRun(), releasedAt = now;
      car.step(*app, now, STEP_MS);
      app->update(now, view);
      if (car.latched && now - car.latchedAt > 1000) {
        // já com a rota encolhida no painel: tem que estar esperando o botão, parado
        if (view.hud.missionWaitingAt >= 0) waitSeen++;
        if (strcmp(view.hud.speed, "0,00") != 0) latchMoving++;
        if (shots && car.confirmed == 1 && !renderedWait) {
          render(view, now, "ao_vivo_AC_aguardando_botao");
          render(view, now, "tela_17_serial_parado_na_coleta_aguardando_RUN");
          renderedWait = true;
        }
        if (shots && car.confirmed == 2 && !shotWaitC) {
          render(view, now, "tela_19_serial_parado_na_entrega_aguardando_RUN");
          shotWaitC = true;
        }
      }
      if (shots && !shotGoing && car.confirmed == 0 && now >= 7000) {
        render(view, now, "tela_16_serial_rumo_a_coleta");
        shotGoing = true;
      }
      if (shots && !shotLeaving && car.confirmed == 1 && releasedAt && now >= releasedAt + 2500) {
        render(view, now, "tela_18_serial_RUN_apertado_rumo_a_entrega");
        shotLeaving = true;
      }
      if (view.plan->serial == lastSerial && view.s < lastS - 0.01f) backward++;
      lastSerial = view.plan->serial;
      lastS = view.s;
      checkOneWay(net, *view.plan, view.s, wrongWay);
      if (!isnan(lastX)) {
        // o carro exibido também não pode andar contra o sentido da trajetória
        Sample p = view.plan->line.sample(view.s);
        float along = (view.carX - lastX) * p.dir.x + (view.carY - lastY) * p.dir.y;
        if (along < -0.5f) backward++;
      }
      lastX = view.carX, lastY = view.carY;
    }
    printf("  pontos confirmados: %d de 2, quadros com ré: %d\n", car.confirmed, backward);
    check(car.confirmed == 2, "%so carro verdadeiro não passou pelos 2 pontos", "");
    check(view.hud.missionDone == 2, "%spainel não registrou coleta e entrega (%.0f)", "", view.hud.missionDone);
    check(backward == 0, "%scarro deu ré na tela em %.0f quadros", "", backward);
    check(wrongWay == 0, "%scontramão ao vivo em %.0f quadros", "", wrongWay);
    check(waitSeen > 0, "%spainel não esperou o botão RUN no ponto", "");
    check(latchMoving == 0, "%scarro andou na tela travado no ponto (%.0f quadros)", "", latchMoving);
    check(view.hud.missionWaitingAt < 0, "%spainel continuou esperando depois do botão RUN", "");
    if (shots) render(view, 0, "ao_vivo_AC_fim");
    delete app;
  }

  // ── 3b) Pontos alterados pela serial (Resetar Rota, novos pontos): não se somam aos antigos e o carro
  // volta para a partida. Só sair o 1º ponto com o carro chegando nele conta como ponto confirmado.
  {
    printf("Ao vivo: resetar e trocar os pontos no meio da missão\n");
    App *app = new App();
    FrameView view, start;
    {
      App fresh;
      fresh.update(1000, start);
    }
    double ts = 0;
    std::string route = "AC";
    uint32_t now = 1000;
    auto feed = [&](uint32_t until) {
      for (; now < until; now += STEP_MS) {
        if (now % 99 < STEP_MS) {
          VehicleData d;
          ts += 0.1;
          d.ts = ts;
          d.route = route;
          d.routeText = route;
          d.running = true;
          d.speed = 0.4f;
          d.pwm = 120;
          d.controlMode = "AUTOMÁTICO";
          app->onVehicleData(d, now);
        }
        app->update(now, view);
      }
    };
    auto atStart = [&]() { return hypotf(view.carX - start.carX, view.carY - start.carY) < 2; };
    feed(9000);   // anda ~3 m com A -> C
    check(!atStart(), "%scarro não saiu da partida", "");
    route = "";   // Resetar Rota
    feed(now + 400);
    check(view.hud.missionAll.empty() && view.hud.missionDone == 0, "%sreset deixou pontos na tela (%s)",
          view.hud.missionAll.c_str());
    check(view.hud.missionWaitingAt < 0 && !view.hud.cargo, "%sreset virou chegada no ponto", "");
    route = "B";   // pontos novos, um por vez
    feed(now + 300);
    route = "BC";
    feed(now + 300);
    printf("  depois do reset: missão %s, feitos %d\n", view.hud.missionAll.c_str(), view.hud.missionDone);
    check(view.hud.missionAll == "BC" && view.hud.missionDone == 0, "%spontos se somaram: %s",
          view.hud.missionAll.c_str());
    feed(now + 4000);
    route = "A";   // lista trocada direto (sem esvaziar)
    feed(now + 100);
    check(view.hud.missionAll == "A" && view.hud.missionDone == 0, "%slista trocada somou pontos: %s",
          view.hud.missionAll.c_str());
    check(atStart(), "%scarro não voltou para a partida ao trocar os pontos", "");
    delete app;
  }

  // Capturas: bateria começa a carregar (battery_state 3 -> 1) com o carro parado, dado pela serial;
  // a tela fecha quando o carregador termina (battery_state 2)
  {
    printf("Capturas: tela da bateria pela serial\n");
    App *app = new App();
    FrameView view;
    double ts = 0;
    bool shotOpening = false, shotCharging = false, shotLevel = false, shotClosing = false;
    bool openedAtStart = false, opened = false, closedAfter = false;
    uint32_t chargeAt = 4000, doneAt = 11000;
    for (uint32_t now = 1000; now < 13000; now += STEP_MS) {
      if (now % 99 < STEP_MS) {
        VehicleData d;
        ts += 0.1;
        d.ts = ts;
        d.running = false;
        d.battery = now < 8000 ? 37 : 38;
        d.batteryState = now < chargeAt ? 3 : now < doneAt ? 1 : 2;   // carregador ligado aos 4 s
        d.controlMode = "AUTOMÁTICO";
        d.clockMinutes = 14 * 60 + 40;
        app->onVehicleData(d, now);
      }
      app->update(now, view);
      const HudView &h = view.hud;
      if (now < chargeAt && h.charging) openedAtStart = true;
      if (h.charging && h.chargingOpen >= 1) opened = true;
      if (now > doneAt + CARGO_FADE_MS + 200 && !h.charging) closedAfter = true;
      if (!shotOpening && h.charging && h.chargingOpen >= 0.45f)
        render(view, now, "tela_20_serial_bateria_tela_fechando_o_mapa"), shotOpening = true;
      if (!shotCharging && now >= chargeAt + 800) render(view, now, "tela_21_serial_bateria_enchendo"), shotCharging = true;
      if (!shotLevel && now >= chargeAt + 3300) render(view, now, "tela_30_serial_bateria_carregando_37"), shotLevel = true;
      if (!shotClosing && now > doneAt && h.chargingOpen < 0.6f && h.charging)
        render(view, now, "tela_31_serial_bateria_carregada_mapa_voltando"), shotClosing = true;
    }
    check(!openedAtStart, "%stela da bateria abriu sem estar carregando", "");
    check(opened && closedAfter, "%stela da bateria não abriu ao carregar ou não fechou ao terminar", "");
    delete app;
  }

  // Capturas: 3 pontos (coleta e entrega alternam, como no navigation.py)
  {
    printf("Capturas: A -> B -> C pela serial\n");
    App *app = new App();
    FakeCar car("ABC");
    FrameView view;
    bool first = false, gps = false;
    uint32_t gpsAt = 0;   // espera a câmera abrir a vista da pista (~1,5 s)
    for (uint32_t now = 1000; now < 120000 && !gps; now += STEP_MS) {
      car.step(*app, now, STEP_MS);
      app->update(now, view);
      if (!first && now >= 5000) render(view, now, "tela_22_serial_tres_pontos_alternados"), first = true;
      if (!gpsAt && view.hud.gps && !view.hud.gpsStop) gpsAt = now;
      if (gpsAt && now >= gpsAt + 1500) {
        // a tela da carga cobre a prévia: a captura mostra o mapa por baixo dela
        FrameView bare = view;
        bare.hud.cargo = false;
        render(bare, now, "tela_23_serial_chegou_na_coleta_gps");
        gps = true;
      }
    }
    delete app;
  }

  // Tela da carga: abre no ponto (coleta azul / entrega verde), segura o carro e fecha com o botão RUN
  {
    printf("Tela da carga A -> C (botão RUN do carro)\n");
    App *app = new App();
    FakeCar car("AC");
    car.autoPress = false;
    FrameView view;
    std::string seen;
    int opened = 0, closedAfterPress = 0, movedWhileOpen = 0;
    uint32_t pressAt = 0;
    float heldX = NAN, heldY = NAN;
    bool prevCargo = false;
    for (uint32_t now = 1000; now < 1000 + 300000; now += STEP_MS) {
      car.step(*app, now, STEP_MS);
      app->update(now, view);
      const HudView &h = view.hud;
      if (h.cargo && !prevCargo) {
        opened++;
        seen += std::string(h.cargoRole == 'E' ? "Entrega " : "Coleta ") + h.cargoPoint + "; ";
      }
      prevCargo = h.cargo;
      if (h.cargo && h.cargoOpen >= 1) {
        uint32_t t = now - h.cargoSince;
        if (t >= 600 && t < 600 + STEP_MS) heldX = view.carX, heldY = view.carY;   // já freado
        if (t > 600 && hypotf(view.carX - heldX, view.carY - heldY) > 0.5f) movedWhileOpen++;
        const char *tag = h.cargoRole == 'E' ? "entrega" : "coleta";
        for (uint32_t at : {400u, 1300u, 2000u, 2450u}) {
          if (t >= at && t < at + STEP_MS) {
            char name[48];
            snprintf(name, sizeof name, "carga_%s_%04ums", tag, at);
            render(view, now, name);
          }
        }
        // Capturas para a pasta screenshots: caixa no lugar, aperto do botão (2,4-2,56 s do loop)
        bool delivery = h.cargoRole == 'E';
        if (t >= 1300 && t < 1300 + STEP_MS)
          render(view, now, delivery ? "tela_26_serial_entrega_retire_a_carga" : "tela_24_serial_coleta_coloque_a_carga");
        if (t >= 2450 && t < 2450 + STEP_MS)
          render(view, now, delivery ? "tela_27_serial_entrega_botao_apertado" : "tela_25_serial_coleta_botao_apertado");
        if (t >= CARGO_PRESS_MS && !pressAt) {
          car.pressRun();
          pressAt = now;
        }
      }
      // Saída da tela (meio da transição de 260 ms) depois do botão na coleta
      if (pressAt && seen.size() < 12 && now - pressAt >= 220 && now - pressAt < 220 + STEP_MS)
        render(view, now, "tela_28_serial_botao_solto_tela_saindo");
      if (pressAt && now - pressAt >= 100 + CARGO_FADE_MS + 2 * STEP_MS) {
        if (!h.cargo) closedAfterPress++;
        pressAt = 0;
      }
      if (h.icon == ManeuverIcon::Done && now > 2000 && opened >= 2) break;
    }
    printf("  telas: %s fecharam depois do botão: %d\n", seen.c_str(), closedAfterPress);
    check(seen == "Coleta A; Entrega C; ", "%stela da carga não abriu na coleta A e na entrega C", "");
    check(closedAfterPress == 2, "%stela da carga não fechou depois do botão (%.0f de 2)", "", closedAfterPress);
    check(movedWhileOpen == 0, "%scarro andou com a tela da carga aberta (%.0f quadros)", "", movedWhileOpen);
    delete app;
  }

  printf(failures ? "\n%d verificação(ões) falharam\n" : "\nTudo certo\n", failures);
  return failures ? 1 : 0;
}
