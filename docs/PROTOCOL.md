# API and synchronization

Single terrarium, single device, single owner in version 1. Browser API schema is available from `/openapi.json`. Browser sessions are opaque random tokens stored hashed in SQLite; cookies are HttpOnly, SameSite=Strict, and Secure by default. A session-specific `X-CSRF-Token` is required for authenticated mutations.

| Method/path | Purpose |
|---|---|
| POST `/api/login` | `{password}` → session cookie and CSRF token |
| GET `/api/session` | Current CSRF token |
| POST `/api/logout` | Revoke session |
| GET `/api/status` | Latest reported snapshot, freshness, alerts, recent commands |
| GET `/api/history?hours=24` | Time-bucketed readings; supported windows 24, 168, 720 hours |
| PUT `/api/config` | `{boot_id,base_revision,config}` → pending command ID |
| POST `/api/overrides` | `{boot_id,base_revision,light,mode}`; light 0/1, mode -1=Auto, 0=Off, 1=On |
| POST `/api/faults/acknowledge` | `{boot_id,base_revision}`; device checks the fault-clear conditions |
| POST `/api/device/sync` | Device telemetry and acknowledged results; no authentication |

Device snapshots use protocol version 1, boot ID, monotonic uptime, settings revision, the entire applied configuration (or null before setup), temperature/humidity, sample age, sensor/clock/display/storage health, Wi-Fi RSSI, simulated flag, profile, inhibit reason, four commanded output states, fault names, and two light selection states. Current firmware adds `schedule_cached` (a saved schedule/profile is available) and `light_modes_persistent` (manual light modes survive restart); these default to false for older firmware. With persistent modes, `until` is zero and mode changes increment the revision. Output order is heater, humidifier, light 1, light 2.

Configuration is flat and validated in both Python and C++. Temperature fields are `day_temp_on/off`, `night_temp_on/off`; humidity fields are `day_rh_on/off`, `night_rh_on/off`. Schedules `day_start/end`, `light1_on/off`, `light2_on/off` use integer minutes after midnight Tehran time. Limits are `max_temperature`, `min_off_seconds`, `heater_max_seconds`, `humidifier_max_seconds`. `enabled` is a boolean. Validation bounds reflect sensor/software capability, **not species-specific safe ranges**.

Each sync response contains `{protocol:1,server_time,command}`. A command includes ID, kind, payload, target boot ID, base revision, and expiry. Only one pending command is permitted. The firmware saves configuration, manual light modes, cached schedule state, and the next revision together in one NVS value before publishing that revision, then reports the result in subsequent snapshots. It remembers the most recent command ID to avoid repeated application; a reboot invalidates old commands through the boot ID.

The server never replaces reported device settings optimistically. A newer revision rejects an older pending request. Acknowledged settings and light selections survive reboot; cancellation only affects commands not yet acknowledged. A reboot cancels pending requests for the previous boot. Network requests are performed in a dedicated FreeRTOS task with timeouts; the control/UI task only exchanges bounded snapshot/command queues. The device rechecks boot, revision, expiry, settings validity, and safety interlocks when applying commands.

History timestamps are server receipt times in UTC. Charts display Tehran time and break across missing/invalid intervals. Device sync runs every ten seconds after the preceding request completes; dashboard polls status every five seconds and history every thirty seconds. Settings acknowledgments can therefore take multiple synchronization cycles.

When clock time is unavailable after restart, `clock_ok` is false while `profile` can still be day/night from saved state. If `schedule_cached` is true, local control uses that profile and held automatic light states after sensor recovery; this is not a claim of current wall time. With no cached schedule, all outputs are inhibited. A valid clock refreshes the cached schedule, which is written only on transitions. Remote commands still require valid time for expiry checking, and TLS requires time for certificate validation.

Old `settings` NVS records migrate without erasing Wi-Fi credentials or latched faults. Once `state_v3` exists it is authoritative; malformed newer records cause storage failure rather than silently reverting to stale settings. Every remote light selection is acknowledged only after storage succeeds, and the server verifies its reported mode and revision.

Device sync trusts any caller, including older firmware that still sends an Authorization header. Payload validation, revision matching, acknowledgments, and firmware safety checks still apply. Browser settings changes require the password session and CSRF token.
