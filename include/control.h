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
// Bit 2 was the removed heater runtime fault; it is never set and is cleared on load.
enum Fault : uint8_t { Overheat = 1, HumidifierTimeout = 4 };
constexpr uint8_t knownFaults = Overheat | HumidifierTimeout;
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
  uint32_t sampleAt = 0;
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
  // Auto and Off are always accepted; forcing ON requires a healthy, enabled system.
  bool setMode(int output, int mode, uint32_t now) {
    if (output < 0 || output >= outputCount || mode < ModeAuto || mode > ModeOn || !configured)
      return false;
    if (mode == ModeOn && (!config.enabled || faults || !sensorReady(now)))
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
  int fanOnMinutes() const {
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
    if (configured && sampleValid && uint32_t(now - sampleAt) < 10000 &&
        temperature >= config.max_temperature)
      faults |= Overheat;
    if (configured && outputs[Humidifier] &&
        uint32_t(now - onAt[Humidifier]) >= uint32_t(config.humidifier_max_seconds) * 1000)
      faults |= HumidifierTimeout;
    reason = !configured            ? "unconfigured"
             : faults               ? "latched fault"
             : !config.enabled      ? "disabled"
             : !scheduleState.known ? "clock invalid"
             : !sensorReady(now)    ? "sensor not ready"
             : !clockValid          ? "running saved state"
                                    : "running";
    const bool fanAuto = configured && scheduleState.known && fanCycleOn(now);
    if (!configured || faults || !config.enabled || !scheduleState.known || !sensorReady(now)) {
      stop(now);
      return;
    }
    set(Lamp, resolve(Lamp, scheduleState.lamp), now);
    set(Fan, resolve(Fan, fanAuto), now);
    const double lo = isDay ? config.day_rh_on : config.night_rh_on;
    const double hi = isDay ? config.day_rh_off : config.night_rh_off;
    bool mist = outputs[Humidifier];
    if (overrides[Humidifier].mode == ModeAuto)
      mist = mist ? humidity < hi : humidity < lo;
    else
      mist = overrides[Humidifier].mode == ModeOn;
    // Minimum OFF time protects restarts in every mode; shutdown never waits.
    if (mist && !outputs[Humidifier] &&
        uint32_t(now - lastOff[Humidifier]) < uint32_t(config.min_off_seconds) * 1000)
      mist = false;
    set(Humidifier, mist, now);
  }

private:
  uint32_t lastOff[outputCount] = {}, onAt[outputCount] = {};
  uint32_t fanCycleStart = 0;
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
