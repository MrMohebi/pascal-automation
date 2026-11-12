# Dashboard deployment

## Run the GitHub Packages image

The [Publish Docker image workflow](https://github.com/MrMohebi/pascal-automation/actions/workflows/docker-publish.yml) builds a Linux AMD64 image on GitHub-hosted runners. It runs API and container persistence checks before publishing the exact tested image. Pushes to `main` publish `latest` and `sha-<full-commit>`; `v*` tags publish their tag name and commit tag without moving `latest`. You can also run it manually on `main`. No self-hosted runner or registry password secret is needed: publishing uses the workflow's `GITHUB_TOKEN` with `packages: write`.

Image: **`ghcr.io/mrmohebi/pascal-automation:latest`**. The repository is private; the package starts private too. On the server, sign in with your GitHub username and a classic personal access token with `read:packages`, entered at Docker's password prompt. See [GitHub's registry authentication instructions](https://docs.github.com/en/packages/working-with-a-github-packages-registry/working-with-the-container-registry#authenticating-with-a-personal-access-token-classic).

```sh
docker login ghcr.io -u MrMohebi
```

Clone this repository (or `git pull` in your existing checkout), then select the published image and generate credentials if `.env` does not exist:

```sh
export PASCAL_IMAGE=ghcr.io/mrmohebi/pascal-automation:latest
sh tools/setup_server_docker.sh
```

Add `PASCAL_IMAGE=ghcr.io/mrmohebi/pascal-automation:latest` to your private `.env` to retain the image choice for future Compose commands. For a pinned deployment, use a published `sha-<full-commit>` tag instead.

With your hostname configured and DNS pointing to the server, start with bundled HTTPS:

```sh
docker compose -f compose.yaml -f compose.https.yaml up -d --no-build --pull always
```

If you use an existing HTTPS reverse proxy, use `docker compose up -d --no-build --pull always` and the routing guidance below. Repeat the applicable command to pull an update; `.env` and the persistent database volume are retained. Keep `--no-build` so Compose uses the published image. The local-build path below remains available.

## Quick start with Docker only

The Dockerfile packages the React dashboard and FastAPI API in one image. The server needs Docker with the Compose plugin; host Python and Node are not required. Run these commands on your server:

```sh
git clone https://github.com/MrMohebi/pascal-automation.git
cd pascal-automation
sh tools/setup_server_docker.sh
```

The setup command builds the image and prompts for an admin username, password, and public hostname (for example `terrarium.example.com`, without `https://`). It creates `.env` in the project directory with private permissions and your host user's ownership. An existing `.env` is preserved; rerunning setup does not reset your password or device token.

For a server without an existing web proxy, point that hostname's DNS at the server and make ports 80/443 available, then start the app with automatic HTTPS:

```sh
docker compose -f compose.yaml -f compose.https.yaml up -d --build
```

Open `https://YOUR_HOSTNAME` and sign in with the credentials you chose. If you already have an HTTPS proxy, use the **Existing HTTPS reverse proxy** section below instead.

Check startup and logs:

```sh
docker compose -f compose.yaml -f compose.https.yaml ps
docker compose -f compose.yaml -f compose.https.yaml logs --tail=100
```

To update later, run `git pull` and repeat the same `up -d --build` command. The named database volume and `.env` survive container replacement. See [Docker Compose startup options](https://docs.docker.com/reference/cli/docker/compose/up/) for the command behavior.

## Updating for the web-only controller

Deploy this server version before using layout-v3 firmware: it accepts the new saved-schedule and persistent-light telemetry fields. Run `git pull` and your chosen Compose `up -d --build` command. Existing `.env` credentials and database volumes are retained; older firmware telemetry remains accepted. Do not rerun credential generation to perform an update.

## Server credentials

Alternatively, if Python is already installed, generate credentials without building the image first. From this project directory:

```sh
python3 tools/setup_server.py
```

This creates a private `.env` containing an scrypt password hash, a random device token, retention, and optional public hostname. The script refuses to overwrite existing credentials. Keep `.env` out of source control and transfer the device token privately to your laptop when provisioning.

## Existing HTTPS reverse proxy

```sh
docker compose up -d --build
```

The app listens on **127.0.0.1:8000** on the host. Point your existing HTTPS reverse proxy to that address. If your proxy runs in a different container, join it and `pascal` to an appropriate shared Docker network and route to `pascal:8000`; localhost inside the proxy container is not the Docker host. Do not publish the application port publicly in plaintext.

The app does not trust forwarded client-IP headers. The login limiter therefore groups requests arriving through a proxy under its address. This is intentional for this single-owner dashboard; it avoids spoofable IP-based rate-limit bypasses. It allows ten failed logins per 15 minutes per observed client address.

## Bundled HTTPS proxy

Set `PASCAL_DOMAIN` in `.env` to a real hostname whose DNS resolves to the server. Ensure ports 80/443 are available and reachable. Then:

```sh
docker compose -f compose.yaml -f compose.https.yaml up -d --build
```

Caddy obtains and renews certificates. Existing proxies already using 80/443 should use the previous option instead. The device makes outbound HTTPS requests to this hostname; no router forwarding is needed at home.

For ESP32 provisioning, supply the root CA PEM matching the server's chain, obtained from the CA's official distribution. Check the certificate chain before choosing a CA; do not copy a leaf certificate as a long-lived trust anchor and do not disable certificate verification. Renewing a leaf certificate under the same trusted CA needs no firmware update; a CA change requires USB reprovisioning. Firmware gets time from NTP before TLS verification; USB clock entry is the fallback.

## Local development

Generate `.env` once, install requirements, and build `web/dist`. Run:

```sh
COOKIE_SECURE=false .venv/bin/python -m uvicorn server.app:create_app --factory --env-file .env --host 127.0.0.1 --port 8000
```

Use http://127.0.0.1:8000 for local browser development. `COOKIE_SECURE=false` is only for this loopback HTTP path. Compose forces secure cookies. For frontend hot reload, run `npm run dev` in `web`; Vite proxies `/api` to port 8000. Real ESP32 networking still requires HTTPS and a trusted CA.

## Persistence and backups

SQLite is in the named `pascal_data` volume. Container rebuilds/restarts keep readings, sessions, and command history. Do not use `docker compose down -v` unless deliberately deleting this data.

Create a consistent SQLite snapshot in the running container, then copy it out:

```sh
docker compose exec pascal python -c "import sqlite3; s=sqlite3.connect('/data/pascal.sqlite'); d=sqlite3.connect('/tmp/pascal-backup.sqlite'); s.backup(d); d.close(); s.close()"
docker compose cp pascal:/tmp/pascal-backup.sqlite ./pascal-backup.sqlite
```

Back up `.env` separately and protect both files. To restore, stop the app, replace the database in its volume from a trusted backup, remove stale WAL/SHM files while the app is stopped, restore ownership to UID 10001, then start and verify status/history. Do not copy a live database file without SQLite's backup API.

`GET /api/health` verifies the application can query SQLite; it does not prove device connectivity. Inspect the dashboard's last report, sensor status, and acknowledged settings for that.
