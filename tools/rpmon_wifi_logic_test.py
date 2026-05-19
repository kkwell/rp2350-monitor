#!/usr/bin/env python3
"""Validate RP2350-Monitor logic analyzer over Wi-Fi TCP.

Required network:
  The host must be connected to the board AP or the same LAN as the board.

Required wiring for full logic validation:
  GP0 -> GP18
  GP1 -> GP19
  GP2 -> GP16
  GP3 -> GP17
"""

from __future__ import annotations

import argparse
import json
import socket
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path


def http_status(host: str, timeout: float) -> dict:
    url = f"http://{host}/api/status"
    with urllib.request.urlopen(url, timeout=timeout) as response:  # noqa: S310 - local board URL
        return json.loads(response.read().decode("utf-8"))


def serial_status(serial: str, timeout: float) -> dict | None:
    cli = Path(__file__).with_name("rpmon_cli.py")
    result = subprocess.run(
        [sys.executable, str(cli), "--serial", serial, "--timeout", str(timeout), "status"],
        text=True,
        capture_output=True,
        check=False,
    )
    for line in reversed((result.stdout + "\n" + result.stderr).splitlines()):
        try:
            doc = json.loads(line)
        except json.JSONDecodeError:
            continue
        if doc.get("type") == "resp" and doc.get("cmd") == "status":
            return doc
    return None


def tcp_reachable(host: str, port: int, timeout: float) -> tuple[bool, str]:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True, ""
    except OSError as exc:
        return False, str(exc)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate RP2350 logic analyzer through Wi-Fi TCP.")
    parser.add_argument("--host", default="192.168.4.1", help="Board AP or station IP")
    parser.add_argument("--port", type=int, default=4242)
    parser.add_argument("--serial", help="Optional USB CDC device used only to read station status before Wi-Fi tests")
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--capture-timeout", type=float, default=12.0)
    parser.add_argument("--read-timeout", type=float, default=10.0)
    args = parser.parse_args()

    usb_status = serial_status(args.serial, args.timeout) if args.serial else None
    wifi = usb_status.get("wifi", {}) if isinstance(usb_status, dict) else {}
    if args.host == "192.168.4.1" and wifi.get("station_status") == "up" and wifi.get("station_ip") not in {"", "0.0.0.0", None}:
        args.host = str(wifi["station_ip"])
    if usb_status:
        print(json.dumps({
            "type": "wifi_usb_status",
            "ok": True,
            "station_status": wifi.get("station_status"),
            "station_ip": wifi.get("station_ip"),
            "ap_active": wifi.get("ap_active"),
            "ssid": wifi.get("ssid"),
        }, separators=(",", ":")))

    try:
        status = http_status(args.host, args.timeout)
    except (OSError, urllib.error.URLError, TimeoutError) as exc:
        tcp_ok, tcp_reason = tcp_reachable(args.host, args.port, args.timeout)
        hint = "Connect this computer to the RP2350-Monitor AP, or use the board station IP on the same LAN."
        category = "host-unreachable"
        if wifi.get("station_status") == "up" and wifi.get("station_ip") == args.host:
            category = "station-up-host-unreachable"
            hint = "The board is connected to Wi-Fi, but this host cannot reach it. Check router guest/client isolation, same-LAN policy, and firewall rules."
        print(json.dumps({
            "type": "wifi_reachability",
            "ok": False,
            "host": args.host,
            "port": args.port,
            "category": category,
            "reason": str(exc),
            "tcp_4242_reachable": tcp_ok,
            "tcp_reason": tcp_reason,
            "hint": hint,
        }, separators=(",", ":")))
        return 2

    print(json.dumps({"type": "wifi_http_status", "ok": True, "status": status}, separators=(",", ":")))

    loopback = Path(__file__).with_name("rpmon_logic_loopback_test.py")
    command = [
        sys.executable,
        str(loopback),
        "--tcp",
        f"{args.host}:{args.port}",
        "--timeout",
        str(args.timeout),
        "--capture-timeout",
        str(args.capture_timeout),
        "--read-timeout",
        str(args.read_timeout),
    ]
    result = subprocess.run(command, text=True, check=False)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
