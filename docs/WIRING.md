# Low-voltage wiring — layout v3

This is the owner's installed wiring for the **ESP32-WROOM-32**. The keypad has been removed. The display is now **128×64**, assumed SSD1306 I²C. The firmware probes addresses `0x3C` and `0x3D`; confirm readable text because an address response alone does not identify the controller.

| Connection | ESP32 GPIO / supply | Header position in supplied diagram |
|---|---|---|
| DHT22 DATA | **4** | J2-13 |
| OLED SDA | **21** | J2-6 |
| OLED SCL | **22** | J2-3 |
| Relay 1 — heater | **25** | J1-9 |
| Relay 2 — humidifier | **26** | J1-10 |
| Relay 3 — light 1 | **27** | J1-11 |
| Relay 4 — light 2 | **14** | J1-12 |
| DHT22 VCC / OLED VCC | 3V3 | J1-1, shared rail |
| DHT22 GND / OLED GND | GND | Any labelled GND, shared rail |

Firmware assignments are centralized in [`include/board_pins.h`](../include/board_pins.h). Use GPIO labels rather than counting pins from an assumed USB orientation. The other four relay channels have no assigned GPIO.

Disconnect all power before changing wiring. The default firmware still simulates relay commands and does not configure or write the relay GPIOs. The assigned ports do not establish input polarity, logic compatibility, or heater protection; see [commissioning](COMMISSIONING.md) before physical switching.

A bare four-pin DHT22 needs a DATA pull-up, normally 4.7–10 kΩ to **3V3**. Verify its labelled pinout before connecting it; the unused sensor pin stays disconnected. Breakout boards may include the pull-up. Keep the sensor away from direct mist, heater radiation, and ESP32 heat. A single sensor does not measure every point in the enclosure.

Confirm the OLED accepts 3.3 V power and its I²C pull-ups go to 3.3 V. Never connect a 5 V signal to an ESP32 GPIO. Relay coil power and input isolation depend on the relay board; do not power relay coils from GPIOs or assume ESP32 3V3 can power them.

The older layout-v1/v2 maps are superseded. Upload the current layout-v3 firmware for these connections. Prior OLED/keypad hardware checks apply to the old display and wiring only.
