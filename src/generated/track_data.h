// GERADO por tools/gen_track.mjs a partir de RoadPanel/src/track — não edite à mão.
#pragma once
#include "../track/TrackTypes.h"

namespace trackdata {

constexpr float IMAGE_W = 1104.0f;
constexpr float IMAGE_H = 626.0f;
constexpr float METERS_PER_PX = 0.02f;

// Nós (cruzamentos): S, T1, T2, B1, B2
constexpr int NODE_COUNT = 5;
static const char *const NODE_NAMES[NODE_COUNT] = {"S", "T1", "T2", "B1", "B2"};
constexpr int NODE_S = 0;

static const Vec2 SEG_topo_1[] = {{264.1f, 97.987f}, {340.652f, 97.987f}, {423.328f, 97.987f}, {471.466f, 97.987f}};
static const Vec2 SEG_topo_2[] = {{471.466f, 97.987f}, {518.584f, 97.987f}, {611.467f, 97.987f}, {704.35f, 97.987f}, {753.509f, 97.987f}};
static const Vec2 SEG_corredor_1[] = {{471.466f, 97.987f}, {471.466f, 154.076f}, {471.466f, 320.832f}, {471.466f, 504.839f}, {471.466f, 560.928f}};
static const Vec2 SEG_corredor_2[] = {{753.509f, 97.987f}, {753.509f, 154.076f}, {753.509f, 320.832f}, {753.509f, 504.839f}, {753.509f, 560.928f}};
static const Vec2 SEG_faixa_direita[] = {{753.509f, 97.987f}, {802.668f, 97.987f}, {967.0f, 97.987f}, {994.399f, 105.214f}, {1012.998f, 122.558f}, {1022.298f, 159.826f}, {1022.298f, 320.832f}, {1022.298f, 481.838f}, {1012.998f, 536.357f}, {994.399f, 553.701f}, {967.0f, 560.928f}, {802.668f, 560.928f}, {753.509f, 560.928f}};
static const Vec2 SEG_baixo[] = {{753.509f, 560.928f}, {704.35f, 560.928f}, {611.467f, 560.928f}, {518.584f, 560.928f}, {471.466f, 560.928f}};
static const Vec2 SEG_esquerda[] = {{471.466f, 560.928f}, {423.328f, 560.928f}, {381.48f, 560.446f}, {345.756f, 550.81f}, {318.197f, 531.539f}, {299.824f, 499.089f}, {289.617f, 464.588f}, {283.493f, 430.086f}, {271.245f, 395.585f}, {253.893f, 366.834f}, {228.376f, 346.133f}, {197.755f, 331.183f}, {162.03f, 320.832f}, {126.306f, 311.632f}, {96.646f, 297.831f}, {79.508f, 277.13f}, {69.987f, 242.629f}, {69.987f, 194.327f}, {73.796f, 148.325f}, {87.125f, 122.558f}, {106.167f, 106.178f}, {131.41f, 99.433f}, {167.134f, 97.987f}, {207.962f, 97.987f}, {264.1f, 97.987f}};

constexpr int SEGMENT_COUNT = 7;
static const SegmentDef SEGMENTS[SEGMENT_COUNT] = {
  {"topo-1", 0, 1, SEG_topo_1, 4},
  {"topo-2", 1, 2, SEG_topo_2, 5},
  {"corredor-1", 1, 3, SEG_corredor_1, 5},
  {"corredor-2", 2, 4, SEG_corredor_2, 5},
  {"faixa-direita", 2, 4, SEG_faixa_direita, 13},
  {"baixo", 4, 3, SEG_baixo, 5},
  {"esquerda", 3, 0, SEG_esquerda, 25},
};

// Volta externa (rota vazia), em índices de SEGMENTS
constexpr int OUTER_LOOP_COUNT = 5;
static const int OUTER_LOOP[OUTER_LOOP_COUNT] = {0, 1, 4, 5, 6};

// Placas já ligadas ao trecho mais próximo (TrackNetwork.setSigns do JS)
constexpr int SIGN_COUNT = 7;
static const SignDef SIGNS[SIGN_COUNT] = {
  {"semaforo", SignKind::TrafficLight, ' ', "Semáforo", {143.7f, 205.8f}, 6, 617.371f, {-0.047f, 0.999f}},
  {"desvio-1", SignKind::Detour, ' ', "Desvio à direita", {384.5f, 169.0f}, 0, 120.4f, {-1.0f, 0.0f}},
  {"desvio-2", SignKind::Detour, ' ', "Desvio à direita", {667.6f, 169.0f}, 1, 196.134f, {-1.0f, 0.0f}},
  {"ponto-A", SignKind::Point, 'A', "Ponto A", {385.6f, 327.7f}, 2, 229.713f, {1.0f, 0.0f}},
  {"ponto-B", SignKind::Point, 'B', "Ponto B", {671.7f, 327.7f}, 3, 229.713f, {1.0f, 0.0f}},
  {"pare", SignKind::Stop, ' ', "PARE", {548.2f, 507.1f}, 5, 205.309f, {1.0f, 0.0f}},
  {"ponto-C", SignKind::Point, 'C', "Ponto C", {938.4f, 327.7f}, 4, 474.092f, {1.0f, -0.005f}},
};

// Partida do carro (aresta, s) e largada da volta (topo-1, s = 0)
constexpr int CAR_START_EDGE = 0;
constexpr float CAR_START_S = 35.0f;
constexpr int LAP_START_EDGE = 0;

// Linha de largada/chegada: centro, direção de quem passa, tamanho e grade
constexpr Vec2 FINISH_AT = {264.1f, 97.987f};
constexpr Vec2 FINISH_DIR = {1.0f, 0.0f};
constexpr float FINISH_ALONG = 36.745f;
constexpr float FINISH_ACROSS = 75.157f;
constexpr int FINISH_COLS = 2;
constexpr int FINISH_ROWS = 4;

// Linha quadriculada: quadrados pretos (4 cantos cada)
constexpr int FINISH_CELL_COUNT = 4;
static const Vec2 FINISH_CELLS[FINISH_CELL_COUNT][4] = {
  {{245.727f, 60.409f}, {264.1f, 60.409f}, {264.1f, 79.198f}, {245.727f, 79.198f}},
  {{245.727f, 97.987f}, {264.1f, 97.987f}, {264.1f, 116.777f}, {245.727f, 116.777f}},
  {{264.1f, 79.198f}, {282.472f, 79.198f}, {282.472f, 97.987f}, {264.1f, 97.987f}},
  {{264.1f, 116.777f}, {282.472f, 116.777f}, {282.472f, 135.566f}, {264.1f, 135.566f}},
};

}  // namespace trackdata
