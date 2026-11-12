#!/usr/bin/env python3
"""Generate local server credentials without printing passwords/tokens."""

import getpass
import os
import secrets
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from server.security import hash_password


def main():
    target = Path(".env")
    if target.exists():
        raise SystemExit(
            ".env already exists; preserve it or move it aside deliberately before generating new credentials."
        )
    username = input("Admin username [admin]: ").strip() or "admin"
    domain = input("Public hostname (blank for local development): ").strip()
    if any(c in username + domain for c in "\n\r'\"$ "):
        raise SystemExit(
            "Use a username/hostname without quotes, spaces or shell characters."
        )
    password = getpass.getpass("Admin password (at least 12 characters): ")
    if len(password) < 12 or password != getpass.getpass("Repeat password: "):
        raise SystemExit("Password too short or confirmation did not match.")
    content = f"ADMIN_USER={username}\nADMIN_PASSWORD_HASH='{hash_password(password)}'\nDEVICE_TOKEN={secrets.token_urlsafe(48)}\nCOOKIE_SECURE=true\nRETENTION_DAYS=90\nPASCAL_DOMAIN={domain}\n"
    fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as file:
        file.write(content)
    print(
        "Created .env with private permissions. Keep this file out of source control."
    )


if __name__ == "__main__":
    main()
