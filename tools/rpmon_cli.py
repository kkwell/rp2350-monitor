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

from rpmon_logic import decode_command as logic_decode_command
from rpmon_logic import export_command as logic_export_command


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


def print_json_line_to_files(line: str, log_files: Iterable[TextIO]) -> Optional[Dict[str, Any]]:
    print(line, flush=True)
    for log_file in log_files:
        log_file.write(line + "\n")
        log_file.flush()
    try:
        return json.loads(line)
    except json.JSONDecodeError:
        return None


def expect_response_to_files(
    transport: Transport,
    payload: Dict[str, Any],
    timeout: float,
    log_files: Iterable[TextIO],
) -> tuple[bool, list[Dict[str, Any]]]:
    transport.send_json(payload)
    deadline = time.monotonic() + timeout
    seen: list[Dict[str, Any]] = []
    while time.monotonic() < deadline:
        line = transport.readline(max(0.05, deadline - time.monotonic()))
        if line is None:
            break
        doc = print_json_line_to_files(line, log_files)
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


def _setting(settings: Dict[str, Any], key: str, default: Any = None) -> Any:
    return settings[key] if key in settings else default


def _int_setting(settings: Dict[str, Any], key: str, default: Optional[int]) -> Optional[int]:
    value = _setting(settings, key, default)
    if value is None:
        return None
    return int(str(value), 0)


def _bool_setting(settings: Dict[str, Any], key: str, default: Optional[bool]) -> Optional[bool]:
    value = _setting(settings, key, default)
    if value is None:
        return None
    if isinstance(value, bool):
        return value
    if isinstance(value, int):
        return value != 0
    text = str(value).strip().lower()
    if text in {"1", "true", "yes", "on", "high"}:
        return True
    if text in {"0", "false", "no", "off", "low"}:
        return False
    raise SystemExit(f"invalid boolean value for {key}: {value}")


def load_logic_settings(path: Optional[str]) -> Dict[str, Any]:
    if not path:
        return {}
    with open(path, "r", encoding="utf-8") as handle:
        settings = json.load(handle)
    if not isinstance(settings, dict):
        raise SystemExit("logic capture settings must be a JSON object")
    return settings


def parse_channel_names(settings: Dict[str, Any], overrides: Optional[list[str]]) -> Dict[str, str]:
    names: Dict[str, str] = {}
    raw_names = settings.get("channel_names")
    if isinstance(raw_names, dict):
        for key, value in raw_names.items():
            names[str(int(str(key), 0))] = str(value)

    raw_channels = settings.get("channels")
    if isinstance(raw_channels, list):
        for item in raw_channels:
            if not isinstance(item, dict) or "name" not in item:
                continue
            gpio = item.get("gpio", item.get("pin", item.get("index")))
            if gpio is None:
                continue
            names[str(int(gpio))] = str(item["name"])

    for item in overrides or []:
        if "=" not in item:
            raise SystemExit("--channel-name must use GPIO=NAME, for example --channel-name 16=uart_rx")
        key, value = item.split("=", 1)
        names[str(int(key, 0))] = value
    return names


def normalize_logic_pull(value: Any) -> str:
    text = str(value).strip().lower()
    aliases = {
        "off": "none",
        "pullup": "up",
        "pull-up": "up",
        "pulldown": "down",
        "pull-down": "down",
    }
    text = aliases.get(text, text)
    if text not in {"none", "up", "down"}:
        raise SystemExit(f"invalid logic pull value: {value}")
    return text


def parse_logic_pin_pulls(
    settings: Dict[str, Any],
    overrides: Optional[list[str]],
    pin_base: Optional[int] = None,
    pin_count: Optional[int] = None,
) -> Dict[str, str]:
    pulls: Dict[str, str] = {}
    raw = settings.get("pin_pulls")
    if isinstance(raw, dict):
        for key, value in raw.items():
            pulls[str(int(str(key), 0))] = normalize_logic_pull(value)
    elif raw is not None:
        raise SystemExit("logic pin_pulls must be a JSON object")

    raw_list = settings.get("pulls")
    if isinstance(raw_list, list):
        if pin_base is None:
            raise SystemExit("settings pulls list requires pin_base")
        for index, value in enumerate(raw_list):
            if pin_count is not None and index >= pin_count:
                raise SystemExit("settings pulls list is longer than pin_count")
            pulls[str(int(pin_base) + index)] = normalize_logic_pull(value)
    elif raw_list is not None:
        raise SystemExit("logic pulls must be a JSON array when provided")

    for item in overrides or []:
        if "=" not in item:
            raise SystemExit("--pin-pull must use GPIO=MODE, for example --pin-pull 17=up")
        key, value = item.split("=", 1)
        pulls[str(int(key, 0))] = normalize_logic_pull(value)
    return pulls


def build_logic_capture_config(args: argparse.Namespace) -> tuple[Dict[str, Any], Dict[str, Any]]:
    settings = load_logic_settings(args.settings)
    trigger = settings.get("trigger") if isinstance(settings.get("trigger"), dict) else {}

    pin_base = args.pin_base if args.pin_base is not None else _int_setting(settings, "pin_base", None)
    pin_count = args.pin_count if args.pin_count is not None else _int_setting(settings, "pin_count", None)
    if pin_base is None or pin_count is None:
        raise SystemExit("logic_capture requires --pin-base/--pin-count or a settings file containing pin_base/pin_count")

    sample_rate = args.sample_rate if args.sample_rate is not None else _int_setting(settings, "sample_rate", 1_000_000)
    samples = args.samples if args.samples is not None else _int_setting(settings, "samples", 1024)
    pull = args.pull if args.pull is not None else str(settings.get("pull", "none"))
    pre_samples = args.pre_samples if args.pre_samples is not None else _int_setting(settings, "pre_samples", 0)
    post_samples = args.post_samples if args.post_samples is not None else _int_setting(settings, "post_samples", 0)
    search_samples = args.search_samples if args.search_samples is not None else _int_setting(settings, "search_samples", 0)
    burst_count = args.burst_count if args.burst_count is not None else _int_setting(settings, "burst_count", 1)

    trigger_pin = args.trigger_pin
    if trigger_pin is None:
        trigger_pin = _int_setting(settings, "trigger_pin", None)
    if trigger_pin is None and trigger:
        trigger_pin = _int_setting(trigger, "pin", None)

    trigger_mode = args.trigger_mode
    if trigger_mode is None:
        trigger_mode = str(settings.get("trigger_mode", trigger.get("mode", "level")))

    trigger_mask = args.trigger_mask
    if trigger_mask is None:
        trigger_mask = _int_setting(settings, "trigger_mask", None)
    if trigger_mask is None and trigger:
        trigger_mask = _int_setting(trigger, "mask", None)

    trigger_value = args.trigger_value
    if trigger_value is None:
        trigger_value = _int_setting(settings, "trigger_value", None)
    if trigger_value is None and trigger:
        trigger_value = _int_setting(trigger, "value", None)

    trigger_level = bool(args.trigger_level) if args.trigger_level is not None else _bool_setting(settings, "trigger_level", None)
    if trigger_level is None and trigger:
        trigger_level = _bool_setting(trigger, "level", True)
    if trigger_level is None:
        trigger_level = True

    config: Dict[str, Any] = {
        "cmd": "logic_config",
        "pin_base": int(pin_base),
        "pin_count": int(pin_count),
        "sample_rate": int(sample_rate),
        "samples": int(samples),
        "pull": pull,
        "pre_samples": int(pre_samples or 0),
        "post_samples": int(post_samples or 0),
        "search_samples": int(search_samples or 0),
        "burst_count": int(burst_count or 1),
    }
    if trigger_pin is not None:
        config["trigger_pin"] = int(trigger_pin)
        config["trigger_mode"] = trigger_mode
        config["trigger_level"] = bool(trigger_level)
    elif trigger_mode == "pattern":
        config["trigger_mode"] = trigger_mode
    if trigger_mask is not None:
        config["trigger_mask"] = int(trigger_mask)
    if trigger_value is not None:
        config["trigger_value"] = int(trigger_value)
    pin_pulls = parse_logic_pin_pulls(settings, args.pin_pull, int(pin_base), int(pin_count))
    if pin_pulls:
        config["pin_pulls"] = pin_pulls

    metadata = {key: value for key, value in config.items() if key != "cmd"}
    channel_names = parse_channel_names(settings, args.channel_name)
    if channel_names:
        metadata["channel_names"] = channel_names
    if args.settings:
        metadata["settings_file"] = args.settings
    return config, metadata


def logic_capture_workflow(
    transport: Transport,
    args: argparse.Namespace,
    log_file: Optional[TextIO] = None,
) -> int:
    output_file = open(args.output, "w", encoding="utf-8")
    logs = [output_file]
    if log_file is not None:
        logs.append(log_file)
    try:
        config, metadata = build_logic_capture_config(args)
        print_json_line_to_files(
            json.dumps({"type": "logic_settings", **metadata}, separators=(",", ":")),
            logs,
        )
        ok, _ = expect_response_to_files(transport, config, args.timeout, logs)
        if not ok:
            return 2
        ok, _ = expect_response_to_files(transport, {"cmd": "logic_start"}, args.timeout, logs)
        if not ok:
            return 2

        deadline = time.monotonic() + args.wait_timeout
        complete = False
        while time.monotonic() < deadline:
            ok, seen = expect_response_to_files(transport, {"cmd": "logic_status"}, args.timeout, logs)
            if not ok:
                return 2
            for doc in seen:
                logic = doc.get("logic")
                if isinstance(logic, dict):
                    complete = bool(logic.get("complete"))
                    running = bool(logic.get("running"))
                    if complete:
                        break
                    if not running and not complete:
                        if args.release:
                            expect_response_to_files(transport, {"cmd": "logic_release"}, args.timeout, logs)
                        return 2
            if complete:
                break
            time.sleep(args.poll_interval)

        if not complete:
            expect_response_to_files(transport, {"cmd": "logic_stop"}, args.timeout, logs)
            return 3

        ok, _ = expect_response_to_files(
            transport,
            {"cmd": "logic_read", "offset_words": 0, "count_words": 0},
            args.read_timeout,
            logs,
        )
        if args.release:
            expect_response_to_files(transport, {"cmd": "logic_release"}, args.timeout, logs)
        return 0 if ok else 2
    finally:
        output_file.close()


def stream(transport: Transport, timeout: float, log_file: Optional[TextIO] = None) -> int:
    while True:
        line = transport.readline(timeout)
        if line is not None:
            print_json_line(line, log_file)


def command_payload(args: argparse.Namespace) -> Dict[str, Any]:
    cmd = args.command
    if cmd in {
        "hello",
        "status",
        "pins",
        "channels",
        "wifi_scan",
        "wifi_ap",
        "buffer_status",
        "logic_start",
        "logic_stop",
        "logic_release",
        "logic_status",
        "logic_caps",
        "probe_caps",
        "probe_status",
        "probe_release",
    }:
        return {"cmd": cmd}
    if cmd == "events_read":
        payload = {"cmd": cmd, "count": args.count}
        if args.since_seq is not None:
            payload["since_seq"] = args.since_seq
        if args.channel is not None:
            payload["channel"] = args.channel
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
    if cmd == "logic_config":
        payload = {
            "cmd": "logic_config",
            "pin_base": args.pin_base,
            "pin_count": args.pin_count,
            "sample_rate": args.sample_rate,
            "samples": args.samples,
            "pull": args.pull,
            "pre_samples": args.pre_samples,
            "post_samples": args.post_samples,
            "search_samples": args.search_samples,
            "burst_count": args.burst_count,
        }
        if args.trigger_pin is not None:
            payload["trigger_pin"] = args.trigger_pin
            payload["trigger_mode"] = args.trigger_mode
            payload["trigger_level"] = bool(args.trigger_level)
        elif args.trigger_mode == "pattern":
            payload["trigger_mode"] = args.trigger_mode
        if args.trigger_mask is not None:
            payload["trigger_mask"] = args.trigger_mask
        if args.trigger_value is not None:
            payload["trigger_value"] = args.trigger_value
        pin_pulls = parse_logic_pin_pulls({}, args.pin_pull, args.pin_base, args.pin_count)
        if pin_pulls:
            payload["pin_pulls"] = pin_pulls
        return payload
    if cmd == "logic_read":
        return {"cmd": "logic_read", "offset_words": args.offset_words, "count_words": args.count_words}
    if cmd == "probe_config":
        payload = {
            "cmd": "probe_config",
            "swclk": args.swclk,
            "swdio": args.swdio,
            "reset": args.reset,
            "swclk_khz": args.swclk_khz,
        }
        return payload
    if cmd == "probe_reset":
        return {"cmd": "probe_reset", "action": args.action, "pulse_ms": args.pulse_ms}
    if cmd == "probe_dap":
        return {"cmd": "probe_dap", "packet_hex": args.hex}
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
    for name in ["hello", "status", "pins", "channels", "wifi_scan", "wifi_ap", "buffer_status", "probe_caps", "probe_status", "probe_release"]:
        sub.add_parser(name)

    events_read = sub.add_parser("events_read")
    events_read.add_argument("--count", type=int, default=16)
    events_read.add_argument("--since-seq", type=int)
    events_read.add_argument("--channel", type=int)

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

    logic_config = sub.add_parser("logic_config")
    logic_config.add_argument("--pin-base", type=int, required=True)
    logic_config.add_argument("--pin-count", type=int, required=True)
    logic_config.add_argument("--sample-rate", type=int, default=1_000_000)
    logic_config.add_argument("--samples", type=int, default=1024)
    logic_config.add_argument("--trigger-pin", type=int)
    logic_config.add_argument("--trigger-mode", choices=["level", "rising", "falling", "pattern"], default="level")
    logic_config.add_argument("--trigger-level", type=int, choices=[0, 1], default=1)
    logic_config.add_argument("--pull", choices=["none", "up", "down"], default="none")
    logic_config.add_argument("--pre-samples", type=int, default=0)
    logic_config.add_argument("--post-samples", type=int, default=0)
    logic_config.add_argument("--search-samples", type=int, default=0)
    logic_config.add_argument("--trigger-mask", type=lambda value: int(value, 0))
    logic_config.add_argument("--trigger-value", type=lambda value: int(value, 0))
    logic_config.add_argument("--burst-count", type=int, default=1)
    logic_config.add_argument("--pin-pull", action="append", help="Override one analyzer input bias as GPIO=none|up|down; repeatable")

    sub.add_parser("logic_start")
    sub.add_parser("logic_stop")
    sub.add_parser("logic_release")
    sub.add_parser("logic_status")
    sub.add_parser("logic_caps")

    logic_read = sub.add_parser("logic_read")
    logic_read.add_argument("--offset-words", type=int, default=0)
    logic_read.add_argument("--count-words", type=int, default=0)

    probe_config = sub.add_parser("probe_config")
    probe_config.add_argument("--swclk", type=int, default=2)
    probe_config.add_argument("--swdio", type=int, default=3)
    probe_config.add_argument("--reset", type=int, default=1, help="nRESET GPIO, or -1 to disable")
    probe_config.add_argument("--swclk-khz", type=int, default=1000)

    probe_reset = sub.add_parser("probe_reset")
    probe_reset.add_argument("--action", choices=["pulse", "assert", "release"], default="pulse")
    probe_reset.add_argument("--pulse-ms", type=int, default=50)

    probe_dap = sub.add_parser("probe_dap")
    probe_dap.add_argument("--hex", required=True, help="Raw CMSIS-DAP packet hex, up to 64 bytes")

    logic_capture = sub.add_parser("logic_capture")
    logic_capture.add_argument("--settings", help="JSON capture settings file")
    logic_capture.add_argument("--pin-base", type=int)
    logic_capture.add_argument("--pin-count", type=int)
    logic_capture.add_argument("--sample-rate", type=int)
    logic_capture.add_argument("--samples", type=int)
    logic_capture.add_argument("--trigger-pin", type=int)
    logic_capture.add_argument("--trigger-mode", choices=["level", "rising", "falling", "pattern"])
    logic_capture.add_argument("--trigger-level", type=int, choices=[0, 1])
    logic_capture.add_argument("--pre-samples", type=int)
    logic_capture.add_argument("--post-samples", type=int)
    logic_capture.add_argument("--search-samples", type=int)
    logic_capture.add_argument("--trigger-mask", type=lambda value: int(value, 0))
    logic_capture.add_argument("--trigger-value", type=lambda value: int(value, 0))
    logic_capture.add_argument("--burst-count", type=int)
    logic_capture.add_argument("--pull", choices=["none", "up", "down"])
    logic_capture.add_argument("--pin-pull", action="append", help="Override one analyzer input bias as GPIO=none|up|down; repeatable")
    logic_capture.add_argument("--channel-name", action="append", help="Attach a capture label as GPIO=NAME; repeatable")
    logic_capture.add_argument("--output", required=True, help="Write raw logic JSONL capture")
    logic_capture.add_argument("--wait-timeout", type=float, default=10.0)
    logic_capture.add_argument("--read-timeout", type=float, default=20.0)
    logic_capture.add_argument("--poll-interval", type=float, default=0.05)
    logic_capture.add_argument("--release", action="store_true")

    logic_decode = sub.add_parser("logic_decode")
    logic_decode.add_argument("--input", required=True)
    logic_decode.add_argument("--capture-id", type=int)
    logic_decode.add_argument("--decoder", choices=["summary", "bursts", "edges", "uart", "spi", "i2c"], default="summary")
    logic_decode.add_argument("--output")
    logic_decode.add_argument("--start-sample", type=int)
    logic_decode.add_argument("--end-sample", type=int)
    logic_decode.add_argument("--pin", type=int, action="append", help="GPIO or relative pin for edge decode; repeatable")
    logic_decode.add_argument("--rx-pin", type=int)
    logic_decode.add_argument("--baud", type=int, default=115200)
    logic_decode.add_argument("--data-bits", type=int, default=8)
    logic_decode.add_argument("--parity", choices=["none", "even", "odd"], default="none")
    logic_decode.add_argument("--stop-bits", type=float, default=1.0)
    logic_decode.add_argument("--invert", action="store_true")
    logic_decode.add_argument("--sck-pin", type=int)
    logic_decode.add_argument("--mosi-pin", type=int)
    logic_decode.add_argument("--miso-pin", type=int)
    logic_decode.add_argument("--cs-pin", type=int)
    logic_decode.add_argument("--mode", type=int, choices=[0, 1, 2, 3], default=0)
    logic_decode.add_argument("--cs-active-high", action="store_true")
    logic_decode.add_argument("--word-bits", type=int, default=8)
    logic_decode.add_argument("--bit-order", choices=["msb", "lsb"], default="msb")
    logic_decode.add_argument("--sda-pin", type=int)
    logic_decode.add_argument("--scl-pin", type=int)

    logic_export = sub.add_parser("logic_export")
    logic_export.add_argument("--input", required=True)
    logic_export.add_argument("--capture-id", type=int)
    logic_export.add_argument("--format", choices=["csv", "vcd"], required=True)
    logic_export.add_argument("--output", required=True)
    logic_export.add_argument("--start-sample", type=int)
    logic_export.add_argument("--end-sample", type=int)

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
    if args.command == "logic_decode":
        return logic_decode_command(args)
    if args.command == "logic_export":
        return logic_export_command(args)

    transport = connect(args)
    log_file: Optional[TextIO] = None
    if args.log:
        log_file = open(args.log, "a", encoding="utf-8")
    try:
        if args.command == "monitor":
            return stream(transport, args.timeout, log_file)
        if args.command == "uart_loopback_test":
            return uart_loopback_test(transport, args, log_file)
        if args.command == "logic_capture":
            return logic_capture_workflow(transport, args, log_file)
        payload = command_payload(args)
        return transact(transport, payload, args.timeout, log_file)
    finally:
        if log_file is not None:
            log_file.close()
        transport.close()


if __name__ == "__main__":
    raise SystemExit(main())
