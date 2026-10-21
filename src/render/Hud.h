#pragma once
// HUD sobre a cena (porte visual de index.html + styles.css + hud.js), em 480x320:
//
//  ┌───────────────────────────────────────────────────────────┐
//  │ 14:32              0,42 M/S              (●●●) (● Ao vivo) │  barra de cima
//  │ ┌──────────────┐        ( ! Pedestre detectado )           │
//  │ │ ↱  1,2 m      │                                           │
//  │ │ Curva suave… │                                           │
//  │ │──────────────│                                           │
//  │ │ MISSÃO       │                                           │
//  │ │ [A] Coleta   │              ┌──────────────────────────┐ │
//  │ │ [C] Entrega  │              │ prévia GPS (quando para) │ │
//  │ └──────────────┘              └──────────────────────────┘ │
//  │                     PWM 120 · SERVO 90° · BATERIA 84% · MODO │
//  └───────────────────────────────────────────────────────────┘

#include "Canvas.h"
#include "Scene.h"
#include "../app/App.h"

class Hud {
 public:
  void draw(Canvas &c, const FrameView &view, uint32_t nowMs);
  /** Painéis opacos deste quadro (para a cena pular o chão atrás deles). Devolve quantos. */
  int occluders(const FrameView &view, Occluder *out) const;
  /** Mostra o fps no canto (útil para ajustar o SPI). */
  int fps = -1;

 private:
  void topBar(Canvas &c, const HudView &h);
  int guidePanel(Canvas &c, const HudView &h, uint32_t nowMs);
  void metrics(Canvas &c, const HudView &h);
  void gpsCard(Canvas &c, const HudView &h);
  void hazard(Canvas &c, const HudView &h);
  void cargo(Canvas &c, const HudView &h, uint32_t nowMs);
};
