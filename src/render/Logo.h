#pragma once
// Logo do time (assets/logo.png, em base64 em generated/logo.h). A máscara é decodificada uma vez,
// antes do setup() (construtor estático), e depois só é pintada: nenhum núcleo decodifica no meio
// do desenho das faixas.

#include "Canvas.h"

namespace logo {

/** Largura e altura da logo na tela (px). */
int width();
int height();
/** Pinta a logo com o canto superior esquerdo em (x, y), na cor c. */
void draw(Canvas &c, int x, int y, Rgb color, int alpha = 255);

}  // namespace logo
