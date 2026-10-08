#pragma once
// Porte de network.js (catmullRom, Polyline): curva suave e medição ao longo dela.

#include <vector>

#include "TrackTypes.h"

constexpr int SAMPLES_PER_SPAN = 12;

/** Catmull-Rom centrípeta em 2D (sem laços nem pontas). */
std::vector<Vec2> catmullRom(const Vec2 *points, int count);

struct Sample {
  Vec2 pos;
  Vec2 dir;   // unitário, no sentido da polilinha
};

struct Projection {
  float s;
  float d;    // INFINITY se nada caiu na janela pedida
};

/** Polilinha com comprimento acumulado, amostragem e projeção. */
class Polyline {
 public:
  Polyline() = default;
  explicit Polyline(std::vector<Vec2> pts);

  Sample sample(float s) const;
  /** Distância s mais próxima de p, opcionalmente só entre s0 e s1. */
  Projection project(Vec2 p, float s0 = 0, float s1 = INFINITY) const;

  std::vector<Vec2> pts;
  std::vector<float> cum;
  float length = 0;
};
