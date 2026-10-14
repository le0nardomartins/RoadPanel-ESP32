#pragma once
// Porte de localizer.js: posição do carro real na trajetória, sem GPS (odometria + marcos).

#include <string>

#include "Planner.h"
#include "../link/VehicleData.h"

class Localizer {
 public:
  enum class SpeedSource : uint8_t { None, Stopped, Telemetry, Pwm };

  Localizer() { reset(); }

  /** Plano novo (s volta a 0 na largada ou no ponto de replanejamento). */
  void reset();
  /** Velocidade estimada (m/s). data = null sem dados. */
  float speed(const VehicleData *data, float pwmToMs);
  /** Um quadro de localização. Devolve o novo s (px) da trajetória. */
  float step(const Plan &plan, float s, const VehicleData *data, float dt, float speedMs, float metersPerPx,
             uint32_t now, bool routeShrank, bool hasRoute);
  /** O servidor confirmou um ponto: o carro está na placa dele (nav zera o contador). */
  void confirmPoint(uint32_t now);
  /** Entrou no modo real: esquece contadores e referências da sessão anterior. */
  void restart();
  /** Volta fechada (passou pela largada). */
  void lapStart(uint32_t now);
  /** Texto curto para o HUD. */
  std::string describe(uint32_t now) const;

  int detours = 0;   // desvios vistos desde o último ponto (nav.action_counter)
  SpeedSource speedSource = SpeedSource::None;

 private:
  float snap(const Plan &plan, float s, EventType type, uint32_t now, bool *ok);

  struct Prev {
    bool valid = false;
    bool rightDetour = false;
    int trafficLight = -1;
    bool stopActive = false;
  } prev_;
  bool wheelsSeen_ = false;
  float gateFloor_ = -INFINITY;   // portões até aqui já foram confirmados
  float overflow_ = 0;
  float behind_ = 0;              // px que um marco mandou recuar, pagos pela odometria (sem ré)
  const PlanEvent *waiting_ = nullptr;   // portão em que a estimativa está esperando
  char waitingPoint_ = 0;
  EventType waitingType_ = EventType::Detour;
  const char *refLabel_ = nullptr;
  uint32_t refAt_ = 0;
};
