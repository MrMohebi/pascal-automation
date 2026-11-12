#include "storage.h"
#include <cassert>
#include <iostream>

pascal::Controller configured() {
  JsonDocument d;
  assert(!deserializeJson(
      d,
      R"({"enabled":true,"day_temp_on":24,"day_temp_off":26,"night_temp_on":18,"night_temp_off":20,"day_rh_on":40,"day_rh_off":50,"night_rh_on":60,"night_rh_off":70,"max_temperature":32,"day_start":480,"day_end":1200,"light1_on":480,"light1_off":1200,"light2_on":1200,"light2_off":480,"min_off_seconds":10,"heater_max_seconds":20,"humidifier_max_seconds":30})"));
  pascal::Config cfg;
  assert(readConfig(d.as<JsonVariantConst>(), cfg));
  pascal::Controller c;
  c.begin(0);
  assert(c.apply(cfg, 0));
  c.updateSchedule(true, 600);
  c.overrides[0].mode = 0;
  c.overrides[1].mode = 1;
  return c;
}
void recover(pascal::Controller &c, double t, double rh) {
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
    pascal::Controller restored;
    restored.begin(0);
    uint32_t rev = 0;
    assert(afterPowerLoss.load(restored, rev) && rev == 7);
    restored.tick(0, false, 0, 0);
    for (bool output : restored.outputs)
      assert(!output);
    recover(restored, 23, 35);
    assert(restored.isDay && restored.outputs[0] && restored.outputs[1]);
    assert(!restored.outputs[2] && restored.outputs[3]);
    assert(restored.overrides[0].mode == 0 && restored.overrides[1].mode == 1);
    assert(restored.config.day_temp_on == 24);
    // New readings win over the state before shutdown.
    restored.sample(27, 55, 12500);
    restored.tick(12500, false, 0, 0);
    assert(!restored.outputs[0] && !restored.outputs[1]);
  }
  {
    auto changed = c;
    changed.overrides[0].mode = 1;
    changed.config.day_temp_on = 23;
    Preferences::failWrite = true;
    assert(!store.save(changed, 8));
    Preferences::failWrite = false;
    pascal::Controller restored;
    uint32_t rev = 0;
    assert(store.load(restored, rev) && rev == 7);
    assert(restored.overrides[0].mode == 0 && restored.config.day_temp_on == 24);
  }
  {
    c.overrides[0].mode = -1;
    c.overrides[1].mode = -1;
    c.updateSchedule(true, 1300);
    assert(store.save(c, 8));
    pascal::Controller restored;
    uint32_t rev = 0;
    assert(store.load(restored, rev) && rev == 8);
    recover(restored, 23, 35);
    assert(!restored.isDay && !restored.outputs[0]);
    assert(!restored.outputs[2] && restored.outputs[3]);
    restored.tick(10000, true, 600, 1800000000);
    assert(restored.isDay && restored.outputs[2] && !restored.outputs[3]);
  }
  {
    // Legacy settings remain readable, but must not override newer state.
    JsonDocument legacy;
    legacy["revision"] = 2;
    writeConfig(legacy["config"].to<JsonObject>(), c.config);
    String encoded;
    serializeJson(legacy, encoded);
    store.prefs.putString("settings", encoded);
    pascal::Controller restored;
    uint32_t rev = 0;
    assert(store.load(restored, rev) && rev == 8);
    const auto good = Preferences::values["state_v3"];
    for (const char *bad : {"{", "{}", "{\"schema\":4}"}) {
      Preferences::values["state_v3"] = bad;
      assert(!store.load(restored, rev) && rev == 8);
    }
    JsonDocument invalid;
    assert(!deserializeJson(invalid, good));
    invalid["modes"][0] = 9;
    encoded.clear();
    serializeJson(invalid, encoded);
    Preferences::values["state_v3"] = encoded;
    assert(!store.load(restored, rev));
    Preferences::values.erase("state_v3");
    pascal::Controller migrated;
    assert(store.load(migrated, rev) && rev == 2);
    assert(!migrated.scheduleState.known && migrated.overrides[0].mode == -1);
    recover(migrated, 23, 35);
    for (bool output : migrated.outputs)
      assert(!output);
  }
  std::cout << "Storage: power-loss recovery, persistent modes, offline auto state, sensor "
               "interlocks, failed writes, corrupt records, and legacy migration passed\n";
}
