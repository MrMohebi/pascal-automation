# Commissioning checklist

## Simulated stage

1. Keep all mains loads and relay inputs disconnected. Confirm `PASCAL_ENABLE_RELAYS=0` in PlatformIO.
2. Confirm `simulated:true`, a readable 128×64 display on SDA21/SCL22, and DHT22 DATA on GPIO4. Verify DHT readings against an independent instrument at the sensor location.
3. Enter actual, confirmed husbandry targets and schedules; do not use unit-test fixture values as care guidance. Test with automation disabled, then inspect simulated control decisions.
4. Disconnect the sensor: all commanded outputs should turn off. Reconnect: three consecutive valid readings are required before operation can resume.
5. Test night/day changes, midnight-crossing schedules, both saved light selections, and return to Auto. Verify cold boot without network restores the saved profile/light states after sensor recovery; establish time and verify schedules resume.
6. Test overtemperature and runtime limits using controlled test inputs away from the animal. Faults must survive reset and remain off until acknowledged with a recovered sensor and temperature below the limit.
7. Provision Wi-Fi/HTTPS and compare the board's serial status with the dashboard. Change settings and light selections from the dashboard and verify the OLED/USB status reflects the acknowledged revision. Reboot and confirm the same revision and selections return.
8. Remove the network connection without removing board power: local control must continue while the dashboard becomes offline. Restart the server and confirm existing history remains. Do not confuse a synthetic test device with the physical ESP32.

## Before any physical appliance activation

The heater was originally described as a bare element without a thermostat. On 2026-11-12 the owner confirmed an independent thermostat and overheat cutoff are now installed. An ESP32, one DHT22, and a general-purpose relay are not an independent safety system. Relay contacts can stick closed, and a valid-looking sensor reading can still be wrong. Keep the independent thermostat and overtemperature cutoff installed, with suitable sensor placement. Have the mains assembly, enclosure, grounding, strain relief, fusing, moisture protection, and load ratings checked by a qualified person.

Identify the relay board model, coil supply, HIGH/LOW trigger mode, input voltage/current, isolation arrangement, and contact ratings for each actual load (including light/humidifier inrush). Separate COM/NO/NC contacts do not establish logic-side isolation. Use normally-open load paths so de-energized relays are off, and hardware input bias/driver circuitry that keeps them off while ESP32 pins are floating during reset or power loss. Verify this with loads disconnected.

Only after those checks should a separate commissioned build explicitly set all three flags:

```ini
build_flags =
    -DPASCAL_ENABLE_RELAYS=1
    -DPASCAL_RELAY_ACTIVE_LOW=0
    -DPASCAL_HARDWARE_COMMISSIONED=1
```

The owner confirmed a 5 V, eight-channel relay module with **active-HIGH, 3.3 V-compatible inputs**, and disconnected mains appliances for the initial relay checks. The named `esp32dev-relays` environment uses these flags; GPIO LOW is OFF and GPIO HIGH (3.3 V) is ON. These values apply to this confirmed setup, not every 5 V relay board. The repository's default build remains simulated. These flags are a deliberate commissioning boundary, not a claim that software can verify installed protection. The dashboard cannot enable physical mode remotely.

Verify there is no relay pulse during power-up, reset, programming, brownout, missing sensor, missing saved schedule and invalid clock, or a watchdog reset. Confirm spare channels stay off. Measure actual appliance operation and cutoff behavior separately from GPIO/indicator state. Supervise initial operation with an independent thermometer; one ambient sensor cannot characterize the entire enclosure or basking surface.

References: [Espressif ESP32-WROOM-32 datasheet](https://documentation.espressif.com/esp32-wroom-32_datasheet_en.html), [DHT22 specifications](https://www.adafruit.com/product/385), [relay failure and selection guidance](https://tp.industry.panasonic.com/en/products/control/relay/cautions_use).
