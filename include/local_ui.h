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
  // Page 1: temperature + humidity. Page 2: Wi-Fi / web status.
  // Page 3: errors, shown only while at least one exists.
  void render(const pascal::Controller &c, uint32_t now, bool wifi, bool server, bool clock,
              bool simulated, uint32_t, bool storage) {
    if (!present)
      return;
    const char *errors[6];
    int errorCount = 0;
    if (!storage)
      errors[errorCount++] = "Storage failure";
    if (c.faults & pascal::Overheat)
      errors[errorCount++] = "Overtemperature";
    if (c.faults & pascal::HumidifierTimeout)
      errors[errorCount++] = "Humidifier runtime";
    if (!c.sensorReady(now))
      errors[errorCount++] = "Sensor (DHT22)";
    if (!c.configured)
      errors[errorCount++] = "Not set up (web)";
    else if (!clock && !c.scheduleState.known)
      errors[errorCount++] = "Clock not set";
    const int pages = errorCount ? 3 : 2;
    const int page = (now / 5000) % pages;
    display.clearDisplay();
    if (page == 0) {
      small(0, "TEMP / HUMIDITY");
      if (c.sensorReady(now)) {
        display.setTextSize(3);
        display.setCursor(0, 12);
        display.printf("%.1fC", c.temperature);
        display.setCursor(0, 36);
        display.printf("%.1f%%", c.humidity);
      } else {
        large(20, 2, "NO DATA");
      }
    } else if (page == 1) {
      small(0, "STATUS");
      display.setTextSize(2);
      display.setCursor(0, 16);
      display.printf("WiFi %s", wifi ? "OK" : "--");
      display.setCursor(0, 36);
      display.printf("Web  %s", server ? "OK" : "--");
    } else {
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.printf("ERRORS (%d)", errorCount);
      for (int i = 0; i < errorCount && i < 5; i++)
        small(11 + i * 9, errors[i]);
    }
    if (page != 0 || !c.sensorReady(now)) {
      display.setTextSize(1);
      display.setCursor(0, 56);
      display.print(simulated ? "SIM" : "");
    }
    display.setTextSize(1);
    display.setCursor(110, 0);
    display.printf("%d/%d", page + 1, pages);
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
