#include "Planner.h"

#include <algorithm>
#include <ctype.h>
#include <string.h>

namespace {

constexpr float NODE_TURN_EVENT_DEG = 30;
constexpr float CURVE_STEP_PX = 8;
constexpr float CURVE_WINDOW_PX = 48;
constexpr float CURVE_MIN_DEG = 20;
constexpr float RAD2DEG = 180.0f / (float)M_PI;

uint32_t planSerial = 0;

// ── carLogic.js ──────────────────────────────────────────────────────────
// Mantenha igual a LANE_GUIDE_MAP de core/navigation.py (e de carLogic.js).
enum class Lane : uint8_t { None, Right, Left };
const char *LANE_GUIDE_MAP(char point) {
  switch (point) {
    case 'A': return "R";
    case 'B': return "LR";
    case 'C': return "LL";
    default: return "";
  }
}
constexpr int MAX_EDGES = 48;
constexpr int EXTRA_LAPS = 2;

/** >0 vira à direita, <0 à esquerda (y para baixo), ~0 segue reto. */
float turnAmount(const TrackNetwork &net, int from, int to) {
  const auto &a = net.edge(from).line.pts;
  Vec2 da = unitBetween(a[a.size() - 2], a[a.size() - 1]);
  Vec2 db = unitBetween(net.edge(to).line.pts[0], net.edge(to).line.pts[1]);
  return da.x * db.y - da.y * db.x;
}

/** Espelho de Navigation.update_lane(). */
Lane lanePreference(char point, int counter) {
  const char *lanes = point ? LANE_GUIDE_MAP(point) : "";
  int n = (int)strlen(lanes);
  if (!n) return Lane::Left;
  return lanes[std::min(counter, n - 1)] == 'R' ? Lane::Right : Lane::Left;
}

int chooseNext(const TrackNetwork &net, int edge, Lane preference) {
  std::vector<int> options = net.successors(edge);
  if (options.size() <= 1) return options.empty() ? -1 : options[0];
  std::vector<int> byTurn = options;
  std::stable_sort(byTurn.begin(), byTurn.end(),
                   [&](int x, int y) { return turnAmount(net, edge, x) < turnAmount(net, edge, y); });
  if (preference == Lane::Right) return byTurn.back();
  if (preference == Lane::Left) return byTurn.front();
  // Sem placa: segue a pista no sentido de referência, o mais reto possível
  int best = options[0];
  for (int option : options) {
    if (fabsf(turnAmount(net, edge, option)) < fabsf(turnAmount(net, edge, best))) best = option;
  }
  return best;
}

/** A placa de desvio só vale para quem vê a frente dela. */
bool seesFront(const TrackNetwork &net, const SignDef &sign, int edge) {
  Vec2 travel = net.edge(edge).line.sample(sign.sBase).dir;
  return dot(travel, sign.facing) < -0.2f;
}

struct Walk {
  std::vector<int> edges;
  std::vector<Target> targets;
  std::string unreachable;
};

Walk walkCarLogic(const TrackNetwork &net, Location start, const std::string &queue, int detoursDone) {
  Walk walk;
  std::string pending = queue;
  int counter = detoursDone;
  Lane preference = Lane::None;   // definida por uma placa de desvio, usada na próxima bifurcação
  int edge = start.edge;
  float fromS = start.s;
  int laps = 0;
  auto first = [&]() { return pending.empty() ? (char)0 : pending[0]; };

  // Começando no meio de um trecho: um desvio já visto nele ainda vale para a próxima bifurcação
  if (edge >= 0) {
    for (int i = 0; i < net.signCount(); i++) {
      const SignDef &sign = net.sign(i);
      if (sign.kind == SignKind::Detour && sign.base == edge && sign.sBase <= fromS + 1 && seesFront(net, sign, edge)) {
        preference = lanePreference(first(), std::max(0, counter - 1));
        break;
      }
    }
  }

  while (edge >= 0 && (int)walk.edges.size() < MAX_EDGES) {
    walk.edges.push_back(edge);
    // Placas desta aresta à frente do carro, na ordem em que aparecem
    std::vector<int> ahead;
    for (int i = 0; i < net.signCount(); i++) {
      if (net.sign(i).base == edge && net.sign(i).sBase > fromS + 1) ahead.push_back(i);
    }
    std::stable_sort(ahead.begin(), ahead.end(), [&](int a, int b) { return net.sign(a).sBase < net.sign(b).sBase; });
    for (int i : ahead) {
      const SignDef &sign = net.sign(i);
      if (sign.kind == SignKind::Detour && seesFront(net, sign, edge)) {
        preference = lanePreference(first(), counter);
        counter += 1;
      } else if (sign.kind == SignKind::Point && !pending.empty() && sign.point == pending[0]) {
        walk.targets.push_back({pending[0], (int)walk.edges.size() - 1, false});
        pending.erase(0, 1);
        counter = 0;
      }
    }
    if (net.edge(edge).to == trackdata::NODE_S) {
      laps += 1;
      // Ponto que não aparece depois de uma volta por ponto (+2) é inalcançável com a lógica atual
      if (pending.empty() || laps >= (int)queue.size() + EXTRA_LAPS) break;
    }
    bool branching = net.successors(edge).size() > 1;
    int next = chooseNext(net, edge, preference);
    if (branching) preference = Lane::None;
    edge = next;
    fromS = -1;
  }
  walk.unreachable = pending;
  return walk;
}

// ── planner.js: eventos ──────────────────────────────────────────────────
std::vector<PlanEvent> curveEvents(const Polyline &line) {
  auto heading = [&](float s) {
    Vec2 d = line.sample(s).dir;
    return atan2f(d.y, d.x);
  };
  auto delta = [](float a, float b) { return atan2f(sinf(b - a), cosf(b - a)); };
  std::vector<PlanEvent> events;
  float start = -1;
  float sign = 0;
  auto push = [&](float s0, float sg) {
    PlanEvent e{EventType::Turn, s0 + CURVE_WINDOW_PX * 0.25f};
    e.turn = sg > 0 ? Turn::Right : Turn::Left;   // y para baixo: positivo = horário = direita
    events.push_back(e);
  };
  for (float s = 0; s + CURVE_WINDOW_PX <= line.length; s += CURVE_STEP_PX) {
    float deg = delta(heading(s), heading(s + CURVE_WINDOW_PX)) * RAD2DEG;
    float sg = deg > 0 ? 1.0f : (deg < 0 ? -1.0f : 0.0f);
    if (start < 0) {
      if (fabsf(deg) > CURVE_MIN_DEG) {
        start = s;
        sign = sg;
      }
    } else if (fabsf(deg) < CURVE_MIN_DEG * 0.5f || sg != sign) {
      push(start, sign);
      start = -1;
    }
  }
  if (start >= 0) push(start, sign);
  return events;
}

std::vector<PlanEvent> collectEvents(const TrackNetwork &net, const std::vector<int> &edges, const Polyline &line,
                                     const std::vector<Range> &ranges, std::vector<Target> &targets) {
  std::vector<PlanEvent> events;

  // Placas: cada passagem por um trecho com placa vira um evento
  for (size_t index = 0; index < edges.size(); index++) {
    const Range &range = ranges[index];
    for (int i = 0; i < net.signCount(); i++) {
      const SignDef &sign = net.sign(i);
      if (sign.base != edges[index]) continue;
      Projection hit = line.project(sign.at, range.s0, range.s1 + 1);
      if (!isfinite(hit.d)) continue;
      Vec2 travel = line.sample(hit.s).dir;
      bool approaching = dot(travel, sign.facing) < -0.2f;   // vê a frente da placa
      PlanEvent e{EventType::Point, hit.s};
      e.sign = i;
      if (sign.kind == SignKind::Point) {
        Target *target = nullptr;
        for (Target &t : targets) {
          if (t.edgeIndex == (int)index && t.point == sign.point && !t.used) { target = &t; break; }
        }
        if (target) target->used = true;
        e.point = sign.point;
        e.target = target != nullptr;
        events.push_back(e);
      } else if (approaching) {
        e.type = sign.kind == SignKind::Stop ? EventType::Stop
                 : sign.kind == SignKind::TrafficLight ? EventType::TrafficLight : EventType::Detour;
        events.push_back(e);
      }
    }
  }

  // Curvas da trajetória inteira, lidas da geometria
  std::vector<PlanEvent> curves = curveEvents(line);
  events.insert(events.end(), curves.begin(), curves.end());

  // Cruzamento em que o carro segue reto: "Siga em frente" (se não houver curva ali)
  for (size_t i = 1; i < edges.size(); i++) {
    if (net.successors(edges[i - 1]).size() < 2) continue;
    const auto &a = net.edge(edges[i - 1]).line.pts;
    const auto &b = net.edge(edges[i]).line.pts;
    Vec2 before = a[a.size() - 1] - a[a.size() - 2];
    Vec2 after = b[1] - b[0];
    float nb = length(before), na = length(after);
    if (nb == 0) nb = 1;
    if (na == 0) na = 1;
    float angle = acosf(std::max(-1.0f, std::min(1.0f, dot(before, after) / (nb * na)))) * RAD2DEG;
    if (angle > NODE_TURN_EVENT_DEG) continue;
    float s = std::max(0.0f, ranges[i].s0 - 20);
    bool nearCurve = false;
    for (const PlanEvent &c : curves) nearCurve |= fabsf(c.s - s) < 60;
    if (nearCurve) continue;
    PlanEvent e{EventType::Turn, s};
    e.turn = Turn::Straight;
    events.push_back(e);
  }

  std::stable_sort(events.begin(), events.end(), [](const PlanEvent &x, const PlanEvent &y) { return x.s < y.s; });
  return events;
}

Plan finishPlan(const TrackNetwork &net, Location start, std::vector<int> edges, std::vector<Target> targets,
                std::string unreachable, PlanMode mode) {
  Plan plan;
  plan.line = net.composePath(start, edges, plan.ranges);
  plan.events = collectEvents(net, edges, plan.line, plan.ranges, targets);
  plan.edges = std::move(edges);
  plan.targets = std::move(targets);
  plan.unreachable = std::move(unreachable);
  plan.length = plan.line.length;
  plan.mode = mode;
  plan.serial = ++planSerial;
  return plan;
}

}  // namespace

std::string parseRoute(const char *raw) {
  std::string out;
  if (!raw) return out;
  // Tokens separados por → -> , ; | ou espaço; só valem A, B ou C sozinhos
  const char *p = raw;
  while (*p) {
    while (*p && !isalnum((unsigned char)*p)) p++;
    const char *startTok = p;
    while (*p && isalnum((unsigned char)*p)) p++;
    if (p - startTok == 1) {
      char c = (char)toupper((unsigned char)*startTok);
      if (c == 'A' || c == 'B' || c == 'C') out += c;
    }
  }
  return out;
}

Plan buildPlan(const TrackNetwork &net, Location start, const std::string &queue, PlanMode mode, int detoursDone) {
  if (mode == PlanMode::Car) {
    Walk walk = walkCarLogic(net, start, queue, detoursDone);
    return finishPlan(net, start, walk.edges, walk.targets, walk.unreachable, mode);
  }
  std::vector<int> edges{start.edge};
  int current = start.edge;
  float currentS = start.s;
  std::vector<Target> targets;
  std::string unreachable;

  for (char point : queue) {
    int si = net.pointSign(point);
    if (si < 0) { unreachable += point; continue; }
    const SignDef &sign = net.sign(si);
    if (current == sign.base && sign.sBase > currentS + 2) {
      targets.push_back({point, (int)edges.size() - 1, false});
      currentS = sign.sBase;
      continue;
    }
    bool found = false;
    std::vector<int> path = net.shortestPath(current, [&](const Edge &e) { return e.index == sign.base; }, &found);
    if (!found) { unreachable += point; continue; }
    edges.insert(edges.end(), path.begin(), path.end());
    current = edges.back();
    currentS = sign.sBase;
    targets.push_back({point, (int)edges.size() - 1, false});
  }

  // Termina na largada: volta externa quando dá, senão menor caminho
  if (net.edge(current).to != trackdata::NODE_S) {
    bool found = false;
    std::vector<int> back;
    if (queue.empty()) back = net.outerLoopFrom(current, &found);
    if (!found) back = net.shortestPath(current, [](const Edge &e) { return e.to == trackdata::NODE_S; }, &found);
    edges.insert(edges.end(), back.begin(), back.end());
  }
  return finishPlan(net, start, edges, targets, unreachable, PlanMode::Shortest);
}

Role roleAt(int index, int total) {
  if (index <= 0) return Role::Pickup;
  if (index >= total - 1) return Role::Delivery;
  return Role::Pass;
}

const char *roleName(int index, int total) {
  switch (roleAt(index, total)) {
    case Role::Pickup: return "Coleta";
    case Role::Delivery: return "Entrega";
    default: return "Passagem";
  }
}

const char *roleAhead(int index, int total) {
  switch (roleAt(index, total)) {
    case Role::Pickup: return "Ponto de coleta à frente";
    case Role::Delivery: return "Ponto de entrega à frente";
    default: return "Ponto de passagem à frente";
  }
}
