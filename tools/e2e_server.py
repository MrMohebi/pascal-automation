"""Loopback-only disposable test server with a synthetic device. Never deploy this."""

import json
import math
import sqlite3
import sys
import tempfile
import threading
import time
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import uvicorn
from fastapi.staticfiles import StaticFiles

from server.app import Settings, create_app
from server.security import hash_password
from test.test_server import configuration, telemetry

directory = tempfile.TemporaryDirectory(prefix="pascal-e2e-")
settings = Settings(
    str(Path(directory.name) / "test.sqlite"),
    "admin",
    hash_password("e2e-password-only"),
    "e2e-device-only-" + "a" * 32,
    False,
    static_dir=str(Path(directory.name) / "no-static"),
)
app = create_app(settings)
state = telemetry()
paused = False
lock = threading.Lock()
delay_commands = 0


@app.post("/__test/scenario/{name}")
def scenario(name: str):
    global state, paused, delay_commands
    with lock:
        if name == "conflict":
            state["revision"] += 1
            if state["config"]:
                state["config"]["day_temp_on"] += 0.1
            return {"ok": True}
        if name == "offline":
            paused = True
            with sqlite3.connect(settings.database) as conn:
                conn.execute("UPDATE device SET received=?", (time.time() - 35,))
            return {"ok": True}
        if name == "fault":
            state["faults"] = ["overtemperature"]
            state["outputs"] = [False] * 4
            return {"ok": True}
        paused = False
        delay_commands = 0
        state = telemetry()
        if name == "configured":
            state.update(
                config=configuration(),
                revision=1,
                profile="day",
                reason="running",
                outputs=[False, False, True, True],
            )
        with sqlite3.connect(settings.database) as conn:
            conn.execute("DELETE FROM commands")
            conn.execute("DELETE FROM telemetry")
            conn.execute("DELETE FROM device")
            for n in range(100):
                conn.execute(
                    "INSERT INTO telemetry(received,temperature,humidity,sensor_ok,outputs,simulated) VALUES(?,?,?,?,?,?)",
                    (
                        time.time() - 86400 + n * 860,
                        24 + math.sin(n / 9),
                        45 + math.sin(n / 8) * 8,
                        1,
                        "[false,false,true,true]",
                        1,
                    ),
                )
    return {"ok": True}


def device():
    global delay_commands
    while True:
        time.sleep(0.5)
        with lock:
            if paused:
                continue
            state["uptime_ms"] += 500
            request = urllib.request.Request(
                "http://127.0.0.1:8765/api/device/sync",
                data=json.dumps(state).encode(),
                headers={
                    "Content-Type": "application/json",
                    "Authorization": "Bearer " + settings.device_token,
                },
            )
            try:
                with urllib.request.urlopen(request, timeout=2) as response:
                    command = json.load(response)["command"]
            except Exception:
                continue
            if command:
                # Keep pending visible long enough to test the real UI acknowledgment flow.
                delay_commands += 1
                if delay_commands < 4:
                    continue
                delay_commands = 0
                status = "applied"
                if command["base_revision"] != state["revision"]:
                    status = "rejected"
                elif command["kind"] == "config":
                    state["config"] = command["payload"]
                    state["revision"] += 1
                    state["profile"] = "day"
                    state["reason"] = "running"
                elif command["kind"] == "override":
                    light = command["payload"]["light"]
                    mode = command["payload"]["mode"]
                    state["overrides"][light] = {
                        "mode": mode,
                        "until": int(time.time()) + 3600,
                    }
                    state["outputs"][light + 2] = mode != 0
                elif command["kind"] == "acknowledge":
                    state["faults"] = []
                state["result"] = {
                    "id": command["id"],
                    "status": status,
                    "message": "Synthetic device acknowledgment",
                }


app.mount(
    "/",
    StaticFiles(
        directory=str(Path(__file__).resolve().parents[1] / "web/dist"), html=True
    ),
)

if __name__ == "__main__":
    threading.Thread(target=device, daemon=True).start()
    uvicorn.run(app, host="127.0.0.1", port=8765, log_level="warning")
