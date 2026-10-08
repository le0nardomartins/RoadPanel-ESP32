#pragma once
// Ajustes de hardware. Pinos iguais aos do display_tft.ino (ESP32 DevKit + ST7796S 480x320 SPI).

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
#define TFT_ROTATION 1       // paisagem (480 x 320)
#define TFT_INVERT true      // igual ao invertDisplay(true) da versão anterior
#define TFT_BGR false        // vermelho e azul trocados? mude para true

// ── Serial com o carro ──
// Mesmo formato do /api/vehicle_info (um JSON por linha) ou o antigo "velocidade,servo".
#define SERIAL_BAUD 115200
#define SERIAL_LINE_MAX 3072

// ── Botão ──
// BOOT (GPIO0) troca a vista: perseguição <-> topo. -1 desliga.
#define VIEW_BUTTON_PIN 0

// ── Desempenho ──
#define RENDER_DUAL_CORE 1   // desenha faixas pares no núcleo 1 e ímpares no núcleo 0
#define SHOW_FPS 0           // 1 = mostra o fps no canto da tela
