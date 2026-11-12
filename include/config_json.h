#pragma once
#include "control.h"
#include <ArduinoJson.h>
#define CONFIG_FLOATS(X)                                                                           \
  X(day_temp_on)                                                                                   \
  X(day_temp_off) X(night_temp_on) X(night_temp_off) X(day_rh_on) X(day_rh_off) X(night_rh_on)     \
      X(night_rh_off) X(max_temperature)
#define CONFIG_INTS(X)                                                                             \
  X(day_start)                                                                                     \
  X(day_end) X(light1_on) X(light1_off) X(light2_on) X(light2_off) X(min_off_seconds)              \
      X(heater_max_seconds) X(humidifier_max_seconds)
inline void writeConfig(JsonObject o, const pascal::Config &c) {
  o["enabled"] = c.enabled;
#define WRITE_FIELD(f) o[#f] = c.f;
  CONFIG_FLOATS(WRITE_FIELD) CONFIG_INTS(WRITE_FIELD)
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
  CONFIG_FLOATS(READ_FLOAT)
  CONFIG_INTS(READ_INT)
#undef READ_FLOAT
#undef READ_INT
      return pascal::valid(c);
}
