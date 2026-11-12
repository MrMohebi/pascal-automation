# Pascal Watcher

ESP32 terrarium monitoring and control, with a 128×32 OLED, 4×4 keypad, and a private web dashboard.

**The shipped firmware is simulated: it never configures or writes the four relay GPIOs.** The DHT22, OLED, keypad, storage, Wi-Fi, and HTTPS synchronization are real. No husbandry targets are included. Four of the eight relay channels are reserved and have no assigned GPIOs.

## Start here

**Wiring update (2026-11-12):** source now uses grouped layout v2. The installed board still uses layout v1; move the wires using the [migration table](docs/WIRING.md#layout-v2--grouped-wiring-2026-11-12) before uploading this build.

1. Follow [low-voltage wiring](docs/WIRING.md). Leave every mains appliance disconnected during assembly and tests.
2. Build and upload using PlatformIO:

   ```sh
   pio run
   pio run -t upload --upload-port /dev/cu.usbserial-0001
   ```

   If `pio` is not on PATH on this Mac, use `~/.platformio/penv/bin/pio`. The firmware uses a 3 MB application partition on a 4 MB board; USB updates only, no OTA. Platform/framework and direct library versions are pinned in `platformio.ini`.

3. Follow the [Docker-only server quick start](docs/DEPLOYMENT.md#quick-start-with-docker-only). Run `sh tools/setup_server_docker.sh` on your server to build the image and create credentials, then start the dashboard behind HTTPS using Compose. Host Python and Node are not required.
4. Provision the board from your laptop:

   ```sh
   python3 -m venv .venv
   .venv/bin/pip install -r tools/requirements.txt
   .venv/bin/python tools/device.py --port /dev/cu.usbserial-0001 provision --ca /path/to/server-root-ca.pem --env-file .env
   ```

   The utility prompts locally for your 2.4 GHz Wi-Fi name/password and HTTPS URL. It does not print credentials. The CA must match your server's certificate chain. Wi-Fi credentials, CA, and token are saved in ESP32 NVS; they are not encrypted against physical flash access. Close the PlatformIO serial monitor before using the utility.

5. Check device status:

   ```sh
   .venv/bin/python tools/device.py --port /dev/cu.usbserial-0001 status
   ```

6. Enter your confirmed settings from the dashboard or keypad. Save with automation disabled first; then enable it to inspect **simulated commanded states**.

## Operation

- DHT22 reads every 2.5 seconds. Three consecutive valid readings are required. Any invalid reading, or ten seconds without a fresh sample, inhibits all four outputs.
- Heater and humidifier turn ON strictly below their ON thresholds and OFF at or above their OFF thresholds. Within the band they retain their previous states. Minimum OFF time applies to restart; shutdown never waits for a minimum run time.
- Day/night climate profiles and both light schedules support midnight crossings. Equal ON/OFF times are rejected rather than interpreted as always-on.
- A light override expires at its next schedule transition, with a monotonic deadline as protection against a backward clock adjustment. Select Auto to clear it earlier. Overrides are not restored after reboot.
- Overtemperature or either maximum runtime latches **all outputs OFF**. Faults survive reboot. Acknowledge only after sensor recovery and temperature is below the configured maximum; a subsequent unchanged problem can trip again.
- Network failures do not interrupt local control. After a cold power loss, automatic outputs wait for NTP or manually entered time. There is no battery-backed clock in this hardware. Day/night time is fixed to Asia/Tehran (`IRST-3:30`) in version 1.
- Remote commands expire after 60 seconds and are bound to a boot ID and settings revision. A dashboard change is pending until the device acknowledges it. A conflicting keypad edit wins; the stale remote request is rejected. An expired command means acknowledgment was not received; inspect the latest device state rather than assuming it was never executed.
- No device-to-server history backfill is stored during network outages; charts show gaps. Server history is retained for 90 days by default.
- ON/OFF means the firmware's **commanded state**, not measured appliance operation. There are no current sensors or relay feedback contacts.

## Keypad and OLED

On the home screen, A/B/C cycle through readings, connectivity, and faults; D opens the menu. Inside the menu:

| Key | Action |
|---|---|
| A / B | Previous/next field; while editing, increase/decrease |
| C | Advance to the next field; accept the current edit first |
| Digits | Enter a field value |
| `#` | Edit/accept the selected field or execute the selected action |
| `*` | Cancel the edit, or leave the menu |

Temperature and humidity digit entry uses tenths: `245` means `24.5`. A/B can also enter negative temperature values. Schedule fields use four digits, e.g. `0830`. Runtime fields use seconds. Enable automation with `0` or `1`. Field edits remain a draft until **SAVE ALL SETTINGS** is selected. All bands, schedules, and safety fields must be valid to save; nothing is prefilled as an animal-care recommendation.

The light menu cycles Auto → On → Off → Auto. The clock action accepts **YYYYMMDDHHMM**, in Tehran time, e.g. a date you enter from your current clock. Clock entry is also available using `tools/device.py ... clock YYYYMMDDHHMM`.

## Development and checks

Use Python 3.13 and Node 24. `uv venv --python 3.13 --seed .venv` is an alternative when Python 3.13 is not installed directly.

```sh
.venv/bin/pip install -r server/requirements-dev.txt -r tools/requirements.txt
sh tools/test_firmware.sh
.venv/bin/python -m pytest test/test_server.py -q
cd web
npm ci
npm run build
npx playwright install chromium
npm test
```

The C++ tests exercise the actual control engine with address/undefined-behavior sanitizers, and prove the simulated output driver makes zero GPIO calls. API tests use temporary SQLite databases. Browser tests run the production dashboard against the real API and an explicitly **synthetic** test device, including pending/acknowledged changes, conflicts, faults, offline state, and mobile layout. Test-only credentials and endpoints exist only in `tools/e2e_server.py`, which binds loopback and is not copied into the deployment image.

See [protocol details](docs/PROTOCOL.md), [commissioning checks](docs/COMMISSIONING.md), and [verification results](docs/VERIFICATION.md).
