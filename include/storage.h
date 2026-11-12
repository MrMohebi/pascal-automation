#pragma once
#include "config_json.h"
#include <Preferences.h>
class Storage {
public:
  Preferences prefs;
  bool begin() { return prefs.begin("pascal", false); }
  bool hasState() { return prefs.isKey("state_v3") || prefs.isKey("settings"); }
  bool load(pascal::Controller &controller, uint32_t &revision) {
    const bool current = prefs.isKey("state_v3");
    JsonDocument d;
    pascal::Config config;
    if (deserializeJson(d, prefs.getString(current ? "state_v3" : "settings", "")) ||
        !d["revision"].is<uint32_t>() || !readConfig(d["config"], config))
      return false;
    auto restored = controller;
    restored.apply(config, 0);
    if (current) {
      if (d["schema"] != 3 || !d["schedule"]["known"].is<bool>() ||
          !d["schedule"]["day"].is<bool>() || !d["schedule"]["lights"].is<JsonArray>() ||
          d["schedule"]["lights"].size() != 2 || !d["modes"].is<JsonArray>() ||
          d["modes"].size() != 2)
        return false;
      restored.scheduleState.known = d["schedule"]["known"];
      restored.scheduleState.day = d["schedule"]["day"];
      restored.isDay = restored.scheduleState.day;
      for (int i = 0; i < 2; i++) {
        if (!d["schedule"]["lights"][i].is<bool>() || !d["modes"][i].is<int>())
          return false;
        int mode = d["modes"][i];
        if (mode < -1 || mode > 1)
          return false;
        restored.overrides[i].mode = mode;
        restored.scheduleState.lights[i] = d["schedule"]["lights"][i];
      }
    }
    controller = restored;
    revision = d["revision"].as<uint32_t>();
    return true;
  }
  bool save(const pascal::Controller &controller, uint32_t revision) {
    JsonDocument d;
    d["schema"] = 3;
    d["revision"] = revision;
    writeConfig(d["config"].to<JsonObject>(), controller.config);
    d["schedule"]["known"] = controller.scheduleState.known;
    d["schedule"]["day"] = controller.scheduleState.day;
    for (int i = 0; i < 2; i++) {
      d["modes"][i] = controller.overrides[i].mode;
      d["schedule"]["lights"][i] = controller.scheduleState.lights[i];
    }
    String s;
    serializeJson(d, s);
    // One NVS value keeps configuration, revision, modes and schedule consistent.
    return prefs.putString("state_v3", s) == s.length();
  }
};
