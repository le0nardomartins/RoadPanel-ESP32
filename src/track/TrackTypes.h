#pragma once
// Tipos básicos da pista, compartilhados pelos dados gerados e pela lógica.
// Unidades: px da pista em medidas oficiais (1 px = 2 cm), x para a direita, y para baixo.

#include <math.h>
#include <stdint.h>

struct Vec2 {
  float x, y;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float k) { return {a.x * k, a.y * k}; }
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline float length(Vec2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
inline Vec2 unitBetween(Vec2 a, Vec2 b) {
  Vec2 d = b - a;
  float len = length(d);
  if (len == 0) len = 1;
  return {d.x / len, d.y / len};
}

enum class SignKind : uint8_t { TrafficLight, Detour, Point, Stop };

struct SegmentDef {
  const char *id;
  int from, to;          // índices em NODE_NAMES
  const Vec2 *points;    // pontos de controle (Catmull-Rom)
  int count;
};

struct SignDef {
  const char *id;
  SignKind kind;
  char point;            // 'A' | 'B' | 'C' para pontos; ' ' nas outras
  const char *label;
  Vec2 at;               // posição da placa
  int base;              // trecho (índice em SEGMENTS) a que ela se liga
  float sBase;           // s da placa ao longo desse trecho
  Vec2 facing;           // para onde a face olha
};
