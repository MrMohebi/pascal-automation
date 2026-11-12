# Pascal Watcher

ESP32 terrarium monitoring and control, with a 128×64 status OLED, and a private web dashboard.

**The shipped firmware is simulated: it never configures or writes the four relay GPIOs.** The DHT22, OLED, storage, Wi-Fi, and HTTPS synchronization are real. No husbandry targets are included. Four of the eight relay channels are reserved and have no assigned GPIOs.

## Start here

**Current wiring (layout v3):** DHT22 DATA **4**, OLED SDA **21** / SCL **22**, heater/humidifier/light 1/light 2 **25 / 26 / 27 / 14**. There is no keypad. Configure through the web dashboard; the display only shows status and saved settings.

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

6. Enter your confirmed settings from the dashboard. Save with automation disabled first; then enable it to inspect **simulated commanded states**.

## Operation

- DHT22 reads every 2.5 seconds. Three consecutive valid readings are required. Any invalid reading, or ten seconds without a fresh sample, inhibits all four outputs.
- Heater and humidifier turn ON strictly below their ON thresholds and OFF at or above their OFF thresholds. Within the band they retain their previous states. Minimum OFF time applies to restart; shutdown never waits for a minimum run time.
- Day/night climate profiles and both light schedules support midnight crossings. Equal ON/OFF times are rejected rather than interpreted as always-on.
- Light ON/OFF selections are saved on the ESP32 and remain selected across schedule transitions, network loss, and restarts until you change them or choose Auto. Configuration changes preserve these selections. Safety interlocks always override the requested state.
- Overtemperature or either maximum runtime latches **all outputs OFF**. Faults survive reboot. Acknowledge only after sensor recovery and temperature is below the configured maximum; a subsequent unchanged problem can trip again.
- Network/server failures do not interrupt local control. A valid clock keeps daily schedules advancing. After a restart without a valid clock, the device uses its saved day/night profile and last automatic light states; the OLED/dashboard label this fallback. Fresh sensor readings still control heat and humidity. When NTP returns, schedules resume using current Tehran time (`IRST-3:30`).
- A cold power loss cannot preserve knowledge of elapsed time without a battery-backed clock ([ESP32 timekeeping](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/system_time.html)). Saved schedule states remain fixed until time is known; no historical timestamp is presented as the current clock. If no schedule has ever been saved, outputs stay OFF until time is established. USB clock entry remains available through `tools/device.py ... clock YYYYMMDDHHMM`.
- Settings, light selections, and schedule state are committed together to nonvolatile storage before acknowledgment or applying a schedule transition. Only settings/mode changes and schedule transitions cause these writes; sensor samples and each loop do not. Settings and persistent light commands increment the revision. On restart, heater/humidifier decisions are recomputed after sensor recovery and minimum OFF time; their prior ON bits are never blindly restored. Continuous-runtime timers start with the new run; latched faults survive reboot.
- Remote commands expire after 60 seconds and are bound to a boot ID and settings revision. A dashboard change is pending until the device acknowledges it. A newer saved revision wins; the stale remote request is rejected. An expired command means acknowledgment was not received; inspect the latest device state rather than assuming it was never executed.
- No device-to-server history backfill is stored during network outages; charts show gaps. Server history is retained for 90 days by default.
- ON/OFF means the firmware's **commanded state**, not measured appliance operation. There are no current sensors or relay feedback contacts.

## Web settings and OLED

The web dashboard is the settings interface. Save climate thresholds, schedules, limits, and automation enable/disable there. Light controls offer **Auto / On / Off**. Changes remain pending until the ESP32 confirms that it saved them; an undelivered or expired request does not replace the last acknowledged state. When the device reconnects, the server displays the device's saved state rather than replaying stale pending commands.

The 128×64 SSD1306 OLED is read-only and rotates pages every five seconds: temperature, humidity, heater/humidifier states, light states, connection status, and the active temperature/humidity targets. Readings use 3× text; output states and targets use 2× text. Small headings and a page counter provide context. Detailed schedules and safety settings stay on the dashboard. Faults interrupt the pages with a stopped-output warning. There are no keys or on-device menus.

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
