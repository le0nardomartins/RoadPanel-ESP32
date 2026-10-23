#pragma once
// Ajustes de hardware. Pinos iguais aos do display_tft.ino (ESP32 DevKit + ST7796S 480x320 SPI).

// ── Velocidade digital (ajuste aqui) ──
// Coeficiente da velocidade do carro 3D no mapa em relação à velocidade real (telemetry.speed, ou a
// estimada pelo PWM). O número em M/S no topo da tela continua mostrando a velocidade real.
//   1.0  = o carro 3D anda igual ao real (2 m/s no carro -> 2 m/s no mapa)
//   0.5  = metade                         (2 m/s no carro -> 1 m/s no mapa)
//   0.25 = um quarto                      (2 m/s no carro -> 0,5 m/s no mapa)
// Para calibrar: cronometre uma volta do carro real e compare com a volta no mapa; se o carrinho
// chega antes, diminua o valor na mesma proporção (ex.: chegou na metade do tempo -> divida por 2).
#define SPEED_SCALE 0.35f

// ── Display ST7796S (SPI) ──
#define TFT_SCLK 18
#define TFT_MOSI 23
#define TFT_MISO 19
#define TFT_DC 2
#define TFT_CS 5
#define TFT_RST 4
#define TFT_BL -1            // backlight ligado direto no 3V3; troque pelo pino se for controlado

// Clock do SPI. 40 MHz funciona em quase todo ST7796S; com fios curtos tente 80 MHz (dobra o fps
// de envio). Se aparecer lixo/cores erradas, volte para 40 ou 27 MHz.
#define TFT_SPI_HZ 40000000
#define TFT_ROTATION 3       // paisagem (480 x 320) de cabeça para baixo (display invertido no carro); 1 = normal
#define TFT_INVERT false     // true deixava as cores invertidas (amarelo->azul, preto->branco)
#define TFT_BGR false        // vermelho e azul trocados? mude para true

// ── Serial com o carro ──
// Mesmo formato do /api/vehicle_info (um JSON por linha) ou o antigo "velocidade,servo".
#define SERIAL_BAUD 115200
#define SERIAL_LINE_MAX 3072

// ── Desempenho ──
#define RENDER_DUAL_CORE 1   // desenha faixas pares no núcleo 1 e ímpares no núcleo 0
#define SHOW_FPS 0           // 1 = mostra o fps no canto da tela
