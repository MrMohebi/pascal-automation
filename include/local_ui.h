#pragma once
#include "board_pins.h"
#include "control.h"
#include <Adafruit_SSD1306.h>

// Read-only OLED: large values, one topic per page, automatic rotation.
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
        display.setTextColor(SSD1306_WHITE);
        display.setTextWrap(false);
        display.clearDisplay();
        display.display();
        break;
      }
    }
  }
  void render(const pascal::Controller &c, uint32_t now, bool wifi, bool server, bool clock,
              bool simulated, uint32_t, bool storage) {
    if (!present)
      return;
    display.clearDisplay();
    const int pages = c.configured && c.scheduleState.known ? 7 : 5;
    const int page = (now / 5000) % pages;
    if (!storage || c.faults) {
      // Faults interrupt the carousel; do not hide them on a later page.
      small(0, "OUTPUTS STOPPED");
      large(20, 2, "FAULT");
      small(46, !storage         ? "Storage failure"
                : (c.faults & 1) ? "Overtemperature"
                : (c.faults & 2) ? "Heater runtime"
                                 : "Humidifier runtime");
      small(56, "Check web dashboard");
    } else {
      if (page == 0 || page == 1) {
        small(0, page == 0 ? "TEMPERATURE" : "HUMIDITY");
        display.setCursor(0, 19);
        if (c.sensorReady(now)) {
          display.setTextSize(3);
          if (page == 0)
            display.printf("%.1fC", c.temperature);
          else
            display.printf("%.1f%%", c.humidity);
        } else {
          display.setTextSize(2);
          display.print("NO DATA");
        }
        small(46, !c.sensorReady(now)      ? "Check DHT22"
                  : !c.configured          ? "Setup via web"
                  : !c.config.enabled      ? "Automation OFF"
                  : !c.scheduleState.known ? "Waiting for clock"
                  : !clock                 ? "Using saved profile"
                  : c.isDay                ? "Day profile"
                                           : "Night profile");
      } else if (page == 2) {
        small(0, "CLIMATE / COMMANDED");
        display.setTextSize(2);
        display.setCursor(0, 16);
        display.printf("Heat %s", c.outputs[0] ? "ON" : "OFF");
        display.setCursor(0, 36);
        display.printf("Mist %s", c.outputs[1] ? "ON" : "OFF");
      } else if (page == 3) {
        small(0, "LIGHTS / COMMANDED");
        display.setTextSize(2);
        display.setCursor(0, 16);
        display.printf("L1 %s", c.outputs[2] ? "ON" : "OFF");
        display.setCursor(0, 36);
        display.printf("L2 %s", c.outputs[3] ? "ON" : "OFF");
      } else if (page == 4) {
        small(0, "CONNECTION");
        display.setTextSize(2);
        display.setCursor(0, 16);
        display.printf("WiFi %s", wifi ? "OK" : "--");
        display.setCursor(0, 36);
        display.printf("Web  %s", server ? "OK" : "--");
      } else {
        const bool temperature = page == 5;
        small(0, temperature ? "ACTIVE TEMP TARGETS" : "ACTIVE RH TARGETS");
        const double on = temperature ? (c.isDay ? c.config.day_temp_on : c.config.night_temp_on)
                                      : (c.isDay ? c.config.day_rh_on : c.config.night_rh_on);
        const double off = temperature ? (c.isDay ? c.config.day_temp_off : c.config.night_temp_off)
                                       : (c.isDay ? c.config.day_rh_off : c.config.night_rh_off);
        display.setTextSize(1);
        display.setCursor(0, 20);
        display.print("ON <");
        display.setCursor(0, 40);
        display.print("OFF>=");
        display.setTextSize(2);
        display.setCursor(36, 16);
        display.printf("%.1f%s", on, temperature ? "C" : "%");
        display.setCursor(36, 36);
        display.printf("%.1f%s", off, temperature ? "C" : "%");
      }
      display.setTextSize(1);
      display.setCursor(0, 56);
      display.printf("%s %s", simulated ? "SIM" : "LIVE",
                     c.scheduleState.known ? (c.isDay ? "DAY" : "NIGHT") : "");
      display.setCursor(110, 56);
      display.printf("%d/%d", page + 1, pages);
    }
    display.display();
  }

private:
  void small(int y, const char *text) {
    display.setTextSize(1);
    display.setCursor(0, y);
    display.print(text);
  }
  void large(int y, int size, const char *text) {
    display.setTextSize(size);
    display.setCursor(0, y);
    display.print(text);
  }
};
