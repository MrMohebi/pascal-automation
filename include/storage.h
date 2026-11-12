#pragma once
#include "config_json.h"
#include <Preferences.h>
class Storage {
public:
  Preferences prefs;
  bool begin() { return prefs.begin("pascal", false); }
  bool load(pascal::Config &config, uint32_t &revision) {
    JsonDocument d;
    if (deserializeJson(d, prefs.getString("settings", "")) || !d["revision"].is<uint32_t>() ||
        !readConfig(d["config"], config))
      return false;
    revision = d["revision"].as<uint32_t>();
    return true;
  }
  bool save(const pascal::Config &config, uint32_t revision) {
    JsonDocument d;
    d["revision"] = revision;
    writeConfig(d["config"].to<JsonObject>(), config);
    String s;
    serializeJson(d, s);
    return prefs.putString("settings", s) == s.length();
  }
};
