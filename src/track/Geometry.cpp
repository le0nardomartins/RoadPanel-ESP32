#include "Geometry.h"

#include <algorithm>

std::vector<Vec2> catmullRom(const Vec2 *points, int count) {
  std::vector<Vec2> out;
  if (count < 3) {
    out.assign(points, points + count);
    return out;
  }
  // pts = [p0, ...points, pn]
  std::vector<Vec2> pts;
  pts.reserve(count + 2);
  pts.push_back(points[0]);
  pts.insert(pts.end(), points, points + count);
  pts.push_back(points[count - 1]);
  out.reserve((pts.size() - 3) * SAMPLES_PER_SPAN + 1);

  auto knot = [](Vec2 a, Vec2 b) {
    float d = length(b - a);
    return sqrtf(d > 0 ? d : 1e-6f);
  };
  auto lerp = [](Vec2 a, Vec2 b, float ta, float tb, float t) {
    float ka = (tb - t) / (tb - ta);
    float kb = (t - ta) / (tb - ta);
    return Vec2{ka * a.x + kb * b.x, ka * a.y + kb * b.y};
  };
  for (size_t i = 0; i + 3 < pts.size(); i++) {
    Vec2 p0 = pts[i], p1 = pts[i + 1], p2 = pts[i + 2], p3 = pts[i + 3];
    float t0 = 0;
    float t1 = t0 + knot(p0, p1);
    float t2 = t1 + knot(p1, p2);
    float t3 = t2 + knot(p2, p3);
    for (int k = 0; k < SAMPLES_PER_SPAN; k++) {
      float t = t1 + ((t2 - t1) * k) / SAMPLES_PER_SPAN;
      Vec2 a1 = lerp(p0, p1, t0, t1, t);
      Vec2 a2 = lerp(p1, p2, t1, t2, t);
      Vec2 a3 = lerp(p2, p3, t2, t3, t);
      out.push_back(lerp(lerp(a1, a2, t0, t2, t), lerp(a2, a3, t1, t3, t), t1, t2, t));
    }
  }
  out.push_back(points[count - 1]);
  return out;
}

Polyline::Polyline(std::vector<Vec2> points) : pts(std::move(points)) {
  cum.resize(pts.size());
  if (pts.empty()) return;
  cum[0] = 0;
  for (size_t i = 1; i < pts.size(); i++) cum[i] = cum[i - 1] + ::length(pts[i] - pts[i - 1]);
  length = cum.back();
}

Sample Polyline::sample(float s) const {
  if (pts.size() < 2) return {pts.empty() ? Vec2{0, 0} : pts[0], {1, 0}};
  float clamped = std::max(0.0f, std::min(length, s));
  size_t lo = 0, hi = cum.size() - 1;
  while (hi - lo > 1) {
    size_t mid = (lo + hi) >> 1;
    if (cum[mid] <= clamped) lo = mid; else hi = mid;
  }
  Vec2 a = pts[lo], b = pts[hi];
  float span = cum[hi] - cum[lo];
  if (span == 0) span = 1;
  float t = (clamped - cum[lo]) / span;
  Vec2 d = b - a;
  float len = ::length(d);
  if (len == 0) len = 1;
  return {{a.x + d.x * t, a.y + d.y * t}, {d.x / len, d.y / len}};
}

Projection Polyline::project(Vec2 p, float s0, float s1) const {
  Projection best{s0, INFINITY};
  for (size_t i = 1; i < pts.size(); i++) {
    if (cum[i] < s0 || cum[i - 1] > s1) continue;
    Vec2 a = pts[i - 1];
    Vec2 v = pts[i] - a;
    float len2 = v.x * v.x + v.y * v.y;
    if (len2 == 0) len2 = 1;
    float t = std::max(0.0f, std::min(1.0f, dot(p - a, v) / len2));
    float d = ::length(a + v * t - p);
    float s = cum[i - 1] + t * sqrtf(len2);
    if (d < best.d && s >= s0 && s <= s1) best = {s, d};
  }
  return best;
}
