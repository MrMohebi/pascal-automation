#!/usr/bin/env python3
"""Provision or inspect an ESP32. Requires pyserial (pip install pyserial==3.5)."""

import argparse
import getpass
import json
import time
from pathlib import Path

import serial


def exchange(port, request):
    link = serial.Serial(port=None, baudrate=115200, timeout=0.3, write_timeout=5)
    link.dtr = False
    link.rts = False
    link.port = port
    try:
        link.open()
        # USB bridges can reset some boards on open despite modem-line settings.
        time.sleep(2)
        link.reset_input_buffer()
        link.write((json.dumps(request, separators=(",", ":")) + "\n").encode())
        link.flush()
        network_check = request["op"] == "network-status"
        deadline = time.monotonic() + (45 if network_check else 10)
        next_check = time.monotonic() + 2
        last_status = None
        while time.monotonic() < deadline:
            if network_check and time.monotonic() >= next_check:
                link.write(b'{"op":"network-status"}\n')
                link.flush()
                next_check = time.monotonic() + 2
            try:
                response = json.loads(link.readline().decode(errors="replace"))
            except (ValueError, UnicodeError):
                continue
            if "error" in response:
                raise RuntimeError(response["error"])
            if (request["op"] == "status" and "protocol" in response) or response.get(
                "ok"
            ):
                if network_check:
                    last_status = response
                    if not response.get("server_connected"):
                        continue
                return response
        if last_status is not None:
            return last_status
        raise RuntimeError(
            "No reply from Pascal firmware; check the port and close other serial monitors."
        )
    finally:
        link.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("status")
    commands.add_parser(
        "network-status", help="Check Wi-Fi and successful server synchronization"
    )
    clock = commands.add_parser("clock")
    clock.add_argument("value", help="Tehran local time as YYYYMMDDHHMM")
    provision = commands.add_parser("provision")
    provision.add_argument(
        "--ca",
        type=Path,
        required=True,
        help="PEM root CA matching the server certificate",
    )
    args = parser.parse_args()
    request = {"op": args.command}
    if args.command == "clock":
        request["value"] = args.value
    elif args.command == "provision":
        request.update(
            ssid=input("2.4 GHz Wi-Fi SSID: "),
            password=getpass.getpass("Wi-Fi password: "),
            url=input("Server URL (https://…): ").rstrip("/"),
            ca=args.ca.read_text(),
        )
        if not request["url"].startswith("https://"):
            raise SystemExit("An HTTPS server URL is required.")
    try:
        print(json.dumps(exchange(args.port, request), indent=2))
    except (RuntimeError, serial.SerialException) as error:
        raise SystemExit(str(error)) from error


if __name__ == "__main__":
    main()
