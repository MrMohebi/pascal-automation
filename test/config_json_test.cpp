#include "config_json.h"
#include <cassert>
#include <iostream>
int main() {
  JsonDocument d;
  assert(!deserializeJson(
      d,
      R"({"enabled":true,"day_rh_on":40,"day_rh_off":50,"night_rh_on":60,"night_rh_off":70,"max_temperature":32,"day_start":480,"day_end":1200,"lamp_on":540,"lamp_off":1140,"fan_day_on_minutes":10,"fan_day_off_minutes":40,"fan_night_on_minutes":5,"fan_night_off_minutes":55,"min_off_seconds":60,"humidifier_max_seconds":120})"));
  pascal::Config c;
  assert(readConfig(d.as<JsonVariantConst>(), c));
  JsonDocument roundtrip;
  writeConfig(roundtrip.to<JsonObject>(), c);
  pascal::Config decoded;
  assert(readConfig(roundtrip.as<JsonVariantConst>(), decoded));
  assert(decoded.enabled && decoded.lamp_off == 1140 && decoded.fan_night_off_minutes == 55);
  // Day/night switches default to enabled when absent (settings saved before they existed).
  assert(decoded.fan_day_enabled && decoded.fan_night_enabled && decoded.humidifier_day_enabled &&
         decoded.humidifier_night_enabled);
  d["fan_night_enabled"] = false;
  d["humidifier_day_enabled"] = false;
  assert(readConfig(d.as<JsonVariantConst>(), c));
  assert(!c.fan_night_enabled && !c.humidifier_day_enabled && c.fan_day_enabled);
  JsonDocument written;
  writeConfig(written.to<JsonObject>(), c);
  assert(written["fan_night_enabled"] == false && written["humidifier_night_enabled"] == true);
  d["fan_night_enabled"] = 0;
  assert(!readConfig(d.as<JsonVariantConst>(), c));
  d.remove("fan_night_enabled");
  d.remove("humidifier_day_enabled");
  d["day_start"] = 480.5;
  assert(!readConfig(d.as<JsonVariantConst>(), c));
  d["day_start"] = 480;
  d["day_rh_on"] = "40";
  assert(!readConfig(d.as<JsonVariantConst>(), c));
  d["day_rh_on"] = 40;
  d.remove("fan_day_on_minutes");
  assert(!readConfig(d.as<JsonVariantConst>(), c));
  d["fan_day_on_minutes"] = 0;
  d["fan_day_off_minutes"] = 0;
  assert(!readConfig(d.as<JsonVariantConst>(), c));
  // Layout v3 settings migrate: light 1 becomes the lamp, the fan gets a 10/40 cycle.
  JsonDocument legacy;
  assert(!deserializeJson(
      legacy,
      R"({"enabled":true,"day_temp_on":24,"day_temp_off":26,"night_temp_on":18,"night_temp_off":20,"day_rh_on":40,"day_rh_off":50,"night_rh_on":60,"night_rh_off":70,"max_temperature":32,"day_start":480,"day_end":1200,"light1_on":540,"light1_off":1260,"light2_on":1200,"light2_off":480,"min_off_seconds":60,"heater_max_seconds":600,"humidifier_max_seconds":120})"));
  assert(!readConfig(legacy.as<JsonVariantConst>(), c));
  assert(readLegacyConfig(legacy.as<JsonVariantConst>(), c));
  assert(c.lamp_on == 540 && c.lamp_off == 1260 && c.fan_day_on_minutes == 10 &&
         c.fan_day_off_minutes == 40 && c.fan_night_on_minutes == 10 &&
         c.fan_night_off_minutes == 40 && c.max_temperature == 32 && c.enabled);
  std::cout << "Firmware config JSON: round-trip, integer types, numeric types, required fields, "
               "fan cycle validation, and v3 migration passed\n";
}
