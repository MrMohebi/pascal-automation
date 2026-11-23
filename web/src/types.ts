export type Configuration = {
  enabled: boolean;
  day_rh_on: number;
  day_rh_off: number;
  night_rh_on: number;
  night_rh_off: number;
  max_temperature: number;
  day_start: number;
  day_end: number;
  lamp_on: number;
  lamp_off: number;
  fan_day_on_minutes: number;
  fan_day_off_minutes: number;
  fan_night_on_minutes: number;
  fan_night_off_minutes: number;
  min_off_seconds: number;
  humidifier_max_seconds: number;
  // Automatic mode only: false keeps the output OFF for that whole period.
  fan_day_enabled: boolean;
  fan_night_enabled: boolean;
  humidifier_day_enabled: boolean;
  humidifier_night_enabled: boolean;
};
export type SwitchKey =
  | "fan_day_enabled"
  | "fan_night_enabled"
  | "humidifier_day_enabled"
  | "humidifier_night_enabled";
export type NumericKey = Exclude<keyof Configuration, "enabled" | SwitchKey>;
export type Device = {
  boot_id: string;
  revision: number;
  config: Configuration | null;
  temperature: number | null;
  humidity: number | null;
  sample_age_ms: number | null;
  sensor_ok: boolean;
  clock_ok: boolean;
  schedule_cached?: boolean;
  modes_persistent?: boolean;
  simulated: boolean;
  profile: string;
  reason: string;
  outputs: boolean[];
  faults: string[];
  // Same order as outputs: lamp, fan, humidifier. -1 Auto, 0 forced OFF, 1 forced ON.
  overrides: { mode: number; until: number }[];
  fan_next_change_s?: number | null;
  display_ok: boolean;
  storage_ok: boolean;
  wifi_rssi: number;
};
export type Command = {
  id: string;
  kind: string;
  created: number;
  status: string;
  message: string;
};
export type Status = {
  online: boolean;
  received_at: number | null;
  device: Device | null;
  alerts: string[];
  commands: Command[];
  server_time: number;
};
export type Point = {
  time: number;
  temperature: number | null;
  humidity: number | null;
  sensor_ok: number;
};
