#pragma once
// Porte de main.js + cameraRig.js + hud.js (parte lógica): estado do painel, simulação,
// modo real pela serial, localização, prévia GPS, alertas e câmera. Não desenha nada:
// a cada quadro produz um FrameView que o renderizador consome. Independente de Arduino.

#include <string>

#include "../link/VehicleData.h"
#include "../track/Localizer.h"
#include "../track/Planner.h"

struct Vec3 {
  float x, y, z;   // metros; y para cima (como o Three.js), z = y da pista
};

enum class Source : uint8_t { Sim, Live };
enum class LinkStatus : uint8_t { Connecting, Live, Waiting, Stale, Offline };
enum class ManeuverIcon : uint8_t { Straight, Right, Left, Stop, Light, Flag, Done, Point };

struct HudView {
  char clock[8] = "--:--";
  char speed[12] = "0,00";
  // estado da origem: "sim" nunca se passa por dado real
  bool sim = true;
  LinkStatus link = LinkStatus::Connecting;
  int light = -1;                       // -1 nenhum, 0 vermelho, 1 amarelo, 2 verde
  ManeuverIcon icon = ManeuverIcon::Straight;
  char iconPoint = 0;                   // letra dentro do hexágono
  std::string maneuverLabel;
  char maneuverDistance[16] = "--";
  uint32_t maneuverChangedAt = 0;       // para a transição do cartão
  std::string missionAll;               // pontos na ordem, ex.: "AC"
  int missionDone = 0;
  bool missionReceived = false;
  std::string pwm = "--", servo = "--", battery = "--", mode = "--";
  std::string warning;                  // rota inalcançável
  std::string estimate;                 // "Posição estimada…" (modo real)
  bool hazard = false;
  bool hazardPerson = true;
  // Cartão da prévia GPS
  bool gps = false;
  bool gpsStop = true;                  // PARE (vermelho) ou ponto (azul)
  std::string gpsBadge, gpsStatus, gpsLabel, gpsTarget;
  char gpsDistance[16] = "--", gpsEta[12] = "--";
  float gpsOpacity = 0;
};

struct FrameView {
  // Câmera (metros)
  Vec3 camPos{0, 0, 0}, camLook{0, 0, 1};
  float fovDeg = 48;
  float fogNear = 12, fogFar = 40;     // metros
  // Carro (px da pista)
  float carX = 0, carY = 0, carDirX = 1, carDirY = 0;
  // Trajetória
  const Plan *plan = nullptr;
  float s = 0;                          // posição do carro nela (px)
  float reveal = 1e9f;                  // até onde a fita já acendeu (px)
  float time = 0;                       // segundos (animações)
  // Prévia GPS: rota do carro até a próxima placa
  bool gpsVisible = false;
  float gpsS0 = 0, gpsS1 = 0, gpsProgress = 0, gpsOpacity = 0;
  Vec2 gpsPin{0, 0};
  int light = -1;
  HudView hud;
};

class App {
 public:
  App();

  /** Linha válida da serial (já normalizada). */
  void onVehicleData(const VehicleData &data, uint32_t now);
  /** JSON sem "hud": o remetente está no ar mas o main.py ainda não publicou. */
  void onWaiting(uint32_t now);
  void onCommand(const PanelCommand &command);

  /** Um quadro: atualiza tudo e preenche view. */
  void update(uint32_t now, FrameView &view);

 private:
  struct MissionView {
    std::string all;
    int doneCount;
    std::string raw;
  };

  std::string currentQueue() const;
  MissionView missionView() const;
  Location currentLocation() const;
  void replan(bool reveal, const Location *from, uint32_t now);
  bool needsReplan() const;
  void consumePoint(uint32_t now);
  void applyServerRoute(const std::string &next, const std::string &raw, uint32_t now);
  void setSource(Source source, uint32_t now, bool byUser = false);
  void finishMission();
  float simSpeedPx(uint32_t now);
  const PlanEvent *nextEvent(EventType type, float maxPx) const;
  void arrivedAtPoint(char point, const char *role, uint32_t now);
  void updateLink(uint32_t now);
  void updateGps(uint32_t now, FrameView &view);
  void updateCamera(float dt, float wx, float wz, float dirX, float dirZ, FrameView &view);
  void updateHud(uint32_t now, float speedMs, FrameView &view);
  std::string signName(const PlanEvent &event) const;

  TrackNetwork network_;
  Localizer localizer_;
  Plan plan_;

  // Estado (main.js: state)
  Source source_ = Source::Sim;
  LinkStatus linkStatus_ = LinkStatus::Connecting;
  bool hasData_ = false;
  VehicleData liveData_;
  PlanMode planMode_ = PlanMode::Shortest;
  float pwmToMs_ = 0.004f;      // m/s por unidade de PWM quando a telemetria não traz velocidade
  bool pointConfirmed_ = false;
  float cruiseMs_ = 0.4f;
  float liveSpeed_ = 0;
  float s_ = 0;
  std::string planQueue_;
  int planConsumed_ = 0;
  bool headingReady_ = false;
  float heading_ = 0;
  bool missionDone_ = false;
  uint32_t missionDoneAt_ = 0;
  // Simulação
  float simSpeed_ = 0.4f;
  std::string simRoute_ = "AC";  // na prova vêm 2 dos 3 pontos: coleta no 1º, entrega no último
  int simDone_ = 0;
  uint32_t pointHoldUntil_ = 0;
  char arrivalPoint_ = 0;
  const char *arrivalRole_ = "Ponto";
  uint32_t stopHoldUntil_ = 0;
  int stoppedAtKey_ = -1;
  // Serial
  std::string serverRoute_, serverRaw_, liveDone_;
  int localConsumed_ = 0;
  bool userPickedSource_ = false;
  uint32_t lastOkAt_ = 0, lastTsChangeAt_ = 0;
  bool everOk_ = false;
  double lastTs_ = NAN;
  int clockMinutes_ = -1;
  uint32_t clockAt_ = 0;
  // Pose exibida (suaviza os saltos das correções)
  float shownX_ = 0, shownY_ = 0;
  bool shownReady_ = false;
  uint32_t last_ = 0;
  bool first_ = true;
  // Fita: acende a partir do carro quando a rota muda
  uint32_t revealStart_ = 0;
  bool revealing_ = false;
  // Prévia GPS
  enum class GpsReason : uint8_t { None, Stop, Point };
  GpsReason gpsReason_ = GpsReason::None;
  bool gpsActive_ = false;
  uint32_t gpsStartedAt_ = 0, gpsEndedAt_ = 0, gpsUntil_ = 0;
  uint32_t gpsPlanSerial_ = 0;
  PlanEvent gpsTarget_{EventType::Point, 0};
  bool gpsTargetIsStart_ = false;
  float gpsS0_ = 0, gpsS1_ = 0;
  Vec2 gpsStart_{0, 0}, gpsEnd_{0, 0};
  // Câmera
  bool camReady_ = false;
  Vec3 camPos_{0, 0, 0}, camLook_{0, 0, 0};
  // Alertas e HUD
  bool hazard_ = false;
  uint32_t hudAt_ = 0;
  int lastManeuverKey_ = -1;
  HudView hud_;
};
