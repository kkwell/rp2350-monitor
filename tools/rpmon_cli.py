#!/usr/bin/env python3
"""Host CLI for the RP2350 protocol monitor.

The CLI intentionally prints newline-delimited JSON so another tool or model can
consume command responses and protocol events without scraping terminal text.
"""

from __future__ import annotations

import argparse
import json
import os
import select
import socket
import sys
import termios
import time
from typing import Any, Dict, Iterable, Optional, TextIO


DEFAULT_HOST = "192.168.4.1"
DEFAULT_PORT = 4242


class Transport:
    def send_json(self, payload: Dict[str, Any]) -> None:
        raise NotImplementedError

    def readline(self, timeout: float) -> Optional[str]:
        raise NotImplementedError

    def close(self) -> None:
        pass


class TcpTransport(Transport):
    def __init__(self, host: str, port: int, timeout: float) -> None:
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.file = self.sock.makefile("rwb", buffering=0)

    def send_json(self, payload: Dict[str, Any]) -> None:
        line = json.dumps(payload, separators=(",", ":")).encode("utf-8") + b"\n"
        self.file.write(line)

    def readline(self, timeout: float) -> Optional[str]:
        self.sock.settimeout(timeout)
        try:
            data = self.file.readline()
        except socket.timeout:
            return None
        if not data:
            return None
        return data.decode("utf-8", errors="replace").rstrip("\r\n")

    def close(self) -> None:
        self.file.close()
        self.sock.close()


class SerialTransport(Transport):
    def __init__(self, device: str, baud: int, timeout: float) -> None:
        try:
            import serial  # type: ignore
        except ImportError as exc:
            self.serial = None
            self.fd = self._open_posix_serial(device, baud)
            self._rx = bytearray()
            return
        self.fd = None
        self._rx = bytearray()
        self.serial = serial.Serial(device, baudrate=baud, timeout=timeout)
        time.sleep(0.2)

    def send_json(self, payload: Dict[str, Any]) -> None:
        line = json.dumps(payload, separators=(",", ":")).encode("utf-8") + b"\n"
        if self.serial is not None:
            self.serial.write(line)
            self.serial.flush()
        else:
            os.write(self.fd, line)
            termios.tcdrain(self.fd)

    def readline(self, timeout: float) -> Optional[str]:
        if self.serial is not None:
            self.serial.timeout = timeout
            data = self.serial.readline()
            if not data:
                return None
            return data.decode("utf-8", errors="replace").rstrip("\r\n")

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if b"\n" in self._rx:
                line, _, rest = self._rx.partition(b"\n")
                self._rx = bytearray(rest)
                return line.decode("utf-8", errors="replace").rstrip("\r")
            remaining = max(0.0, deadline - time.monotonic())
            readable, _, _ = select.select([self.fd], [], [], min(remaining, 0.25))
            if readable:
                chunk = os.read(self.fd, 256)
                if chunk:
                    self._rx.extend(chunk)
        return None

    def close(self) -> None:
        if self.serial is not None:
            self.serial.close()
        elif self.fd is not None:
            os.close(self.fd)

    @staticmethod
    def _open_posix_serial(device: str, baud: int) -> int:
        fd = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(fd)
        speed = getattr(termios, f"B{baud}", None)
        if speed is None:
            os.close(fd)
            raise SystemExit(f"unsupported serial baud rate without pyserial: {baud}")

        attrs[0] = 0
        attrs[1] = 0
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[3] = 0
        attrs[4] = speed
        attrs[5] = speed
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        termios.tcflush(fd, termios.TCIOFLUSH)
        return fd


def parse_tcp(value: str, fallback_port: int) -> tuple[str, int]:
    if ":" not in value:
        return value, fallback_port
    host, port = value.rsplit(":", 1)
    return host, int(port)


def connect(args: argparse.Namespace) -> Transport:
    if args.serial:
        return SerialTransport(args.serial, args.serial_baud, args.timeout)
    host, port = parse_tcp(args.tcp, args.port)
    return TcpTransport(host, port, args.timeout)


def print_json_line(line: str, log_file: Optional[TextIO] = None) -> Optional[Dict[str, Any]]:
    print(line, flush=True)
    if log_file is not None:
        log_file.write(line + "\n")
        log_file.flush()
    try:
        return json.loads(line)
    except json.JSONDecodeError:
        return None


def transact(transport: Transport, payload: Dict[str, Any], timeout: float, log_file: Optional[TextIO] = None) -> int:
    transport.send_json(payload)
    deadline = time.monotonic() + timeout
    status = 1
    while time.monotonic() < deadline:
        line = transport.readline(max(0.05, deadline - time.monotonic()))
        if line is None:
            break
        doc = print_json_line(line, log_file)
        if doc and doc.get("type") == "resp" and doc.get("cmd") == payload.get("cmd"):
            status = 0 if doc.get("ok") else 2
            break
    return status


def expect_response(
    transport: Transport,
    payload: Dict[str, Any],
    timeout: float,
    log_file: Optional[TextIO] = None,
) -> tuple[bool, list[Dict[str, Any]]]:
    transport.send_json(payload)
    deadline = time.monotonic() + timeout
    seen: list[Dict[str, Any]] = []
    while time.monotonic() < deadline:
        line = transport.readline(max(0.05, deadline - time.monotonic()))
        if line is None:
            break
        doc = print_json_line(line, log_file)
        if not doc:
            continue
        seen.append(doc)
        if doc.get("type") == "resp" and doc.get("cmd") == payload.get("cmd"):
            return bool(doc.get("ok")), seen
    return False, seen


def uart_loopback_test(transport: Transport, args: argparse.Namespace, log_file: Optional[TextIO] = None) -> int:
    expected_hex = args.hex.lower()
    rx_stream = ""

    def record_rx(doc: Dict[str, Any]) -> bool:
        nonlocal rx_stream
        if (
            doc.get("type") == "event"
            and doc.get("channel") == args.id
            and doc.get("proto") == "uart"
            and doc.get("dir") == "rx"
        ):
            rx_stream += str(doc.get("hex", "")).lower()
            return expected_hex in rx_stream
        return False

    steps = [
        {
            "cmd": "channel_config",
            "id": args.id,
            "type": "uart",
            "instance": args.instance,
            "tx": args.tx,
            "rx": args.rx,
            "baud": args.baud,
            "loopback": True,
        },
        {"cmd": "channel_start", "id": args.id},
        {"cmd": "channel_write", "id": args.id, "hex": args.hex},
    ]
    for payload in steps:
        ok, seen = expect_response(transport, payload, args.timeout, log_file)
        if any(record_rx(doc) for doc in seen):
            if args.stop:
                expect_response(transport, {"cmd": "channel_stop", "id": args.id}, args.timeout, log_file)
            return 0
        if not ok:
            return 2

    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        line = transport.readline(max(0.05, deadline - time.monotonic()))
        if line is None:
            break
        doc = print_json_line(line, log_file)
        if (
            doc
            and doc.get("type") == "event"
            and doc.get("channel") == args.id
            and doc.get("proto") == "uart"
            and doc.get("dir") == "rx"
            and record_rx(doc)
        ):
            if args.stop:
                expect_response(transport, {"cmd": "channel_stop", "id": args.id}, args.timeout, log_file)
            return 0

    if args.stop:
        expect_response(transport, {"cmd": "channel_stop", "id": args.id}, args.timeout, log_file)
    return 3


def stream(transport: Transport, timeout: float, log_file: Optional[TextIO] = None) -> int:
    while True:
        line = transport.readline(timeout)
        if line is not None:
            print_json_line(line, log_file)


def command_payload(args: argparse.Namespace) -> Dict[str, Any]:
    cmd = args.command
    if cmd in {"hello", "status", "pins", "channels", "wifi_scan", "wifi_ap", "buffer_status"}:
        return {"cmd": cmd}
    if cmd == "events_read":
        payload = {"cmd": cmd, "count": args.count}
        if args.since_seq is not None:
            payload["since_seq"] = args.since_seq
        return payload
    if cmd == "wifi_connect":
        payload: Dict[str, Any] = {"cmd": cmd}
        if args.slot is not None:
            payload["slot"] = args.slot
        return payload
    if cmd == "wifi_clear":
        return {"cmd": cmd, "slot": args.slot}
    if cmd == "wifi_set":
        return {
            "cmd": "wifi_set",
            "slot": args.slot,
            "ssid": args.ssid,
            "password": args.password or "",
            "save": not args.no_save,
        }
    if cmd == "config_uart":
        payload = {
            "cmd": "channel_config",
            "id": args.id,
            "type": "uart",
            "instance": args.instance,
            "tx": args.tx,
            "rx": args.rx,
            "baud": args.baud,
        }
        if args.loopback:
            payload["loopback"] = True
        return payload
    if cmd == "config_spi":
        payload: Dict[str, Any] = {
            "cmd": "channel_config",
            "id": args.id,
            "type": "spi",
            "instance": args.instance,
            "sck": args.sck,
            "mosi": args.mosi,
            "miso": args.miso,
            "baud": args.baud,
        }
        if args.cs is not None:
            payload["cs"] = args.cs
        return payload
    if cmd == "config_i2c":
        payload = {
            "cmd": "channel_config",
            "id": args.id,
            "type": "i2c",
            "instance": args.instance,
            "sda": args.sda,
            "scl": args.scl,
            "baud": args.baud,
        }
        if args.address is not None:
            payload["address"] = args.address
        return payload
    if cmd == "config_gpio":
        return {
            "cmd": "channel_config",
            "id": args.id,
            "type": "gpio",
            "gpio": args.gpio,
            "direction": args.direction,
            "pull": args.pull,
            "initial": bool(args.initial),
        }
    if cmd == "config_can":
        return {"cmd": "channel_config", "id": args.id, "type": "can"}
    if cmd == "start":
        return {"cmd": "channel_start", "id": args.id}
    if cmd == "stop":
        return {"cmd": "channel_stop", "id": args.id}
    if cmd == "release":
        return {"cmd": "channel_release", "id": args.id}
    if cmd == "write":
        return {"cmd": "channel_write", "id": args.id, "hex": args.hex}
    if cmd == "spi_xfer":
        payload = {"cmd": "spi_xfer", "id": args.id, "hex": args.hex}
        if args.read_len is not None:
            payload["read_len"] = args.read_len
        return payload
    if cmd == "i2c_xfer":
        return {
            "cmd": "i2c_xfer",
            "id": args.id,
            "addr": args.addr,
            "write": args.write or "",
            "read_len": args.read_len,
        }
    if cmd == "gpio_read":
        return {"cmd": "gpio_read", "id": args.id}
    if cmd == "gpio_write":
        return {"cmd": "gpio_write", "id": args.id, "level": bool(args.level)}
    if cmd == "raw_json":
        return json.loads(args.payload)
    raise SystemExit(f"unsupported command: {cmd}")


def add_common_channel_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--id", type=int, required=True)
    parser.add_argument("--instance", type=int, default=0, choices=[0, 1])


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="RP2350 Monitor host CLI")
    parser.add_argument("--tcp", default=DEFAULT_HOST, help="TCP host or host:port, default: 192.168.4.1")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--serial", help="USB CDC serial device, e.g. /dev/tty.usbmodemXXXX")
    parser.add_argument("--baud", dest="serial_baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--log", help="Append received JSON lines to this JSONL file")

    sub = parser.add_subparsers(dest="command", required=True)
    for name in ["hello", "status", "pins", "channels", "wifi_scan", "wifi_ap", "buffer_status"]:
        sub.add_parser(name)

    events_read = sub.add_parser("events_read")
    events_read.add_argument("--count", type=int, default=16)
    events_read.add_argument("--since-seq", type=int)

    wifi_connect = sub.add_parser("wifi_connect")
    wifi_connect.add_argument("--slot", type=int, choices=range(3))

    wifi_clear = sub.add_parser("wifi_clear")
    wifi_clear.add_argument("--slot", type=int, required=True, choices=range(3))

    wifi_set = sub.add_parser("wifi_set")
    wifi_set.add_argument("--slot", type=int, default=0, choices=range(3))
    wifi_set.add_argument("--ssid", required=True)
    wifi_set.add_argument("--password", default="")
    wifi_set.add_argument("--no-save", action="store_true")

    uart = sub.add_parser("config_uart")
    add_common_channel_args(uart)
    uart.add_argument("--tx", type=int, required=True)
    uart.add_argument("--rx", type=int, required=True)
    uart.add_argument("--baud", type=int, default=115200)
    uart.add_argument("--loopback", action="store_true")

    spi = sub.add_parser("config_spi")
    add_common_channel_args(spi)
    spi.add_argument("--sck", type=int, required=True)
    spi.add_argument("--mosi", type=int, required=True)
    spi.add_argument("--miso", type=int, required=True)
    spi.add_argument("--cs", type=int)
    spi.add_argument("--baud", type=int, default=1_000_000)

    i2c = sub.add_parser("config_i2c")
    add_common_channel_args(i2c)
    i2c.add_argument("--sda", type=int, required=True)
    i2c.add_argument("--scl", type=int, required=True)
    i2c.add_argument("--baud", type=int, default=100_000)
    i2c.add_argument("--address", type=lambda v: int(v, 0))

    gpio = sub.add_parser("config_gpio")
    gpio.add_argument("--id", type=int, required=True)
    gpio.add_argument("--gpio", type=int, required=True)
    gpio.add_argument("--direction", choices=["input", "output"], default="input")
    gpio.add_argument("--pull", choices=["none", "up", "down"], default="none")
    gpio.add_argument("--initial", type=int, choices=[0, 1], default=0)

    can = sub.add_parser("config_can")
    can.add_argument("--id", type=int, required=True)

    start = sub.add_parser("start")
    start.add_argument("--id", type=int, required=True)

    stop = sub.add_parser("stop")
    stop.add_argument("--id", type=int, required=True)

    release = sub.add_parser("release")
    release.add_argument("--id", type=int, required=True)

    write = sub.add_parser("write")
    write.add_argument("--id", type=int, required=True)
    write.add_argument("--hex", required=True)

    spi_xfer = sub.add_parser("spi_xfer")
    spi_xfer.add_argument("--id", type=int, required=True)
    spi_xfer.add_argument("--hex", required=True)
    spi_xfer.add_argument("--read-len", type=int)

    i2c_xfer = sub.add_parser("i2c_xfer")
    i2c_xfer.add_argument("--id", type=int, required=True)
    i2c_xfer.add_argument("--addr", type=lambda v: int(v, 0), required=True)
    i2c_xfer.add_argument("--write", default="")
    i2c_xfer.add_argument("--read-len", type=int, default=0)

    gpio_read = sub.add_parser("gpio_read")
    gpio_read.add_argument("--id", type=int, required=True)

    gpio_write = sub.add_parser("gpio_write")
    gpio_write.add_argument("--id", type=int, required=True)
    gpio_write.add_argument("--level", type=int, choices=[0, 1], required=True)

    raw = sub.add_parser("raw_json")
    raw.add_argument("payload")

    loopback = sub.add_parser("uart_loopback_test")
    loopback.add_argument("--id", type=int, default=7)
    loopback.add_argument("--instance", type=int, default=0, choices=[0, 1])
    loopback.add_argument("--tx", type=int, default=0)
    loopback.add_argument("--rx", type=int, default=1)
    loopback.add_argument("--baud", type=int, default=115200)
    loopback.add_argument("--hex", default="52504d4f4e2d4c4f4f504241434b")
    loopback.add_argument("--stop", action="store_true")

    sub.add_parser("monitor")
    return parser


def main(argv: Optional[Iterable[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    transport = connect(args)
    log_file: Optional[TextIO] = None
    if args.log:
        log_file = open(args.log, "a", encoding="utf-8")
    try:
        if args.command == "monitor":
            return stream(transport, args.timeout, log_file)
        if args.command == "uart_loopback_test":
            return uart_loopback_test(transport, args, log_file)
        payload = command_payload(args)
        return transact(transport, payload, args.timeout, log_file)
    finally:
        if log_file is not None:
            log_file.close()
        transport.close()


if __name__ == "__main__":
    raise SystemExit(main())
