import React, { useEffect, useState } from "react";
import { createRoot } from "react-dom/client";
import type {
  Configuration,
  Device,
  NumericKey,
  Point,
  Status,
  SwitchKey,
} from "./types";
import "./style.css";

async function api(path: string, method = "GET", body?: unknown, csrf = "") {
  const response = await fetch(`/api${path}`, {
    method,
    credentials: "same-origin",
    signal: AbortSignal.timeout(8000),
    headers: { "Content-Type": "application/json", "X-CSRF-Token": csrf },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }),
  });
  const data = await response
    .json()
    .catch(() => ({ detail: "Unexpected server response" }));
  if (!response.ok) {
    const message =
      typeof data.detail === "string"
        ? data.detail
        : Array.isArray(data.detail)
          ? data.detail.map((e: { msg: string }) => e.msg).join("; ")
          : "Request failed";
    throw Object.assign(new Error(message), { status: response.status });
  }
  return data;
}
const date = (epoch: number, withDate = false) =>
  new Date(epoch * 1000).toLocaleString("en-GB", {
    timeZone: "Asia/Tehran",
    ...(withDate ? { day: "2-digit", month: "short" } : {}),
    hour: "2-digit",
    minute: "2-digit",
  });
const minuteText = (n: number) =>
  `${String(Math.floor(n / 60)).padStart(2, "0")}:${String(n % 60).padStart(2, "0")}`;
const durationText = (seconds: number) =>
  seconds < 90
    ? `${Math.max(1, Math.round(seconds))} s`
    : seconds < 5400
      ? `${Math.round(seconds / 60)} min`
      : `${Math.floor(seconds / 3600)} h ${Math.round((seconds % 3600) / 60)} min`;

// Same order as the firmware's relay outputs (GPIO 25, 26, 27).
const OUTPUTS = [
  { name: "Lamp", symbol: "☀", pin: 25 },
  { name: "Fan", symbol: "✣", pin: 26 },
  { name: "Humidifier", symbol: "◉", pin: 27 },
] as const;
const MODES = [
  [-1, "Auto"],
  [1, "On"],
  [0, "Off"],
] as const;
const modeName = (mode: number | undefined) =>
  mode === 1 ? "Forced ON" : mode === 0 ? "Forced OFF" : "Auto";
const running = (d: Device) =>
  d.reason === "running" || d.reason === "running saved state";

function autoDescription(d: Device, i: number) {
  const c = d.config;
  if (!c) return "Setup required";
  const period = d.profile === "night" ? "night" : "day";
  if (i === 0)
    return !d.clock_ok && d.schedule_cached
      ? "Holding saved automatic state"
      : `Schedule ${minuteText(c.lamp_on)}–${minuteText(c.lamp_off)}`;
  if (i === 1) {
    const on = c[`fan_${period}_on_minutes`],
      off = c[`fan_${period}_off_minutes`];
    if (d.profile === "unknown") return "Waiting for day/night profile";
    if (!c[`fan_${period}_enabled`] || on === 0) return `Off all ${period}`;
    if (off === 0) return `Runs all ${period}`;
    return `${period === "day" ? "Day" : "Night"} cycle ${on} min on / ${off} min off`;
  }
  if (d.profile !== "unknown" && !c[`humidifier_${period}_enabled`])
    return `Off all ${period}`;
  return d.profile === "unknown"
    ? "Humidity controlled"
    : `ON below ${c[`${period}_rh_on`]}% · OFF at ${c[`${period}_rh_off`]}%`;
}
function Logo() {
  return (
    <div className="logo">
      <span aria-hidden="true">
        p<span className="leaf">●</span>
      </span>
      <div>
        pascal<small>TERRARIUM WATCHER</small>
      </div>
    </div>
  );
}

function Chart({
  points,
  metric,
  bucket,
}: {
  points: Point[];
  metric: "temperature" | "humidity";
  bucket: number;
}) {
  const [hover, setHover] = useState<Point | null>(null);
  const valid = points.filter((p) => p.sensor_ok && p[metric] !== null);
  const values = valid.map((p) => p[metric] as number);
  if (!values.length)
    return (
      <div className="chart-empty">
        No valid readings in this period.
        <small>
          History appears after the device connects and its sensor is ready.
        </small>
      </div>
    );
  const min = Math.floor(Math.min(...values) - 1),
    max = Math.ceil(Math.max(...values) + 1);
  const start = points[0].time,
    end = points.at(-1)!.time;
  const x = (t: number) => 45 + ((t - start) / Math.max(end - start, 1)) * 680;
  const y = (v: number) => 160 - ((v - min) / (max - min)) * 135;
  const groups: string[][] = [[]];
  let previous: number | null = null;
  for (const p of points) {
    if (!p.sensor_ok || p[metric] === null) {
      if (groups.at(-1)!.length) groups.push([]);
      previous = null;
      continue;
    }
    if (previous !== null && p.time - previous > bucket * 2) groups.push([]);
    groups.at(-1)!.push(`${x(p.time)},${y(p[metric] as number)}`);
    previous = p.time;
  }
  return (
    <div className={`chart ${metric}`}>
      <div className="chart-caption">
        {hover
          ? `${date(hover.time, true)} · ${hover[metric]?.toFixed(1) ?? "—"} ${metric === "temperature" ? "°C" : "% RH"}`
          : "Move over the chart to inspect a reading"}
      </div>
      <svg
        viewBox="0 0 750 195"
        role="img"
        aria-label={`${metric} history in Tehran time`}
        onPointerLeave={() => setHover(null)}
        onPointerMove={(e) => {
          // Map through the SVG transform: the drawing is letterboxed when
          // max-height limits it, so the element box is not the plot area.
          const matrix = e.currentTarget.getScreenCTM();
          if (!matrix) return;
          const point = new DOMPoint(e.clientX, e.clientY).matrixTransform(
            matrix.inverse(),
          );
          const t =
            start + ((point.x - 45) / 680) * Math.max(end - start, 1);
          // Snap to the nearest valid reading so the caption never shows a gap.
          setHover(
            valid.reduce((a, b) =>
              Math.abs(a.time - t) <= Math.abs(b.time - t) ? a : b,
            ),
          );
        }}
      >
        <title>
          {metric} history. Gaps indicate missing or invalid readings.
        </title>
        {[0, 1, 2, 3].map((i) => (
          <g key={i}>
            <line
              x1="45"
              x2="725"
              y1={25 + i * 45}
              y2={25 + i * 45}
              className="grid-line"
            />
            <text x="35" y={29 + i * 45} textAnchor="end">
              {(max - ((max - min) * i) / 3).toFixed(0)}
            </text>
          </g>
        ))}
        {groups
          .filter((g) => g.length)
          .map((g, i) =>
            g.length === 1 ? (
              <circle
                key={i}
                cx={g[0].split(",")[0]}
                cy={g[0].split(",")[1]}
                r="3"
                className="data-dot"
              />
            ) : (
              <polyline key={i} points={g.join(" ")} className="data-line" />
            ),
          )}
        <text x="45" y="188">
          {date(start, true)}
        </text>
        <text x="725" y="188" textAnchor="end">
          {date(end, true)}
        </text>
        {hover && (
          <>
            <line
              x1={x(hover.time)}
              x2={x(hover.time)}
              y1="20"
              y2="165"
              className="hover-line"
            />
            <circle
              cx={x(hover.time)}
              cy={y(hover[metric] as number)}
              r="4.5"
              className="hover-dot"
            />
          </>
        )}
      </svg>
    </div>
  );
}

type Field =
  | [NumericKey, string, "number" | "time", string?]
  | [SwitchKey, string, "check", string?];
// A period switch that is off greys out the inputs it makes irrelevant.
const switchFor = (key: string): SwitchKey | null =>
  key.startsWith("fan_day_")
    ? "fan_day_enabled"
    : key.startsWith("fan_night_")
      ? "fan_night_enabled"
      : key.startsWith("day_rh_")
        ? "humidifier_day_enabled"
        : key.startsWith("night_rh_")
          ? "humidifier_night_enabled"
          : null;
const sections: { title: string; description: string; fields: Field[] }[] = [
  {
    title: "Day & night",
    description:
      "Daily transitions in Tehran time. Overnight periods are supported.",
    fields: [
      ["day_start", "Day starts", "time"],
      ["day_end", "Night starts", "time"],
    ],
  },
  {
    title: "Lamp",
    description:
      "Daily lamp schedule (GPIO 25). Overnight periods are supported. A forced ON/OFF from the overview stays selected until you choose Auto.",
    fields: [
      ["lamp_on", "Lamp · ON", "time"],
      ["lamp_off", "Lamp · OFF", "time"],
    ],
  },
  {
    title: "Exhaust fan",
    description:
      "Repeating cycle (GPIO 26): runs for ON minutes, then rests for OFF minutes. Each day/night change restarts the cycle with ON. Untick a period to keep the fan off for all of it, or set OFF to 0 to run it continuously.",
    fields: [
      ["fan_day_enabled", "Run during the day", "check"],
      ["fan_night_enabled", "Run during the night", "check"],
      ["fan_day_on_minutes", "Day · ON for", "number", "minutes"],
      ["fan_day_off_minutes", "Day · then OFF for", "number", "minutes"],
      ["fan_night_on_minutes", "Night · ON for", "number", "minutes"],
      ["fan_night_off_minutes", "Night · then OFF for", "number", "minutes"],
    ],
  },
  {
    title: "Humidity",
    description:
      "Humidifier (GPIO 27) turns on below ON and off at OFF. Values are % RH. Untick a period to keep the humidifier off for all of it.",
    fields: [
      ["humidifier_day_enabled", "Run during the day", "check"],
      ["humidifier_night_enabled", "Run during the night", "check"],
      ["day_rh_on", "Day · ON below", "number"],
      ["day_rh_off", "Day · OFF at", "number"],
      ["night_rh_on", "Night · ON below", "number"],
      ["night_rh_off", "Night · OFF at", "number"],
    ],
  },
  {
    title: "Safety limits",
    description:
      "Required before automation can run. Reaching the maximum temperature or humidifier runtime switches every output OFF until acknowledged.",
    fields: [
      ["max_temperature", "Maximum temperature", "number", "°C"],
      ["min_off_seconds", "Humidifier minimum OFF time", "number", "seconds"],
      ["humidifier_max_seconds", "Maximum humidifier run", "number", "seconds"],
    ],
  },
];

function SettingsForm({
  status,
  blocked,
  submit,
}: {
  status: Status;
  blocked: boolean;
  submit: (path: string, method: string, body: unknown) => Promise<boolean>;
}) {
  const device = status.device;
  const fromConfig = (c: Configuration | null | undefined) => {
    const next: Record<string, string> = {};
    sections.forEach((s) =>
      s.fields.forEach(
        ([k, , kind]) =>
          (next[k] = c
            ? kind === "time"
              ? minuteText(c[k] as number)
              : String(c[k])
            : kind === "check"
              ? "true"
              : ""),
      ),
    );
    return next;
  };
  const [draft, setDraft] = useState<Record<string, string>>(() =>
    fromConfig(device?.config),
  );
  const [enabled, setEnabled] = useState(device?.config?.enabled ?? false);
  const [dirty, setDirty] = useState(false);
  const [base, setBase] = useState({
    base_revision: device?.revision ?? 0,
    boot_id: device?.boot_id ?? "",
  });
  const reset = () => {
    setDraft(fromConfig(device?.config));
    setEnabled(device?.config?.enabled ?? false);
    setBase({
      base_revision: device?.revision ?? 0,
      boot_id: device?.boot_id ?? "",
    });
    setDirty(false);
  };
  useEffect(() => {
    if (!dirty) reset();
  }, [device?.revision, device?.boot_id]);
  const conflict =
    dirty &&
    (device?.revision !== base.base_revision ||
      device?.boot_id !== base.boot_id);
  return (
    <form
      onSubmit={async (e) => {
        e.preventDefault();
        const config: Record<string, number | boolean> = { enabled };
        sections.forEach((s) =>
          s.fields.forEach(([key, , kind]) => {
            const val = draft[key];
            config[key] =
              kind === "check"
                ? val === "true"
                : kind === "time"
                  ? Number(val.split(":")[0]) * 60 + Number(val.split(":")[1])
                  : Number(val);
          }),
        );
        if (await submit("/config", "PUT", { ...base, config }))
          setDirty(false);
      }}
    >
      <div className="section-title">
        <div>
          <span className="eyebrow">LOCAL & REMOTE</span>
          <h2>Terrarium settings</h2>
        </div>
        <span className="muted">Revision {device?.revision ?? "—"}</span>
      </div>
      <p className="muted">
        Enter your confirmed husbandry settings. No animal-care targets are
        preselected. Acknowledged settings are saved on the ESP32 and shown on its display.
      </p>
      {conflict && (
        <div className="notice error">
          Settings changed on the device while you were editing. Reload before
          saving.{" "}
          <button type="button" onClick={reset}>
            Reload settings
          </button>
        </div>
      )}
      <div className="settings-grid">
        {sections.map((s) => (
          <section className="panel settings-section" key={s.title}>
            <h3>{s.title}</h3>
            <p>{s.description}</p>
            <div className="field-grid">
              {s.fields.map(([key, label, kind, unit]) =>
                kind === "check" ? (
                  <label key={key} className="checkbox period-switch">
                    <input
                      type="checkbox"
                      checked={draft[key] === "true"}
                      onChange={(e) => {
                        const value = String(e.target.checked);
                        setDraft((previous) => ({ ...previous, [key]: value }));
                        setDirty(true);
                      }}
                    />{" "}
                    {label}
                  </label>
                ) : (
                <label key={key}>
                  {label}
                  {unit && <small>{unit}</small>}
                  <input
                    required
                    type={kind}
                    step={
                      kind === "number"
                        ? key.includes("seconds") || key.includes("minutes")
                          ? "1"
                          : "0.1"
                        : undefined
                    }
                    min={key.includes("minutes") ? 0 : undefined}
                    max={key.includes("minutes") ? 1440 : undefined}
                    disabled={
                      switchFor(key) !== null &&
                      draft[switchFor(key)!] === "false"
                    }
                    value={draft[key] ?? ""}
                    onChange={(e) => {
                      const value = e.target.value;
                      setDraft((previous) => ({ ...previous, [key]: value }));
                      setDirty(true);
                    }}
                  />
                </label>
                ),
              )}
            </div>
          </section>
        ))}
      </div>
      <div className="save-bar">
        <label className="checkbox">
          <input
            type="checkbox"
            checked={enabled}
            onChange={(e) => {
              setEnabled(e.target.checked);
              setDirty(true);
            }}
          />{" "}
          Enable automatic control{device?.simulated ? " (simulated)" : ""}
        </label>
        <div>
          <button type="button" className="secondary" onClick={reset}>
            Reset edits
          </button>
          <button disabled={blocked || conflict || !dirty} type="submit">
            Save to device
          </button>
        </div>
      </div>
    </form>
  );
}

function App() {
  const [auth, setAuth] = useState<{ csrf: string } | null>(
      null,
    ),
    [checking, setChecking] = useState(true);
  const [status, setStatus] = useState<Status | null>(null),
    [error, setError] = useState(""),
    [message, setMessage] = useState("");
  const [unavailable, setUnavailable] = useState(false),
    [busy, setBusy] = useState(false),
    [tab, setTab] = useState("overview");
  const [hours, setHours] = useState(24),
    [history, setHistory] = useState<{
      points: Point[];
      bucket_seconds: number;
    }>({ points: [], bucket_seconds: 60 });
  const [historyError, setHistoryError] = useState("");
  const [now, setNow] = useState(Date.now() / 1000);
  useEffect(() => {
    api("/session")
      .then(setAuth)
      .catch(() => {})
      .finally(() => setChecking(false));
    const t = setInterval(() => setNow(Date.now() / 1000), 1000);
    return () => clearInterval(t);
  }, []);
  useEffect(() => {
    if (!auth) return;
    let alive = true;
    const poll = async () => {
      try {
        const s = await api("/status");
        if (alive) {
          setStatus(s);
          setUnavailable(false);
        }
      } catch (e) {
        if (alive) {
          setUnavailable(true);
          if ((e as { status?: number }).status === 401) setAuth(null);
        }
      }
    };
    poll();
    const timer = setInterval(poll, 5000);
    return () => {
      alive = false;
      clearInterval(timer);
    };
  }, [auth]);
  useEffect(() => {
    if (!auth) return;
    let alive = true;
    const poll = async () => {
      try {
        const h = await api(`/history?hours=${hours}`);
        if (alive) {
          setHistory(h);
          setHistoryError("");
        }
      } catch {
        if (alive) setHistoryError("History is temporarily unavailable.");
      }
    };
    poll();
    const timer = setInterval(poll, 30000);
    return () => {
      alive = false;
      clearInterval(timer);
    };
  }, [auth, hours]);
  async function submit(path: string, method: string, body: unknown) {
    setBusy(true);
    setError("");
    setMessage("");
    try {
      await api(path, method, body, auth?.csrf);
      setMessage("Change queued. Waiting for the device to acknowledge it.");
      setStatus(await api("/status"));
      return true;
    } catch (e) {
      setError((e as Error).message);
      return false;
    } finally {
      setBusy(false);
    }
  }
  if (checking)
    return (
      <main className="login">
        <Logo />
        <p>Connecting to Pascal…</p>
      </main>
    );
  if (!auth)
    return (
      <main className="login">
        <Logo />
        <div className="panel login-panel">
          <span className="eyebrow">A LITTLE CLOSER TO NATURE</span>
          <h1>
            Your terrarium,
            <br />
            wherever you are.
          </h1>
          <p>Sign in to check the climate, lighting, and daily rhythm.</p>
          <form
            onSubmit={async (e) => {
              e.preventDefault();
              setError("");
              setBusy(true);
              const f = new FormData(e.currentTarget);
              try {
                setAuth(await api("/login", "POST", Object.fromEntries(f)));
                setError("");
              } catch (e) {
                setError((e as Error).message);
              } finally {
                setBusy(false);
              }
            }}
          >
            <label>
              Password
              <input
                name="password"
                type="password"
                autoComplete="current-password"
                required
              />
            </label>
            {error && (
              <p role="alert" className="error-text">
                {error}
              </p>
            )}
            <button disabled={busy}>{busy ? "Signing in…" : "Sign in"}</button>
          </form>
        </div>
        <small>Pascal Watcher · Private terrarium monitoring</small>
      </main>
    );
  const d = status?.device,
    age = status?.received_at ? Math.max(0, now - status.received_at) : null;
  const online = !!status?.online && !unavailable && age !== null && age < 30;
  const pending = status?.commands.find((c) => c.status === "pending");
  const blocked = !online || busy || !!pending || !d?.clock_ok;
  const version = {
    base_revision: d?.revision ?? 0,
    boot_id: d?.boot_id ?? "",
  };
  const fresh = online && !!d?.sensor_ok;
  return (
    <div className="shell">
      <header>
        <Logo />
        <div className="header-right">
          <span className={`pill ${online ? "connected" : "disconnected"}`}>
            <i />
            {online ? "Device online" : "Device offline"}
          </span>
          <button
            className="text-button"
            onClick={async () => {
              try {
                await api("/logout", "POST", {}, auth.csrf);
                setAuth(null);
                setStatus(null);
                setMessage("");
                setError("");
              } catch (e) {
                setError((e as Error).message);
              }
            }}
          >
            Sign out
          </button>
        </div>
      </header>
      <main>
        <div className="heading">
          <div>
            <span className="eyebrow">YOUR CHAMELEON’S HOME</span>
            <h1>A window into the terrarium.</h1>
            <p>Climate, light, and a steady daily rhythm.</p>
          </div>
          <div className="time">
            {date(now, true)}
            <small>Asia/Tehran · {d?.profile ?? "awaiting device"}</small>
          </div>
        </div>
        {(!d || d.simulated) && (
          <div className="notice simulation">
            <span className="notice-icon">◇</span>
            <div>
              <strong>Simulation mode</strong>
              <span>
                Output states are simulated. The firmware does not drive the
                relay pins.
              </span>
            </div>
          </div>
        )}
        {unavailable && (
          <div role="alert" className="notice error">
            Server connection lost. Controls are disabled; displayed data may be
            stale.
          </div>
        )}
        {error && (
          <div role="alert" className="notice error">
            {error}
            <button className="text-button" onClick={() => setError("")}>
              Dismiss
            </button>
          </div>
        )}
        {pending ? (
          <div role="status" className="notice">
            Waiting for device acknowledgment: {pending.kind}. Changes are not
            yet confirmed.
          </div>
        ) : (
          message && (
            <div role="status" className="notice">
              {status?.commands[0]
                ? `${status.commands[0].status}: ${status.commands[0].message}`
                : message}
            </div>
          )
        )}
        <nav aria-label="Dashboard">
          <button
            className={tab === "overview" ? "active" : ""}
            onClick={() => setTab("overview")}
          >
            Overview
          </button>
          <button
            className={tab === "settings" ? "active" : ""}
            onClick={() => setTab("settings")}
          >
            Settings
          </button>
          <span>
            {age === null
              ? "No device readings yet"
              : `Last report ${Math.floor(age)}s ago`}
          </span>
        </nav>
        {tab === "settings" && status ? (
          <SettingsForm status={status} blocked={blocked} submit={submit} />
        ) : (
          <>
            <div className="readings">
              <section className={`panel reading ${fresh ? "" : "stale"}`}>
                <span className="eyebrow">TEMPERATURE</span>
                <div className="measurement">
                  {d?.temperature?.toFixed(1) ?? "—"}
                  <span>°C</span>
                </div>
                <div className="reading-foot">
                  <span className="dot warm" />
                  {fresh ? "Ambient temperature" : "Awaiting fresh sensor data"}
                  <span>
                    {d?.config
                      ? `Max ${d.config.max_temperature} °C`
                      : "No limit set"}
                  </span>
                </div>
              </section>
              <section className={`panel reading ${fresh ? "" : "stale"}`}>
                <span className="eyebrow">HUMIDITY</span>
                <div className="measurement">
                  {d?.humidity?.toFixed(1) ?? "—"}
                  <span>% RH</span>
                </div>
                <div className="reading-foot">
                  <span className="dot cool" />
                  {fresh ? "Relative humidity" : "Awaiting fresh sensor data"}
                  <span>
                    {d?.config && d.profile !== "unknown"
                      ? `${d.config[`${d.profile}_rh_on` as keyof Configuration]}–${d.config[`${d.profile}_rh_off` as keyof Configuration]} %`
                      : "No target set"}
                  </span>
                </div>
              </section>
            </div>
            <div className="section-title">
              <div>
                <span className="eyebrow">THREE CHANNELS · ONE HABITAT</span>
                <h2>Equipment</h2>
              </div>
              <span className="muted">
                Commanded states · no appliance feedback
              </span>
            </div>
            <div className="equipment">
              {OUTPUTS.map(({ name, symbol, pin }, i) => {
                const mode = d?.overrides[i]?.mode;
                const state = d?.outputs[i];
                const forceOnBlocked =
                  !d?.config ||
                  !d.config.enabled ||
                  !!d.faults.length ||
                  !d.sensor_ok;
                const detail = !d
                  ? "Awaiting device"
                  : !d.config
                    ? "Setup required"
                    : !running(d)
                      ? `Held OFF · ${d.reason}`
                      : mode === -1
                        ? autoDescription(d, i)
                        : `${modeName(mode)} · saved on device`;
                const next =
                  d && i === 1 && mode === -1 && running(d) && d.fan_next_change_s != null && online
                    ? `Turns ${state ? "OFF" : "ON"} in ${durationText(Math.max(0, d.fan_next_change_s - (age ?? 0)))}`
                    : null;
                return (
                  <section className="panel equipment-card" key={name}>
                    <div className="equipment-top">
                      <span className="equipment-symbol" aria-hidden="true">
                        {symbol}
                      </span>
                      <span
                        className={`pill ${online && state ? "on" : "off"}`}
                        aria-label={`${name} is ${!d ? "unknown" : state ? "on" : "off"}`}
                      >
                        <i />
                        {!d
                          ? "UNKNOWN"
                          : `${state ? "ON" : "OFF"}${online ? "" : " · LAST"}`}
                      </span>
                    </div>
                    <h3>{name}</h3>
                    <span className="mode-line">
                      Mode <b>{d ? modeName(mode) : "—"}</b> · GPIO {pin}
                    </span>
                    <p>
                      {detail}
                      {next && <span className="next-change">{next}</span>}
                    </p>
                    <div className="segmented" role="group" aria-label={`${name} mode`}>
                      {MODES.map(([value, label]) => (
                        <button
                          key={value}
                          disabled={blocked || !d?.config || (value === 1 && forceOnBlocked)}
                          title={
                            value === 1 && forceOnBlocked
                              ? "Forcing ON needs automation enabled, no faults, and a working sensor"
                              : undefined
                          }
                          className={mode === value ? "selected" : ""}
                          aria-label={`${name} ${label}`}
                          aria-pressed={mode === value}
                          onClick={() =>
                            submit("/overrides", "POST", {
                              ...version,
                              output: i,
                              mode: value,
                            })
                          }
                        >
                          {label}
                        </button>
                      ))}
                    </div>
                  </section>
                );
              })}
            </div>
            <section className="panel history">
              <div className="section-title">
                <div>
                  <span className="eyebrow">THE BIGGER PICTURE</span>
                  <h2>Climate history</h2>
                </div>
                <div className="segmented">
                  {[
                    [24, "24 hours"],
                    [168, "7 days"],
                    [720, "30 days"],
                  ].map(([n, label]) => (
                    <button
                      key={n}
                      className={hours === n ? "selected" : ""}
                      onClick={() => setHours(n as number)}
                    >
                      {label}
                    </button>
                  ))}
                </div>
              </div>
              {historyError ? (
                <p role="alert">{historyError}</p>
              ) : (
                <>
                  <div className="chart-title">
                    <span className="dot warm" />
                    Temperature <small>°C</small>
                  </div>
                  <Chart
                    points={history.points}
                    metric="temperature"
                    bucket={history.bucket_seconds}
                  />
                  <div className="chart-title">
                    <span className="dot cool" />
                    Humidity <small>% RH</small>
                  </div>
                  <Chart
                    points={history.points}
                    metric="humidity"
                    bucket={history.bucket_seconds}
                  />
                </>
              )}
              <p className="history-note">
                Tehran time · Gaps represent missing or invalid data · 90-day
                default retention
              </p>
            </section>
            <div className="bottom-grid">
              <section className="panel">
                <div className="section-title">
                  <h2>System notes</h2>
                  <span className="count">{status?.alerts.length ?? 0}</span>
                </div>
                {status?.alerts.length ? (
                  <ul className="alerts">
                    {status.alerts.map((a) => (
                      <li key={a}>{a}</li>
                    ))}
                  </ul>
                ) : (
                  <p className="muted">
                    {d
                      ? "No active alerts."
                      : "Waiting for the first device report."}
                  </p>
                )}
                {!!d?.faults.length && (
                  <button
                    disabled={blocked}
                    onClick={() =>
                      submit("/faults/acknowledge", "POST", version)
                    }
                  >
                    Acknowledge resolved faults
                  </button>
                )}
                <div className="diagnostics">
                  <span>
                    Clock <b>{d?.clock_ok ? "Ready" : d?.schedule_cached ? "Saved schedule" : "Not set"}</b>
                  </span>
                  <span>
                    OLED <b>{d?.display_ok ? "Detected" : "Not detected"}</b>
                  </span>
                  <span>
                    Wi-Fi <b>{d?.wifi_rssi || "—"} dBm</b>
                  </span>
                </div>
              </section>
              <section className="panel">
                <h2>Recent changes</h2>
                {status?.commands.length ? (
                  <ul className="changes">
                    {status.commands.slice(0, 6).map((c) => (
                      <li key={c.id}>
                        <div>
                          <strong>
                            {c.kind === "config"
                              ? "Settings"
                              : c.kind === "override"
                                ? "Equipment mode"
                                : "Fault acknowledgment"}
                          </strong>
                          <small>
                            {date(c.created, true)} · {c.message}
                          </small>
                        </div>
                        <span className={`command-status ${c.status}`}>
                          {c.status}
                        </span>
                      </li>
                    ))}
                  </ul>
                ) : (
                  <p className="muted">
                    Settings and equipment mode changes will appear here with
                    device acknowledgments.
                  </p>
                )}
              </section>
            </div>
          </>
        )}
        <footer>
          <span>pascal / a watchful little companion</span>
          <span>Local control continues when the internet is unavailable.</span>
        </footer>
      </main>
    </div>
  );
}

createRoot(document.getElementById("root")!).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);
