#!/usr/bin/env python3
"""Ask for one dashboard password and save it privately for Docker Compose."""

import getpass
import os
from pathlib import Path


def main():
    target = Path(".env")
    if target.exists():
        raise SystemExit(
            ".env already exists; preserve it or move it aside deliberately before generating new credentials."
        )
    password = getpass.getpass("Dashboard password: ")
    if not 1 <= len(password) <= 256 or any(c in password for c in "\n\r\0"):
        raise SystemExit("Use a password of 1 to 256 characters on one line.")
    # Quote for Compose's dotenv parser, including its dollar interpolation.
    escaped = password.replace("\\", "\\\\").replace('"', '\\"').replace("$", "$$")
    content = f'ADMIN_PASSWORD="{escaped}"\n'
    fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as file:
        file.write(content)
    print(
        "Created .env with private permissions. Keep this file out of source control."
    )


if __name__ == "__main__":
    main()
