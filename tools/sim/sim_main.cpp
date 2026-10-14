// Ambiente de testes no PC: roda o App, a Scene e o Hud do firmware (os mesmos .cpp do ESP32)
// em tempo virtual, sem display nem serial, e confere a simulação:
//
//  - cada missão (AB, AC, BA, BC, CA, CB) coleta no 1º ponto e entrega no último, na ordem;
//  - o carro só anda no sentido da pista (mão única): trajetória ligada nó a nó e sempre a favor
//    do sentido de cada trecho;
//  - ao parar numa placa (ponto ou PARE) a câmera abre e mostra a pista inteira, segura essa
//    vista enquanto o carro espera e volta para trás do carro quando ele segue;
//  - o zoom não reinicia nada: o carro sai de onde parou e a missão continua de onde estava;
//  - o demo passa de uma missão para a próxima sem voltar à partida.
//
// Salva quadros em PNG (480x320, como no TFT) na pasta de saída. Uso:
//   tools\sim\build.bat      (compila a DLL e roda pelo run.py; quadros em tools\sim\out)

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
  int stuckZoom = 0;   // vista de cima que não voltou para trás do carro quando ele andou
  float movedAfterFinish = 0;   // px andados depois de "Percurso finalizado"
  uint32_t end = 0;             // relógio no fim da rodada (para continuar a mesma simulação)
};

/**
 * Roda o App por até maxMs. Fotografa cada chegada (já com a vista da pista inteira) e confere
 * enquadramento, sentido e saltos. stopAfterFinish: para 1 s depois de "Percurso finalizado".
 */
RunResult run(App &app, const std::string &tag, uint32_t maxMs, bool stopAfterFinish, bool shots,
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
  Vec2 finishPos{0, 0};
  for (uint32_t now = start; now < start + maxMs; now += STEP_MS) {
    r.end = now;
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

    // O carro nunca "pula" (reinício da simulação ou da missão apareceria aqui)
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
      Occluder occ[MAX_OCCLUDERS];
      int n = hud.occluders(view, occ);
      float visible = trackVisible(net, view, occ, n);
      r.worstZoomVisible = std::min(r.worstZoomVisible, visible);
      r.zoomChecks++;
      // Parado esperando: a posição não mudou desde a chegada
      const Arrival &a = r.arrivals.back();
      check(hypotf(view.carX - a.pos.x, view.carY - a.pos.y) < 1, "%scarro andou durante o zoom (%.1f px)", "",
            hypotf(view.carX - a.pos.x, view.carY - a.pos.y));
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
      finishPos = {view.carX, view.carY};
      if (shots) render(view, now, tag + "_finalizado");
    }
    if (r.finished && h.icon == ManeuverIcon::Done) {
      r.movedAfterFinish = std::max(r.movedAfterFinish, hypotf(view.carX - finishPos.x, view.carY - finishPos.y));
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
  app->setSimRoute(route, 0);
  FrameView view;
  uint32_t nextShot = 0, finishedAt = 0;
  int count = 0;
  for (uint32_t now = 1000; now < 1000 + 300000; now += stepMs) {
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

  // ── 1) Cada missão de 2 pontos: coleta no 1º, entrega no 2º ──
  const char *missions[] = {"AB", "AC", "BA", "BC", "CA", "CB"};
  for (const char *mission : missions) {
    App *app = new App();   // App tem a trajetória inteira; no heap para não estourar a pilha
    app->setSimRoute(mission, 0);
    printf("Missão %c -> %c\n", mission[0], mission[1]);
    RunResult r = run(*app, std::string("missao_") + mission, 400000, true, true);
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
    printf("  pista visível no zoom: %.0f%%, maior salto: %.1f px\n", r.worstZoomVisible * 100, r.maxJumpPx);
    delete app;
  }

  // ── 1c) Desenho: nenhum quadro com faixa/mancha escura cortando a tela ──
  for (const char *mission : {"AC", "BC", "CA"}) {
    App *app = new App();
    app->setSimRoute(mission, 0);
    FrameView view;
    int worst = 0;
    uint32_t worstAt = 0, finishedAt = 0;
    long drawAllocs = 0, frames = 0;
    size_t biggest = 0;
    bool firstFrame = true;
    for (uint32_t now = 1000; now < 1000 + 300000; now += STEP_MS) {
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
      int dark = darkPixels();
      if (dark > worst) worst = dark, worstAt = now;
      if (view.hud.icon == ManeuverIcon::Done && !finishedAt && now > 2000) finishedAt = now;
      if (finishedAt && now - finishedAt > 1000) break;
    }
    printf("Desenho %c -> %c: pior quadro com %d pixels escuros (%.2f s)\n", mission[0], mission[1], worst,
           (worstAt - 1000) / 1000.0);
    if (worst > 12000) {
      delete app;
      app = new App();   // refaz até o pior quadro e grava para conferir
      app->setSimRoute(mission, 0);
      for (uint32_t now = 1000; now <= worstAt; now += STEP_MS) app->update(now, view);
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

  // ── 1b) Quadros lentos (~6 fps, como no ESP32): o carro rápido não pula a PARE nem os pontos ──
  {
    STEP_MS = 166;
    App *app = new App();
    app->setSimRoute("BC", 0);
    printf("Missão B -> C a ~6 fps\n");
    RunResult r = run(*app, "lento", 400000, true, false);
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

  // ── 2) Fim do percurso: o carro para na linha de chegada e fica lá; missão nova sai dali ──
  {
    App *app = new App();
    printf("Percurso completo A -> C e espera na chegada\n");
    RunResult r = run(*app, "fim", 400000, false, false);
    check(r.finished, "%spercurso não terminou", "");
    check(r.movedAfterFinish < 1, "%scarro saiu da linha de chegada depois de finalizar (%.0f px)", "", r.movedAfterFinish);
    printf("  terminou; andou %.1f px depois de finalizar\n", r.movedAfterFinish);
    PanelCommand next;
    next.hasSimRoute = true;
    next.simRoute = "BA";
    app->onCommand(next);
    printf("Nova missão B -> A pela serial, saindo da chegada\n");
    RunResult r2 = run(*app, "fim_nova", 400000, true, false, r.end + STEP_MS);
    std::string seen;
    for (const Arrival &a : r2.arrivals) if (!a.stop) seen += a.badge + "; ";
    printf("  paradas: %s maior salto: %.1f px\n", seen.c_str(), r2.maxJumpPx);
    check(seen == "Coleta B; Entrega A; ", "%snova missão não coletou em B e entregou em A", "");
    check(r2.finished, "%snova missão não terminou", "");
    check(r2.maxJumpPx < 12, "%scarro saltou %.1f px ao começar a nova missão (reinício?)", "", r2.maxJumpPx);
    delete app;
  }

  // ── 3) Modo real pela "serial": marcos corrigem a estimativa, mas o carro nunca dá ré ──
  // Um carro "verdadeiro" anda a 0,40 m/s na trajetória da missão e a telemetria diz 0,50 m/s:
  // a estimativa do painel passa à frente e cada marco (desvio, semáforo, PARE) manda recuar.
  for (const char *mission : {"AC", "BA", "CB"}) {
    printf("Ao vivo %c -> %c (telemetria 25%% acima da real)\n", mission[0], mission[1]);
    TrackNetwork net;
    Plan truth = buildPlan(net, {trackdata::CAR_START_EDGE, trackdata::CAR_START_S}, mission, PlanMode::Shortest);
    App *app = new App();
    FrameView view;
    std::string route = mission;
    float sTrue = 0, lastS = 0;
    uint32_t lastSerial = 0, stopUntil = 0, lightUntil = 0, detourUntil = 0;
    size_t nextEvent = 0;
    int backward = 0, confirmed = 0, wrongWay = 0;
    float maxJump = 0, lastX = NAN, lastY = NAN;
    double ts = 0;
    for (uint32_t now = 1000; now < 1000 + 400000 && sTrue < truth.length; now += STEP_MS) {
      bool stopped = now < stopUntil;
      if (!stopped) sTrue += 0.40f / PX_TO_M * STEP_MS / 1000.0f;
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
        }
        nextEvent++;
      }
      if (now % 99 < STEP_MS) {   // ~10 linhas por segundo na serial
        VehicleData d;
        ts += 0.1;
        d.ts = ts;
        d.route = route;
        d.routeText = route;
        d.running = true;
        d.speed = stopped ? 0 : 0.50f;
        d.stopActive = stopped;
        d.trafficLight = now < lightUntil ? 2 : -1;
        d.rightDetour = now < detourUntil;
        app->onVehicleData(d, now);
      }
      app->update(now, view);
      if (view.plan->serial == lastSerial && view.s < lastS - 0.01f) backward++;
      lastSerial = view.plan->serial;
      lastS = view.s;
      checkOneWay(net, *view.plan, view.s, wrongWay);
      if (!isnan(lastX)) {
        // o carro exibido também não pode andar contra o sentido da trajetória
        Sample p = view.plan->line.sample(view.s);
        float along = (view.carX - lastX) * p.dir.x + (view.carY - lastY) * p.dir.y;
        if (along < -0.5f) backward++;
        maxJump = std::max(maxJump, hypotf(view.carX - lastX, view.carY - lastY));
      }
      lastX = view.carX, lastY = view.carY;
    }
    printf("  pontos confirmados: %d de 2, quadros com ré: %d\n", confirmed, backward);
    check(confirmed == 2, "%so carro verdadeiro não passou pelos 2 pontos", "");
    check(view.hud.missionDone == 2, "%spainel não registrou coleta e entrega (%.0f)", "", view.hud.missionDone);
    check(backward == 0, "%scarro deu ré na tela em %.0f quadros", "", backward);
    check(wrongWay == 0, "%scontramão ao vivo em %.0f quadros", "", wrongWay);
    if (strcmp(mission, "AC") == 0) render(view, 0, "ao_vivo_AC_fim");
    delete app;
  }

  printf(failures ? "\n%d verificação(ões) falharam\n" : "\nTudo certo\n", failures);
  return failures ? 1 : 0;
}
