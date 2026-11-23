#pragma once
#include "control.h"
#include <ArduinoJson.h>
#define CONFIG_FLOATS(X)                                                                           \
  X(day_rh_on) X(day_rh_off) X(night_rh_on) X(night_rh_off) X(max_temperature)
#define CONFIG_INTS(X)                                                                             \
  X(day_start)                                                                                     \
  X(day_end) X(lamp_on) X(lamp_off) X(fan_day_on_minutes) X(fan_day_off_minutes)                   \
      X(fan_night_on_minutes) X(fan_night_off_minutes) X(min_off_seconds)                          \
          X(humidifier_max_seconds)
// Optional on read (default true) so settings saved before they existed still load.
#define CONFIG_PERIOD_BOOLS(X)                                                                     \
  X(fan_day_enabled) X(fan_night_enabled) X(humidifier_day_enabled) X(humidifier_night_enabled)
inline void writeConfig(JsonObject o, const pascal::Config &c) {
  o["enabled"] = c.enabled;
#define WRITE_FIELD(f) o[#f] = c.f;
  CONFIG_FLOATS(WRITE_FIELD) CONFIG_INTS(WRITE_FIELD) CONFIG_PERIOD_BOOLS(WRITE_FIELD)
#undef WRITE_FIELD
}
inline bool readConfig(JsonVariantConst o, pascal::Config &c) {
  if (!o.is<JsonObjectConst>() || !o["enabled"].is<bool>())
    return false;
  c.enabled = o["enabled"].as<bool>();
#define READ_FLOAT(f)                                                                              \
  if (!o[#f].is<double>())                                                                         \
    return false;                                                                                  \
  c.f = o[#f].as<double>();
#define READ_INT(f)                                                                                \
  if (!o[#f].is<int>())                                                                            \
    return false;                                                                                  \
  c.f = o[#f].as<int>();
#define READ_PERIOD_BOOL(f)                                                                        \
  if (o[#f].isNull())                                                                              \
    c.f = true;                                                                                    \
  else if (o[#f].is<bool>())                                                                       \
    c.f = o[#f].as<bool>();                                                                        \
  else                                                                                             \
    return false;
  CONFIG_FLOATS(READ_FLOAT)
  CONFIG_INTS(READ_INT)
  CONFIG_PERIOD_BOOLS(READ_PERIOD_BOOL)
#undef READ_FLOAT
#undef READ_INT
#undef READ_PERIOD_BOOL
  return pascal::valid(c);
}
// Layout v3 settings (heater, humidifier, two lights). The heater thresholds are
// dropped, light 1 becomes the lamp, and the new fan starts on a 10/40 minute cycle.
inline bool readLegacyConfig(JsonVariantConst o, pascal::Config &c) {
  if (!o.is<JsonObjectConst>() || !o["enabled"].is<bool>())
    return false;
  for (const char *f : {"day_rh_on", "day_rh_off", "night_rh_on", "night_rh_off",
                        "max_temperature"})
    if (!o[f].is<double>())
      return false;
  for (const char *f :
       {"day_start", "day_end", "light1_on", "light1_off", "min_off_seconds",
        "humidifier_max_seconds"})
    if (!o[f].is<int>())
      return false;
  c.enabled = o["enabled"].as<bool>();
  c.day_rh_on = o["day_rh_on"].as<double>();
  c.day_rh_off = o["day_rh_off"].as<double>();
  c.night_rh_on = o["night_rh_on"].as<double>();
  c.night_rh_off = o["night_rh_off"].as<double>();
  c.max_temperature = o["max_temperature"].as<double>();
  c.day_start = o["day_start"].as<int>();
  c.day_end = o["day_end"].as<int>();
  c.lamp_on = o["light1_on"].as<int>();
  c.lamp_off = o["light1_off"].as<int>();
  c.fan_day_on_minutes = c.fan_night_on_minutes = 10;
  c.fan_day_off_minutes = c.fan_night_off_minutes = 40;
  c.min_off_seconds = o["min_off_seconds"].as<int>();
  c.humidifier_max_seconds = o["humidifier_max_seconds"].as<int>();
  return pascal::valid(c);
}
