# Low-voltage wiring

Disconnect USB/power while wiring. Keep relay input wiring and all mains appliances disconnected during the initial simulated stage.

## Layout v2 — grouped wiring (2026-11-12)

**This layout needs the updated firmware. The board currently runs layout v1.** Disconnect all power, move the peripheral wires using the table below, and then upload the new build before testing. Keep relay inputs disconnected; their assignments are for later commissioning.

J1/J2 and position numbers refer to the **19-pin headers in your supplied diagram**. Match the GPIO labels on the actual board; the diagram does not establish USB orientation. All firmware assignments live in [`include/board_pins.h`](../include/board_pins.h).

| Device connection | New GPIO / power | Header position | Previous GPIO (v1) |
|---|---|---|---|
| Future heater IN | 32 | J1-7 | 25 |
| Future humidifier IN | 33 | J1-8 | 26 |
| Future light 1 IN | 25 | J1-9 | 27 |
| Future light 2 IN | 26 | J1-10 | 23 |
| OLED SCL | 27 | J1-11 | 22 |
| OLED SDA | 14 | J1-12 | 21 |
| DHT22 DATA | 13 | J1-15 | 4 |
| Keypad R1 | 23 | J2-2 | 13 |
| Keypad R2 | 22 | J2-3 | 14 |
| Keypad R3 | 21 | J2-6 | 16 |
| Keypad R4 | 19 | J2-8 | 17 |
| Keypad C1 | 18 | J2-9 | 18 (unchanged) |
| Keypad C2 | 17 | J2-11 | 19 |
| Keypad C3 | 16 | J2-12 | 32 |
| Keypad C4 | 4 | J2-13 | 33 |
| OLED VCC and DHT22 VCC | Shared 3V3 rail | From J1-1 | 3V3 (unchanged) |
| OLED GND and DHT22 GND | Shared GND rail | From J1-14 | GND (unchanged) |

Use a breadboard power rail or insulated distribution connector to share 3V3 and GND between the sensor and display. Confirm the OLED supports 3.3 V power. A passive keypad does not need a power or ground wire.

### Where the wire groups sit

Read top to bottom in the same order as your diagram. Header position and GPIO number are different things:

```text
        J1 (left header)                  J2 (right header)
Pos   Label   Connect to          Pos   Label   Connect to
 1    3V3     Shared 3V3 rail       1    GND     --
 2    EN      --                   2    IO23    Keypad R1
 3    SVP     --                   3    IO22    Keypad R2
 4    SVN     --                   4    U0TX    -- USB serial
 5    IO34    --                   5    U0RX    -- USB serial
 6    IO35    --                   6    IO21    Keypad R3
 7    IO32    Future heater        7    GND     --
 8    IO33    Future humidifier    8    IO19    Keypad R4
 9    IO25    Future light 1       9    IO18    Keypad C1
10    IO26    Future light 2      10    IO5     -- boot strap
11    IO27    OLED SCL            11    IO17    Keypad C2
12    IO14    OLED SDA            12    IO16    Keypad C3
13    IO12    -- boot strap       13    IO4     Keypad C4
14    GND     Shared GND rail     14    IO0     -- boot strap
15    IO13    DHT22 DATA          15    IO2     -- boot strap
16    SD2     -- flash            16    IO15    -- boot strap
17    SD3     -- flash            17    SD1     -- flash
18    CMD     -- flash            18    SD0     -- flash
19    5V      --                  19    CLK     -- flash
```

The four relay signal pins are consecutive, the OLED signal pins are adjacent, and the DHT22 data pin is beside GND. All eight keypad wires stay on J2, with gaps to avoid UART0, GND, and GPIO5. An uninterrupted eight-pin keypad block would require different hardware/wiring compromises.

These assignments avoid boot-strapping pins (0, 2, 5, 12, 15), UART0 (1, 3), flash pins (6–11), and input-only pins (34–39, which also lack internal pull-ups). GPIO16/17 are available on this **WROOM-32** module; do not copy this map to a PSRAM-equipped variant without checking its pin use. See [Espressif GPIO documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/gpio.html) and [ESP32-WROOM-32 datasheet](https://documentation.espressif.com/esp32-wroom-32_datasheet_en.html).

### Moving from the working v1 wiring

1. Unplug USB and every other power source. Leave the relay board inputs and mains appliances disconnected.
2. Label the existing keypad wires R1–R4 and C1–C4 using the previous GPIO column above, before removing them. The existing ribbon order was already verified; retain those row/column identities.
3. Move each peripheral's signal wires to the new GPIOs. Keep the DHT pull-up connected between its DATA wire and 3V3; move it too if it was attached directly at the old GPIO4 location.
4. Recheck VCC/GND and skipped header positions. Tell the operator doing the USB upload that the wiring now uses **layout v2**. Upload the v2 build before testing the peripherals; the old firmware will not match these wires.
5. After upload, verify fresh DHT22 readings, readable OLED text, and every keypad key again. Previous hardware checks only verified layout v1.

## Peripheral details

A bare four-pin DHT22 needs a DATA pull-up, normally 4.7–10 kΩ to **3V3**. Verify the sensor's labelled pinout/datasheet before connecting it; the unused pin remains unconnected. Three-pin breakout boards may already include the pull-up. Keep the sensor clear of direct mist, heater radiation, and the ESP32's own heat. A single ambient sensor does not measure the animal's basking surface or every point in the enclosure.

The OLED is assumed to use SSD1306. Firmware probes I²C addresses `0x3C` and `0x3D`; detection alone does not prove controller compatibility. Verify readable text. Keep I²C pull-ups at 3.3 V, even if a module advertises 5 V power compatibility.

The keypad is a passive matrix: it has no VCC/GND connection. Its ribbon order varies. Identify rows and columns using a continuity meter and confirm all 16 keys individually. The expected map is:

```text
1 2 3 A
4 5 6 B
7 8 9 C
* 0 # D
```

Each relay's COM/NO/NC terminals are separate switch contacts. This does **not** determine whether its IN input is active-high or active-low, or whether the board's logic/power grounds are isolated. The board model, input circuit, coil supply, current ratings, and load types must be checked before connecting GPIOs. Never power relay coils from ESP32 GPIO pins or assume the ESP32 3V3 supply can power eight relays. Never connect 5 V to an ESP32 GPIO.

The four spare channels have no assigned GPIO and must remain disconnected/disabled. This document intentionally covers low-voltage assembly; mains installation requires the separate commissioning checks.
