#pragma once
// Porte de network.js: rede de caminhos da pista (grafo dirigido, mão única).
// Cada trecho de trackdata::SEGMENTS vira uma aresta com a curva já suavizada.

#include <functional>
#include <vector>

#include "Geometry.h"
#include "../generated/track_data.h"

struct Edge {
  int index;            // = índice do trecho em SEGMENTS (base)
  const char *id;
  int from, to;         // nós
  const Vec2 *points;   // pontos de controle
  int count;
  Polyline line;
  float length;
};

struct Location {
  int edge;
  float s;
};

/** Trajetória contínua montada a partir de uma lista de arestas. */
struct Range {
  float s0, s1;
};

class TrackNetwork {
 public:
  TrackNetwork();

  const Edge &edge(int i) const { return edges_[i]; }
  int edgeCount() const { return (int)edges_.size(); }
  const SignDef &sign(int i) const { return trackdata::SIGNS[i]; }
  int signCount() const { return trackdata::SIGN_COUNT; }

  /** Placa do ponto A/B/C (-1 se não existe). */
  int pointSign(char point) const;
  float signS(const SignDef &sign) const { return sign.sBase; }

  std::vector<int> successors(int edge) const;
  float turnCost(int a, int b) const;
  /** Menor caminho saindo do fim de fromEdge até uma aresta que satisfaça isGoal. Vazio = sem caminho. */
  std::vector<int> shortestPath(int fromEdge, const std::function<bool(const Edge &)> &isGoal, bool *found) const;
  /** Volta externa a partir do fim de `edge` (vazio se a aresta não está nela). */
  std::vector<int> outerLoopFrom(int edge, bool *found) const;
  /** Junta arestas numa trajetória suave a partir de start; preenche ranges por aresta. */
  Polyline composePath(Location start, const std::vector<int> &edges, std::vector<Range> &ranges) const;

 private:
  std::vector<Edge> edges_;
};
