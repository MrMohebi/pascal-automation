# Verification

## Docker-only setup — 2026-11-12

- Updated Docker image builds the dashboard and API and includes the credential setup utility.
- Container-based setup verified password hashing, device token generation, host file ownership, mode 0600, and refusal to overwrite existing credentials. The shell wrapper preserves an existing `.env` when launched from another directory.
- Combined application/Caddy Compose configuration validates with temporary credentials.
- Container integration checks pass: dashboard, authentication, synthetic device telemetry, command acknowledgment, and history/session persistence across container replacement. Temporary containers, volumes, and credentials were removed.
- These checks ran locally. Public DNS, certificate issuance, and deployment to the owner's server remain unverified.

## Grouped wiring update — 2026-11-12

Firmware now uses layout v2 from [WIRING.md](WIRING.md), centralized in `include/board_pins.h`: keypad on J2, four adjacent future relay signals on J1, adjacent OLED signal pins on J1, and DHT22 DATA beside J1 GND.

- PlatformIO build passes for the new mapping.
- All 24 native controller scenarios, JSON serialization checks, and the simulated output-driver check pass. The simulated driver still makes zero GPIO configuration/write calls.
- The new mapping has **not been uploaded or physically verified**. The connected board retains layout v1 until the owner moves the wires. The hardware results below apply only to layout v1.
- The verified layout-v1 application, bootloader, and partition images were preserved in ignored `secrets/installed-layout-v1/` before rebuilding. This is a copy of the known installed Pascal build, not a backup of the firmware that preceded Pascal Watcher.

New layout-v2 application: `.pio/build/esp32dev/firmware.bin`, 1332208 bytes, SHA-256 `fae1c0556d164ca339616eb8c90e8c76cd2dc3aa9f7abc14f5ee72656de95039`.

## Original layout-v1 verification — 2026-11-11

## Completed software checks

- PlatformIO ESP32-WROOM-32 build passes. The simulated firmware uses 51,560 bytes of static RAM and about 1.30 MB of the 3 MB application partition.
- 24 native controller scenarios pass under address/undefined-behavior sanitizers: threshold boundaries, hysteresis, minimum OFF time, day/night switching, midnight schedules, sensor failure/recovery/staleness, clock loss, runtime latches, overtemperature acknowledgment, override expiry, restart behavior, and millisecond rollover.
- Simulated output-driver test confirms **zero GPIO configuration/write calls**, even when all four commanded outputs are ON.
- Firmware JSON tests pass: settings round-trip, required fields, strict integer schedule values, and rejection of strings in numeric fields.
- 20 backend tests pass: authentication/CSRF, login rate limiting, device token, online/offline state, pending acknowledgments, conflicting revisions, reboot cancellation, expiry, validation, override interlocks, retention, history gaps, and persistence.
- All 7 Playwright browser tests pass against the real API with a **synthetic test device**: desktop/mobile layout, charts, pending light commands, acknowledged settings, conflicting local edits, offline behavior, server disconnection, and fault acknowledgment.
- The production React/TypeScript build passes. Desktop and 390-pixel mobile screenshots were visually inspected.
- Docker image `pascal-watcher:local` builds and passes an HTTP integration test. Login, static dashboard, telemetry, command acknowledgment, and history/session persistence were checked across container replacement using the same named volume. Disposable test containers/volumes were removed.
- The optional Caddy HTTPS configuration validates. Public DNS, certificate issuance, and a remote server deployment have not been exercised.
- Frontend `npm audit` and backend `pip-audit` reported no known vulnerabilities after updating dependencies.

## Hardware scope

The USB-connected device was identified as ESP32-D0WD-V3 revision 3.1, 40 MHz, using the CP2102 serial bridge at `/dev/cu.usbserial-0001`.

The owner initially left the peripherals unwired, then connected the DHT22, OLED, and keypad according to the wiring map during the USB retry. Real sensor readings, OLED detection, and storage health were verified over USB after successful flashing. The owner also confirmed the OLED text is readable and all keypad keys behave correctly in the menu/edit/cancel check. Wi-Fi credentials, server endpoint/CA, and husbandry settings have not been provisioned. No physical Wi-Fi outage test or real sensor-to-server flow has been claimed.

The relay board is described as eight separate COM/NO/NC channels; its input polarity, supply, logic compatibility, and load ratings remain unverified. Four channels are reserved. No mains appliances or relay inputs were activated.

USB flash operations encountered intermittent serial corruption and a disappearing/reappearing device. Only the original partition table was successfully backed up; **no complete original firmware backup exists**. See the final USB result below before relying on the board state.

## Final USB result

**The firmware is now successfully installed and running.** After reconnection, the ordinary 115200-baud application upload reached 74% before another serial read error. The bootloader, partition table, and OTA boot-data image had already been hash-verified. Recovery used the ROM loader at 57600 baud to write the application in eleven sector-aligned blocks (ten of 131,072 bytes and one of 21,488 bytes), retrying failed blocks and verifying each successful write. A final `verify-flash` against the entire application at `0x10000` succeeded with a matching digest.

Installed layout-v1 application (preserved copy): `secrets/installed-layout-v1/firmware.bin` (1,332,208 bytes), SHA-256 `6077f9995fdf3df46b88d4d3a05602a8f9f7d9f42aea7bf8c0d63a983bdddf34`. Per-block logs and the full-image verification are in the ignored `secrets/flash-chunks/` directory. No complete original firmware backup was recovered. Intermittent transfer errors remain an observed USB-link limitation; the successful full-image verification establishes the final installed bytes despite those retries.

USB startup checks passed:

- Five real snapshots over approximately 20 seconds retained the same boot ID, with uptime increasing from 9,500 to 29,716 ms. No unexpected reset occurred during that observation.
- DHT22 reported 25.7 °C and 36.0–36.5% RH, with `sensor_ok:true`. These are actual readings, not calibration against an independent instrument.
- `display_ok:true` and `storage_ok:true`; no latched faults.
- `simulated:true`, `config:null`, revision 0, and all four commanded outputs OFF throughout.
- Each real snapshot passed the backend telemetry schema validation.
- Invalid clock input was rejected; setting current Tehran time through the USB command succeeded, with `clock_ok:true` and outputs still OFF.
- The owner confirmed readable OLED text and correct keypad behavior using D, #, digits 1234567890, *, C/A/B, and * to exit. This canceled the draft without saving settings.

The raw smoke-test snapshots are in the ignored `secrets/usb-smoke.json`. Opening the serial port can reset this dev board; uptime comparisons were deliberately made on one continuously open connection. Wi-Fi/server provisioning, actual husbandry settings, independent measurement checks, and physical relay commissioning remain separate from these USB checks.

Dashboard screenshots use synthetic test readings: [desktop](screenshots/dashboard-desktop.png), [mobile](screenshots/dashboard-mobile.png).

## Remaining commissioning work

Follow [wiring](WIRING.md), [deployment](DEPLOYMENT.md), and [commissioning](COMMISSIONING.md). Hardware-dependent checks remain separate from passing software tests. The default firmware cannot energize relay pins; physical operation requires the documented hardware protection and a deliberately commissioned build.
