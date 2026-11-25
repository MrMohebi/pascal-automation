#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pascal {
// Output order matches boardpins::relays and every JSON array.
enum Output : uint8_t { Lamp = 0, Fan = 1, Humidifier = 2 };
constexpr int outputCount = 3;
// Durable per-output selection.
constexpr int ModeAuto = -1, ModeOff = 0, ModeOn = 1;

struct Config {
  bool enabled = false;
  double day_rh_on = 0, day_rh_off = 0, night_rh_on = 0, night_rh_off = 0;
  double max_temperature = 0;
  int day_start = 0, day_end = 0, lamp_on = 0, lamp_off = 0;
  // Fan cycles: ON for *_on_minutes, then OFF for *_off_minutes, repeating.
  // ON=0 keeps the fan off for that period; OFF=0 keeps it running.
  int fan_day_on_minutes = 0, fan_day_off_minutes = 0;
  int fan_night_on_minutes = 0, fan_night_off_minutes = 0;
  int min_off_seconds = 0, humidifier_max_seconds = 0;
  // Automatic mode only: false keeps that output OFF for the whole day or night.
  bool fan_day_enabled = true, fan_night_enabled = true;
  bool humidifier_day_enabled = true, humidifier_night_enabled = true;
};
inline bool band(double lo, double hi, double min, double max) {
  return std::isfinite(lo) && std::isfinite(hi) && lo >= min && hi <= max && lo < hi;
}
inline bool schedule(int start, int end) {
  return start >= 0 && start < 1440 && end >= 0 && end < 1440 && start != end;
}
inline bool cycle(int on, int off) {
  return on >= 0 && on <= 1440 && off >= 0 && off <= 1440 && on + off > 0;
}
inline bool valid(const Config &c) {
  return band(c.day_rh_on, c.day_rh_off, 0, 100) && band(c.night_rh_on, c.night_rh_off, 0, 100) &&
         std::isfinite(c.max_temperature) && c.max_temperature > -40 &&
         c.max_temperature <= 80 && schedule(c.day_start, c.day_end) &&
         schedule(c.lamp_on, c.lamp_off) &&
         cycle(c.fan_day_on_minutes, c.fan_day_off_minutes) &&
         cycle(c.fan_night_on_minutes, c.fan_night_off_minutes) && c.min_off_seconds >= 1 &&
         c.min_off_seconds <= 3600 && c.humidifier_max_seconds >= 1 &&
         c.humidifier_max_seconds <= 86400;
}
inline bool within(int minute, int start, int end) {
  return start < end ? minute >= start && minute < end : minute >= start || minute < end;
}
// Active protection conditions. Nothing latches: each clears by itself.
enum Fault : uint8_t { Overheat = 1 };
// Overheat clears once the temperature is this far below the maximum.
constexpr double overheatHysteresis = 1.0;
// Without a valid reading for this long, overheat protection stops holding the lamp OFF.
constexpr uint32_t overheatSensorTimeout = 60000;
struct Override {
  int mode = ModeAuto;
};
struct ScheduleState {
  bool known = false, day = false, lamp = false;
  bool operator==(const ScheduleState &other) const {
    return known == other.known && day == other.day && lamp == other.lamp;
  }
  bool operator!=(const ScheduleState &other) const { return !(*this == other); }
};

class Controller {
public:
  Config config;
  bool configured = false, outputs[outputCount] = {false, false, false}, isDay = false;
  double temperature = NAN, humidity = NAN;
  uint8_t faults = 0, goodSamples = 0;
  uint32_t sampleAt = 0, failedReads = 0;
  bool sampled = false, sampleValid = false;
  Override overrides[outputCount];
  ScheduleState scheduleState;
  const char *reason = "unconfigured";
  void begin(uint32_t now) {
    for (int i = 0; i < outputCount; i++)
      lastOff[i] = now;
    fanCycleValid = false;
  }
  bool apply(const Config &c, uint32_t now) {
    if (!valid(c))
      return false;
    stop(now);
    config = c;
    configured = true;
    fanCycleValid = false; // Restart the fan cycle with the new timings.
    return true;
  }
  // A failed DHT22 read (NaN or out of range) is common and is ignored: the last valid
  // reading stays in use until it is 10 s old, so up to three failures in a row are
  // tolerated. sampleAt is the time of the last valid reading.
  void sample(double t, double rh, uint32_t now) {
    sampled = true;
    if (!(std::isfinite(t) && std::isfinite(rh) && t >= -40 && t <= 80 && rh >= 0 &&
          rh <= 100)) {
      failedReads++;
      return;
    }
    if (!sampleValid || uint32_t(now - sampleAt) >= 10000)
      goodSamples = 0; // Recovering from an outage needs three fresh readings again.
    temperature = t;
    humidity = rh;
    sampleAt = now;
    sampleValid = true;
    validAt = now;
    goodSamples = std::min(3, int(goodSamples) + 1);
  }
  bool sensorReady(uint32_t now) const {
    return sampled && sampleValid && goodSamples >= 3 && uint32_t(now - sampleAt) < 10000;
  }
  // A fresh valid reading at or above the maximum. Without a sensor this is never true,
  // so overheat protection only works while the DHT22 is connected.
  bool tooHot(uint32_t now) const {
    return configured && sampled && sampleValid && uint32_t(now - sampleAt) < 10000 &&
           temperature >= config.max_temperature;
  }
  // True while the humidifier is OFF only because it reached its maximum run.
  bool humidifierResting(uint32_t now) const {
    return configured && !outputs[Humidifier] && restAfterMaxRun &&
           uint32_t(now - lastOff[Humidifier]) < uint32_t(config.min_off_seconds) * 1000;
  }
  // Auto and Off are always accepted; forcing ON requires automation enabled.
  bool setMode(int output, int mode, uint32_t) {
    if (output < 0 || output >= outputCount || mode < ModeAuto || mode > ModeOn || !configured)
      return false;
    if (mode == ModeOn && !config.enabled)
      return false;
    if (output == Fan && mode == ModeAuto && overrides[Fan].mode != ModeAuto)
      fanCycleValid = false; // Returning to Auto starts a fresh ON period.
    overrides[output].mode = mode;
    return true;
  }
  void updateSchedule(bool clockValid, int minute) {
    if (!configured || !clockValid)
      return;
    scheduleState.known = true;
    scheduleState.day = within(minute, config.day_start, config.day_end);
    scheduleState.lamp = within(minute, config.lamp_on, config.lamp_off);
    isDay = scheduleState.day;
  }
  bool fanEnabled() const { return isDay ? config.fan_day_enabled : config.fan_night_enabled; }
  bool humidifierEnabled() const {
    return isDay ? config.humidifier_day_enabled : config.humidifier_night_enabled;
  }
  int fanOnMinutes() const {
    if (!fanEnabled())
      return 0;
    return isDay ? config.fan_day_on_minutes : config.fan_night_on_minutes;
  }
  int fanOffMinutes() const {
    return isDay ? config.fan_day_off_minutes : config.fan_night_off_minutes;
  }
  // Automatic fan state; the cycle restarts (ON first) at each day/night change.
  bool fanCycleOn(uint32_t now) {
    if (!fanCycleValid || fanCycleDay != isDay) {
      fanCycleValid = true;
      fanCycleDay = isDay;
      fanCycleStart = now;
    }
    const uint32_t on = uint32_t(fanOnMinutes()), off = uint32_t(fanOffMinutes());
    if (on == 0)
      return false;
    if (off == 0)
      return true;
    const uint32_t elapsed = uint32_t(now - fanCycleStart) % ((on + off) * 60000u);
    return elapsed < on * 60000u;
  }
  // Seconds until the automatic fan state changes, or -1 if it never changes.
  int32_t fanCycleRemaining(uint32_t now) const {
    const uint32_t on = uint32_t(fanOnMinutes()), off = uint32_t(fanOffMinutes());
    if (!fanCycleValid || on == 0 || off == 0)
      return -1;
    const uint32_t elapsed = uint32_t(now - fanCycleStart) % ((on + off) * 60000u);
    const uint32_t boundary = elapsed < on * 60000u ? on * 60000u : (on + off) * 60000u;
    return int32_t((boundary - elapsed + 999) / 1000);
  }
  void tick(uint32_t now, bool clockValid, int minute, int64_t) {
    updateSchedule(clockValid, minute);
    isDay = scheduleState.day;
    if (!sampled || uint32_t(now - sampleAt) >= 10000)
      goodSamples = 0;
    // Overheat holds only the lamp OFF and recovers by itself (with hysteresis).
    if (tooHot(now))
      faults |= Overheat;
    else if (!configured || uint32_t(now - validAt) >= overheatSensorTimeout ||
             (sampleValid && uint32_t(now - sampleAt) < 10000 &&
              temperature < config.max_temperature - overheatHysteresis))
      faults &= ~Overheat;
    reason = !configured            ? "unconfigured"
             : !config.enabled      ? "disabled"
             : !scheduleState.known ? "clock invalid"
             : !sensorReady(now)    ? "running without sensor"
             : !clockValid          ? "running saved state"
                                    : "running";
    const bool fanAuto = configured && scheduleState.known && fanCycleOn(now);
    if (!configured || !config.enabled || !scheduleState.known) {
      stop(now);
      return;
    }
    set(Lamp, !(faults & Overheat) && resolve(Lamp, scheduleState.lamp), now);
    set(Fan, resolve(Fan, fanAuto), now);
    const double lo = isDay ? config.day_rh_on : config.night_rh_on;
    const double hi = isDay ? config.day_rh_off : config.night_rh_off;
    bool mist = outputs[Humidifier];
    // Lamp and fan never need the sensor; Auto humidity control pauses without it.
    if (overrides[Humidifier].mode == ModeAuto)
      mist = humidifierEnabled() && sensorReady(now) && (mist ? humidity < hi : humidity < lo);
    else
      mist = overrides[Humidifier].mode == ModeOn;
    // After its maximum continuous run the humidifier rests for the minimum OFF
    // time, then resumes. This affects only the humidifier and needs no acknowledgment.
    const bool maxRun = outputs[Humidifier] && uint32_t(now - onAt[Humidifier]) >=
                                                   uint32_t(config.humidifier_max_seconds) * 1000;
    if (maxRun)
      mist = false;
    // Minimum OFF time protects restarts in every mode; shutdown never waits.
    if (mist && !outputs[Humidifier] &&
        uint32_t(now - lastOff[Humidifier]) < uint32_t(config.min_off_seconds) * 1000)
      mist = false;
    if (outputs[Humidifier] != mist)
      restAfterMaxRun = maxRun;
    set(Humidifier, mist, now);
  }

private:
  uint32_t lastOff[outputCount] = {}, onAt[outputCount] = {};
  uint32_t fanCycleStart = 0, validAt = 0;
  bool restAfterMaxRun = false;
  bool fanCycleValid = false, fanCycleDay = false;
  bool resolve(int output, bool automatic) const {
    return overrides[output].mode == ModeAuto ? automatic : overrides[output].mode == ModeOn;
  }
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
    for (int i = 0; i < outputCount; i++)
      set(i, false, now);
  }
};
} // namespace pascal
