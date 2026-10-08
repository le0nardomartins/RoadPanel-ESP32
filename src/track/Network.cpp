#include "Network.h"

#include <algorithm>

namespace {
constexpr float TURN_PENALTY = 40;   // px "extras" por curva, para preferir caminhos retos
constexpr float STRAIGHT_DEG = 25;   // abaixo disso a junção é reta e o nó fica na curva

float angleBetween(Vec2 a, Vec2 b) {
  float d = std::max(-1.0f, std::min(1.0f, dot(a, b)));
  return acosf(d) * 180.0f / (float)M_PI;
}
}  // namespace

TrackNetwork::TrackNetwork() {
  edges_.reserve(trackdata::SEGMENT_COUNT);
  for (int i = 0; i < trackdata::SEGMENT_COUNT; i++) {
    const SegmentDef &seg = trackdata::SEGMENTS[i];
    Polyline line(catmullRom(seg.points, seg.count));
    float len = line.length;
    edges_.push_back({i, seg.id, seg.from, seg.to, seg.points, seg.count, std::move(line), len});
  }
}

int TrackNetwork::pointSign(char point) const {
  for (int i = 0; i < trackdata::SIGN_COUNT; i++) {
    if (trackdata::SIGNS[i].kind == SignKind::Point && trackdata::SIGNS[i].point == point) return i;
  }
  return -1;
}

std::vector<int> TrackNetwork::successors(int edge) const {
  std::vector<int> out;
  for (const Edge &next : edges_) {
    if (next.from == edges_[edge].to && next.index != edges_[edge].index) out.push_back(next.index);
  }
  return out;
}

float TrackNetwork::turnCost(int a, int b) const {
  const auto &ap = edges_[a].line.pts;
  const auto &bp = edges_[b].line.pts;
  Vec2 da = unitBetween(ap[ap.size() - 2], ap[ap.size() - 1]);
  Vec2 db = unitBetween(bp[0], bp[1]);
  return angleBetween(da, db) > STRAIGHT_DEG ? TURN_PENALTY : 0;
}

std::vector<int> TrackNetwork::shortestPath(int fromEdge, const std::function<bool(const Edge &)> &isGoal,
                                            bool *found) const {
  const int n = (int)edges_.size();
  std::vector<float> dist(n, INFINITY);
  std::vector<int> prev(n, -1);
  std::vector<bool> done(n, false);
  std::vector<int> queue;
  for (int next : successors(fromEdge)) {
    dist[next] = edges_[next].length + turnCost(fromEdge, next);
    queue.push_back(next);
  }
  while (!queue.empty()) {
    // fila pequena (7 arestas): ordena como o JS e tira o menor
    std::stable_sort(queue.begin(), queue.end(), [&](int a, int b) { return dist[a] < dist[b]; });
    int id = queue.front();
    queue.erase(queue.begin());
    if (done[id]) continue;
    done[id] = true;
    if (isGoal(edges_[id])) {
      std::vector<int> path;
      for (int cur = id; cur >= 0; cur = prev[cur]) path.insert(path.begin(), cur);
      if (found) *found = true;
      return path;
    }
    for (int next : successors(id)) {
      float cost = dist[id] + edges_[next].length + turnCost(id, next);
      if (cost < dist[next]) {
        dist[next] = cost;
        prev[next] = id;
        queue.push_back(next);
      }
    }
  }
  if (found) *found = false;
  return {};
}

std::vector<int> TrackNetwork::outerLoopFrom(int edge, bool *found) const {
  for (int i = 0; i < trackdata::OUTER_LOOP_COUNT; i++) {
    if (trackdata::OUTER_LOOP[i] == edge) {
      *found = true;
      return std::vector<int>(trackdata::OUTER_LOOP + i + 1, trackdata::OUTER_LOOP + trackdata::OUTER_LOOP_COUNT);
    }
  }
  *found = false;
  return {};
}

Polyline TrackNetwork::composePath(Location start, const std::vector<int> &list, std::vector<Range> &ranges) const {
  std::vector<Vec2> controls;
  std::vector<int> owner;   // a que aresta (índice na lista) pertence cada ponto de controle
  const Edge &first = edges_[list[0]];
  Sample startPoint = first.line.sample(start.s);
  controls.push_back(startPoint.pos);
  owner.push_back(0);
  // pontos de controle da 1ª aresta que ainda estão à frente do carro
  for (int k = 1; k < first.count; k++) {
    float ps = first.line.project(first.points[k]).s;
    if (ps > start.s + 12) {
      controls.push_back(first.points[k]);
      owner.push_back(0);
    }
  }
  for (size_t i = 1; i < list.size(); i++) {
    const Edge &a = edges_[list[i - 1]];
    const Edge &b = edges_[list[i]];
    bool turning = angleBetween(unitBetween(a.points[a.count - 2], a.points[a.count - 1]),
                                unitBetween(b.points[0], b.points[1])) > STRAIGHT_DEG;
    if (turning && controls.size() > 1) {
      controls.pop_back();   // tira o nó (último ponto da aresta anterior)
      owner.pop_back();
    }
    for (int k = 1; k < b.count; k++) {
      controls.push_back(b.points[k]);
      owner.push_back((int)i);
    }
  }
  if (controls.size() == 1) controls.push_back(startPoint.pos + startPoint.dir);

  Polyline line(catmullRom(controls.data(), (int)controls.size()));
  // faixa de s de cada aresta: do 1º ao último ponto de controle que ela possui
  ranges.assign(list.size(), {INFINITY, -INFINITY});
  for (size_t k = 0; k < owner.size(); k++) {
    float s = line.cum[std::min(k * SAMPLES_PER_SPAN, line.cum.size() - 1)];
    Range &r = ranges[owner[k]];
    r.s0 = std::min(r.s0, s);
    r.s1 = std::max(r.s1, s);
  }
  // cada aresta começa onde a anterior termina (cobre a curva da junção)
  for (size_t i = 1; i < ranges.size(); i++) ranges[i].s0 = std::min(ranges[i].s0, ranges[i - 1].s1);
  ranges[0].s0 = 0;
  return line;
}
