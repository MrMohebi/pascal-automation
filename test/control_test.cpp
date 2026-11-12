#include "control.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace pascal;
constexpr int64_t epoch = 1800000000;
Config example() {
  // Synthetic test values; these are not husbandry recommendations.
  Config c;
  c.enabled = true;
  c.day_temp_on = 24;
  c.day_temp_off = 26;
  c.night_temp_on = 18;
  c.night_temp_off = 20;
  c.day_rh_on = 40;
  c.day_rh_off = 50;
  c.night_rh_on = 60;
  c.night_rh_off = 70;
  c.max_temperature = 32;
  c.day_start = 480;
  c.day_end = 1200;
  c.light1_on = 480;
  c.light1_off = 1200;
  c.light2_on = 1200;
  c.light2_off = 480;
  c.min_off_seconds = 10;
  c.heater_max_seconds = 20;
  c.humidifier_max_seconds = 30;
  return c;
}
Controller ready(uint32_t now = 10000) {
  Controller c;
  c.begin(0);
  assert(c.apply(example(), 0));
  c.sample(23, 35, now - 5000);
  c.sample(23, 35, now - 2500);
  c.sample(23, 35, now);
  return c;
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
    tests++;
  }
  {
    auto c = ready();
    c.tick(10000, false, 600, 0);
    allOff(c);
    tests++;
  }
  {
    auto c = ready();
    c.tick(10000, true, 600, epoch);
    assert(c.outputs[0] && c.outputs[1] && c.outputs[2] && !c.outputs[3]);
    tests++;
  }
  {
    auto c = ready(9000);
    c.tick(9999, true, 600, epoch);
    assert(!c.outputs[0] && !c.outputs[1]);
    tests++;
  }
  {
    auto c = ready();
    c.sample(24, 40, 10000);
    c.tick(10000, true, 600, epoch);
    assert(!c.outputs[0] && !c.outputs[1]);
    tests++;
  }
  {
    auto c = ready();
    c.tick(10000, true, 600, epoch);
    c.sample(25, 45, 12500);
    c.tick(12500, true, 600, epoch);
    assert(c.outputs[0] && c.outputs[1]);
    c.sample(26, 50, 15000);
    c.tick(15000, true, 600, epoch);
    assert(!c.outputs[0] && !c.outputs[1]);
    tests++;
  }
  {
    auto c = ready();
    c.tick(10000, true, 600, epoch);
    c.sample(27, 55, 12500);
    c.tick(12500, true, 600, epoch);
    c.sample(23, 35, 15000);
    c.tick(15000, true, 600, epoch);
    assert(!c.outputs[0]);
    c.sample(23, 35, 22500);
    c.tick(22500, true, 600, epoch);
    assert(c.outputs[0]);
    tests++;
  }
  {
    auto c = ready();
    c.tick(10000, true, 1300, epoch);
    assert(!c.outputs[0] && c.outputs[1] && !c.outputs[2] && c.outputs[3]);
    tests++;
  }
  {
    auto c = ready();
    c.tick(10000, true, 600, epoch);
    c.sample(NAN, 35, 12500);
    c.tick(12500, true, 600, epoch);
    allOff(c);
    c.sample(23, 35, 15000);
    c.tick(15000, true, 600, epoch);
    allOff(c);
    c.sample(23, 35, 17500);
    c.tick(17500, true, 600, epoch);
    allOff(c);
    c.sample(23, 35, 20000);
    c.tick(20000, true, 600, epoch);
    assert(c.outputs[2]);
    tests++;
  }
  {
    auto c = ready();
    c.tick(10000, true, 600, epoch);
    c.tick(20000, true, 600, epoch);
    allOff(c);
    assert(!c.sensorReady(20000));
    tests++;
  }
  {
    auto c = ready();
    c.tick(10000, true, 600, epoch);
    c.sample(32, 35, 12500);
    c.tick(12500, true, 600, epoch);
    allOff(c);
    assert(c.faults & Overheat);
    assert(!c.acknowledge(12500));
    c.sample(25, 35, 15000);
    c.tick(15000, true, 600, epoch);
    allOff(c);
    assert(c.acknowledge(15000));
    assert(!c.faults);
    tests++;
  }
  {
    auto c = ready();
    c.tick(10000, true, 600, epoch);
    for (uint32_t n = 12500; n <= 30000; n += 2500) {
      c.sample(23, 35, n);
      c.tick(n, true, 600, epoch);
    }
    assert(c.faults & HeaterTimeout);
    allOff(c);
    tests++;
  }
  {
    auto c = ready();
    c.sample(27, 35, 10000);
    c.tick(10000, true, 600, epoch);
    for (uint32_t n = 12500; n <= 40000; n += 2500) {
      c.sample(27, 35, n);
      c.tick(n, true, 600, epoch);
    }
    assert(c.faults & HumidifierTimeout);
    allOff(c);
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
    assert(secondsToTransition(479 * 60, 480, 1200) == 60);
    assert(secondsToTransition(480 * 60, 480, 1200) == 720 * 60);
    assert(secondsToTransition(1439 * 60, 480, 1200) == 481 * 60);
    tests++;
  }
  {
    auto c = ready();
    c.sample(27, 55, 10000);
    assert(c.overrideLight(0, 0, epoch, 479 * 60, 10000));
    c.tick(10000, true, 600, epoch);
    assert(!c.outputs[2]);
    c.sample(27, 55, 70000);
    c.tick(70000, true, 600, epoch + 60);
    assert(c.outputs[2] && c.overrides[0].mode == -1);
    tests++;
  }
  {
    auto c = ready();
    assert(c.overrideLight(0, 0, epoch, 479 * 60, 10000));
    c.sample(27, 55, 70000);
    c.tick(70000, true, 600, epoch - 10);
    assert(c.overrides[0].mode == -1);
    tests++;
  }
  {
    auto c = ready();
    assert(!c.overrideLight(0, 1, 0, 600 * 60, 10000));
    assert(!c.overrideLight(3, 1, epoch, 0, 10000));
    assert(!c.overrideLight(0, 8, epoch, 0, 10000));
    tests++;
  }
  {
    auto c = ready();
    assert(c.overrideLight(0, 1, epoch, 0, 10000));
    Controller reboot;
    reboot.begin(0);
    reboot.apply(c.config, 0);
    reboot.faults = c.faults;
    reboot.tick(0, false, 0, 0);
    allOff(reboot);
    assert(reboot.overrides[0].mode == -1);
    tests++;
  }
  {
    auto c = ready();
    c.faults = Overheat;
    Controller reboot;
    reboot.apply(c.config, 0);
    reboot.faults = c.faults;
    reboot.sample(23, 35, 0);
    reboot.tick(0, true, 600, epoch);
    allOff(reboot);
    assert(reboot.faults & Overheat);
    tests++;
  }
  {
    auto c = example();
    c.day_temp_on = c.day_temp_off;
    assert(!valid(c));
    c = example();
    c.max_temperature = 25;
    assert(!valid(c));
    c = example();
    c.light1_off = c.light1_on;
    assert(!valid(c));
    c = example();
    c.day_rh_on = NAN;
    assert(!valid(c));
    c = example();
    c.min_off_seconds = 0;
    assert(!valid(c));
    tests++;
  }
  {
    Controller c;
    c.begin(UINT32_MAX - 5000);
    c.apply(example(), UINT32_MAX - 5000);
    c.sample(23, 35, 0);
    c.sample(23, 35, 2500);
    c.sample(23, 35, 5000);
    c.tick(5000, true, 600, epoch);
    assert(c.outputs[0]);
    tests++;
  }
  {
    auto c = ready();
    c.tick(10000, true, 600, epoch);
    c.sample(23, 35, 12500);
    c.tick(12500, false, 600, 0);
    allOff(c);
    tests++;
  }
  std::cout << tests << " controller scenarios passed\n";
}
