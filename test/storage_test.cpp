#include "storage.h"
#include <cassert>
#include <iostream>

using namespace pascal;
constexpr const char *configJson =
    R"({"enabled":true,"day_rh_on":40,"day_rh_off":50,"night_rh_on":60,"night_rh_off":70,"max_temperature":32,"day_start":480,"day_end":1200,"lamp_on":540,"lamp_off":1140,"fan_day_on_minutes":10,"fan_day_off_minutes":40,"fan_night_on_minutes":5,"fan_night_off_minutes":55,"min_off_seconds":10,"humidifier_max_seconds":30})";
Controller configured() {
  JsonDocument d;
  assert(!deserializeJson(d, configJson));
  Config cfg;
  assert(readConfig(d.as<JsonVariantConst>(), cfg));
  Controller c;
  c.begin(0);
  assert(c.apply(cfg, 0));
  c.updateSchedule(true, 600);
  c.overrides[Lamp].mode = ModeOff;
  c.overrides[Fan].mode = ModeOn;
  return c;
}
void recover(Controller &c, double t, double rh) {
  c.sample(t, rh, 2500);
  c.sample(t, rh, 5000);
  c.sample(t, rh, 10000);
  c.tick(10000, false, 0, 0);
}
int main() {
  Storage store;
  assert(store.begin() && !store.hasState());
  auto c = configured();
  assert(store.save(c, 7));
  {
    Storage afterPowerLoss;
    Controller restored;
    restored.begin(0);
    uint32_t rev = 0;
    assert(afterPowerLoss.load(restored, rev) && rev == 7);
    restored.tick(0, false, 0, 0);
    for (bool output : restored.outputs)
      assert(!output);
    recover(restored, 23, 35);
    assert(restored.isDay && !restored.outputs[Lamp] && restored.outputs[Fan]);
    assert(restored.outputs[Humidifier]);
    assert(restored.overrides[Lamp].mode == ModeOff && restored.overrides[Fan].mode == ModeOn &&
           restored.overrides[Humidifier].mode == ModeAuto);
    assert(restored.config.fan_night_on_minutes == 5);
    // New readings win over the state before shutdown.
    restored.sample(27, 55, 12500);
    restored.tick(12500, false, 0, 0);
    assert(!restored.outputs[Humidifier]);
  }
  {
    auto changed = c;
    changed.overrides[Lamp].mode = ModeOn;
    changed.config.day_rh_on = 39;
    Preferences::failWrite = true;
    assert(!store.save(changed, 8));
    Preferences::failWrite = false;
    Controller restored;
    uint32_t rev = 0;
    assert(store.load(restored, rev) && rev == 7);
    assert(restored.overrides[Lamp].mode == ModeOff && restored.config.day_rh_on == 40);
  }
  {
    // Saved automatic lamp state is used while the clock is unknown.
    c.overrides[Lamp].mode = ModeAuto;
    c.overrides[Fan].mode = ModeAuto;
    c.updateSchedule(true, 1300);
    assert(store.save(c, 8));
    Controller restored;
    uint32_t rev = 0;
    assert(store.load(restored, rev) && rev == 8);
    recover(restored, 23, 65);
    assert(!restored.isDay && !restored.outputs[Lamp] && restored.outputs[Fan]);
    restored.tick(10000, true, 600, 1800000000);
    assert(restored.isDay && restored.outputs[Lamp]);
  }
  {
    // Corrupt current records are rejected.
    const auto good = Preferences::values["state_v4"];
    Controller restored;
    uint32_t rev = 0;
    for (const char *bad : {"{", "{}", "{\"schema\":3}"}) {
      Preferences::values["state_v4"] = bad;
      assert(!store.load(restored, rev) && rev == 0);
    }
    JsonDocument invalid;
    assert(!deserializeJson(invalid, good));
    invalid["modes"][2] = 9;
    String encoded;
    serializeJson(invalid, encoded);
    Preferences::values["state_v4"] = encoded;
    assert(!store.load(restored, rev));
    invalid["modes"][2] = -1;
    invalid["modes"].as<JsonArray>().remove(2);
    encoded.clear();
    serializeJson(invalid, encoded);
    Preferences::values["state_v4"] = encoded;
    assert(!store.load(restored, rev));
    Preferences::values.erase("state_v4");
  }
  {
    // Layout v3 state migrates once into state_v4, keeping revision, lamp mode and schedule.
    Preferences::values.clear();
    Storage legacyStore;
    assert(legacyStore.begin() && !legacyStore.hasState());
    Preferences::values["state_v3"] =
        R"({"schema":3,"revision":10,"config":{"enabled":true,"day_temp_on":22,"day_temp_off":29,"night_temp_on":18,"night_temp_off":21,"day_rh_on":35,"day_rh_off":50,"night_rh_on":50,"night_rh_off":70,"max_temperature":30,"day_start":480,"day_end":1261,"light1_on":540,"light1_off":1260,"light2_on":540,"light2_off":1260,"min_off_seconds":1,"heater_max_seconds":3600,"humidifier_max_seconds":3600},"schedule":{"known":true,"day":false,"lights":[true,false]},"modes":[1,0]})";
    assert(legacyStore.hasState());
    Controller migrated;
    migrated.begin(0);
    uint32_t rev = 0;
    assert(legacyStore.load(migrated, rev) && rev == 10);
    assert(Preferences::values.count("state_v4"));
    assert(migrated.config.lamp_on == 540 && migrated.config.lamp_off == 1260);
    assert(migrated.config.fan_day_on_minutes == 10 && migrated.config.fan_day_off_minutes == 40);
    assert(migrated.config.max_temperature == 30 && migrated.config.enabled);
    assert(migrated.overrides[Lamp].mode == ModeOn && migrated.overrides[Fan].mode == ModeAuto &&
           migrated.overrides[Humidifier].mode == ModeAuto);
    assert(migrated.scheduleState.known && !migrated.scheduleState.day &&
           migrated.scheduleState.lamp);
    Controller again;
    assert(legacyStore.load(again, rev) && rev == 10 && again.overrides[Lamp].mode == ModeOn);
    // A failed migration write leaves the old record and reports failure.
    Preferences::values.erase("state_v4");
    Preferences::failWrite = true;
    Controller failed;
    assert(!legacyStore.load(failed, rev));
    Preferences::failWrite = false;
    assert(Preferences::values.count("state_v3") && !Preferences::values.count("state_v4"));
  }
  {
    // Pre-v3 "settings" record migrates with automatic modes and an unknown schedule.
    Preferences::values.clear();
    Storage oldest;
    Preferences::values["settings"] =
        R"({"revision":2,"config":{"enabled":false,"day_temp_on":24,"day_temp_off":26,"night_temp_on":18,"night_temp_off":20,"day_rh_on":40,"day_rh_off":50,"night_rh_on":60,"night_rh_off":70,"max_temperature":32,"day_start":480,"day_end":1200,"light1_on":480,"light1_off":1200,"light2_on":1200,"light2_off":480,"min_off_seconds":10,"heater_max_seconds":20,"humidifier_max_seconds":30}})";
    Controller migrated;
    uint32_t rev = 0;
    assert(oldest.load(migrated, rev) && rev == 2);
    assert(!migrated.scheduleState.known && migrated.overrides[Lamp].mode == ModeAuto);
    migrated.config.enabled = true;
    recover(migrated, 23, 35);
    for (bool output : migrated.outputs)
      assert(!output);
  }
  std::cout << "Storage: power-loss recovery, persistent modes, offline auto state, sensor "
               "interlocks, failed writes, corrupt records, and v3/legacy migration passed\n";
}
