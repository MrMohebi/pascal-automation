#pragma once
#include "board_pins.h"
#include "control.h"
#include <Adafruit_SSD1306.h>
#include <Keypad.h>
#include <functional>

struct Field {
  const char *name;
  const char *key;
  int kind;
  double pascal::Config::*decimal;
  int pascal::Config::*integer;
};
// kind: 0 tenths, 1 minute-of-day, 2 seconds, 3 enabled.
static const Field fields[] = {
    {"Day heat ON", "day_temp_on", 0, &pascal::Config::day_temp_on, nullptr},
    {"Day heat OFF", "day_temp_off", 0, &pascal::Config::day_temp_off, nullptr},
    {"Night heat ON", "night_temp_on", 0, &pascal::Config::night_temp_on, nullptr},
    {"Night heat OFF", "night_temp_off", 0, &pascal::Config::night_temp_off, nullptr},
    {"Day RH ON", "day_rh_on", 0, &pascal::Config::day_rh_on, nullptr},
    {"Day RH OFF", "day_rh_off", 0, &pascal::Config::day_rh_off, nullptr},
    {"Night RH ON", "night_rh_on", 0, &pascal::Config::night_rh_on, nullptr},
    {"Night RH OFF", "night_rh_off", 0, &pascal::Config::night_rh_off, nullptr},
    {"Max temperature", "max_temperature", 0, &pascal::Config::max_temperature, nullptr},
    {"Day starts", "day_start", 1, nullptr, &pascal::Config::day_start},
    {"Night starts", "day_end", 1, nullptr, &pascal::Config::day_end},
    {"Light 1 ON", "light1_on", 1, nullptr, &pascal::Config::light1_on},
    {"Light 1 OFF", "light1_off", 1, nullptr, &pascal::Config::light1_off},
    {"Light 2 ON", "light2_on", 1, nullptr, &pascal::Config::light2_on},
    {"Light 2 OFF", "light2_off", 1, nullptr, &pascal::Config::light2_off},
    {"Minimum OFF sec", "min_off_seconds", 2, nullptr, &pascal::Config::min_off_seconds},
    {"Heater max sec", "heater_max_seconds", 2, nullptr, &pascal::Config::heater_max_seconds},
    {"Humidifier max sec", "humidifier_max_seconds", 2, nullptr,
     &pascal::Config::humidifier_max_seconds},
    {"Automation 0/1", "enabled", 3, nullptr, nullptr}};
constexpr int fieldCount = sizeof(fields) / sizeof(fields[0]);

class LocalUI {
public:
  Adafruit_SSD1306 display{128, 32, &Wire, -1};
  std::function<bool(const pascal::Config &, uint32_t)> save;
  std::function<bool(int, int)> light;
  std::function<bool()> acknowledge;
  std::function<bool(const String &)> setClock;
  bool present = false;
  void begin() {
    Wire.begin(boardpins::oledSda, boardpins::oledScl);
    Wire.setTimeOut(25);
    for (uint8_t address : {uint8_t(0x3c), uint8_t(0x3d)}) {
      Wire.beginTransmission(address);
      if (Wire.endTransmission() == 0 &&
          display.begin(SSD1306_SWITCHCAPVCC, address, false, false)) {
        present = true;
        break;
      }
    }
    if (present) {
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.clearDisplay();
      display.display();
    }
  }
  void key(char k, const pascal::Controller &c, uint32_t revision) {
    if (!k)
      return;
    if (!menu) {
      if (k == 'D') {
        menu = true;
        draft = c.config;
        baseRevision = revision;
        index = 0;
        editing = false;
        note = "";
      } else if (k == 'A' || k == 'B' || k == 'C')
        page = (page + 1) % 3;
      return;
    }
    if (k == '*') {
      if (editing) {
        draft = editBackup;
        editing = false;
        input = "";
      } else
        menu = false;
      return;
    }
    if (editing) {
      if (k >= '0' && k <= '9' && input.length() < 12)
        input += k;
      else if (k == 'A' || k == 'B') {
        if (index < fieldCount) {
          auto &f = fields[index];
          int step = k == 'A' ? 1 : -1;
          if (f.kind == 0)
            draft.*(f.decimal) += step * .1;
          else if (f.kind == 3)
            draft.enabled = !draft.enabled;
          else
            draft.*(f.integer) += step;
          input = "";
        }
      } else if (k == '#' || k == 'C') {
        if (index < fieldCount && input.length()) {
          auto &f = fields[index];
          long v = input.toInt();
          if (f.kind == 0)
            draft.*(f.decimal) = v / 10.0;
          else if (f.kind == 1) {
            if (input.length() != 4 || v / 100 > 23 || v % 100 > 59) {
              note = "Use HHMM";
              return;
            }
            draft.*(f.integer) = (v / 100) * 60 + v % 100;
          } else if (f.kind == 2) {
            if (v > 86400) {
              note = "Value too large";
              return;
            }
            draft.*(f.integer) = v;
          } else {
            if (v > 1) {
              note = "Use 0 or 1";
              return;
            }
            draft.enabled = v == 1;
          }
        } else if (index == fieldCount + 3) {
          note = setClock(input) ? "Clock set" : "Invalid date/time";
        }
        editing = false;
        input = "";
        if (k == 'C')
          index = (index + 1) % (fieldCount + 5);
      }
      return;
    }
    if (k == 'A')
      index = (index + fieldCount + 4) % (fieldCount + 5);
    if (k == 'B' || k == 'C')
      index = (index + 1) % (fieldCount + 5);
    if (k == '#') {
      note = "";
      if (index < fieldCount || index == fieldCount + 3) {
        editBackup = draft;
        editing = true;
        input = "";
      } else if (index == fieldCount) {
        note = save(draft, baseRevision) ? "Settings saved" : "Invalid/conflict";
        if (note == "Settings saved")
          baseRevision = revision + 1;
      } else if (index == fieldCount + 1 || index == fieldCount + 2) {
        int n = index - fieldCount - 1;
        int mode = c.overrides[n].mode;
        int next = mode == -1 ? 1 : mode == 1 ? 0 : -1;
        note = light(n, next) ? "Light updated" : "Blocked by interlock";
      } else if (index == fieldCount + 4)
        note = acknowledge() ? "Faults cleared" : "Fault still active";
    }
  }
  void render(const pascal::Controller &c, uint32_t now, bool wifi, bool server, bool clock,
              bool simulated) {
    if (!present)
      return;
    display.clearDisplay();
    display.setCursor(0, 0);
    if (menu) {
      if (index < fieldCount) {
        const auto &f = fields[index];
        display.println(f.name);
        if (input.length())
          display.println(input);
        else if (f.kind == 0)
          display.println(draft.*(f.decimal), 1);
        else if (f.kind == 1) {
          int v = draft.*(f.integer);
          display.printf("%02d:%02d\n", v / 60, v % 60);
        } else if (f.kind == 2)
          display.println(draft.*(f.integer));
        else
          display.println(draft.enabled ? "ON" : "OFF");
        display.println(editing ? (f.kind == 0 ? "245 = 24.5" : "# accept  * cancel")
                                : "# edit  C next");
      } else {
        const char *extra[] = {"SAVE ALL SETTINGS", "LIGHT 1 AUTO/ON/OFF", "LIGHT 2 AUTO/ON/OFF",
                               "CLOCK YYYYMMDDHHMM", "ACKNOWLEDGE FAULTS"};
        display.println(extra[index - fieldCount]);
        display.println(editing ? input : "# select");
        display.println("A/B move  * back");
      }
      display.println(note);
    } else if (page == 0) {
      display.println(simulated ? "PASCAL / SIMULATED" : "PASCAL / RELAYS");
      if (c.sampleValid && uint32_t(now - c.sampleAt) < 10000)
        display.printf("%.1f C   %.1f %%RH\n", c.temperature, c.humidity);
      else
        display.println("Sensor unavailable");
      display.printf("H:%d M:%d L:%d%d\n", c.outputs[0], c.outputs[1], c.outputs[2], c.outputs[3]);
      display.println(c.reason);
    } else if (page == 1) {
      display.printf("WiFi:%s Server:%s\n", wifi ? "OK" : "--", server ? "OK" : "--");
      display.printf("Clock:%s %s\n", clock ? "OK" : "--", c.isDay ? "DAY" : "NIGHT");
      display.printf("Sensor ready: %s\n", c.sensorReady(now) ? "YES" : "NO");
      display.println("D menu  A next");
    } else {
      display.println("FAULTS / COMMANDED");
      display.printf("Heat:%d Run H:%d M:%d\n", !!(c.faults & 1), !!(c.faults & 2),
                     !!(c.faults & 4));
      display.println("No appliance feedback");
      display.println("D menu  A next");
    }
    display.display();
  }

private:
  bool menu = false, editing = false;
  int index = 0, page = 0;
  uint32_t baseRevision = 0;
  pascal::Config draft, editBackup;
  String input, note;
};
