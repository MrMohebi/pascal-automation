"""Exercise the built image with disposable credentials, a volume, and a synthetic device."""

import os
import secrets
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import httpx

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from server.security import hash_password
from test.test_server import configuration, telemetry


def docker(*args):
    return subprocess.check_output(
        ["docker", *args], text=True, stderr=subprocess.STDOUT
    ).strip()


def main():
    name = "pascal-check-" + secrets.token_hex(5)
    volume = name + "-data"
    token = secrets.token_urlsafe(48)
    password = secrets.token_urlsafe(24)
    with tempfile.TemporaryDirectory(prefix="pascal-container-") as directory:
        env = Path(directory) / "environment"
        env.write_text(
            f"ADMIN_USER=admin\nADMIN_PASSWORD_HASH={hash_password(password)}\nDEVICE_TOKEN={token}\nCOOKIE_SECURE=false\n"
        )
        os.chmod(env, 0o600)
        docker("volume", "create", volume)

        def start():
            docker(
                "run",
                "-d",
                "--name",
                name,
                "--env-file",
                str(env),
                "--read-only",
                "--tmpfs",
                "/tmp",
                "--cap-drop",
                "ALL",
                "--security-opt",
                "no-new-privileges:true",
                "-v",
                volume + ":/data",
                "-p",
                "127.0.0.1::8000",
                "pascal-watcher:local",
            )
            address = docker("port", name, "8000/tcp")
            url = "http://" + address
            for _ in range(60):
                try:
                    if httpx.get(url + "/api/health", timeout=1).status_code == 200:
                        return url
                except httpx.HTTPError:
                    pass
                time.sleep(0.5)
            raise RuntimeError(
                "Container failed to become ready: " + docker("logs", name)
            )

        try:
            url = start()
            client = httpx.Client(base_url=url)
            assert client.get("/").status_code == 200
            assert client.get("/api/status").status_code == 401
            login = client.post(
                "/api/login", json={"username": "admin", "password": password}
            )
            assert login.status_code == 200
            client.headers["X-CSRF-Token"] = login.json()["csrf"]
            state = telemetry()
            headers = {"Authorization": "Bearer " + token}
            assert (
                client.post("/api/device/sync", json=state, headers=headers).status_code
                == 200
            )
            command = client.put(
                "/api/config",
                json={
                    "base_revision": 0,
                    "boot_id": state["boot_id"],
                    "config": configuration(),
                },
            )
            assert command.status_code == 200
            received = client.post(
                "/api/device/sync", json=state, headers=headers
            ).json()["command"]
            assert received["id"] == command.json()["id"]
            state.update(
                config=configuration(),
                revision=1,
                result={
                    "id": received["id"],
                    "status": "applied",
                    "message": "Container test device",
                },
            )
            assert (
                client.post("/api/device/sync", json=state, headers=headers).status_code
                == 200
            )
            assert (
                client.get("/api/status").json()["commands"][0]["status"] == "applied"
            )
            before = client.get("/api/history").json()["points"]
            assert before
            cookies = client.cookies
            client.close()
            docker("rm", "-f", name)
            url = start()
            client = httpx.Client(base_url=url, cookies=cookies)
            status = client.get("/api/status")
            assert status.status_code == 200
            assert status.json()["device"]["revision"] == 1
            assert client.get("/api/history").json()["points"] == before
            assert (
                client.get("/api/status").headers["x-content-type-options"] == "nosniff"
            )
            client.close()
            print(
                "Container checks passed: static dashboard, authentication, device sync, command acknowledgment, history/session persistence across container replacement."
            )
        finally:
            subprocess.run(
                ["docker", "rm", "-f", name],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            docker("volume", "rm", volume)


if __name__ == "__main__":
    main()
