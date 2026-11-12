# API and synchronization

Single terrarium, single device, single owner in version 1. Browser API schema is available from `/openapi.json`. Browser sessions are opaque random tokens stored hashed in SQLite; cookies are HttpOnly, SameSite=Strict, and Secure by default. A session-specific `X-CSRF-Token` is required for authenticated mutations.

| Method/path | Purpose |
|---|---|
| POST `/api/login` | `{username,password}` → session cookie and CSRF token |
| GET `/api/session` | Current username and CSRF token |
| POST `/api/logout` | Revoke session |
| GET `/api/status` | Latest reported snapshot, freshness, alerts, recent commands |
| GET `/api/history?hours=24` | Time-bucketed readings; supported windows 24, 168, 720 hours |
| PUT `/api/config` | `{boot_id,base_revision,config}` → pending command ID |
| POST `/api/overrides` | `{boot_id,base_revision,light,mode}`; light 0/1, mode -1=Auto, 0=Off, 1=On |
| POST `/api/faults/acknowledge` | `{boot_id,base_revision}`; device checks the fault-clear conditions |
| POST `/api/device/sync` | Device telemetry and acknowledged results; bearer device token |

Device snapshots use protocol version 1, boot ID, monotonic uptime, settings revision, the entire applied configuration (or null before setup), temperature/humidity, sample age, sensor/clock/display/storage health, Wi-Fi RSSI, simulated flag, profile, inhibit reason, four commanded output states, fault names, and two override states. Output order is heater, humidifier, light 1, light 2.

Configuration is flat and validated in both Python and C++. Temperature fields are `day_temp_on/off`, `night_temp_on/off`; humidity fields are `day_rh_on/off`, `night_rh_on/off`. Schedules `day_start/end`, `light1_on/off`, `light2_on/off` use integer minutes after midnight Tehran time. Limits are `max_temperature`, `min_off_seconds`, `heater_max_seconds`, `humidifier_max_seconds`. `enabled` is a boolean. Validation bounds reflect sensor/software capability, **not species-specific safe ranges**.

Each sync response contains `{protocol:1,server_time,command}`. A command includes ID, kind, payload, target boot ID, base revision, and expiry. Only one pending command is permitted. The firmware applies config changes atomically to NVS before incrementing the revision, then reports the result in subsequent snapshots. It remembers the most recent command ID to avoid repeated application; a reboot invalidates old commands through the boot ID.

The server never replaces reported device settings optimistically. A local revision change rejects an older pending request. A reboot cancels pending requests for the previous boot. Network requests are performed in a dedicated FreeRTOS task with timeouts; the control/UI task only exchanges bounded snapshot/command queues. The device rechecks boot, revision, expiry, settings validity, and safety interlocks when applying commands.

History timestamps are server receipt times in UTC. Charts display Tehran time and break across missing/invalid intervals. Device sync runs every ten seconds after the preceding request completes; dashboard polls status every five seconds and history every thirty seconds. Settings acknowledgments can therefore take multiple synchronization cycles.
