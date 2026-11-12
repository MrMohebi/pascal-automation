#!/bin/sh
# Run from any directory; credentials are created in this project's root.
set -eu
cd "$(dirname "$0")/.."
if [ -e .env ]; then
  echo '.env already exists; keeping your credentials. See docs/DEPLOYMENT.md to start the server.'
  exit 0
fi
task_image=ghcr.io/mrmohebi/pascal-automation:latest
docker pull "$task_image"
docker run --rm -it \
  --user "$(id -u):$(id -g)" \
  --mount "type=bind,src=$(pwd),dst=/config" \
  --workdir /config \
  --entrypoint python \
  "$task_image" /app/tools/setup_server.py
echo 'Setup complete. Start with: docker compose up -d'
