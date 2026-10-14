#pragma once
// Estado do carro já normalizado (porte de carLink.js: normalize()).
// Vem da serial, em JSON com o mesmo formato de /api/vehicle_info, ou no formato antigo "speed,steering".

#include <math.h>
#include <stdint.h>

#include <string>

struct VehicleData {
  double ts = NAN;            // _ts do servidor (NAN se não veio)
  std::string route;          // pontos válidos, ex.: "AC"
  std::string routeText;      // texto recebido, ex.: "A → C"
  bool running = false;
  float pwm = 0;
  float servo = 90;
  float speed = 0;            // m/s (média das rodas)
  float battery = NAN;        // %
  // Ultrassônicos: 0 livre, 1 longe (~50 cm), 2 perto (~30 cm), 3 crítico (~8 cm)
  int proxLeft = 0, proxFrontLeft = 0, proxFrontRight = 0, proxRight = 0;
  int trafficLight = -1;      // 0 vermelho, 1 amarelo, 2 verde, -1 nenhum
  bool stopActive = false;
  bool rightDetour = false;
  bool pedestrian = false;    // command.stop (a IA viu uma pessoa)
  std::string controlMode;
  int clockMinutes = -1;      // hora do dia em minutos, se o remetente mandar "time": "HH:MM"
};

// Comandos opcionais que a serial também aceita (não fazem parte do vehicle_info)
struct PanelCommand {
  int source = -1;            // 0 simulação, 1 serial
  int planMode = -1;          // 0 caminho livre, 1 tabela do carro
  bool hasSimRoute = false;   // missão da simulação, ex.: "BA" (coleta em B, entrega em A)
  std::string simRoute;
};

enum class ParseResult : uint8_t { None, Vehicle, Command, Error };

/** Uma linha da serial -> dados do carro ou comando do painel. Não aloca a não ser nas strings. */
ParseResult parseSerialLine(const char *line, VehicleData &data, PanelCommand &command);
