// RoadPanel no ESP32 + TFT ST7796S 480x320.
//
// Porte do painel JS (RoadPanel/) para C++: a lógica (rede da pista, trajetória, localizador,
// missão) é a mesma do JS, e a cena 3D + HUD são desenhados em software, em faixas de
// 480x32, enviadas por DMA. Com RENDER_DUAL_CORE, os dois núcleos desenham faixas em paralelo.
//
// Dados: uma linha por mensagem na serial (115200), no formato do /api/vehicle_info:
//   {"hud":{"route":"A → C","traffic_light_code":-1,"stop_active":false,"right_detour_active":false,
//    "control_mode":"AUTONOMO"},"command":{"run":true,"speed":120,"servo":90,"stop":false},
//    "telemetry":{"speed":0.42,"battery":84,"f_left":0,"f_right":0},"_ts":1712345678.1,"time":"14:32"}
// ou o formato antigo "0.42,90" (velocidade m/s, servo). Sem dados, o painel fica em simulação.

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <esp_heap_caps.h>

#include "Config.h"
#include "app/App.h"
#include "link/VehicleData.h"
#include "render/Hud.h"
#include "render/Scene.h"
#include "generated/fonts.h"

// ── Display (LovyanGFX) ──────────────────────────────────────────────────
class Lcd : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796 panel_;
  lgfx::Bus_SPI bus_;

 public:
  Lcd() {
    auto bus = bus_.config();
    bus.spi_host = VSPI_HOST;
    bus.spi_mode = 0;
    bus.freq_write = TFT_SPI_HZ;
    bus.freq_read = 16000000;
    bus.spi_3wire = false;
    bus.use_lock = true;
    bus.dma_channel = SPI_DMA_CH_AUTO;
    bus.pin_sclk = TFT_SCLK;
    bus.pin_mosi = TFT_MOSI;
    bus.pin_miso = TFT_MISO;
    bus.pin_dc = TFT_DC;
    bus_.config(bus);
    panel_.setBus(&bus_);

    auto cfg = panel_.config();
    cfg.pin_cs = TFT_CS;
    cfg.pin_rst = TFT_RST;
    cfg.pin_busy = -1;
    cfg.panel_width = 320;
    cfg.panel_height = 480;
    cfg.offset_rotation = 0;
    cfg.readable = true;
    cfg.invert = TFT_INVERT;
    cfg.rgb_order = TFT_BGR;
    cfg.dlen_16bit = false;
    cfg.bus_shared = false;
    panel_.config(cfg);
    setPanel(&panel_);
  }
};

static Lcd lcd;
static App app;
static Scene scene;
static Hud hud;
static FrameView view;

// Faixas: 2 buffers por núcleo (um sendo desenhado, outro indo por DMA)
static uint16_t *buffers[2][2];

// ── Desenho em dois núcleos ──────────────────────────────────────────────
#if RENDER_DUAL_CORE
static TaskHandle_t mainTask = nullptr, workerTask = nullptr;
static volatile int frameId = 0;        // quadro que o ajudante deve desenhar
static volatile int pushedBand = -1;    // última faixa enviada ao display neste quadro
static volatile int workerBand = -1;    // última faixa ímpar pronta
static volatile uint32_t frameNow = 0;

static void renderBand(int band, uint16_t *buf) {
  Canvas c;
  c.begin(buf, band * BAND_H);
  scene.drawBand(c);
  hud.draw(c, view, frameNow);
}

/** Núcleo 0: desenha as faixas ímpares do quadro atual. */
static void workerLoop(void *) {
  int done = 0;
  for (;;) {
    while (frameId == done) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    done = frameId;
    for (int band = 1; band < BAND_COUNT; band += 2) {
      // o buffer é o da faixa band-4: só reaproveita depois que ela saiu por DMA
      // (o envio da faixa band-3 começa depois que a band-4 terminou)
      while (pushedBand < band - 3) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));
      renderBand(band, buffers[1][(band / 2) % 2]);
      workerBand = band;
      xTaskNotifyGive(mainTask);
    }
  }
}
#endif

static void drawFrame(uint32_t now) {
  app.update(now, view);
  scene.prepare(view);
  Occluder occ[MAX_OCCLUDERS];
  scene.setOccluders(occ, hud.occluders(view, occ));

  lcd.startWrite();
#if RENDER_DUAL_CORE
  frameNow = now;
  pushedBand = -1;
  workerBand = -1;
  frameId = frameId + 1;
  xTaskNotifyGive(workerTask);
  for (int band = 0; band < BAND_COUNT; band++) {
    uint16_t *buf;
    if (band % 2 == 0) {
      buf = buffers[0][(band / 2) % 2];
      Canvas c;
      c.begin(buf, band * BAND_H);
      scene.drawBand(c);
      hud.draw(c, view, now);
    } else {
      while (workerBand < band) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));
      buf = buffers[1][(band / 2) % 2];
    }
    // pushImageDMA espera o envio anterior terminar e dispara este
    lcd.pushImageDMA(0, band * BAND_H, SCREEN_W, BAND_H, (lgfx::swap565_t *)buf);
    pushedBand = band;
    xTaskNotifyGive(workerTask);
  }
#else
  for (int band = 0; band < BAND_COUNT; band++) {
    uint16_t *buf = buffers[0][band % 2];
    Canvas c;
    c.begin(buf, band * BAND_H);
    scene.drawBand(c);
    hud.draw(c, view, now);
    lcd.pushImageDMA(0, band * BAND_H, SCREEN_W, BAND_H, (lgfx::swap565_t *)buf);
  }
#endif
  lcd.waitDMA();
  lcd.endWrite();
}

// ── Abertura (como o loader do painel JS) ────────────────────────────────
static void splash(float progress, const char *text) {
  static const Rgb BG = hex(0xf3f5f9), INK = hex(0x0e1726), INK3 = hex(0x5b6a82), ROUTE = hex(0x1f5fd8);
  lcd.startWrite();
  for (int band = 0; band < BAND_COUNT; band++) {
    uint16_t *buf = buffers[0][band % 2];
    Canvas c;
    c.begin(buf, band * BAND_H);
    c.fillRect(0, band * BAND_H, SCREEN_W, BAND_H, BG);
    int w = Canvas::textWidth(fonts::distance, "APEX", 4);
    c.text(fonts::distance, (SCREEN_W - w) / 2, 130, "APEX", INK, 255, 4);
    w = Canvas::textWidth(fonts::title, "Mapa da pista");
    c.text(fonts::title, (SCREEN_W - w) / 2, 156, "Mapa da pista", INK);
    c.roundRect(140, 180, 200, 6, 3, hex(0xd9dee6));
    c.roundRect(140, 180, 200 * progress, 6, 3, ROUTE);
    w = Canvas::textWidth(fonts::body, text);
    c.text(fonts::body, (SCREEN_W - w) / 2, 210, text, INK3);
    lcd.pushImageDMA(0, band * BAND_H, SCREEN_W, BAND_H, (lgfx::swap565_t *)buf);
  }
  lcd.waitDMA();
  lcd.endWrite();
}

// ── Serial ───────────────────────────────────────────────────────────────
static char line[SERIAL_LINE_MAX];
static size_t lineLen = 0;
static bool lineOverflow = false;

static void readSerial(uint32_t now) {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch != '\n') {
      if (lineLen + 1 < sizeof line) line[lineLen++] = ch;
      else lineOverflow = true;
      continue;
    }
    line[lineLen] = 0;
    if (!lineOverflow) {
      VehicleData data;
      PanelCommand command;
      switch (parseSerialLine(line, data, command)) {
        case ParseResult::Vehicle: app.onVehicleData(data, now); break;
        case ParseResult::Command: app.onCommand(command); break;
        case ParseResult::None: if (lineLen) app.onWaiting(now); break;
        default: break;   // linha inválida: ignora (nunca trava o painel)
      }
    }
    lineLen = 0;
    lineOverflow = false;
  }
}

void setup() {
  Serial.setRxBufferSize(SERIAL_LINE_MAX * 2);
  Serial.begin(SERIAL_BAUD);
  for (auto &core : buffers)
    for (auto &buf : core) {
      buf = (uint16_t *)heap_caps_malloc(SCREEN_W * BAND_H * 2, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
      if (!buf) {
        Serial.println("Sem memória para as faixas da tela");
        for (;;) delay(1000);
      }
    }
  lcd.init();
  lcd.setRotation(TFT_ROTATION);
#if TFT_BL >= 0
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif
  lcd.initDMA();
  splash(0.2f, "Montando a pista");
#if RENDER_DUAL_CORE
  mainTask = xTaskGetCurrentTaskHandle();
  xTaskCreatePinnedToCore(workerLoop, "faixas", 6144, nullptr, 2, &workerTask, 0);
#endif
  // primeira trajetória e câmera (o App monta tudo no primeiro update)
  app.update(millis(), view);
  splash(1.0f, "Pronto");
  hud.fps = SHOW_FPS ? 0 : -1;
  Serial.printf("RoadPanel pronto. Memória livre: %u bytes\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
  uint32_t now = millis();
  readSerial(now);
  drawFrame(now);

  // fps (média de 1 s)
  static uint32_t fpsAt = 0;
  static int frames = 0;
  frames++;
  if (now - fpsAt >= 1000) {
    if (SHOW_FPS) hud.fps = frames;
    fpsAt = now;
    frames = 0;
  }
}
