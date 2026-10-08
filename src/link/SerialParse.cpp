// Leitura de uma linha da serial. Porte de carLink.js: normalize() — tolerante a campos
// ausentes, tipos trocados e JSON quebrado (vira erro, nunca exceção).

#include <ArduinoJson.h>
#include <stdlib.h>
#include <string.h>

#include "VehicleData.h"
#include "../track/Planner.h"

namespace {

bool truthy(JsonVariantConst v) {
  if (v.isNull()) return false;
  if (v.is<bool>()) return v.as<bool>();
  if (v.is<double>()) return v.as<double>() != 0;
  if (v.is<const char *>()) return v.as<const char *>()[0] != 0;
  return true;   // objeto/lista
}

/** Number(value) do JS: número finito ou fallback. */
double num(JsonVariantConst v, double fallback) {
  if (v.is<double>()) {
    double d = v.as<double>();
    return isfinite(d) ? d : fallback;
  }
  if (v.is<bool>()) return v.as<bool>() ? 1 : 0;
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    char *end = nullptr;
    double d = strtod(s, &end);
    return (end != s && isfinite(d)) ? d : fallback;
  }
  return fallback;
}

/** a ?? b do JS */
JsonVariantConst either(JsonVariantConst a, JsonVariantConst b) { return a.isNull() ? b : a; }

/** Formato antigo do painel TFT: "velocidade_m_s,servo_graus". */
bool parseLegacy(const char *line, VehicleData &data) {
  char *end = nullptr;
  double speed = strtod(line, &end);
  if (end == line || *end != ',') return false;
  const char *rest = end + 1;
  double servo = strtod(rest, &end);
  if (end == rest) return false;
  data = VehicleData{};
  data.speed = (float)speed;
  data.servo = (float)servo;
  data.running = speed > 0.005;
  return true;
}

}  // namespace

ParseResult parseSerialLine(const char *line, VehicleData &data, PanelCommand &command) {
  while (*line == ' ' || *line == '\t') line++;
  if (!*line) return ParseResult::None;
  if (*line != '{') return parseLegacy(line, data) ? ParseResult::Vehicle : ParseResult::Error;

  JsonDocument doc;
  if (deserializeJson(doc, line)) return ParseResult::Error;
  JsonObjectConst raw = doc.as<JsonObjectConst>();
  if (raw.isNull()) return ParseResult::Error;

  // Comandos do painel: {"panel": {"view": "top"|"chase", "source": "sim"|"serial", "plan": "car"|"shortest"}}
  JsonObjectConst panel = raw["panel"];
  if (!panel.isNull()) {
    command = PanelCommand{};
    const char *view = panel["view"] | "";
    const char *source = panel["source"] | "";
    const char *plan = panel["plan"] | "";
    if (!strcmp(view, "chase")) command.view = 0;
    if (!strcmp(view, "top")) command.view = 1;
    if (!strcmp(source, "sim")) command.source = 0;
    if (!strcmp(source, "serial") || !strcmp(source, "live")) command.source = 1;
    if (!strcmp(plan, "shortest")) command.planMode = 0;
    if (!strcmp(plan, "car")) command.planMode = 1;
    return ParseResult::Command;
  }

  JsonObjectConst hud = raw["hud"];
  if (hud.isNull()) return ParseResult::None;   // servidor no ar, main.py ainda não publicou
  JsonObjectConst telemetry = raw["telemetry"];
  JsonObjectConst cmd = raw["command"];

  VehicleData d;
  d.ts = num(raw["_ts"], NAN);
  // Aceita lista estruturada (route_list), senão o texto
  JsonArrayConst list = hud["route_list"];
  if (!list.isNull()) {
    for (JsonVariantConst item : list) {
      const char *p = item.as<const char *>();
      std::string one = parseRoute(p ? p : "");
      if (one.size() == 1) d.route += one;
    }
  } else {
    d.route = parseRoute(hud["route"] | "");
  }
  d.routeText = hud["route"].is<const char *>() ? hud["route"].as<const char *>() : "";
  d.running = truthy(either(cmd["run"], hud["running"]));
  d.pwm = (float)num(either(cmd["speed"], hud["speed"]), 0);
  d.servo = (float)num(either(cmd["servo"], hud["servo"]), 90);
  d.speed = (float)num(telemetry["speed"], 0);
  d.battery = (float)num(telemetry["battery"], NAN);
  d.proxLeft = (int)num(telemetry["left"], 0);
  d.proxFrontLeft = (int)num(telemetry["f_left"], 0);
  d.proxFrontRight = (int)num(telemetry["f_right"], 0);
  d.proxRight = (int)num(telemetry["right"], 0);
  d.trafficLight = (int)num(hud["traffic_light_code"], -1);
  d.stopActive = truthy(hud["stop_active"]);
  d.rightDetour = truthy(hud["right_detour_active"]);
  d.pedestrian = truthy(cmd["stop"]);
  d.controlMode = hud["control_mode"].is<const char *>() ? hud["control_mode"].as<const char *>() : "";
  // Hora opcional "HH:MM" (o ESP32 não tem relógio)
  const char *time = raw["time"] | "";
  int hh, mm;
  if (sscanf(time, "%d:%d", &hh, &mm) == 2 && hh >= 0 && hh < 24 && mm >= 0 && mm < 60) d.clockMinutes = hh * 60 + mm;
  data = std::move(d);
  return ParseResult::Vehicle;
}
