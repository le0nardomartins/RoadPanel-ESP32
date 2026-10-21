#pragma once
// Cena 3D do painel desenhada em software, faixa a faixa (porte visual de trackMeshes.js,
// routeRibbon.js, gpsPreview.js, signs.js e car.js):
//
// - chão: para cada linha da tela, o raio da câmera cruza o chão numa reta; cada pixel
//   consulta o campo de distância da pista (track_sdf.h) e vira asfalto, ilha, campo com
//   malha de pontos, linha preta ou quadriculado, com antisserrilhado e névoa;
// - fita azul da missão e rota da prévia GPS: faixas no chão com o mesmo "shader" do JS;
// - placas (sprites com a face de signs.js) e o carro (caixas com luz, como o carro reserva do JS).

#include <vector>

#include "Canvas.h"
#include "../app/App.h"

/** Retângulo arredondado opaco do HUD: o chão embaixo dele não precisa ser calculado. */
struct Occluder {
  int x, y, w, h, r;
};
constexpr int MAX_OCCLUDERS = 4;

class Scene {
 public:
  void setOccluders(const Occluder *list, int count) {
    occluderCount_ = count < MAX_OCCLUDERS ? count : MAX_OCCLUDERS;
    for (int i = 0; i < occluderCount_; i++) occluders_[i] = list[i];
  }
  /** Uma vez por quadro, antes das faixas: câmera, linhas do chão, fita, placas e carro. */
  void prepare(const FrameView &view);
  /** Desenha a faixa atual (chão, fita, rota GPS, placas, carro). */
  void drawBand(Canvas &c);

  static constexpr Rgb FOG = hex(0xe4e9f1);

 private:
  struct Row {
    bool ground;
    float wx0, wy0, dwx, dwy;   // ponto do chão (px da pista) em x = 0 e passo por pixel
    float aa;                   // meia largura do pixel no chão (px da pista)
    float fog;                  // 0..1
  };
  struct Strip {               // trecho de faixa no chão (fita ou rota GPS)
    Vec2 p, dir, m0, m1;        // início, direção, bissetrizes nas pontas
    float len, s0;              // comprimento e posição ao longo da trajetória (px)
    int x0, y0, x1, y1;         // caixa na tela
  };
  /** Lista de faixas com memória fixa. Antes era um std::vector que crescia a cada quadro: na vista
   *  de cima a rota inteira fica visível, o new falhava no ESP32 sem PSRAM e a placa reiniciava. */
  static constexpr int MAX_STRIPS = 320;
  struct StripList {
    Strip items[MAX_STRIPS];
    int count = 0;
    const Strip *begin() const { return items; }
    const Strip *end() const { return items + count; }
  };
  struct Projected {
    float x, y, depth;
    bool ok;
  };
  struct SignDraw {
    const Sprite *sprite;
    float cx, cy;                 // centro da placa na tela
    float ax, ay, bx, by;         // afim: 1 px da imagem -> tela (eixo horizontal e vertical da placa)
    float depth, fog;
    float postX0, postY0, postX1, postY1, postWidth;   // haste: do chão ao topo, já em perspectiva
    Rgb tint;
  };
  /** Carro: imagem pré-renderizada do modelo 3D (car_sprites.h). */
  struct CarDraw {
    bool visible = false;
    bool affine = false;        // vista de cima: imagem girada/achatada
    const Sprite *sprite = nullptr;
    float cx = 0, cy = 0;       // centro do carro na tela
    float height = 0;           // altura na tela (imagem de perseguição)
    float ax = 0, ay = 0, bx = 0, by = 0;   // transformação afim (vista de cima)
    float depth = 0;
  };

  Projected project(float X, float Y, float Z) const;   // metros
  void buildStrips(StripList &out, const Polyline &line, float s0, float s1, float halfWidth, float step);
  void prepareCar(const FrameView &view);
  void prepareSigns(const FrameView &view);
  void shadeGround(float wx, float wy, float aa, bool carShadow, float &r, float &g, float &b) const;
  void drawStrips(Canvas &c, const StripList &strips, bool gps);
  /** Fumaça verde nas bordas (2 s ao começar a carregar), por cima da cena e por baixo do HUD. */
  void drawSmoke(Canvas &c) const;

  // Câmera (metros)
  float px_, py_, pz_;          // posição
  float fx_, fy_, fz_;          // frente
  float rx_, rz_;               // direita (horizontal)
  float ux_, uy_, uz_;          // cima
  float fpx_;                   // distância focal em pixels
  float horizonY_;
  Row rows_[SCREEN_H];
  const FrameView *view_ = nullptr;
  StripList ribbon_, gps_;
  float ribbonHalf_ = 0, gpsHalf_ = 0;
  std::vector<SignDraw> signs_;
  CarDraw car_;
  void drawCar(Canvas &c) const;
  int shadowY0_ = 0, shadowY1_ = -1;
  Projected pin_{0, 0, 0, false};
  Occluder occluders_[MAX_OCCLUDERS];
  int occluderCount_ = 0;
};
