#pragma once
// Porte de planner.js + carLogic.js + mission.js: rota (missão) -> trajetória e eventos.
// Rotas são strings de pontos, na ordem: "AC" = coleta em A, entrega em C.

#include <string>
#include <vector>

#include "Network.h"

enum class PlanMode : uint8_t { Shortest, Car };
enum class EventType : uint8_t { Point, Stop, TrafficLight, Detour, Turn };
enum class Turn : uint8_t { None, Right, Left, Straight };

struct PlanEvent {
  EventType type;
  float s;
  Turn turn = Turn::None;
  char point = 0;
  bool target = false;   // ponto da missão (não só passagem)
  int sign = -1;         // placa (índice em trackdata::SIGNS) ou -1
};

struct Target {
  char point;
  int edgeIndex;   // índice na lista de arestas do plano
  bool used;
};

struct Plan {
  std::vector<int> edges;
  Polyline line;
  std::vector<Range> ranges;
  std::vector<PlanEvent> events;   // ordenados por s
  std::vector<Target> targets;
  std::string unreachable;
  PlanMode mode = PlanMode::Shortest;
  float length = 0;
  uint32_t serial = 0;             // muda a cada plano novo (para quem guarda cache)
};

/** Lê a rota em qualquer formato: "A → B → C", "A -> B", "A,B", "Nenhum ponto". Só A/B/C, em maiúsculas. */
std::string parseRoute(const char *raw);

/** Monta o plano a partir de start. detoursDone = desvios vistos desde o último ponto (modo carro). */
Plan buildPlan(const TrackNetwork &network, Location start, const std::string &queue, PlanMode mode,
               int detoursDone = 0);

// ── mission.js: papéis dos pontos (coleta no primeiro, entrega no último) ──
enum class Role : uint8_t { Pickup, Pass, Delivery };
Role roleAt(int index, int total);
const char *roleName(int index, int total);    // "Coleta" | "Passagem" | "Entrega"
const char *roleAhead(int index, int total);   // "Ponto de coleta à frente" ...
