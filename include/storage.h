#pragma once
#include "config_json.h"
#include <Preferences.h>
class Storage {
public:
  Preferences prefs;
  bool begin() { return prefs.begin("pascal", false); }
  bool hasState() {
    return prefs.isKey("state_v4") || prefs.isKey("state_v3") || prefs.isKey("settings");
  }
  bool load(pascal::Controller &controller, uint32_t &revision) {
    if (prefs.isKey("state_v4"))
      return loadCurrent(controller, revision);
    // Older layouts: migrate once, then commit in the current format.
    auto restored = controller;
    uint32_t savedRevision = 0;
    if (!loadLegacy(restored, savedRevision) || !save(restored, savedRevision))
      return false;
    controller = restored;
    revision = savedRevision;
    return true;
  }
  bool save(const pascal::Controller &controller, uint32_t revision) {
    JsonDocument d;
    d["schema"] = 4;
    d["revision"] = revision;
    writeConfig(d["config"].to<JsonObject>(), controller.config);
    d["schedule"]["known"] = controller.scheduleState.known;
    d["schedule"]["day"] = controller.scheduleState.day;
    d["schedule"]["lamp"] = controller.scheduleState.lamp;
    for (int i = 0; i < pascal::outputCount; i++)
      d["modes"][i] = controller.overrides[i].mode;
    String s;
    serializeJson(d, s);
    // One NVS value keeps configuration, revision, modes and schedule consistent.
    return prefs.putString("state_v4", s) == s.length();
  }

private:
  bool loadCurrent(pascal::Controller &controller, uint32_t &revision) {
    JsonDocument d;
    pascal::Config config;
    if (deserializeJson(d, prefs.getString("state_v4", "")) || d["schema"] != 4 ||
        !d["revision"].is<uint32_t>() || !readConfig(d["config"], config) ||
        !d["schedule"]["known"].is<bool>() || !d["schedule"]["day"].is<bool>() ||
        !d["schedule"]["lamp"].is<bool>() || !d["modes"].is<JsonArray>() ||
        d["modes"].size() != pascal::outputCount)
      return false;
    auto restored = controller;
    restored.apply(config, 0);
    for (int i = 0; i < pascal::outputCount; i++) {
      if (!d["modes"][i].is<int>())
        return false;
      const int mode = d["modes"][i];
      if (mode < pascal::ModeAuto || mode > pascal::ModeOn)
        return false;
      restored.overrides[i].mode = mode;
    }
    restored.scheduleState.known = d["schedule"]["known"];
    restored.scheduleState.day = d["schedule"]["day"];
    restored.scheduleState.lamp = d["schedule"]["lamp"];
    restored.isDay = restored.scheduleState.day;
    controller = restored;
    revision = d["revision"].as<uint32_t>();
    return true;
  }
  bool loadLegacy(pascal::Controller &restored, uint32_t &revision) {
    const bool v3 = prefs.isKey("state_v3");
    JsonDocument d;
    pascal::Config config;
    if (deserializeJson(d, prefs.getString(v3 ? "state_v3" : "settings", "")) ||
        !d["revision"].is<uint32_t>() || !readLegacyConfig(d["config"], config))
      return false;
    restored.apply(config, 0);
    if (v3) {
      if (d["schema"] != 3 || !d["schedule"]["known"].is<bool>() ||
          !d["schedule"]["day"].is<bool>() || !d["schedule"]["lights"][0].is<bool>() ||
          !d["modes"][0].is<int>())
        return false;
      const int lampMode = d["modes"][0];
      if (lampMode < pascal::ModeAuto || lampMode > pascal::ModeOn)
        return false;
      restored.overrides[pascal::Lamp].mode = lampMode;
      restored.scheduleState.known = d["schedule"]["known"];
      restored.scheduleState.day = d["schedule"]["day"];
      restored.scheduleState.lamp = d["schedule"]["lights"][0];
      restored.isDay = restored.scheduleState.day;
    }
    revision = d["revision"].as<uint32_t>();
    return true;
  }
};
