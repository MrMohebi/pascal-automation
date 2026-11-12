import json
import os
import secrets
import sqlite3
import threading
import time
from collections import defaultdict, deque
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path

from fastapi import Depends, FastAPI, Header, HTTPException, Request, Response
from fastapi.staticfiles import StaticFiles

from .models import ConfigChange, Login, OverrideChange, Telemetry, Versioned
from .security import token_hash, verify_password


@dataclass
class Settings:
    database: str
    admin_user: str
    password_hash: str
    device_token: str
    secure_cookie: bool = True
    retention_days: int = 90
    static_dir: str = "web/dist"

    @classmethod
    def from_env(cls):
        result = cls(
            database=os.getenv("DATABASE_PATH", "data/pascal.sqlite"),
            admin_user=os.getenv("ADMIN_USER", "admin"),
            password_hash=os.environ["ADMIN_PASSWORD_HASH"],
            device_token=os.environ["DEVICE_TOKEN"],
            secure_cookie=os.getenv("COOKIE_SECURE", "true").lower() == "true",
            retention_days=int(os.getenv("RETENTION_DAYS", "90")),
            static_dir=os.getenv("STATIC_DIR", "web/dist"),
        )
        if (
            len(result.device_token) < 32
            or not result.password_hash.startswith("scrypt$")
            or result.retention_days < 1
        ):
            raise RuntimeError("Run tools/setup_server.py to create valid credentials")
        return result


def create_app(settings: Settings | None = None) -> FastAPI:
    settings = settings or Settings.from_env()
    Path(settings.database).parent.mkdir(parents=True, exist_ok=True)

    @contextmanager
    def db():
        conn = sqlite3.connect(settings.database, timeout=10)
        conn.row_factory = sqlite3.Row
        try:
            with conn:
                yield conn
        finally:
            conn.close()

    with db() as conn:
        conn.executescript("""
            PRAGMA journal_mode=WAL;
            CREATE TABLE IF NOT EXISTS device (id INTEGER PRIMARY KEY CHECK(id=1), received REAL NOT NULL, snapshot TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS telemetry (id INTEGER PRIMARY KEY, received REAL NOT NULL, temperature REAL, humidity REAL, sensor_ok INTEGER NOT NULL, outputs TEXT NOT NULL, simulated INTEGER NOT NULL);
            CREATE INDEX IF NOT EXISTS telemetry_received ON telemetry(received);
            CREATE TABLE IF NOT EXISTS commands (id TEXT PRIMARY KEY, created REAL NOT NULL, expires_at INTEGER NOT NULL, boot_id TEXT NOT NULL, base_revision INTEGER NOT NULL, kind TEXT NOT NULL, payload TEXT NOT NULL, status TEXT NOT NULL, message TEXT NOT NULL DEFAULT 'Awaiting device acknowledgment');
            CREATE INDEX IF NOT EXISTS commands_created ON commands(created);
            CREATE TABLE IF NOT EXISTS sessions (token TEXT PRIMARY KEY, csrf TEXT NOT NULL, expires REAL NOT NULL);
        """)

    app = FastAPI(
        title="Pascal Watcher", version="1.0.0", docs_url=None, redoc_url=None
    )
    app.state.settings = settings
    attempts: dict[str, deque] = defaultdict(deque)
    attempts_lock = threading.Lock()

    @app.middleware("http")
    async def security_headers(request: Request, call_next):
        try:
            length = int(request.headers.get("content-length", "0"))
        except ValueError:
            return Response(status_code=400)
        if length < 0:
            return Response(status_code=400)
        if length > 32768:
            return Response(status_code=413)
        response = await call_next(request)
        response.headers["X-Content-Type-Options"] = "nosniff"
        response.headers["X-Frame-Options"] = "DENY"
        response.headers["Referrer-Policy"] = "same-origin"
        response.headers["Content-Security-Policy"] = (
            "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'self'; form-action 'self'"
        )
        if request.url.path.startswith("/api/"):
            response.headers["Cache-Control"] = "no-store"
        return response

    def session(request: Request):
        token = request.cookies.get("pascal_session", "")
        with db() as conn:
            row = conn.execute(
                "SELECT csrf,expires FROM sessions WHERE token=?", (token_hash(token),)
            ).fetchone()
        if not row or row["expires"] <= time.time():
            raise HTTPException(401, "Sign in required")
        if request.method not in ("GET", "HEAD") and not secrets.compare_digest(
            request.headers.get("x-csrf-token", "").encode(), row["csrf"].encode()
        ):
            raise HTTPException(403, "Invalid CSRF token")
        return row

    def device_auth(authorization: str = Header(default="")):
        if not secrets.compare_digest(
            authorization.encode(), ("Bearer " + settings.device_token).encode()
        ):
            raise HTTPException(401, "Invalid device token")

    def expire(conn, now):
        conn.execute(
            "UPDATE commands SET status='expired',message='Acknowledgment expired; inspect reported device state' WHERE status='pending' AND expires_at<=?",
            (now,),
        )

    def command_dict(row):
        if not row:
            return None
        result = dict(row)
        result["payload"] = json.loads(result["payload"])
        return result

    @app.get("/api/health")
    def health():
        with db() as conn:
            conn.execute("SELECT 1").fetchone()
        return {"ok": True}

    @app.post("/api/login")
    def login(body: Login, request: Request, response: Response):
        now = time.time()
        ip = request.client.host if request.client else "unknown"
        with attempts_lock:
            if len(attempts) >= 1024 and ip not in attempts:
                raise HTTPException(429, "Please try again later")
            recent = attempts[ip]
            while recent and recent[0] < now - 900:
                recent.popleft()
            if len(recent) >= 10:
                raise HTTPException(429, "Too many sign-in attempts; wait 15 minutes")
            recent.append(now)
        password_ok = verify_password(body.password, settings.password_hash)
        if not password_ok or not secrets.compare_digest(
            body.username.encode(), settings.admin_user.encode()
        ):
            raise HTTPException(401, "Incorrect username or password")
        with attempts_lock:
            attempts.pop(ip, None)
        token, csrf = secrets.token_urlsafe(32), secrets.token_urlsafe(32)
        with db() as conn:
            conn.execute("DELETE FROM sessions WHERE expires<=?", (now,))
            conn.execute(
                "INSERT INTO sessions VALUES (?,?,?)",
                (token_hash(token), csrf, now + 43200),
            )
        response.set_cookie(
            "pascal_session",
            token,
            max_age=43200,
            httponly=True,
            secure=settings.secure_cookie,
            samesite="strict",
            path="/",
        )
        return {"csrf": csrf, "username": settings.admin_user}

    @app.get("/api/session")
    def current_session(auth=Depends(session)):
        return {"csrf": auth["csrf"], "username": settings.admin_user}

    @app.post("/api/logout")
    def logout(request: Request, response: Response, auth=Depends(session)):
        with db() as conn:
            conn.execute(
                "DELETE FROM sessions WHERE token=?",
                (token_hash(request.cookies.get("pascal_session", "")),),
            )
        response.delete_cookie("pascal_session", path="/")
        return {"ok": True}

    @app.post("/api/device/sync", dependencies=[Depends(device_auth)])
    def sync(body: Telemetry):
        now = time.time()
        snapshot = body.model_dump()
        with db() as conn:
            conn.execute("BEGIN IMMEDIATE")
            if body.result:
                row = conn.execute(
                    "SELECT * FROM commands WHERE id=? AND boot_id=?",
                    (body.result.id, body.boot_id),
                ).fetchone()
                if row and row["status"] in ("pending", "expired"):
                    status, message = body.result.status, body.result.message
                    if (
                        status == "applied"
                        and row["kind"] == "config"
                        and (
                            body.revision != row["base_revision"] + 1
                            or snapshot["config"] != json.loads(row["payload"])
                        )
                    ):
                        status, message = (
                            "rejected",
                            "Reported settings do not match requested settings",
                        )
                    conn.execute(
                        "UPDATE commands SET status=?,message=? WHERE id=?",
                        (status, message, row["id"]),
                    )
            expire(conn, now)
            conn.execute(
                "UPDATE commands SET status='rejected',message='Device restarted or settings changed locally' WHERE status='pending' AND (boot_id!=? OR base_revision!=?)",
                (body.boot_id, body.revision),
            )
            conn.execute(
                "INSERT INTO device VALUES (1,?,?) ON CONFLICT(id) DO UPDATE SET received=excluded.received,snapshot=excluded.snapshot",
                (now, json.dumps(snapshot)),
            )
            conn.execute(
                "INSERT INTO telemetry(received,temperature,humidity,sensor_ok,outputs,simulated) VALUES(?,?,?,?,?,?)",
                (
                    now,
                    body.temperature if body.sensor_ok else None,
                    body.humidity if body.sensor_ok else None,
                    body.sensor_ok,
                    json.dumps(body.outputs),
                    body.simulated,
                ),
            )
            cutoff = now - settings.retention_days * 86400
            conn.execute("DELETE FROM telemetry WHERE received<?", (cutoff,))
            conn.execute("DELETE FROM commands WHERE created<?", (cutoff,))
            conn.execute("DELETE FROM sessions WHERE expires<=?", (now,))
            command = conn.execute(
                "SELECT * FROM commands WHERE status='pending' ORDER BY created LIMIT 1"
            ).fetchone()
        return {
            "protocol": 1,
            "server_time": int(now),
            "command": command_dict(command),
        }

    @app.get("/api/status", dependencies=[Depends(session)])
    def status():
        now = time.time()
        with db() as conn:
            expire(conn, now)
            row = conn.execute("SELECT * FROM device WHERE id=1").fetchone()
            commands = [
                command_dict(r)
                for r in conn.execute(
                    "SELECT * FROM commands ORDER BY created DESC LIMIT 20"
                )
            ]
        reported = json.loads(row["snapshot"]) if row else None
        online = bool(row and now - row["received"] < 30)
        alerts = []
        if not online:
            alerts.append(
                "Device offline — displayed values and output states are last reported"
            )
        if reported:
            if not reported["sensor_ok"]:
                alerts.append("Sensor unavailable or recovering; outputs inhibited")
            if not reported["clock_ok"]:
                alerts.append("Clock invalid; outputs inhibited")
            if not reported["display_ok"]:
                alerts.append("OLED not detected")
            if not reported["config"]:
                alerts.append("Setup required: enter and save all climate settings")
            elif not reported["config"]["enabled"]:
                alerts.append("Automation is disabled")
            elif reported["sensor_ok"]:
                cfg = reported["config"]
                profile = reported["profile"]
                if profile in ("day", "night"):
                    if (
                        not cfg[f"{profile}_temp_on"]
                        <= reported["temperature"]
                        <= cfg[f"{profile}_temp_off"]
                    ):
                        alerts.append(
                            "Temperature is outside the configured control band"
                        )
                    if (
                        not cfg[f"{profile}_rh_on"]
                        <= reported["humidity"]
                        <= cfg[f"{profile}_rh_off"]
                    ):
                        alerts.append("Humidity is outside the configured control band")
            alerts.extend(
                "Latched fault: " + fault.replace("_", " ")
                for fault in reported["faults"]
            )
        return {
            "online": online,
            "received_at": row["received"] if row else None,
            "device": reported,
            "alerts": alerts,
            "commands": commands,
            "server_time": now,
        }

    @app.get("/api/history", dependencies=[Depends(session)])
    def history(hours: int = 24):
        if hours not in (24, 168, 720):
            raise HTTPException(422, "Supported history windows: 24, 168, 720 hours")
        bucket = {24: 60, 168: 300, 720: 1800}[hours]
        with db() as conn:
            points = [
                dict(row)
                for row in conn.execute(
                    "SELECT CAST(received / ? AS INTEGER) * ? AS time, AVG(temperature) AS temperature, AVG(humidity) AS humidity, MIN(sensor_ok) AS sensor_ok, MIN(simulated) AS simulated FROM telemetry WHERE received>=? GROUP BY CAST(received / ? AS INTEGER) ORDER BY time",
                    (bucket, bucket, time.time() - hours * 3600, bucket),
                )
            ]
        return {"points": points, "bucket_seconds": bucket}

    def enqueue(kind: str, payload: dict, version: Versioned):
        now = time.time()
        with db() as conn:
            conn.execute("BEGIN IMMEDIATE")
            expire(conn, now)
            row = conn.execute("SELECT * FROM device WHERE id=1").fetchone()
            if not row or now - row["received"] >= 30:
                raise HTTPException(
                    409, "Device is offline; reconnect before changing settings"
                )
            current = json.loads(row["snapshot"])
            if not current["clock_ok"]:
                raise HTTPException(409, "Set the device clock before remote changes")
            if (
                version.base_revision != current["revision"]
                or version.boot_id != current["boot_id"]
            ):
                raise HTTPException(
                    409, "Device settings changed; refresh before saving"
                )
            if conn.execute("SELECT 1 FROM commands WHERE status='pending'").fetchone():
                raise HTTPException(
                    409, "Wait for the pending change to be acknowledged"
                )
            if kind == "override" and (
                not current["config"]
                or (
                    payload["mode"] != -1
                    and (
                        not current["config"]["enabled"]
                        or current["faults"]
                        or not current["sensor_ok"]
                    )
                )
            ):
                raise HTTPException(
                    409, "Light override blocked by setup or safety interlock"
                )
            command_id = secrets.token_hex(16)
            conn.execute(
                "INSERT INTO commands(id,created,expires_at,boot_id,base_revision,kind,payload,status) VALUES(?,?,?,?,?,?,?,'pending')",
                (
                    command_id,
                    now,
                    int(now) + 60,
                    version.boot_id,
                    version.base_revision,
                    kind,
                    json.dumps(payload),
                ),
            )
        return {"id": command_id, "status": "pending"}

    @app.put("/api/config", dependencies=[Depends(session)])
    def config(body: ConfigChange):
        return enqueue("config", body.config.model_dump(), body)

    @app.post("/api/overrides", dependencies=[Depends(session)])
    def override(body: OverrideChange):
        return enqueue("override", {"light": body.light, "mode": body.mode}, body)

    @app.post("/api/faults/acknowledge", dependencies=[Depends(session)])
    def acknowledge(body: Versioned):
        return enqueue("acknowledge", {}, body)

    if Path(settings.static_dir).is_dir():
        app.mount(
            "/", StaticFiles(directory=settings.static_dir, html=True), name="dashboard"
        )
    return app
