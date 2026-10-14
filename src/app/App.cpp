#include "App.h"

#include <algorithm>
#include <stdio.h>

namespace {

constexpr float METERS_PER_PX = trackdata::METERS_PER_PX;
constexpr float PX_TO_WORLD = trackdata::METERS_PER_PX;   // 1 unidade do mundo = 1 m
constexpr uint32_t STOP_HOLD_MS = 3000;   // igual a STOP_WAIT_SECONDS do detector de placas
constexpr uint32_t POINT_HOLD_MS = 4000;
constexpr uint32_t STALE_MS = 3000;
constexpr uint32_t REVEAL_MS = 1100;
constexpr uint32_t GPS_DRAW_MS = 1100;
constexpr uint32_t GPS_FADE_MS = 450;
constexpr uint32_t HUD_INTERVAL_MS = 120;
constexpr float MAX_DT = 0.5f;   // s: quadros lentos do ESP32 não atrasam a simulação (antes 0,1 s)
constexpr float SNAP_DISTANCE_PX = 4;

// Câmera mais alta e olhando para baixo: horizonte perto de 30% do topo, como no BYD
struct Chase {
  float back, up, ahead, lookUp;
};
// Na tela de 480x320 o carro fica um pouco mais alto que no JS (ahead 4,6 -> 3,4) para não
// ficar atrás da barra de métricas.
constexpr Chase CHASE{4.8f, 3.7f, 3.4f, 0.0f};
constexpr float FOV_DEG = 48;

const Location CAR_START{trackdata::CAR_START_EDGE, trackdata::CAR_START_S};
const Location LAP_START{trackdata::LAP_START_EDGE, 0};

float angleLerp(float a, float b, float k) { return a + atan2f(sinf(b - a), cosf(b - a)) * k; }
Vec3 lerp3(Vec3 a, Vec3 b, float k) { return {a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k}; }

void formatMeters(char *out, size_t size, float meters) {
  if (!isfinite(meters)) {
    snprintf(out, size, "--");
  } else if (meters < 1) {
    snprintf(out, size, "%d cm", (int)std::max(0.0f, roundf(meters * 100)));
  } else {
    snprintf(out, size, "%.1f m", meters);
    for (char *c = out; *c; c++) if (*c == '.') *c = ',';
  }
}

std::string tail(const std::string &s, int from) { return from < (int)s.size() ? s.substr(std::max(0, from)) : ""; }

std::string arrowRoute(const std::string &route) {
  std::string out;
  for (char c : route) {
    if (!out.empty()) out += " → ";
    out += c;
  }
  return out;
}

}  // namespace

App::App() {}

// ── Missão efetiva ───────────────────────────────────────────────────────
std::string App::currentQueue() const {
  if (source_ == Source::Live) return tail(serverRoute_, localConsumed_);
  return tail(simRoute_, simDone_);
}

App::MissionView App::missionView() const {
  if (source_ == Source::Live) {
    return {liveDone_ + serverRoute_, (int)liveDone_.size() + localConsumed_, serverRaw_};
  }
  return {simRoute_, simDone_, arrowRoute(simRoute_)};
}

/** Onde o carro está, em termos de aresta da rede (para replanejar dali). */
Location App::currentLocation() const {
  if (plan_.edges.empty()) return CAR_START;
  int index = -1;
  for (size_t i = 0; i < plan_.ranges.size(); i++) {
    if (s_ >= plan_.ranges[i].s0 && s_ <= plan_.ranges[i].s1 + 0.5f) { index = (int)i; break; }
  }
  if (index < 0) index = (int)plan_.edges.size() - 1;
  const Edge &edge = network_.edge(plan_.edges[index]);
  Sample p = plan_.line.sample(s_);
  return {edge.index, std::min(edge.length - 1, edge.line.project(p.pos).s)};
}

void App::replan(bool reveal, const Location *from, uint32_t now) {
  std::string queue = currentQueue();
  Location start = from ? *from : currentLocation();
  plan_ = buildPlan(network_, start, queue, planMode_, source_ == Source::Live ? localizer_.detours : 0);
  planQueue_ = queue;
  planConsumed_ = 0;
  s_ = 0;
  missionDone_ = false;
  localizer_.reset();
  if (reveal) {
    revealStart_ = now;
    revealing_ = true;
  }
}

/** Missão mudou por fora (serial) e não só porque o carro alcançou pontos? */
bool App::needsReplan() const { return tail(planQueue_, planConsumed_) != currentQueue(); }

void App::arrivedAtPoint(char point, const char *role, uint32_t now) {
  pointHoldUntil_ = now + POINT_HOLD_MS;
  arrivalPoint_ = point;
  arrivalRole_ = role;
}

void App::consumePoint(uint32_t now) {
  planConsumed_ += 1;
  if (source_ == Source::Live) {
    localConsumed_ += 1;
  } else {
    int total = (int)simRoute_.size();
    arrivedAtPoint(simRoute_[simDone_], roleName(simDone_, total), now);
    simDone_ += 1;
  }
}

void App::finishMission() {
  if (missionDone_) return;
  missionDone_ = true;
}

// ── Serial ───────────────────────────────────────────────────────────────
/**
 * Sincroniza com a lista que o carro manda (nav.route). Quando ela encolhe, o carro
 * confirmou um ponto: a posição vai para a placa do ponto e a trajetória é refeita dali.
 */
void App::applyServerRoute(const std::string &next, const std::string &raw, uint32_t now) {
  serverRaw_ = raw;
  const std::string prev = serverRoute_;
  if (prev == next) return;
  int confirmed = 0;
  for (int k = 1; k <= (int)prev.size(); k++) {
    if (prev.substr(k) == next) { confirmed = k; break; }
  }
  serverRoute_ = next;
  if (confirmed > 0) {
    liveDone_ += prev.substr(0, confirmed);
    if (source_ == Source::Live) {
      int index = (int)liveDone_.size() - 1;
      arrivedAtPoint(liveDone_[index], roleName(index, (int)(liveDone_.size() + next.size())), now);
    }
    if (source_ == Source::Live && !plan_.edges.empty()) {
      int seen = 0;
      for (const PlanEvent &e : plan_.events) {
        if (e.type == EventType::Point && e.target && ++seen == confirmed) {
          // vai até a placa se ainda não chegou; já tendo passado, não volta (mão única)
          s_ = std::max(s_, std::min(plan_.length, e.s + 1));
          break;
        }
      }
    }
    localizer_.confirmPoint(now);
    pointConfirmed_ = true;
    localConsumed_ = 0;
    if (source_ == Source::Live && !plan_.edges.empty()) replan(false, nullptr, now);
  } else if (!(next.size() > prev.size() && next.compare(0, prev.size(), prev) == 0)) {
    liveDone_.clear();
    localConsumed_ = 0;
    missionDone_ = false;
  } else {
    missionDone_ = false;   // pontos novos depois de finalizar: novo percurso
  }
}

void App::onVehicleData(const VehicleData &data, uint32_t now) {
  lastOkAt_ = now;
  everOk_ = true;
  // _ts parado = "Dados parados"; sem _ts (formato antigo), cada linha conta como nova
  if (isnan(data.ts) || data.ts != lastTs_) {
    lastTs_ = data.ts;
    lastTsChangeAt_ = now;
  }
  liveData_ = data;
  hasData_ = true;
  if (data.clockMinutes >= 0) {
    clockMinutes_ = data.clockMinutes;
    clockAt_ = now;
  }
  updateLink(now);
  if (!userPickedSource_ && linkStatus_ == LinkStatus::Live && source_ != Source::Live) setSource(Source::Live, now);
  if (source_ == Source::Live) applyServerRoute(data.route, data.routeText, now);
}

void App::onWaiting(uint32_t now) {
  lastOkAt_ = now;
  everOk_ = true;
  linkStatus_ = LinkStatus::Waiting;
}

void App::onCommand(const PanelCommand &command) {
  if (command.source >= 0) setSource(command.source ? Source::Live : Source::Sim, last_, true);
  if (command.hasSimRoute) setSimRoute(command.simRoute, last_);
  if (command.planMode >= 0) {
    PlanMode mode = command.planMode ? PlanMode::Car : PlanMode::Shortest;
    if (mode != planMode_) {
      planMode_ = mode;
      replan(true, nullptr, last_);
    }
  }
}

void App::updateLink(uint32_t now) {
  if (!everOk_) {
    linkStatus_ = now > STALE_MS ? LinkStatus::Offline : LinkStatus::Connecting;
  } else if (now - lastOkAt_ > STALE_MS) {
    linkStatus_ = LinkStatus::Offline;
  } else if (linkStatus_ == LinkStatus::Waiting && !hasData_) {
    // continua aguardando o main.py
  } else {
    linkStatus_ = now - lastTsChangeAt_ > STALE_MS ? LinkStatus::Stale : LinkStatus::Live;
  }
}

void App::setSource(Source source, uint32_t now, bool byUser) {
  if (byUser) userPickedSource_ = true;
  if (source_ == source) return;
  source_ = source;
  if (source == Source::Live) {
    // Modo real: o carro só anda com o que chega da serial; começa na partida
    serverRoute_.clear();
    liveDone_.clear();
    localConsumed_ = 0;
    localizer_.restart();
    if (hasData_) applyServerRoute(liveData_.route, liveData_.routeText, now);
    replan(true, &CAR_START, now);
  } else {
    replan(true, nullptr, now);
  }
}

// ── Simulação ────────────────────────────────────────────────────────────
void App::setSimRoute(const std::string &route, uint32_t now) {
  simRoute_ = route;
  simDone_ = 0;
  stopHoldUntil_ = pointHoldUntil_ = 0;
  missionDone_ = false;
  // Antes do 1º quadro o update() já monta o plano da partida; depois, segue de onde o carro está
  if (!first_ && source_ == Source::Sim) replan(true, nullptr, now);
}

const PlanEvent *App::nextEvent(EventType type, float maxPx) const {
  for (const PlanEvent &e : plan_.events) {
    if (e.type == type && e.s - s_ >= -2 && e.s - s_ <= maxPx) return &e;
  }
  return nullptr;
}

float App::simSpeedPx(uint32_t now) const {
  if (missionDone_ || now < stopHoldUntil_ || now < pointHoldUntil_) return 0;
  return simSpeed_ / METERS_PER_PX;
}

// ── Prévia GPS ───────────────────────────────────────────────────────────
std::string App::signName(const PlanEvent &event) const {
  if (gpsTargetIsStart_) return "Linha de chegada";
  if (event.type == EventType::Point) {
    MissionView mission = missionView();
    size_t index = mission.all.find(event.point, mission.doneCount);
    char buf[48];
    if (event.target && index != std::string::npos) {
      snprintf(buf, sizeof buf, "%s · ponto %c", roleName((int)index, (int)mission.all.size()), event.point);
    } else {
      snprintf(buf, sizeof buf, "Ponto %c", event.point);
    }
    return buf;
  }
  return event.sign >= 0 ? trackdata::SIGNS[event.sign].label : "Placa";
}

void App::updateGps(uint32_t now, FrameView &view) {
  GpsReason reason = GpsReason::None;
  if (now < pointHoldUntil_) reason = GpsReason::Point;
  else if (source_ == Source::Sim) reason = now < stopHoldUntil_ ? GpsReason::Stop : GpsReason::None;
  else if (linkStatus_ == LinkStatus::Live && hasData_ && liveData_.stopActive) reason = GpsReason::Stop;

  if (reason != GpsReason::None && (reason != gpsReason_ || (gpsActive_ && gpsPlanSerial_ != plan_.serial))) {
    // Abre: rota do carro até a próxima placa (ou próximo ponto)
    gpsReason_ = reason;
    gpsUntil_ = reason == GpsReason::Point ? pointHoldUntil_ : now + STOP_HOLD_MS;
    gpsTargetIsStart_ = true;
    gpsTarget_ = PlanEvent{EventType::Point, plan_.length};
    for (const PlanEvent &e : plan_.events) {
      bool wanted = reason == GpsReason::Point ? (e.type == EventType::Point && e.target) : e.type != EventType::Turn;
      if (wanted && e.s > s_ + 12) {
        gpsTarget_ = e;
        gpsTargetIsStart_ = false;
        break;
      }
    }
    float s1 = std::min(plan_.length, gpsTarget_.s);
    float span = std::max(6.0f, s1 - s_);
    gpsS0_ = s_;
    gpsS1_ = s_ + span;
    gpsStart_ = plan_.line.sample(gpsS0_).pos;
    gpsEnd_ = plan_.line.sample(s1).pos;
    gpsActive_ = true;
    gpsStartedAt_ = now;
    gpsPlanSerial_ = plan_.serial;
    HudView &h = hud_;
    if (reason == GpsReason::Point) {
      char badge[32];
      snprintf(badge, sizeof badge, "%s %c", arrivalRole_, arrivalPoint_ ? arrivalPoint_ : ' ');
      h.gpsBadge = badge;
      h.gpsStop = false;
      h.gpsLabel = gpsTargetIsStart_ ? "Próximo destino" : "Próximo ponto";
    } else {
      h.gpsBadge = "PARE";
      h.gpsStop = true;
      h.gpsLabel = "Próxima placa";
    }
    h.gpsTarget = signName(gpsTarget_);
  } else if (reason == GpsReason::None && gpsReason_ != GpsReason::None) {
    gpsActive_ = false;
    gpsEndedAt_ = now;
    gpsReason_ = GpsReason::None;
  }

  // Desenho: a rota "desenha" em 1,1 s e some em 0,45 s
  float drawn = std::min(1.0f, (now - gpsStartedAt_) / (float)GPS_DRAW_MS);
  float opacity = gpsActive_ ? 1.0f : std::max(0.0f, 1.0f - (now - gpsEndedAt_) / (float)GPS_FADE_MS);
  if (gpsEndedAt_ == 0 && !gpsActive_) opacity = 0;
  view.gpsVisible = opacity > 0;
  view.gpsOpacity = opacity;
  view.gpsProgress = 1 - powf(1 - drawn, 3);
  view.gpsS0 = gpsS0_;
  view.gpsS1 = gpsS1_;
  view.gpsPin = gpsEnd_;
  hud_.gps = view.gpsVisible;
  hud_.gpsOpacity = opacity;
  if (!gpsActive_) return;

  float meters = std::max(0.0f, gpsTarget_.s - s_) * METERS_PER_PX;
  float speed = source_ == Source::Sim ? simSpeed_ : cruiseMs_;
  int left = (int)ceilf(std::max(0.0f, ((int32_t)(gpsUntil_ - now)) / 1000.0f - 0.05f));
  const char *where = gpsReason_ == GpsReason::Point ? "No ponto" : "Parado na placa";
  formatMeters(hud_.gpsDistance, sizeof hud_.gpsDistance, meters);
  if (speed > 0.02f) snprintf(hud_.gpsEta, sizeof hud_.gpsEta, "~%d s", std::max(1, (int)roundf(meters / speed)));
  else snprintf(hud_.gpsEta, sizeof hud_.gpsEta, "--");
  char status[48];
  if (left > 0 && source_ == Source::Sim) snprintf(status, sizeof status, "%s · segue em %d s", where, left);
  else snprintf(status, sizeof status, "%s", where);
  hud_.gpsStatus = status;
}

// ── Câmera (cameraRig.js) ────────────────────────────────────────────────
void App::updateCamera(float dt, float wx, float wz, float dirX, float dirZ, FrameView &view) {
  Vec3 target, look;
  // Sempre atrás do carro; parado numa placa (PARE ou ponto) abre e mostra a pista inteira,
  // quase de cima, segura essa vista enquanto o carro espera e volta sozinha quando ele segue.
  // Só a câmera muda: a simulação e a missão continuam de onde estavam.
  bool framing = gpsActive_;
  if (framing) {
    // Vista de cima da pista toda (todos os trechos), encaixada na área livre da tela
    // (à direita do cartão da missão e acima do cartão da prévia). Norte da pista para cima.
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (int e = 0; e < network_.edgeCount(); e++) {
      for (const Vec2 &p : network_.edge(e).line.pts) {
        minX = std::min(minX, p.x), maxX = std::max(maxX, p.x), minZ = std::min(minZ, p.y), maxZ = std::max(maxZ, p.y);
      }
    }
    const float margin = 1.0f;   // m: meia largura da faixa + folga
    float w = (maxX - minX) * PX_TO_WORLD + 2 * margin, h = (maxZ - minZ) * PX_TO_WORLD + 2 * margin;
    // área livre: x 176..470 e y 44..232 de 480x320
    const float safeW = 0.61f, safeH = 0.59f, safeX = 0.173f, safeY = -0.069f, aspect = 1.5f;
    float tanHalf = tanf(FOV_DEG * (float)M_PI / 360.0f);
    float dist = std::max(6.0f, std::max(h / (safeH * 2 * tanHalf), w / (safeW * aspect * 2 * tanHalf)));
    float viewW = 2 * dist * tanHalf * aspect, viewH = 2 * dist * tanHalf;
    Vec3 center{(minX + maxX) / 2 * PX_TO_WORLD - safeX * viewW, 0, (minZ + maxZ) / 2 * PX_TO_WORLD - safeY * viewH};
    const float tilt = 0.18f;   // leve inclinação, mantém a sensação de 3D
    target = {center.x, dist * cosf(tilt), center.z + dist * sinf(tilt)};
    look = center;
  } else {
    target = {wx - dirX * CHASE.back, CHASE.up, wz - dirZ * CHASE.back};
    look = {wx + dirX * CHASE.ahead, CHASE.lookUp, wz + dirZ * CHASE.ahead};
  }
  if (!camReady_) {
    camPos_ = target;
    camLook_ = look;
    camReady_ = true;
  }
  // Aproximação exponencial: segue o carro sem tremer e sem atraso perceptível
  float k = 1 - expf(-dt * (framing ? 2.6f : 5.5f));
  camPos_ = lerp3(camPos_, target, k);
  camLook_ = lerp3(camLook_, look, std::min(1.0f, k * 1.6f));
  view.camPos = camPos_;
  view.camLook = camLook_;
  view.fovDeg = FOV_DEG;
  bool chaseFog = !framing;
  view.fogNear = chaseFog ? 12 : 300;
  view.fogFar = chaseFog ? 40 : 400;
}

// ── HUD (hud.js) ─────────────────────────────────────────────────────────
void App::updateHud(uint32_t now, float speedMs, FrameView &view) {
  if (now - hudAt_ < HUD_INTERVAL_MS && hudAt_ != 0) return;
  hudAt_ = now;
  HudView &h = hud_;

  // Relógio: o ESP32 não tem RTC; usa a hora que a serial mandar ("time": "HH:MM")
  if (clockMinutes_ >= 0) {
    int minutes = (clockMinutes_ + (int)((now - clockAt_) / 60000)) % (24 * 60);
    snprintf(h.clock, sizeof h.clock, "%02d:%02d", minutes / 60, minutes % 60);
  }
  snprintf(h.speed, sizeof h.speed, "%.2f", speedMs);
  for (char *c = h.speed; *c; c++) if (*c == '.') *c = ',';
  h.sim = source_ == Source::Sim;
  h.link = linkStatus_;
  h.light = view.light;

  // Próxima manobra (pickManeuver)
  MissionView mission = missionView();
  ManeuverIcon icon;
  char point = 0;
  std::string label;
  float distancePx = 0;
  if (missionDone_) {
    icon = ManeuverIcon::Done;
    label = "Percurso finalizado";
  } else {
    const PlanEvent *next = nullptr;
    for (const PlanEvent &e : plan_.events) {
      bool wanted = e.type == EventType::Turn || e.type == EventType::Stop || e.type == EventType::TrafficLight ||
                    (e.type == EventType::Point && e.target);
      if (wanted && e.s - s_ > 3) { next = &e; break; }
    }
    if (!next) {
      icon = ManeuverIcon::Flag;
      label = "Volta à largada";
      distancePx = plan_.length - s_;
    } else {
      distancePx = next->s - s_;
      if (next->type == EventType::Turn) {
        icon = next->turn == Turn::Right ? ManeuverIcon::Right : next->turn == Turn::Left ? ManeuverIcon::Left
                                                                                        : ManeuverIcon::Straight;
        label = next->turn == Turn::Right ? "Curva suave à direita"
                : next->turn == Turn::Left ? "Curva suave à esquerda" : "Siga em frente";
      } else if (next->type == EventType::Stop) {
        icon = ManeuverIcon::Stop;
        label = "Placa PARE";
      } else if (next->type == EventType::TrafficLight) {
        icon = ManeuverIcon::Light;
        label = "Semáforo";
      } else {
        icon = ManeuverIcon::Point;
        point = next->point;
        size_t index = mission.all.find(next->point, mission.doneCount);
        if (index != std::string::npos) {
          label = roleAhead((int)index, (int)mission.all.size());
        } else {
          char buf[32];
          snprintf(buf, sizeof buf, "Ponto %c à frente", next->point);
          label = buf;
        }
      }
    }
  }
  int key = (int)icon * 256 + point;
  if (key != lastManeuverKey_ || label != h.maneuverLabel) {
    lastManeuverKey_ = key;
    h.maneuverChangedAt = now;
  }
  h.icon = icon;
  h.iconPoint = point;
  h.maneuverLabel = label;
  if (icon == ManeuverIcon::Done) snprintf(h.maneuverDistance, sizeof h.maneuverDistance, "Fim");
  else formatMeters(h.maneuverDistance, sizeof h.maneuverDistance, distancePx * METERS_PER_PX);

  // Missão: o que veio do código + papel de cada ponto
  h.missionAll = mission.all;
  h.missionDone = mission.doneCount;
  h.missionReceived = !mission.raw.empty() && !mission.all.empty();

  // Métricas
  const VehicleData *data = source_ == Source::Live && hasData_ ? &liveData_ : nullptr;
  char buf[24];
  if (data) {
    snprintf(buf, sizeof buf, "%d", (int)roundf(data->pwm));
    h.pwm = buf;
    snprintf(buf, sizeof buf, "%d°", (int)roundf(data->servo));
    h.servo = buf;
    if (isfinite(data->battery) && data->battery != 0) {
      snprintf(buf, sizeof buf, "%d%%", (int)roundf(data->battery));
      h.battery = buf;
    } else {
      h.battery = "--";
    }
  } else {
    h.pwm = h.servo = h.battery = "--";
  }
  if (source_ == Source::Sim) {
    h.mode = missionDone_ ? "Finalizado" : now < stopHoldUntil_ ? "Parado na placa" : "Simulando";
  } else if (data) {
    if (!data->controlMode.empty()) {
      std::string m = data->controlMode;
      for (size_t i = 1; i < m.size(); i++) m[i] = (char)tolower((unsigned char)m[i]);
      h.mode = m;
    } else {
      h.mode = data->running ? "Em movimento" : "Parado";
    }
  } else {
    h.mode = "--";
  }

  if (!plan_.unreachable.empty()) {
    std::string pts;
    for (char c : plan_.unreachable) pts += std::string(pts.empty() ? "" : ", ") + c;
    h.warning = (plan_.mode == PlanMode::Car ? "Tabela do carro não leva a " : "Sem caminho até ") + pts;
  } else {
    h.warning.clear();
  }
  h.estimate = source_ == Source::Live && linkStatus_ == LinkStatus::Live ? localizer_.describe(now) : "";
}

// ── Quadro ───────────────────────────────────────────────────────────────
void App::update(uint32_t now, FrameView &view) {
  if (first_) {
    first_ = false;
    last_ = now;
    replan(true, &CAR_START, now);
  }
  float dt = std::min(MAX_DT, (now - last_) / 1000.0f);
  last_ = now;
  updateLink(now);

  if (needsReplan()) replan(true, nullptr, now);

  float speedMs = 0;
  if (source_ == Source::Live) {
    // Carro real: telemetria + marcos detectados (o ponto só conta quando a serial confirma)
    const VehicleData *data = linkStatus_ == LinkStatus::Live && hasData_ ? &liveData_ : nullptr;
    // Acompanha a velocidade real: freia e acelera junto (~0,25 s), sem dar saltos
    float target = localizer_.speed(data, pwmToMs_);
    liveSpeed_ += (target - liveSpeed_) * (1 - expf(-dt * 8));
    if (target == 0 && liveSpeed_ < 0.01f) liveSpeed_ = 0;
    speedMs = liveSpeed_;
    s_ = localizer_.step(plan_, s_, data, dt, speedMs, METERS_PER_PX, now, pointConfirmed_, !serverRoute_.empty());
    pointConfirmed_ = false;
    if (s_ >= plan_.length) {
      replan(false, &LAP_START, now);
      localizer_.lapStart(now);
      // Volta fechada depois do último ponto: percurso finalizado na linha de chegada
      if (serverRoute_.empty() && !liveDone_.empty()) finishMission();
    }
  } else {
    float speedPx = simSpeedPx(now);
    speedMs = speedPx * METERS_PER_PX;
    if (speedPx > 0) {
      float next = s_ + speedPx * dt;
      // PARE no caminho deste quadro: para nela (por cruzamento, vale para qualquer velocidade e fps)
      if (const PlanEvent *stop = nextEvent(EventType::Stop, next - s_ + 8)) {
        int key = (int)plan_.length * 8192 + (int)stop->s;
        if (stoppedAtKey_ != key) {
          stoppedAtKey_ = key;
          stopHoldUntil_ = now + STOP_HOLD_MS;
          next = std::max(s_, std::min(next, stop->s - 8));
        }
      }
      s_ = next;
      int seen = 0;
      for (const PlanEvent &e : plan_.events) {
        if (e.type != EventType::Point || !e.target) continue;
        if (seen++ < planConsumed_) continue;
        if (s_ < e.s) break;
        s_ = e.s + 0.5f;   // para na placa, sem passar dela
        consumePoint(now);
        break;
      }
      if (s_ >= plan_.length) {
        // Volta completa: missão cumprida para na linha de chegada e fica lá (percurso finalizado);
        // sem missão, segue em voltas. Missão nova pela serial ({"panel":{"sim_route":...}}) sai dali.
        bool hadMission = !planQueue_.empty();
        replan(false, &LAP_START, now);
        if (hadMission && currentQueue().empty()) finishMission();
      }
    }
  }

  Sample p = plan_.line.sample(s_);
  Sample ahead = plan_.line.sample(s_ + 6);
  float yaw = atan2f(ahead.pos.y - p.pos.y, ahead.pos.x - p.pos.x);

  // Direção suavizada: sem "quinas" de amostragem na trajetória
  heading_ = headingReady_ ? angleLerp(heading_, yaw, 1 - expf(-dt * 14)) : yaw;
  headingReady_ = true;
  float dirX = cosf(heading_), dirZ = sinf(heading_);
  // Posição: acompanha direto; saltos de correção deslizam em ~0,3 s. O avanço normal do quadro
  // não conta como salto (com quadros lentos ele passa de SNAP_DISTANCE_PX e o carro ficava para trás).
  float jump = shownReady_ ? hypotf(p.pos.x - shownX_, p.pos.y - shownY_) : 0;
  float expected = speedMs / METERS_PER_PX * dt;
  float follow = shownReady_ ? (jump > expected + SNAP_DISTANCE_PX ? 1 - expf(-dt * 9) : 1) : 1;
  shownX_ += (p.pos.x - shownX_) * follow;
  shownY_ += (p.pos.y - shownY_) * follow;
  shownReady_ = true;
  if (source_ == Source::Live && speedMs > 0.05f) cruiseMs_ += (speedMs - cruiseMs_) * std::min(1.0f, dt * 0.5f);

  view.light = source_ == Source::Live && hasData_ && liveData_.trafficLight >= 0 && liveData_.trafficLight <= 2
                   ? liveData_.trafficLight : -1;
  updateGps(now, view);

  // Pedestre (command.stop) ou obstáculo crítico nos ultrassônicos da frente
  hazard_ = false;
  if (source_ == Source::Live && linkStatus_ == LinkStatus::Live && hasData_) {
    if (liveData_.pedestrian) {
      hazard_ = true;
      hud_.hazardPerson = true;
    } else if (liveData_.proxFrontLeft >= 3 || liveData_.proxFrontRight >= 3) {
      hazard_ = true;
      hud_.hazardPerson = false;
    }
  }
  hud_.hazard = hazard_;

  updateCamera(dt, shownX_ * PX_TO_WORLD, shownY_ * PX_TO_WORLD, dirX, dirZ, view);

  view.carX = shownX_;
  view.carY = shownY_;
  view.carDirX = dirX;
  view.carDirY = dirZ;
  view.plan = &plan_;
  view.s = s_;
  view.time = now / 1000.0f;
  if (revealing_) {
    float t = std::min(1.0f, (now - revealStart_) / (float)REVEAL_MS);
    float eased = 1 - powf(1 - t, 3);
    view.reveal = t >= 1 ? 1e9f : s_ + eased * std::max(plan_.length - s_, 1.0f);
    if (t >= 1) revealing_ = false;
  } else {
    view.reveal = 1e9f;
  }
  updateHud(now, speedMs, view);
  view.hud = hud_;
}
