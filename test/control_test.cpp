#include "control.h"
#include <cassert>
#include <iostream>
#include <string>
using namespace pascal;
constexpr int64_t epoch = 1800000000;
constexpr uint32_t minute = 60000;
Config example() {
  // Synthetic test values; these are not husbandry recommendations.
  Config c;
  c.enabled = true;
  c.day_rh_on = 40;
  c.day_rh_off = 50;
  c.night_rh_on = 60;
  c.night_rh_off = 70;
  c.max_temperature = 32;
  c.day_start = 480;
  c.day_end = 1200;
  c.lamp_on = 540;
  c.lamp_off = 1140;
  c.fan_day_on_minutes = 10;
  c.fan_day_off_minutes = 40;
  c.fan_night_on_minutes = 5;
  c.fan_night_off_minutes = 55;
  c.min_off_seconds = 10;
  c.humidifier_max_seconds = 30;
  return c;
}
Controller ready(uint32_t now = 10000, double t = 23, double rh = 45) {
  Controller c;
  c.begin(0);
  assert(c.apply(example(), 0));
  c.sample(t, rh, now - 5000);
  c.sample(t, rh, now - 2500);
  c.sample(t, rh, now);
  return c;
}
// Keep the sensor fresh while advancing time.
void run(Controller &c, uint32_t now, bool clock = true, int at = 600, double t = 23,
         double rh = 45) {
  c.sample(t, rh, now);
  c.tick(now, clock, at, clock ? epoch : 0);
}
void allOff(const Controller &c) {
  for (bool s : c.outputs)
    assert(!s);
}
int main() {
  int tests = 0;
  {
    Controller c;
    c.begin(0);
    c.tick(10000, true, 600, epoch);
    allOff(c);
    tests++;
  }
  {
    auto c = ready();
    c.config.enabled = false;
    c.tick(10000, true, 600, epoch);
    allOff(c);
    assert(std::string(c.reason) == "disabled");
    tests++;
  }
  {
    auto c = ready();
    c.tick(10000, false, 600, 0);
    allOff(c);
    tests++;
  }
  {
    // Day, inside lamp schedule, dry air: everything runs; the fan starts its ON period.
    auto c = ready(10000, 23, 35);
    c.tick(10000, true, 600, epoch);
    assert(c.outputs[Lamp] && c.outputs[Fan] && c.outputs[Humidifier]);
    tests++;
  }
  {
    // Lamp follows its own schedule, not day/night.
    auto c = ready();
    c.tick(10000, true, 500, epoch);
    assert(c.isDay && !c.outputs[Lamp]);
    c.tick(10000, true, 540, epoch);
    assert(c.outputs[Lamp]);
    c.tick(10000, true, 1140, epoch);
    assert(!c.outputs[Lamp]);
    tests++;
  }
  {
    // Day fan cycle: 10 min ON, 40 min OFF, repeating.
    auto c = ready();
    run(c, 10000);
    assert(c.outputs[Fan]);
    run(c, 10000 + 10 * minute - 1000);
    assert(c.outputs[Fan]);
    assert(c.fanCycleRemaining(10000 + 10 * minute - 1000) == 1);
    run(c, 10000 + 10 * minute);
    assert(!c.outputs[Fan]);
    assert(c.fanCycleRemaining(10000 + 10 * minute) == 40 * 60);
    run(c, 10000 + 49 * minute);
    assert(!c.outputs[Fan]);
    run(c, 10000 + 50 * minute);
    assert(c.outputs[Fan]);
    run(c, 10000 + 60 * minute);
    assert(!c.outputs[Fan]);
    tests++;
  }
  {
    // Night uses its own cycle and restarts ON at the transition.
    auto c = ready();
    run(c, 10000, true, 600);
    run(c, 10000 + 20 * minute, true, 600);
    assert(!c.outputs[Fan]);
    run(c, 10000 + 21 * minute, true, 1300, 23, 65);
    assert(!c.isDay && c.outputs[Fan]);
    run(c, 10000 + 26 * minute, true, 1300, 23, 65);
    assert(!c.outputs[Fan]);
    run(c, 10000 + 80 * minute, true, 1300, 23, 65);
    assert(!c.outputs[Fan]);
    run(c, 10000 + 81 * minute, true, 1300, 23, 65);
    assert(c.outputs[Fan]);
    tests++;
  }
  {
    // ON=0 keeps the fan off; OFF=0 keeps it running.
    auto c = ready();
    auto cfg = example();
    cfg.fan_day_on_minutes = 0;
    assert(c.apply(cfg, 10000));
    run(c, 10000);
    assert(!c.outputs[Fan] && c.fanCycleRemaining(10000) == -1);
    cfg.fan_day_on_minutes = 10;
    cfg.fan_day_off_minutes = 0;
    assert(c.apply(cfg, 10000));
    for (uint32_t n = 10000; n < 10000 + 120 * minute; n += 7 * minute) {
      run(c, n);
      assert(c.outputs[Fan]);
    }
    tests++;
  }
  {
    // Fan modes: forced OFF / ON win over the cycle; Auto restarts a fresh ON period.
    auto c = ready();
    run(c, 10000);
    assert(c.setMode(Fan, ModeOff, 10000));
    run(c, 12500);
    assert(!c.outputs[Fan]);
    run(c, 10000 + 20 * minute);
    assert(c.setMode(Fan, ModeOn, 10000 + 20 * minute));
    run(c, 10000 + 20 * minute);
    assert(c.outputs[Fan]);
    assert(c.setMode(Fan, ModeAuto, 10000 + 30 * minute));
    run(c, 10000 + 30 * minute);
    assert(c.outputs[Fan]);
    run(c, 10000 + 40 * minute);
    assert(!c.outputs[Fan]);
    tests++;
  }
  {
    // Humidifier hysteresis.
    auto c = ready();
    c.sample(23, 45, 10000);
    c.tick(10000, true, 600, epoch);
    assert(!c.outputs[Humidifier]);
    run(c, 12500, true, 600, 23, 39);
    assert(c.outputs[Humidifier]);
    run(c, 15000, true, 600, 23, 49);
    assert(c.outputs[Humidifier]);
    run(c, 17500, true, 600, 23, 50);
    assert(!c.outputs[Humidifier]);
    tests++;
  }
  {
    // Minimum OFF time delays restart, including when forced ON.
    auto c = ready(10000, 23, 35);
    run(c, 10000, true, 600, 23, 35);
    assert(c.outputs[Humidifier]);
    run(c, 12500, true, 600, 23, 55);
    assert(!c.outputs[Humidifier]);
    run(c, 15000, true, 600, 23, 35);
    assert(!c.outputs[Humidifier]);
    assert(c.setMode(Humidifier, ModeOn, 15000));
    run(c, 17500, true, 600, 23, 55);
    assert(!c.outputs[Humidifier]);
    run(c, 22500, true, 600, 23, 55);
    assert(c.outputs[Humidifier]);
    tests++;
  }
  {
    // Forced humidifier ignores humidity; after its maximum run it rests, then resumes.
    // Lamp and fan keep running and nothing latches.
    auto c = ready(10000, 23, 80);
    assert(c.setMode(Humidifier, ModeOn, 10000));
    c.tick(10000, true, 600, epoch);
    assert(c.outputs[Humidifier]);
    for (uint32_t n = 12500; n < 40000; n += 2500)
      run(c, n, true, 600, 23, 80);
    assert(c.outputs[Humidifier]);
    run(c, 40000, true, 600, 23, 80);
    assert(!c.outputs[Humidifier] && c.humidifierResting(40000));
    assert(c.outputs[Lamp] && c.outputs[Fan] && !c.faults);
    run(c, 47500, true, 600, 23, 80);
    assert(!c.outputs[Humidifier]);
    run(c, 50000, true, 600, 23, 80);
    assert(c.outputs[Humidifier] && !c.humidifierResting(50000));
    tests++;
  }
  {
    // Auto humidifier that never reaches its OFF level runs in max-run/rest cycles.
    auto c = ready();
    c.sample(23, 35, 10000);
    c.tick(10000, true, 600, epoch);
    assert(c.outputs[Humidifier]);
    for (uint32_t n = 12500; n <= 40000; n += 2500)
      run(c, n, true, 600, 23, 35);
    assert(!c.outputs[Humidifier] && c.outputs[Lamp] && c.outputs[Fan] && !c.faults);
    assert(std::string(c.reason) == "running");
    for (uint32_t n = 42500; n < 50000; n += 2500)
      run(c, n, true, 600, 23, 35);
    assert(!c.outputs[Humidifier]);
    run(c, 50000, true, 600, 23, 35);
    assert(c.outputs[Humidifier]);
    // A normal Auto shutdown is not reported as a rest.
    run(c, 52500, true, 600, 23, 55);
    assert(!c.outputs[Humidifier] && !c.humidifierResting(52500));
    tests++;
  }
  {
    // Lamp force ON/OFF persists across schedule changes until Auto.
    auto c = ready();
    assert(c.setMode(Lamp, ModeOff, 10000));
    run(c, 10000);
    assert(!c.outputs[Lamp]);
    assert(c.setMode(Lamp, ModeOn, 12500));
    run(c, 12500, true, 1300);
    assert(c.outputs[Lamp]);
    assert(c.setMode(Lamp, ModeAuto, 15000));
    run(c, 15000, true, 1300);
    assert(!c.outputs[Lamp]);
    tests++;
  }
  {
    auto c = ready();
    assert(!c.setMode(3, ModeOn, 10000));
    assert(!c.setMode(-1, ModeOn, 10000));
    assert(!c.setMode(Lamp, 8, 10000));
    assert(!c.setMode(Lamp, -2, 10000));
    tests++;
  }
  {
    // Forcing ON is blocked by interlocks; Off and Auto stay available.
    auto c = ready();
    c.config.enabled = false;
    assert(!c.setMode(Fan, ModeOn, 10000));
    assert(c.setMode(Fan, ModeOff, 10000));
    assert(c.setMode(Fan, ModeAuto, 10000));
    c = ready();
    c.faults = Overheat; // Being too hot never blocks a mode change.
    assert(c.setMode(Lamp, ModeOn, 10000));
    assert(c.setMode(Lamp, ModeOff, 10000));
    c = ready();
    assert(c.setMode(Humidifier, ModeOn, 30000)); // No sensor needed to force ON.
    Controller unconfigured;
    assert(!unconfigured.setMode(Lamp, ModeOff, 0));
    tests++;
  }
  {
    // Up to three failed reads in a row keep the last valid reading; a 10 s outage only
    // pauses Auto humidity control, and recovery needs three good samples.
    auto c = ready(10000, 23, 35);
    c.tick(10000, true, 600, epoch);
    assert(c.outputs[Humidifier]);
    for (uint32_t n = 12500; n <= 17500; n += 2500) {
      c.sample(NAN, NAN, n);
      c.tick(n, true, 600, epoch);
      assert(c.sensorReady(n) && c.outputs[Humidifier] && c.humidity == 35);
      assert(std::string(c.reason) == "running");
    }
    c.sample(NAN, 35, 20000);
    c.tick(20000, true, 600, epoch);
    assert(!c.sensorReady(20000) && c.failedReads == 4);
    assert(c.outputs[Lamp] && c.outputs[Fan] && !c.outputs[Humidifier]);
    assert(std::string(c.reason) == "running without sensor");
    c.sample(23, 35, 22500);
    c.tick(22500, true, 600, epoch);
    c.sample(23, 35, 25000);
    c.tick(25000, true, 600, epoch);
    assert(!c.outputs[Humidifier]);
    c.sample(150, 35, 27500); // Out-of-range reading counts as a failed read.
    c.sample(23, 35, 30000);
    c.tick(30000, true, 600, epoch);
    assert(c.outputs[Humidifier] && std::string(c.reason) == "running");
    tests++;
  }
  {
    // Sensor never connected: lamp and fan run, mist can be forced, and a stale
    // overheat condition clears once there has been no reading for a minute.
    Controller c;
    c.begin(0);
    assert(c.apply(example(), 0));
    c.tick(20000, true, 600, epoch);
    assert(!c.sensorReady(20000));
    assert(c.outputs[Lamp] && c.outputs[Fan] && !c.outputs[Humidifier]);
    c.faults = Overheat;
    c.tick(30000, true, 600, epoch);
    assert(c.faults && !c.outputs[Lamp] && c.outputs[Fan]);
    c.tick(80000, true, 600, epoch);
    assert(!c.faults && c.outputs[Lamp]);
    assert(c.setMode(Humidifier, ModeOn, 80000));
    c.tick(80000, true, 600, epoch);
    assert(c.outputs[Humidifier]);
    tests++;
  }
  {
    // Overtemperature holds only the lamp OFF (even when forced ON) and recovers by
    // itself once the temperature is 1 C below the maximum.
    auto c = ready();
    assert(c.setMode(Lamp, ModeOn, 10000));
    c.tick(10000, true, 600, epoch);
    assert(c.outputs[Lamp]);
    run(c, 12500, true, 600, 32);
    assert(c.faults & Overheat);
    assert(!c.outputs[Lamp] && c.outputs[Fan]);
    run(c, 15000, true, 600, 31.5);
    assert((c.faults & Overheat) && !c.outputs[Lamp]);
    run(c, 17500, true, 600, 30.9);
    assert(!c.faults && c.outputs[Lamp]);
    // A failed reading while hot keeps the lamp OFF until a minute without readings.
    run(c, 20000, true, 600, 33);
    assert(!c.outputs[Lamp]);
    c.sample(NAN, NAN, 22500);
    c.tick(22500, true, 600, epoch);
    assert((c.faults & Overheat) && !c.outputs[Lamp]);
    c.tick(80000, true, 600, epoch);
    assert(!c.faults && c.outputs[Lamp]);
    tests++;
  }
  {
    assert(within(0, 1200, 480));
    assert(!within(480, 1200, 480));
    assert(within(1200, 1200, 480));
    assert(!within(1199, 1200, 480));
    tests++;
  }
  {
    auto c = ready();
    c.tick(10000, true, 479, epoch);
    assert(!c.scheduleState.day && !c.scheduleState.lamp);
    c.tick(10000, true, 540, epoch);
    assert(c.scheduleState.day && c.scheduleState.lamp);
    tests++;
  }
  {
    // A restored forced mode survives reboot.
    auto c = ready();
    assert(c.setMode(Lamp, ModeOn, 10000));
    Controller reboot;
    reboot.begin(0);
    reboot.apply(c.config, 0);
    c.updateSchedule(true, 1300);
    reboot.scheduleState = c.scheduleState;
    reboot.overrides[Lamp] = c.overrides[Lamp];
    // Restored saved schedule and forced lamp apply right away, sensor or not.
    reboot.tick(0, false, 0, 0);
    assert(reboot.outputs[Lamp] && reboot.overrides[Lamp].mode == ModeOn);
    tests++;
  }
  {
    auto c = example();
    c.day_rh_on = c.day_rh_off;
    assert(!valid(c));
    c = example();
    c.max_temperature = 81;
    assert(!valid(c));
    c = example();
    c.lamp_off = c.lamp_on;
    assert(!valid(c));
    c = example();
    c.day_rh_on = NAN;
    assert(!valid(c));
    c = example();
    c.min_off_seconds = 0;
    assert(!valid(c));
    c = example();
    c.fan_night_on_minutes = 0;
    c.fan_night_off_minutes = 0;
    assert(!valid(c));
    c = example();
    c.fan_day_on_minutes = -1;
    assert(!valid(c));
    c = example();
    c.fan_day_off_minutes = 1441;
    assert(!valid(c));
    c = example();
    c.fan_day_on_minutes = 0;
    assert(valid(c));
    tests++;
  }
  {
    // millis() wrap-around.
    Controller c;
    c.begin(UINT32_MAX - 5000);
    c.apply(example(), UINT32_MAX - 5000);
    c.sample(23, 35, 0);
    c.sample(23, 35, 2500);
    c.sample(23, 35, 5000);
    c.tick(5000, true, 600, epoch);
    assert(c.outputs[Humidifier] && c.outputs[Fan]);
    tests++;
  }
  {
    // Clock loss keeps the saved profile; the fan keeps cycling on uptime.
    auto c = ready();
    c.tick(10000, true, 600, epoch);
    run(c, 12500, false, 0, 23, 35);
    assert(c.outputs[Lamp] && c.outputs[Fan] && c.outputs[Humidifier] && c.isDay);
    run(c, 15000, false, 0);
    assert(std::string(c.reason) == "running saved state");
    run(c, 10000 + 11 * minute, false, 0);
    assert(!c.outputs[Fan]);
    tests++;
  }
  {
    // Config changes preserve selected modes.
    auto c = ready();
    c.tick(10000, true, 600, epoch);
    assert(c.setMode(Lamp, ModeOff, 10000));
    auto cfg = c.config;
    cfg.day_rh_on = 39;
    assert(c.apply(cfg, 10000));
    c.tick(10000, false, 0, 0);
    assert(c.overrides[Lamp].mode == ModeOff && !c.outputs[Lamp] && c.outputs[Fan]);
    c.sample(NAN, NAN, 12500);
    c.tick(12500, false, 0, 0);
    assert(!c.outputs[Lamp] && c.outputs[Fan] && !c.outputs[Humidifier]);
    tests++;
  }
  {
    // Fan off all night: Auto stays OFF at night and resumes its cycle by day.
    auto c = ready();
    auto cfg = example();
    cfg.fan_night_enabled = false;
    assert(c.apply(cfg, 10000));
    run(c, 10000, true, 1300, 23, 65);
    assert(!c.isDay && !c.outputs[Fan] && c.fanCycleRemaining(10000) == -1);
    run(c, 10000 + 30 * minute, true, 1300, 23, 65);
    assert(!c.outputs[Fan]);
    run(c, 10000 + 31 * minute, true, 600);
    assert(c.isDay && c.outputs[Fan]);
    // A forced ON still wins over the night switch.
    run(c, 10000 + 32 * minute, true, 1300, 23, 65);
    assert(c.setMode(Fan, ModeOn, 10000 + 32 * minute));
    run(c, 10000 + 32 * minute, true, 1300, 23, 65);
    assert(c.outputs[Fan]);
    tests++;
  }
  {
    // Humidifier off all night ignores dry air at night only.
    auto c = ready();
    auto cfg = example();
    cfg.humidifier_night_enabled = false;
    assert(c.apply(cfg, 0));
    run(c, 10000, true, 1300, 23, 20);
    assert(!c.outputs[Humidifier]);
    run(c, 12500, true, 600, 23, 20);
    assert(c.outputs[Humidifier]);
    // Running humidifier stops at the night transition.
    run(c, 15000, true, 1300, 23, 20);
    assert(!c.outputs[Humidifier]);
    tests++;
  }
  {
    // Humidifier off all day.
    auto c = ready();
    auto cfg = example();
    cfg.humidifier_day_enabled = false;
    assert(c.apply(cfg, 0));
    run(c, 10000, true, 600, 23, 20);
    assert(!c.outputs[Humidifier] && c.outputs[Fan]);
    tests++;
  }
  std::cout << tests << " controller scenarios passed\n";
}
