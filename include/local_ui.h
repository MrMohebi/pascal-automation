#pragma once
#include "board_pins.h"
#include "control.h"
#include <Adafruit_SSD1306.h>

// Read-only display. All settings and light selections come from the web dashboard.
class LocalUI {
public:
  Adafruit_SSD1306 display{128, 64, &Wire, -1};
  bool present = false;
  void begin() {
    Wire.begin(boardpins::oledSda, boardpins::oledScl);
    Wire.setTimeOut(25);
    for (uint8_t address : {uint8_t(0x3c), uint8_t(0x3d)}) {
      Wire.beginTransmission(address);
      if (Wire.endTransmission() == 0 &&
          display.begin(SSD1306_SWITCHCAPVCC, address, false, false)) {
        present = true;
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setTextWrap(false);
        display.clearDisplay();
        display.display();
        break;
      }
    }
  }
  void render(const pascal::Controller &c, uint32_t now, bool wifi, bool server, bool clock,
              bool simulated, uint32_t revision, bool storage) {
    if (!present)
      return;
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(simulated ? "PASCAL / SIMULATED" : "PASCAL / RELAYS");
    const int page = (now / 6000) % 4;
    if (page == 0 || !c.configured) {
      if (c.sampleValid && uint32_t(now - c.sampleAt) < 10000)
        display.printf("%.1f C  %.1f %%RH\n", c.temperature, c.humidity);
      else
        display.println("Sensor unavailable");
      display.printf("CMD H:%d M:%d L:%d%d\n", c.outputs[0], c.outputs[1], c.outputs[2],
                     c.outputs[3]);
      display.printf("WiFi:%s Server:%s\n", wifi ? "OK" : "--", server ? "OK" : "--");
      display.printf("%s %s\n", c.scheduleState.known ? (c.isDay ? "DAY" : "NIGHT") : "PROFILE --",
                     clock ? "Clock OK" : "Clock --");
      display.println(!clock && c.scheduleState.known ? "Using saved schedule" : c.reason);
      display.printf("Store:%s Rev:%lu\n", storage ? "OK" : "FAIL", (unsigned long)revision);
      display.println(c.configured
                          ? (c.config.enabled ? "Automation enabled" : "Automation disabled")
                          : "Setup via dashboard");
    } else if (page == 1) {
      display.println("CLIMATE: ON / OFF");
      display.printf("Day C  %.1f / %.1f\n", c.config.day_temp_on, c.config.day_temp_off);
      display.printf("Night C %.1f / %.1f\n", c.config.night_temp_on, c.config.night_temp_off);
      display.printf("Day RH %.1f / %.1f\n", c.config.day_rh_on, c.config.day_rh_off);
      display.printf("Night RH %.1f / %.1f\n", c.config.night_rh_on, c.config.night_rh_off);
      window("Day", c.config.day_start, c.config.day_end);
      display.println("Settings via web");
    } else if (page == 2) {
      display.println("LIGHTS: SAVED MODES");
      display.printf("L1 %s CMD:%d\n", mode(c.overrides[0].mode), c.outputs[2]);
      window("L1", c.config.light1_on, c.config.light1_off);
      display.printf("L2 %s CMD:%d\n", mode(c.overrides[1].mode), c.outputs[3]);
      window("L2", c.config.light2_on, c.config.light2_off);
      display.println(clock ? "Schedules: live time" : "Schedules: saved");
      display.println("ON/OFF stays saved");
    } else {
      display.println("LIMITS / FAULTS");
      display.printf("Max temp: %.1f C\n", c.config.max_temperature);
      display.printf("Min OFF: %d s\n", c.config.min_off_seconds);
      display.printf("Heat max: %d s\n", c.config.heater_max_seconds);
      display.printf("Mist max: %d s\n", c.config.humidifier_max_seconds);
      display.printf("HOT:%d H:%d M:%d\n", !!(c.faults & 1), !!(c.faults & 2), !!(c.faults & 4));
      display.println(c.faults ? "Clear fault via web" : "No latched faults");
    }
    display.display();
  }

private:
  static const char *mode(int value) { return value == -1 ? "AUTO" : value ? "ON" : "OFF"; }
  void window(const char *name, int start, int end) {
    display.printf("%s %02d:%02d-%02d:%02d\n", name, start / 60, start % 60, end / 60, end % 60);
  }
};
