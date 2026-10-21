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

// ── mission.js: papéis dos pontos, iguais aos de core/navigation.py ──
// Navigation.add_point alterna mission_state a cada ponto: o primeiro de uma lista vazia é coleta (1),
// o seguinte entrega (0), e assim por diante; ponto adicionado com a lista cheia continua a partir do
// último. O carro para em TODOS os pontos (point_stop_latched) até o botão RUN.
// Papéis guardados como letras numa string, um por ponto: 'C' coleta, 'E' entrega.
constexpr char ROLE_PICKUP = 'C';
constexpr char ROLE_DELIVERY = 'E';
/** command.mission_state (1 coleta, 0 entrega) -> papel; 0 se desconhecido (-1). */
char roleFromMissionState(int missionState);
/** count papéis alternados começando em first (espelho de add_point). */
std::string alternateRoles(int count, char first = ROLE_PICKUP);
/** Papéis de count pontos adicionados depois de uma lista que termina em lastRole (0 = lista vazia). */
std::string appendRoles(char lastRole, int count);
const char *roleName(char role);    // "Coleta" | "Entrega"
const char *roleAhead(char role);   // "Ponto de coleta à frente" ...
