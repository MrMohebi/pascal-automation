#include "config_json.h"
#include <cassert>
#include <iostream>
int main() {
  JsonDocument d;
  assert(!deserializeJson(
      d,
      R"({"enabled":true,"day_temp_on":24,"day_temp_off":26,"night_temp_on":18,"night_temp_off":20,"day_rh_on":40,"day_rh_off":50,"night_rh_on":60,"night_rh_off":70,"max_temperature":32,"day_start":480,"day_end":1200,"light1_on":480,"light1_off":1200,"light2_on":1200,"light2_off":480,"min_off_seconds":60,"heater_max_seconds":600,"humidifier_max_seconds":120})"));
  pascal::Config c;
  assert(readConfig(d.as<JsonVariantConst>(), c));
  JsonDocument roundtrip;
  writeConfig(roundtrip.to<JsonObject>(), c);
  pascal::Config decoded;
  assert(readConfig(roundtrip.as<JsonVariantConst>(), decoded));
  assert(decoded.enabled && decoded.day_temp_on == 24 && decoded.light2_off == 480);
  d["day_start"] = 480.5;
  assert(!readConfig(d.as<JsonVariantConst>(), c));
  d["day_start"] = 480;
  d["day_temp_on"] = "24";
  assert(!readConfig(d.as<JsonVariantConst>(), c));
  d["day_temp_on"] = 24;
  d.remove("max_temperature");
  assert(!readConfig(d.as<JsonVariantConst>(), c));
  std::cout
      << "Firmware config JSON: round-trip, integer types, numeric types, required fields passed\n";
}
