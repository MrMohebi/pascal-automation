#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pascal {
struct Config {
  bool enabled = false;
  double day_temp_on = 0, day_temp_off = 0, night_temp_on = 0, night_temp_off = 0;
  double day_rh_on = 0, day_rh_off = 0, night_rh_on = 0, night_rh_off = 0;
  double max_temperature = 0;
  int day_start = 0, day_end = 0, light1_on = 0, light1_off = 0, light2_on = 0, light2_off = 0;
  int min_off_seconds = 0, heater_max_seconds = 0, humidifier_max_seconds = 0;
};
inline bool band(double lo, double hi, double min, double max) {
  return std::isfinite(lo) && std::isfinite(hi) && lo >= min && hi <= max && lo < hi;
}
inline bool schedule(int start, int end) {
  return start >= 0 && start < 1440 && end >= 0 && end < 1440 && start != end;
}
inline bool valid(const Config &c) {
  return band(c.day_temp_on, c.day_temp_off, -40, 80) &&
         band(c.night_temp_on, c.night_temp_off, -40, 80) &&
         band(c.day_rh_on, c.day_rh_off, 0, 100) && band(c.night_rh_on, c.night_rh_off, 0, 100) &&
         std::isfinite(c.max_temperature) &&
         c.max_temperature > std::max(c.day_temp_off, c.night_temp_off) &&
         c.max_temperature <= 80 && schedule(c.day_start, c.day_end) &&
         schedule(c.light1_on, c.light1_off) && schedule(c.light2_on, c.light2_off) &&
         c.min_off_seconds >= 1 && c.min_off_seconds <= 3600 && c.heater_max_seconds >= 1 &&
         c.heater_max_seconds <= 86400 && c.humidifier_max_seconds >= 1 &&
         c.humidifier_max_seconds <= 86400;
}
inline bool within(int minute, int start, int end) {
  return start < end ? minute >= start && minute < end : minute >= start || minute < end;
}
inline int secondsToTransition(int secondOfDay, int start, int end) {
  int a = (start * 60 - secondOfDay + 86400) % 86400, b = (end * 60 - secondOfDay + 86400) % 86400;
  return std::min(a == 0 ? 86400 : a, b == 0 ? 86400 : b);
}
enum Fault : uint8_t { Overheat = 1, HeaterTimeout = 2, HumidifierTimeout = 4 };
struct Override {
  int mode = -1;
  int64_t until = 0;
  uint32_t started = 0, duration = 0;
};

class Controller {
public:
  Config config;
  bool configured = false, outputs[4] = {false, false, false, false}, isDay = false;
  double temperature = NAN, humidity = NAN;
  uint8_t faults = 0, goodSamples = 0;
  uint32_t sampleAt = 0;
  bool sampled = false, sampleValid = false;
  Override overrides[2];
  const char *reason = "unconfigured";
  void begin(uint32_t now) {
    for (int i = 0; i < 4; i++)
      lastOff[i] = now;
  }
  bool apply(const Config &c, uint32_t now) {
    if (!valid(c))
      return false;
    stop(now);
    config = c;
    configured = true;
    overrides[0] = Override{};
    overrides[1] = Override{};
    return true;
  }
  void sample(double t, double rh, uint32_t now) {
    temperature = t;
    humidity = rh;
    sampleAt = now;
    sampled = true;
    sampleValid =
        std::isfinite(t) && std::isfinite(rh) && t >= -40 && t <= 80 && rh >= 0 && rh <= 100;
    goodSamples = sampleValid ? std::min(3, int(goodSamples) + 1) : 0;
  }
  bool sensorReady(uint32_t now) const {
    return sampled && sampleValid && goodSamples >= 3 && uint32_t(now - sampleAt) < 10000;
  }
  bool acknowledge(uint32_t now) {
    if (!configured || !sensorReady(now) || temperature >= config.max_temperature)
      return false;
    faults = 0;
    stop(now);
    return true;
  }
  bool overrideLight(int light, int mode, int64_t epoch, int secondsOfDay, uint32_t now) {
    if (light < 0 || light > 1 || mode < -1 || mode > 1 || !configured)
      return false;
    if (mode == -1) {
      overrides[light] = Override{};
      return true;
    }
    if (!config.enabled || faults || !sensorReady(now) || epoch < 1704067200)
      return false;
    int duration = secondsToTransition(secondsOfDay, light ? config.light2_on : config.light1_on,
                                       light ? config.light2_off : config.light1_off);
    overrides[light] = {mode, epoch + duration, now, uint32_t(duration) * 1000};
    return true;
  }
  void tick(uint32_t now, bool clockValid, int minute, int64_t epoch) {
    if (!sampled || uint32_t(now - sampleAt) >= 10000)
      goodSamples = 0;
    if (configured && sampleValid && uint32_t(now - sampleAt) < 10000 &&
        temperature >= config.max_temperature)
      faults |= Overheat;
    if (configured)
      for (int i = 0; i < 2; i++) {
        uint32_t limit =
            uint32_t(i ? config.humidifier_max_seconds : config.heater_max_seconds) * 1000;
        if (outputs[i] && uint32_t(now - onAt[i]) >= limit)
          faults |= (i ? HumidifierTimeout : HeaterTimeout);
      }
    for (auto &o : overrides)
      if (o.mode != -1 &&
          (!clockValid || epoch >= o.until || uint32_t(now - o.started) >= o.duration))
        o = Override{};
    reason = !configured         ? "unconfigured"
             : faults            ? "latched fault"
             : !config.enabled   ? "disabled"
             : !clockValid       ? "clock invalid"
             : !sensorReady(now) ? "sensor not ready"
                                 : "running";
    if (!configured || faults || !config.enabled || !clockValid || !sensorReady(now)) {
      stop(now);
      return;
    }
    isDay = within(minute, config.day_start, config.day_end);
    double values[2] = {temperature, humidity};
    double lo[2] = {isDay ? config.day_temp_on : config.night_temp_on,
                    isDay ? config.day_rh_on : config.night_rh_on};
    double hi[2] = {isDay ? config.day_temp_off : config.night_temp_off,
                    isDay ? config.day_rh_off : config.night_rh_off};
    for (int i = 0; i < 2; i++) {
      if (outputs[i] && values[i] >= hi[i])
        set(i, false, now);
      else if (!outputs[i] && values[i] < lo[i] &&
               uint32_t(now - lastOff[i]) >= uint32_t(config.min_off_seconds) * 1000)
        set(i, true, now);
    }
    for (int i = 0; i < 2; i++)
      set(i + 2,
          overrides[i].mode == -1 ? within(minute, i ? config.light2_on : config.light1_on,
                                           i ? config.light2_off : config.light1_off)
                                  : overrides[i].mode == 1,
          now);
  }

private:
  uint32_t lastOff[4] = {}, onAt[4] = {};
  void set(int i, bool state, uint32_t now) {
    if (outputs[i] == state)
      return;
    outputs[i] = state;
    if (state)
      onAt[i] = now;
    else
      lastOff[i] = now;
  }
  void stop(uint32_t now) {
    for (int i = 0; i < 4; i++)
      set(i, false, now);
  }
};
} // namespace pascal
