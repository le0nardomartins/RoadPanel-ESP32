#include "Localizer.h"

#include <algorithm>
#include <stdio.h>

namespace {
// Distância de detecção (px antes da placa) em que o marco costuma disparar
float lead(EventType type) {
  switch (type) {
    case EventType::Detour: return 40;
    case EventType::TrafficLight: return 90;
    case EventType::Stop: return 30;
    default: return 0;
  }
}
const char *label(EventType type) {
  switch (type) {
    case EventType::Detour: return "placa de desvio";
    case EventType::TrafficLight: return "semáforo";
    case EventType::Stop: return "placa PARE";
    default: return "ponto";
  }
}
// Janela de busca do marco: pouco atrás (a estimativa pode ter passado) e bem à frente
constexpr float SEARCH_BACK = 160;
constexpr float SEARCH_AHEAD = 520;
constexpr float GATE_SLACK = 20;      // quanto a estimativa pode passar do portão
constexpr float GATE_RELEASE = 75;    // ~1,5 m de odometria além do portão para desistir de esperar
}  // namespace

void Localizer::reset() {
  gateFloor_ = -INFINITY;
  overflow_ = 0;
  waiting_ = nullptr;
  speedSource = SpeedSource::None;
}

float Localizer::speed(const VehicleData *data, float pwmToMs) {
  if (!data || !data->running) {
    speedSource = data ? SpeedSource::Stopped : SpeedSource::None;
    return 0;
  }
  if (data->speed > 0.005f) {
    wheelsSeen_ = true;
    speedSource = SpeedSource::Telemetry;
    return data->speed;
  }
  // Rodas já mandaram velocidade alguma vez: 0 nelas é o carro parado de verdade
  if (wheelsSeen_) {
    speedSource = SpeedSource::Telemetry;
    return 0;
  }
  if (data->pwm > 0 && pwmToMs > 0) {
    speedSource = SpeedSource::Pwm;
    return data->pwm * pwmToMs;
  }
  speedSource = SpeedSource::Stopped;
  return 0;
}

float Localizer::step(const Plan &plan, float s, const VehicleData *data, float dt, float speedMs, float metersPerPx,
                      uint32_t now, bool routeShrank, bool hasRoute) {
  // 1) Odometria
  float next = s + (speedMs / metersPerPx) * dt;

  // 2) Marcos: bordas de subida do que o carro detectou
  if (data && prev_.valid) {
    bool ok;
    if (data->rightDetour && !prev_.rightDetour) {
      float v = snap(plan, next, EventType::Detour, now, &ok);
      if (ok) next = v;
      if (hasRoute) detours += 1;
    }
    if (data->trafficLight >= 0 && prev_.trafficLight < 0) {
      float v = snap(plan, next, EventType::TrafficLight, now, &ok);
      if (ok) next = v;
    }
    if (data->stopActive && !prev_.stopActive && !routeShrank) {
      float v = snap(plan, next, EventType::Stop, now, &ok);
      if (ok) next = v;
    }
  }
  if (data) prev_ = {true, data->rightDetour, data->trafficLight, data->stopActive};
  else prev_.valid = false;

  // 3) Portão: não passa da próxima placa de desvio / ponto-alvo sem confirmação
  const PlanEvent *gate = nullptr;
  for (const PlanEvent &e : plan.events) {
    if (e.s > gateFloor_ + 1 && (e.type == EventType::Detour || (e.type == EventType::Point && e.target))) {
      gate = &e;
      break;
    }
  }
  if (gate && next > gate->s + GATE_SLACK) {
    overflow_ += next - std::max(s, gate->s + GATE_SLACK);
    if (overflow_ > GATE_RELEASE) {
      gateFloor_ = gate->s;   // placa perdida: segue a estimativa
      overflow_ = 0;
      waiting_ = nullptr;
    } else {
      next = std::max(s, gate->s + GATE_SLACK);
      waiting_ = gate;
      waitingType_ = gate->type;
      waitingPoint_ = gate->point;
    }
  } else {
    waiting_ = nullptr;
  }
  return next;
}

float Localizer::snap(const Plan &plan, float s, EventType type, uint32_t now, bool *ok) {
  const PlanEvent *best = nullptr;
  float bestScore = 0;
  for (const PlanEvent &event : plan.events) {
    if (event.type != type) continue;
    float d = event.s - s;
    if (d < -SEARCH_BACK || d > SEARCH_AHEAD) continue;
    // Prefere o primeiro à frente; atrás só se não houver nenhum à frente
    float score = d >= -lead(type) ? fabsf(d) : fabsf(d) + SEARCH_AHEAD;
    if (!best || score < bestScore) {
      best = &event;
      bestScore = score;
    }
  }
  *ok = best != nullptr;
  if (!best) return s;
  float target = std::max(0.0f, best->s - lead(type));
  if (type == EventType::Detour) gateFloor_ = std::max(gateFloor_, best->s);
  overflow_ = 0;
  refLabel_ = label(type);
  refAt_ = now;
  return target;
}

void Localizer::confirmPoint(uint32_t now) {
  detours = 0;
  refLabel_ = "ponto";
  refAt_ = now;
}

void Localizer::restart() {
  prev_ = {};
  wheelsSeen_ = false;
  detours = 0;
  refLabel_ = nullptr;
  reset();
}

void Localizer::lapStart(uint32_t now) {
  reset();
  refLabel_ = "largada";
  refAt_ = now;
}

std::string Localizer::describe(uint32_t now) const {
  char buf[96];
  if (waiting_) {
    if (waitingType_ == EventType::Detour) return "Aguardando a placa de desvio";
    snprintf(buf, sizeof buf, "Aguardando o ponto %c", waitingPoint_);
    return buf;
  }
  if (!refLabel_) return "Posição estimada pela velocidade";
  unsigned seconds = (now - refAt_) / 1000;
  if (seconds < 2) snprintf(buf, sizeof buf, "Última referência: %s (agora)", refLabel_);
  else snprintf(buf, sizeof buf, "Última referência: %s (há %u s)", refLabel_, seconds);
  return buf;
}
