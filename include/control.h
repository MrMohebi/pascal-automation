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
// The DHT22 drops reads often. The last valid reading is used for up to two
// minutes; after that, heater and humidifier stop until a valid read returns.
constexpr uint32_t sensorHoldMs = 120000;
enum Fault : uint8_t { Overheat = 1, HeaterTimeout = 2, HumidifierTimeout = 4 };
struct Override {
  int mode = -1; // Durable selection: Auto=-1, Off=0, On=1.
};
struct ScheduleState {
  bool known = false, day = false, lights[2] = {false, false};
  bool operator==(const ScheduleState &other) const {
    return known == other.known && day == other.day && lights[0] == other.lights[0] &&
           lights[1] == other.lights[1];
  }
  bool operator!=(const ScheduleState &other) const { return !(*this == other); }
};

class Controller {
public:
  Config config;
  bool configured = false, outputs[4] = {false, false, false, false}, isDay = false;
  double temperature = NAN, humidity = NAN;
  uint8_t faults = 0;
  uint32_t sampleAt = 0, failedReads = 0; // sampleAt: time of the last valid read.
  bool sampled = false, lastReadOk = false;
  Override overrides[2];
  ScheduleState scheduleState;
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
    return true;
  }
  void sample(double t, double rh, uint32_t now) {
    lastReadOk =
        std::isfinite(t) && std::isfinite(rh) && t >= -40 && t <= 80 && rh >= 0 && rh <= 100;
    if (!lastReadOk) {
      failedReads++; // Keep the previous valid reading.
      return;
    }
    temperature = t;
    humidity = rh;
    sampleAt = now;
    sampled = true;
    failedReads = 0;
  }
  // True while the last valid reading is less than two minutes old.
  bool sensorReady(uint32_t now) const {
    return sampled && uint32_t(now - sampleAt) < sensorHoldMs;
  }
  bool acknowledge(uint32_t now) {
    if (!configured || !sensorReady(now) || temperature >= config.max_temperature)
      return false;
    faults = 0;
    stop(now);
    return true;
  }
  bool overrideLight(int light, int mode, uint32_t now) {
    if (light < 0 || light > 1 || mode < -1 || mode > 1 || !configured)
      return false;
    (void)now; // Lights do not depend on the sensor.
    if (mode != -1 && (!config.enabled || faults))
      return false;
    overrides[light].mode = mode;
    return true;
  }
  void updateSchedule(bool clockValid, int minute) {
    if (!configured || !clockValid)
      return;
    scheduleState.known = true;
    scheduleState.day = within(minute, config.day_start, config.day_end);
    scheduleState.lights[0] = within(minute, config.light1_on, config.light1_off);
    scheduleState.lights[1] = within(minute, config.light2_on, config.light2_off);
    isDay = scheduleState.day;
  }
  void tick(uint32_t now, bool clockValid, int minute, int64_t) {
    updateSchedule(clockValid, minute);
    isDay = scheduleState.day;
    const bool sensorOk = sensorReady(now);
    if (configured && sensorOk && temperature >= config.max_temperature)
      faults |= Overheat;
    if (configured)
      for (int i = 0; i < 2; i++) {
        uint32_t limit =
            uint32_t(i ? config.humidifier_max_seconds : config.heater_max_seconds) * 1000;
        if (outputs[i] && uint32_t(now - onAt[i]) >= limit)
          faults |= (i ? HumidifierTimeout : HeaterTimeout);
      }
    reason = !configured            ? "unconfigured"
             : faults               ? "latched fault"
             : !config.enabled      ? "disabled"
             : !scheduleState.known ? "clock invalid"
             : !sensorOk            ? "sensor stale; heat/mist off"
             : !clockValid          ? "running saved state"
                                    : "running";
    if (!configured || faults || !config.enabled || !scheduleState.known) {
      stop(now);
      return;
    }
    for (int i = 0; i < 2; i++)
      set(i + 2, overrides[i].mode == -1 ? scheduleState.lights[i] : overrides[i].mode == 1, now);
    if (!sensorOk) {
      // Stale sensor: stop heater and humidifier; lights keep running.
      set(0, false, now);
      set(1, false, now);
      return;
    }
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
