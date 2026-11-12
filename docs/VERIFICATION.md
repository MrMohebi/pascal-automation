# Verification

## Password-only setup and open device sync — 2026-11-12

- The only required environment setting is now `ADMIN_PASSWORD`. Setup and dashboard login ask only for a password. Existing password hashes remain accepted for upgrades; `ADMIN_PASSWORD` takes precedence.
- Removed device-token authentication from sync, the USB provisioning utility, and firmware. Older devices can continue sending a token; it is ignored. Data validation, settings revisions, persistent state, and safety interlocks remain active.
- 30 API tests passed, including password-only environment startup, missing/invalid password handling, legacy hash migration, token-free sync, and rejection of malformed telemetry. All eight browser tests passed with password-only login.
- Built the Docker image and passed real container checks for login, token-free device sync, configuration acknowledgment, and history/session persistence across replacement.
- Tested the setup utility's generated `.env` through real Compose containers with spaces, dollar signs, single/double quotes, backslashes, trailing backslashes, and non-ASCII passwords; values reached the container unchanged and files had mode 0600. Temporary containers and credentials were removed.
- PlatformIO firmware build passed. This change was not flashed to the connected ESP32, and no production server was deployed.


## Single Compose file using GHCR — 2026-11-12

- `compose.yaml` now contains one service using `ghcr.io/mrmohebi/pascal-automation:latest`, port 8000, `.env`, restart policy, and the existing `pascal_data` volume. The separate HTTPS Compose file and Caddy configuration were removed; the owner selected an existing reverse proxy.
- Credential generation now creates only admin username, password hash, and device token. Verified the password hash, mode 0600, and refusal to overwrite an existing `.env`.
- An isolated copy of the actual Compose file ran the published image successfully: dashboard, login, authenticated status, and session/database persistence across container recreation passed. The disposable local test used a random loopback port and disabled secure cookies only in its temporary `.env`; production secure-cookie defaults and the owner's HTTPS proxy were not changed or tested.
- Temporary containers, volume, and credentials were removed. No production server deployment was performed.


## GPIO4 correction and larger OLED pages — 2026-11-12

- The owner corrected the sensor connection to GPIO4 (J2-13) and confirmed the 128×64 OLED is readable. Wiring documentation and firmware now match GPIO4.
- OLED content is reduced to separate, automatically rotating pages every five seconds. Temperature/humidity readings use text size 3; output states and active targets use size 2. Detailed schedules and limits remain on the web dashboard. Faults take priority over normal pages.
- Before configuration and a known profile, five status pages rotate. The two active-target pages are added once settings/profile are available. No keypad is required.
- The final large-text firmware was flashed and its entire image verified: 1,330,912 bytes, SHA-256 `dddfcbea396dc0dfba2b2986696addf14945b647b30b692b1f489a43d426c3d5`. A compressed USB transfer failed; all eleven fallback ROM-loader blocks verified. Private evidence is under `secrets/flash-large-oled/`.
- PlatformIO build passes. The GPIO4-only intermediate image initially returned no sensor readings, so a power/ground/pull-up check was requested. After the final display update, three consecutive USB snapshots reported healthy DHT22 readings of 26.0–26.1 °C and 33.8–33.9% RH, healthy storage, and OLED detection. Uptime increased from 9,497 to 14,616 ms with one boot ID; all outputs remained simulated and OFF. Snapshots passed server schema validation and are saved privately in `secrets/usb-large-oled.json`. The sensor check is now resolved; these readings are not independent calibration.


## Web-only controller, layout v3 — 2026-11-12

- Removed keypad dependency, GPIO scanning, and menus. OLED is 128×64, SDA21/SCL22; DHT22 DATA17; heater/humidifier/light1/light2 use 25/26/27/14. Relay switching remains simulated.
- Firmware build passes. All 27 controller scenarios and configuration JSON checks pass; the simulated driver makes zero GPIO calls.
- Host tests exercise the actual storage serialization/load/save code with a Preferences substitute: recovery across new controller/storage instances, persistent modes, cached day/night and automatic light states, sensor recovery, failed writes, corrupt records, and migration from legacy settings. This is not a physical power-cut test of ESP32 flash.
- All 24 backend tests and 8 browser tests pass, including persistent light acknowledgment/revision checking, stale commands, cached-clock alerts, and a synthetic device restart with saved light selection.
- Production web and Docker builds pass. Container integration checks pass for authentication, device sync, acknowledgments, and database/session persistence after container replacement. Mobile layout was visually inspected.
- Application upload completed in eleven verified ROM-loader blocks at 57600 baud; full-image digest matched. The board had an older bootloader and 1280 KiB app partition, which rejected the new image. Installing the matching bootloader, 3 MiB app partition table, and OTA boot metadata resolved the boot loop. NVS remains at 0x9000 with the same 20 KiB size; it was not erased.
- Initial layout-v3 application: 1,330,976 bytes, SHA-256 `74c7027cade8139fa9557ef685902bcb4f618495509971cb04db3c53f26fb131`. Private logs/images are in `secrets/flash-layout-v3/`.
- USB reports the new persistent-light capability, healthy storage, OLED detection, unconfigured settings, simulated mode, and all outputs OFF. At the first post-upload check DHT22 readings were unavailable on GPIO17; the owner was asked to check its wiring. The owner subsequently confirmed the 128×64 OLED text is readable and clarified that DHT22 DATA is actually connected to GPIO4; see the correction above.
- Real server provisioning, configured-device power-loss recovery, Wi-Fi loss/reconnect, and physical appliance switching remain unverified. Host storage tests and synthetic-device browser tests do not establish those physical results.


## Docker-only setup — 2026-11-12

- Updated Docker image builds the dashboard and API and includes the credential setup utility.
- Container-based setup verified password hashing, device token generation, host file ownership, mode 0600, and refusal to overwrite existing credentials. The shell wrapper preserves an existing `.env` when launched from another directory.
- Combined application/Caddy Compose configuration validates with temporary credentials.
- Container integration checks pass: dashboard, authentication, synthetic device telemetry, command acknowledgment, and history/session persistence across container replacement. Temporary containers, volumes, and credentials were removed.
- These checks ran locally. Public DNS, certificate issuance, and deployment to the owner's server remain unverified.

## Historical grouped wiring update (superseded) — 2026-11-12

At this stage firmware used layout v2, centralized in `include/board_pins.h`: keypad on J2, four adjacent future relay signals on J1, adjacent OLED signal pins on J1, and DHT22 DATA beside J1 GND.

- PlatformIO build passes for the new mapping.
- All 24 native controller scenarios, JSON serialization checks, and the simulated output-driver check pass. The simulated driver still makes zero GPIO configuration/write calls.
- Layout v2 was built but never uploaded or physically verified. The board still ran layout v1 at this stage. The hardware results below apply only to layout v1.
- The verified layout-v1 application, bootloader, and partition images were preserved in ignored `secrets/installed-layout-v1/` before rebuilding. This is a copy of the known installed Pascal build, not a backup of the firmware that preceded Pascal Watcher.

Historical layout-v2 application (the build path has since been overwritten): 1332208 bytes, SHA-256 `fae1c0556d164ca339616eb8c90e8c76cd2dc3aa9f7abc14f5ee72656de95039`.

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

**Layout-v1 firmware was successfully installed and running at this stage.** After reconnection, the ordinary 115200-baud application upload reached 74% before another serial read error. The bootloader, partition table, and OTA boot-data image had already been hash-verified. Recovery used the ROM loader at 57600 baud to write the application in eleven sector-aligned blocks (ten of 131,072 bytes and one of 21,488 bytes), retrying failed blocks and verifying each successful write. A final `verify-flash` against the entire application at `0x10000` succeeded with a matching digest.

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
