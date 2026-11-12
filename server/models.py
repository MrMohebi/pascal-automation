from typing import Literal

from pydantic import BaseModel, ConfigDict, Field, model_validator


class Model(BaseModel):
    model_config = ConfigDict(extra="forbid", allow_inf_nan=False)


class Configuration(Model):
    enabled: bool = Field(strict=True)
    day_temp_on: float = Field(ge=-40, le=80)
    day_temp_off: float = Field(ge=-40, le=80)
    night_temp_on: float = Field(ge=-40, le=80)
    night_temp_off: float = Field(ge=-40, le=80)
    day_rh_on: float = Field(ge=0, le=100)
    day_rh_off: float = Field(ge=0, le=100)
    night_rh_on: float = Field(ge=0, le=100)
    night_rh_off: float = Field(ge=0, le=100)
    max_temperature: float = Field(ge=-40, le=80)
    day_start: int = Field(ge=0, lt=1440, strict=True)
    day_end: int = Field(ge=0, lt=1440, strict=True)
    light1_on: int = Field(ge=0, lt=1440, strict=True)
    light1_off: int = Field(ge=0, lt=1440, strict=True)
    light2_on: int = Field(ge=0, lt=1440, strict=True)
    light2_off: int = Field(ge=0, lt=1440, strict=True)
    min_off_seconds: int = Field(ge=1, le=3600, strict=True)
    heater_max_seconds: int = Field(ge=1, le=86400, strict=True)
    humidifier_max_seconds: int = Field(ge=1, le=86400, strict=True)

    @model_validator(mode="after")
    def validate_relationships(self):
        for prefix in ("day_temp", "night_temp", "day_rh", "night_rh"):
            if getattr(self, prefix + "_on") >= getattr(self, prefix + "_off"):
                raise ValueError(f"{prefix}: ON threshold must be below OFF threshold")
        if self.max_temperature <= max(self.day_temp_off, self.night_temp_off):
            raise ValueError(
                "Maximum temperature must exceed both temperature OFF thresholds"
            )
        for start, end in (
            (self.day_start, self.day_end),
            (self.light1_on, self.light1_off),
            (self.light2_on, self.light2_off),
        ):
            if start == end:
                raise ValueError("Schedule ON/start and OFF/end times must differ")
        return self


class OverrideState(Model):
    mode: Literal[-1, 0, 1]
    until: int = Field(ge=0)


class CommandResult(Model):
    id: str = Field(min_length=1, max_length=64)
    status: Literal["applied", "rejected"]
    message: str = Field(max_length=240)


class Telemetry(Model):
    protocol: Literal[1]
    boot_id: str = Field(min_length=1, max_length=64, pattern=r"^[a-zA-Z0-9_-]+$")
    uptime_ms: int = Field(ge=0, le=4294967295)
    revision: int = Field(ge=0, le=4294967295, strict=True)
    config: Configuration | None
    temperature: float | None = Field(ge=-40, le=80)
    humidity: float | None = Field(ge=0, le=100)
    sample_age_ms: int | None = Field(ge=0, le=4294967295)
    sensor_ok: bool
    clock_ok: bool
    simulated: bool
    profile: Literal["day", "night", "unknown"]
    reason: str = Field(max_length=100)
    wifi_rssi: int = Field(ge=-150, le=0)
    display_ok: bool
    storage_ok: bool
    outputs: list[bool] = Field(min_length=4, max_length=4)
    faults: list[
        Literal[
            "overtemperature", "heater_runtime", "humidifier_runtime", "storage_failure"
        ]
    ] = Field(max_length=4)
    overrides: list[OverrideState] = Field(min_length=2, max_length=2)
    result: CommandResult | None = None

    @model_validator(mode="after")
    def valid_sensor(self):
        if self.sensor_ok and (
            self.temperature is None
            or self.humidity is None
            or self.sample_age_ms is None
            or self.sample_age_ms >= 10000
        ):
            raise ValueError("Healthy sensor requires fresh, complete readings")
        return self


class Login(Model):
    username: str = Field(min_length=1, max_length=80)
    password: str = Field(min_length=1, max_length=256)


class Versioned(Model):
    base_revision: int = Field(ge=0, le=4294967294, strict=True)
    boot_id: str = Field(min_length=1, max_length=64)


class ConfigChange(Versioned):
    config: Configuration


class OverrideChange(Versioned):
    light: Literal[0, 1]
    mode: Literal[-1, 0, 1]
