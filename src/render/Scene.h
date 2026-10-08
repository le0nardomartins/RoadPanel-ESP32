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
  struct Projected {
    float x, y, depth;
    bool ok;
  };
  struct SignDraw {
    const Sprite *sprite;
    float cx, top, height, squeeze, depth, fog;
    float postX, postTop, postBottom, postWidth;
    Rgb tint;
  };
  struct Face {
    float xy[8];
    Rgb color;
    int lampCount;
    float lamps[2][8];
    Rgb lampColor;
  };

  Projected project(float X, float Y, float Z) const;   // metros
  void buildStrips(std::vector<Strip> &out, const Polyline &line, float s0, float s1, float halfWidth);
  void prepareCar(const FrameView &view);
  void prepareSigns(const FrameView &view);
  void shadeGround(float wx, float wy, float aa, bool carShadow, float &r, float &g, float &b) const;
  void drawStrips(Canvas &c, const std::vector<Strip> &strips, bool gps);

  // Câmera (metros)
  float px_, py_, pz_;          // posição
  float fx_, fy_, fz_;          // frente
  float rx_, rz_;               // direita (horizontal)
  float ux_, uy_, uz_;          // cima
  float fpx_;                   // distância focal em pixels
  float horizonY_;
  Row rows_[SCREEN_H];
  const FrameView *view_ = nullptr;
  std::vector<Strip> ribbon_, gps_;
  float ribbonHalf_ = 0, gpsHalf_ = 0;
  std::vector<SignDraw> signs_;
  std::vector<Face> carFaces_;
  std::vector<float> carEdges_;   // pares de pontos (x0,y0,x1,y1) das arestas visíveis
  int shadowY0_ = 0, shadowY1_ = -1;
  Projected pin_{0, 0, 0, false};
  Occluder occluders_[MAX_OCCLUDERS];
  int occluderCount_ = 0;
};
