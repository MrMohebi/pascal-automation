# Server deployment

One `compose.yaml` runs Pascal from **`ghcr.io/mrmohebi/pascal-automation:latest`** on port **8000**, with the database stored in the persistent `pascal_data` volume. Your existing reverse proxy handles HTTPS. There is no local build or bundled proxy.

## Run the GitHub Packages image

From your checkout on the server, sign in to the registry if needed:

```sh
docker login ghcr.io -u MrMohebi
```

Enter a GitHub classic token with `read:packages` at the password prompt ([GitHub authentication guide](https://docs.github.com/en/packages/working-with-a-github-packages-registry/working-with-the-container-registry#authenticating-with-a-personal-access-token-classic)).

Create a `.env` file with just your dashboard password:

```dotenv
ADMIN_PASSWORD='your-password-here'
```

Or run `sh tools/setup_server_docker.sh` to enter the password privately; it asks only once and saves `.env` for you. Existing `.env` files are kept unchanged. No username, password hash, device token, domain, or image variable is required. The server hashes the password in memory at startup. Keep `.env` private and outside source control. The application defaults to secure login cookies and 90 days of history.

Start:

```sh
docker compose up -d
```

Point your existing HTTPS proxy at **`http://SERVER_IP:8000`**, then open the HTTPS hostname and enter your password. The published port is reachable on the server's interfaces so your proxy can connect. If the proxy runs on the host, `http://127.0.0.1:8000` also works; localhost inside another container is that container, not the Docker host. Use the HTTPS hostname for browser login because login cookies are secure by default.

Use the same HTTPS hostname when provisioning the ESP32, with the root CA PEM matching the proxy certificate. No device token is needed. Firmware gets time from NTP before TLS verification; USB clock entry is the fallback.

Device sync is open: anyone who can reach `/api/device/sync` can submit readings and receive pending commands. The dashboard and its settings controls still require your password. Input validation and appliance safety interlocks remain active.

## Updates and status

Pull the new image, then replace the container:

```sh
docker compose pull
docker compose up -d
```

[Compose pull](https://docs.docker.com/reference/cli/docker/compose/pull/) downloads the image; `up -d` starts it. Settings and history stay in the same volume.

```sh
docker compose ps
docker compose logs --tail=100
```

The old `compose.https.yaml` and Caddy configuration are removed. If you previously ran the bundled Caddy service, stop and remove that old container; your existing HTTPS proxy now handles routing. Old `PASCAL_DOMAIN`, `PASCAL_IMAGE`, `ADMIN_USER`, and `DEVICE_TOKEN` entries in `.env` are no longer used and can be removed. An existing `ADMIN_PASSWORD_HASH` still works for upgrades; to simplify it, replace the old credential entries with `ADMIN_PASSWORD`. If both are present, `ADMIN_PASSWORD` takes priority. Previously provisioned firmware can still send its old token; the server ignores it. New token-free provisioning requires the updated firmware. Keep the same project directory/project name to reuse the existing database volume.

## Backups

Keep `.env` private and back it up alongside the database. Do not use `docker compose down -v` unless you intend to delete stored data.

Create a consistent SQLite backup:

```sh
docker compose exec pascal python -c "import sqlite3; s=sqlite3.connect('/data/pascal.sqlite'); d=sqlite3.connect('/tmp/pascal-backup.sqlite'); s.backup(d); d.close(); s.close()"
docker compose cp pascal:/tmp/pascal-backup.sqlite ./pascal-backup.sqlite
```

## Image publication

[GitHub Actions](https://github.com/MrMohebi/pascal-automation/actions/workflows/docker-publish.yml) builds Linux AMD64 images, tests them, and publishes `latest` plus `sha-<full-commit>` on pushes to `main`. To pin a version, replace `latest` in `compose.yaml` with the desired published commit tag. The workflow can also run manually on `main`; `v*` tags publish release tags without changing `latest`.

The container health check verifies database access. Actual device connectivity is shown separately by the dashboard's last report and acknowledged settings.
