import sqlite3
import time

import pytest
from fastapi.testclient import TestClient

from server.app import Settings, create_app
from server.security import hash_password, verify_password


def test_only_password_env_is_needed(monkeypatch, tmp_path):
    for key in ("ADMIN_USER", "ADMIN_PASSWORD_HASH", "DEVICE_TOKEN"):
        monkeypatch.delenv(key, raising=False)
    monkeypatch.setenv("ADMIN_PASSWORD", "home-password")
    monkeypatch.setenv("DATABASE_PATH", str(tmp_path / "env.sqlite"))
    monkeypatch.setenv("COOKIE_SECURE", "true")
    client = TestClient(create_app(), base_url="https://testserver")
    assert client.post("/api/login", json={"password": "wrong"}).status_code == 401
    assert (
        client.post("/api/login", json={"password": "home-password"}).status_code == 200
    )
    assert client.get("/api/status").status_code == 200


def test_old_password_hash_and_new_password_priority(monkeypatch):
    monkeypatch.delenv("ADMIN_PASSWORD", raising=False)
    monkeypatch.setenv("ADMIN_PASSWORD_HASH", hash_password("old-password"))
    assert verify_password("old-password", Settings.from_env().password_hash)
    monkeypatch.setenv("ADMIN_PASSWORD", "new-password")
    settings = Settings.from_env()
    assert verify_password("new-password", settings.password_hash)
    assert not verify_password("old-password", settings.password_hash)


@pytest.mark.parametrize("password", [None, "", "x" * 257])
def test_missing_or_invalid_password_is_rejected(monkeypatch, password):
    monkeypatch.delenv("ADMIN_PASSWORD_HASH", raising=False)
    monkeypatch.delenv("ADMIN_PASSWORD", raising=False)
    if password is not None:
        monkeypatch.setenv("ADMIN_PASSWORD", password)
    with pytest.raises(RuntimeError, match="ADMIN_PASSWORD"):
        Settings.from_env()


def configuration():
    # Synthetic test data only; never installed as default care settings.
    return dict(
        enabled=True,
        day_temp_on=24,
        day_temp_off=26,
        night_temp_on=18,
        night_temp_off=20,
        day_rh_on=40,
        day_rh_off=50,
        night_rh_on=60,
        night_rh_off=70,
        max_temperature=32,
        day_start=480,
        day_end=1200,
        light1_on=480,
        light1_off=1200,
        light2_on=540,
        light2_off=1140,
        min_off_seconds=60,
        heater_max_seconds=600,
        humidifier_max_seconds=120,
    )


def telemetry():
    return dict(
        protocol=1,
        boot_id="test-boot",
        uptime_ms=10000,
        revision=0,
        config=None,
        temperature=24.5,
        humidity=45.0,
        sample_age_ms=100,
        sensor_ok=True,
        clock_ok=True,
        schedule_cached=False,
        light_modes_persistent=True,
        simulated=True,
        profile="unknown",
        reason="unconfigured",
        wifi_rssi=-50,
        display_ok=True,
        storage_ok=True,
        outputs=[False] * 4,
        faults=[],
        overrides=[dict(mode=-1, until=0)] * 2,
    )


@pytest.fixture
def rig(tmp_path):
    settings = Settings(
        str(tmp_path / "db.sqlite"),
        hash_password("correct-password"),
        False,
        static_dir=str(tmp_path / "absent"),
    )
    client = TestClient(create_app(settings))
    auth = client.post("/api/login", json={"password": "correct-password"})
    assert auth.status_code == 200
    client.headers["X-CSRF-Token"] = auth.json()["csrf"]
    return client, settings


def sync(rig, data=None):
    client, settings = rig
    return client.post(
        "/api/device/sync",
        json=data or telemetry(),
    )


def test_auth_and_csrf(rig):
    client, settings = rig
    outsider = TestClient(create_app(settings))
    assert outsider.get("/api/status").status_code == 401
    assert outsider.post("/api/device/sync", json=telemetry()).status_code == 200
    assert outsider.post("/api/login", json={"password": "wrong"}).status_code == 401
    assert (
        client.post("/api/logout", headers={"X-CSRF-Token": "wrong"}).status_code == 403
    )
    assert client.post("/api/logout").status_code == 200
    assert client.get("/api/session").status_code == 401


def test_device_sync_ignores_old_token_but_validates_payload(rig):
    _, settings = rig
    outsider = TestClient(create_app(settings))
    assert (
        outsider.post(
            "/api/device/sync",
            json=telemetry(),
            headers={"Authorization": "Bearer old-token"},
        ).status_code
        == 200
    )
    assert (
        outsider.post("/api/device/sync", json={"temperature": "broken"}).status_code
        == 422
    )
    assert outsider.put("/api/config", json={}).status_code == 401


def test_login_rate_limit(rig):
    client, _ = rig
    for _ in range(10):
        assert client.post("/api/login", json={"password": "wrong"}).status_code == 401
    assert client.post("/api/login", json={"password": "wrong"}).status_code == 429


def test_device_online_and_expiry(rig, monkeypatch):
    client, _ = rig
    now = time.time()
    monkeypatch.setattr("server.app.time.time", lambda: now)
    assert sync(rig).status_code == 200
    assert client.get("/api/status").json()["online"]
    monkeypatch.setattr("server.app.time.time", lambda: now + 30)
    assert not client.get("/api/status").json()["online"]
    assert (
        client.put(
            "/api/config",
            json={
                "base_revision": 0,
                "boot_id": "test-boot",
                "config": configuration(),
            },
        ).status_code
        == 409
    )


def test_config_acknowledged_not_optimistic(rig):
    client, _ = rig
    sync(rig)
    payload = {"base_revision": 0, "boot_id": "test-boot", "config": configuration()}
    response = client.put("/api/config", json=payload)
    assert response.status_code == 200
    status = client.get("/api/status").json()
    assert status["device"]["config"] is None
    assert status["commands"][0]["status"] == "pending"
    assert client.put("/api/config", json=payload).status_code == 409
    command = sync(rig).json()["command"]
    assert command["payload"] == configuration()
    data = telemetry()
    data.update(
        revision=1,
        config=configuration(),
        result={
            "id": command["id"],
            "status": "applied",
            "message": "Applied on device",
        },
    )
    assert sync(rig, data).json()["command"] is None
    status = client.get("/api/status").json()
    assert status["device"]["revision"] == 1
    assert status["commands"][0]["status"] == "applied"
    assert client.put("/api/config", json=payload).status_code == 409


@pytest.mark.parametrize(
    "change", [{"revision": 1, "config": configuration()}, {"boot_id": "another-boot"}]
)
def test_local_changes_and_reboot_reject_pending(rig, change):
    client, _ = rig
    sync(rig)
    client.put(
        "/api/config",
        json={"base_revision": 0, "boot_id": "test-boot", "config": configuration()},
    )
    data = telemetry()
    data.update(change)
    assert sync(rig, data).json()["command"] is None
    assert client.get("/api/status").json()["commands"][0]["status"] == "rejected"


def test_expired_command_never_replayed(rig, monkeypatch):
    client, _ = rig
    now = time.time()
    monkeypatch.setattr("server.app.time.time", lambda: now)
    sync(rig)
    client.put(
        "/api/config",
        json={"base_revision": 0, "boot_id": "test-boot", "config": configuration()},
    )
    monkeypatch.setattr("server.app.time.time", lambda: now + 61)
    assert sync(rig).json()["command"] is None
    assert client.get("/api/status").json()["commands"][0]["status"] == "expired"


def test_wrong_configuration_not_acknowledged(rig):
    client, _ = rig
    sync(rig)
    request = client.put(
        "/api/config",
        json={"base_revision": 0, "boot_id": "test-boot", "config": configuration()},
    ).json()
    data = telemetry()
    data.update(
        revision=1,
        config=configuration(),
        result={"id": request["id"], "status": "applied", "message": "OK"},
    )
    data["config"]["enabled"] = False
    sync(rig, data)
    assert client.get("/api/status").json()["commands"][0]["status"] == "rejected"


@pytest.mark.parametrize(
    "field,value",
    [
        ("day_temp_on", 27),
        ("max_temperature", 25),
        ("min_off_seconds", 0),
        ("day_rh_off", 101),
        ("light1_off", 480),
        ("day_start", 1440),
        ("heater_max_seconds", 1.5),
    ],
)
def test_invalid_configuration(rig, field, value):
    client, _ = rig
    sync(rig)
    c = configuration()
    c[field] = value
    assert (
        client.put(
            "/api/config",
            json={"base_revision": 0, "boot_id": "test-boot", "config": c},
        ).status_code
        == 422
    )


def test_override_interlocks_and_ack(rig):
    client, _ = rig
    data = telemetry()
    sync(rig, data)
    body = {"base_revision": 0, "boot_id": "test-boot", "light": 0, "mode": 1}
    assert client.post("/api/overrides", json=body).status_code == 409
    data.update(config=configuration(), profile="day")
    sync(rig, data)
    assert client.post("/api/overrides", json=body).status_code == 200
    command = sync(rig, data).json()["command"]
    data.update(
        result={
            "id": command["id"],
            "status": "rejected",
            "message": "Safety interlock",
        },
        faults=["overtemperature"],
    )
    sync(rig, data)
    assert client.post("/api/overrides", json=body).status_code == 409
    assert client.get("/api/status").json()["commands"][0]["status"] == "rejected"


def test_invalid_sensor_and_clock(rig):
    client, _ = rig
    data = telemetry()
    data["temperature"] = None
    assert sync(rig, data).status_code == 422
    data.update(sensor_ok=False, clock_ok=False)
    assert sync(rig, data).status_code == 200
    status = client.get("/api/status").json()
    assert len(status["alerts"]) >= 2
    assert (
        client.put(
            "/api/config",
            json={
                "base_revision": 0,
                "boot_id": "test-boot",
                "config": configuration(),
            },
        ).status_code
        == 409
    )


def test_history_retention_and_persistence(rig):
    client, settings = rig
    sync(rig)
    with sqlite3.connect(settings.database) as conn:
        conn.execute(
            "INSERT INTO telemetry(received,temperature,humidity,sensor_ok,outputs,simulated) VALUES(?,?,?,?,?,?)",
            (time.time() - 91 * 86400, 20, 40, 1, "[]", 1),
        )
    sync(rig)
    with sqlite3.connect(settings.database) as conn:
        assert conn.execute("SELECT count(*) FROM telemetry").fetchone()[0] == 2
    assert client.get("/api/history").json()["points"][0]["temperature"] == 24.5
    assert client.get("/api/history?hours=5").status_code == 422
    restarted = TestClient(create_app(settings))
    restarted.cookies.update(client.cookies)
    assert restarted.get("/api/status").json()["device"]["temperature"] == 24.5
    assert restarted.get("/api/history").json()["points"]


def test_bad_readings_create_history_gaps(rig):
    client, _ = rig
    data = telemetry()
    data.update(sensor_ok=False, temperature=None, humidity=None)
    sync(rig, data)
    point = client.get("/api/history").json()["points"][0]
    assert point["temperature"] is None and point["humidity"] is None


def test_secure_session_cookie(tmp_path):
    settings = Settings(
        str(tmp_path / "secure.sqlite"),
        hash_password("correct-password"),
    )
    client = TestClient(create_app(settings), base_url="https://testserver")
    response = client.post("/api/login", json={"password": "correct-password"})
    assert "Secure" in response.headers["set-cookie"]
    assert "HttpOnly" in response.headers["set-cookie"]
    assert "SameSite=strict" in response.headers["set-cookie"]
    assert client.get("/api/status").status_code == 200


def test_saved_schedule_alert_after_offline_restart(rig):
    client, _ = rig
    data = telemetry()
    data.update(
        config=configuration(),
        clock_ok=False,
        schedule_cached=True,
        profile="night",
        reason="running saved state",
        outputs=[False, True, False, True],
    )
    assert sync(rig, data).status_code == 200
    status = client.get("/api/status").json()
    assert any("using saved profile" in message for message in status["alerts"])
    assert not any(
        "Clock invalid; outputs inhibited" in message for message in status["alerts"]
    )
    assert status["device"]["outputs"] == data["outputs"]


@pytest.mark.parametrize(
    "reported_revision,reported_mode,expected",
    [(1, 1, "applied"), (0, 1, "rejected"), (1, 0, "rejected")],
)
def test_persistent_light_requires_saved_revision_and_matching_mode(
    rig, reported_revision, reported_mode, expected
):
    client, _ = rig
    data = telemetry()
    data.update(config=configuration(), profile="day", schedule_cached=True)
    sync(rig, data)
    response = client.post(
        "/api/overrides",
        json={"base_revision": 0, "boot_id": "test-boot", "light": 0, "mode": 1},
    )
    assert response.status_code == 200
    data.update(
        revision=reported_revision,
        overrides=[dict(mode=reported_mode, until=0), dict(mode=-1, until=0)],
        result=dict(id=response.json()["id"], status="applied", message="Saved"),
    )
    assert sync(rig, data).status_code == 200
    assert client.get("/api/status").json()["commands"][0]["status"] == expected
    if expected == "applied":
        data.update(boot_id="new-boot", clock_ok=False, result=None)
        assert sync(rig, data).status_code == 200
        restored = client.get("/api/status").json()["device"]
        assert restored["overrides"][0]["mode"] == 1
        assert restored["revision"] == 1
