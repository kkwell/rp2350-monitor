#!/usr/bin/env python3
"""Local RP2350 Monitor UI bridge.

The bridge serves the browser-native monitor UI and translates the shared AI/UI
operation contract into the RP2350-Monitor JSONL protocol. It is intentionally
self-contained so the macOS GUI can bundle it directly without depending on the
development repo's helper CLI.
"""

from __future__ import annotations

import argparse
import json
import os
import select
import socket
import sys
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional

try:
    import termios
    import tty
except ImportError:  # Windows serial is handled by optional pyserial.
    termios = None  # type: ignore[assignment]
    tty = None  # type: ignore[assignment]


ROOT = Path(__file__).resolve().parents[1]
BAUD = 115200
READ_CHUNK_BYTES = 8192
LOGIC_INTERRUPT_ACTIONS = {
    "logic.configure",
    "logic.capture",
    "logic.stop",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Serve RP2350 Monitor UI with a local operation bridge")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5178)
    parser.add_argument("--serial", default="", help="USB CDC serial device, for example /dev/cu.usbmodemXXXX")
    parser.add_argument("--tcp", default="", help="Wi-Fi endpoint in host:port form")
    return parser.parse_args()


class MonitorSession:
    def __init__(self, serial_device: str, tcp_endpoint: str) -> None:
        self.serial_device = serial_device
        self.tcp_endpoint = tcp_endpoint
        self.fd: Optional[int] = None
        self.socket: Optional[socket.socket] = None
        self.pyserial: Any = None

    def __enter__(self) -> "MonitorSession":
        if self.serial_device:
            self._open_serial(self.serial_device)
        elif self.tcp_endpoint:
            host, port = parse_tcp_endpoint(self.tcp_endpoint)
            self.socket = socket.create_connection((host, port), timeout=3.0)
            self.socket.setblocking(False)
        else:
            raise RuntimeError("bridge requires --serial or --tcp")
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        if self.pyserial is not None:
            self.pyserial.close()
            self.pyserial = None
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None
        if self.socket is not None:
            self.socket.close()
            self.socket = None

    def transact(self, payload: Dict[str, Any], timeout: float = 2.5) -> Dict[str, Any]:
        command_name = str(payload.get("cmd", ""))
        encoded = json.dumps(payload, separators=(",", ":")).encode("utf-8") + b"\n"
        self._write(encoded)
        deadline = time.monotonic() + timeout
        pending = b""
        lines: List[str] = []
        documents: List[Dict[str, Any]] = []

        while time.monotonic() < deadline:
            chunk = self._read(max(0.05, min(0.25, deadline - time.monotonic())))
            if not chunk:
                continue
            pending += chunk
            while b"\n" in pending:
                raw, pending = pending.split(b"\n", 1)
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                lines.append(line)
                try:
                    document = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if isinstance(document, dict):
                    documents.append(document)
                    if document.get("type") == "resp" and document.get("cmd") == command_name:
                        return {"lines": lines, "documents": documents, "response": document}

        raise TimeoutError(f"RP2350-Monitor command {command_name or '<unknown>'} timed out")

    def _open_serial(self, device: str) -> None:
        if os.name == "nt":
            try:
                import serial  # type: ignore[import-not-found]
            except ImportError as exc:
                raise RuntimeError("Windows serial bridge requires pyserial") from exc
            self.pyserial = serial.Serial(device, BAUD, timeout=0.05, write_timeout=1.0)
            return

        if termios is None or tty is None:
            raise RuntimeError("POSIX serial bridge requires termios support")
        self.fd = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        tty.setraw(self.fd, termios.TCSANOW)
        attrs = termios.tcgetattr(self.fd)
        attrs[2] |= termios.CLOCAL | termios.CREAD
        attrs[4] = termios.B115200
        attrs[5] = termios.B115200
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        termios.tcflush(self.fd, termios.TCIOFLUSH)

    def _write(self, data: bytes) -> None:
        if self.pyserial is not None:
            self.pyserial.write(data)
            self.pyserial.flush()
            return
        if self.fd is not None:
            offset = 0
            while offset < len(data):
                try:
                    offset += os.write(self.fd, data[offset:])
                except BlockingIOError:
                    time.sleep(0.01)
            return
        if self.socket is not None:
            self.socket.sendall(data)
            return
        raise RuntimeError("transport is not open")

    def _read(self, timeout: float) -> bytes:
        if self.pyserial is not None:
            return bytes(self.pyserial.read(READ_CHUNK_BYTES))
        if self.fd is not None:
            readable, _, _ = select.select([self.fd], [], [], timeout)
            if not readable:
                return b""
            try:
                return os.read(self.fd, READ_CHUNK_BYTES)
            except BlockingIOError:
                return b""
        if self.socket is not None:
            readable, _, _ = select.select([self.socket], [], [], timeout)
            if not readable:
                return b""
            return self.socket.recv(READ_CHUNK_BYTES)
        return b""


class Bridge:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.lock = threading.Lock()
        self.state_lock = threading.Lock()
        self.logic_generation = 0
        self.channel_signatures: Dict[str, tuple[Any, ...]] = {}

    def run_operation(self, operation: Dict[str, Any]) -> Dict[str, Any]:
        return self._run_operation(operation)

    def _run_operation(self, operation: Dict[str, Any]) -> Dict[str, Any]:
        action = str(operation.get("action", ""))
        params = operation.get("params") if isinstance(operation.get("params"), dict) else {}
        logic_generation: Optional[int] = None
        if action in LOGIC_INTERRUPT_ACTIONS:
            logic_generation = self.next_logic_generation()
        lines: List[str] = []
        documents: List[Dict[str, Any]] = []
        try:
            for item in self.commands_for(action, params):
                if "__sleep__" in item:
                    time.sleep(float(item["__sleep__"]))
                    continue
                if "__poll_logic__" in item:
                    self.poll_logic_complete(lines, documents, wait_forever=bool(item.get("__wait_forever__")), generation=logic_generation)
                    continue
                if "__poll_wifi_scan__" in item:
                    self.poll_wifi_scan(lines, documents)
                    continue
                if "__read_logic_chunks__" in item:
                    self.read_logic_chunks(lines, documents)
                    continue
                if "__read_logic_if_complete__" in item:
                    logic = extract_logic(documents) or {}
                    if logic.get("complete"):
                        self.read_logic_chunks(lines, documents)
                    continue
                if "__restart_logic_if_missed__" in item:
                    logic = extract_logic(documents) or {}
                    if logic.get("configured") and not logic.get("running") and not logic.get("complete"):
                        result = self.transact({"cmd": "logic_start"}, timeout=timeout_for({"cmd": "logic_start"}))
                        lines.extend(result["lines"])
                        documents.extend(result["documents"])
                    continue
                if "__run_stimulus__" in item:
                    self.run_stimulus(item["__run_stimulus__"], lines, documents)
                    continue
                if "__prepare_stimulus__" in item:
                    self.prepare_stimulus(item["__prepare_stimulus__"], lines, documents)
                    continue
                if "__spi_transfer__" in item:
                    self.spi_transfer(item["__spi_transfer__"], lines, documents)
                    continue
                if "__uart_write__" in item:
                    self.uart_write(item["__uart_write__"], lines, documents)
                    continue
                if "__i2c_transfer__" in item:
                    self.i2c_transfer(item["__i2c_transfer__"], lines, documents)
                    continue
                if "__release_logic_pin_owners__" in item:
                    self.release_logic_pin_owners(item["__release_logic_pin_owners__"], lines, documents)
                    continue
                if "__release_pin_owners__" in item:
                    self.release_pin_owners(item["__release_pin_owners__"], lines, documents)
                    continue
                ignore_error = bool(item.get("__ignore_error__"))
                payload = {key: value for key, value in item.items() if not key.startswith("__")}
                try:
                    result = self.transact(payload, timeout=timeout_for(payload))
                except Exception:
                    if ignore_error:
                        continue
                    raise
                lines.extend(result["lines"])
                documents.extend(result["documents"])
                response = result.get("response")
                if isinstance(response, dict) and response.get("ok") is False and not ignore_error:
                    payload = self.response(False, action, lines, documents, str(response.get("msg", "command failed")))
                    self.normalize_logic_snapshot(payload, action, params)
                    return payload
                if payload.get("cmd") == "channel_release" and payload.get("id") is not None:
                    self.forget_channel(int(payload["id"]))
        except Exception as exc:  # noqa: BLE001
            payload = self.response(False, action, lines, documents, str(exc))
            self.normalize_logic_snapshot(payload, action, params)
            return payload

        payload = self.response(True, action, lines, documents)
        self.normalize_logic_snapshot(payload, action, params)
        self.remember_successful_operation(action, params)
        return payload

    def remember_successful_operation(self, action: str, params: Dict[str, Any]) -> None:
        if action == "spi.configure":
            self.remember_spi_channel(params)
        elif action == "uart.configure":
            self.remember_uart_channel(params)
        elif action == "i2c.configure":
            self.remember_i2c_channel(params)

    def next_logic_generation(self) -> int:
        with self.state_lock:
            self.logic_generation += 1
            return self.logic_generation

    def current_logic_generation(self) -> int:
        with self.state_lock:
            return self.logic_generation

    def transact(self, payload: Dict[str, Any], timeout: float) -> Dict[str, Any]:
        # The current firmware and GUI paths are most reliable with one JSONL
        # command per USB/TCP open. Keeping a single serial descriptor open can
        # race with firmware-side line handling on some macOS USB CDC stacks.
        with self.lock:
            with MonitorSession(self.args.serial, self.args.tcp) as session:
                return session.transact(payload, timeout=timeout)

    def response(
        self,
        ok: bool,
        action: str,
        lines: List[str],
        documents: List[Dict[str, Any]],
        error: str = "",
    ) -> Dict[str, Any]:
        payload = {
            "ok": ok,
            "lines": lines,
            "documents": documents,
            "snapshot": self.snapshot_from(documents, action),
        }
        if error:
            payload["error"] = error
        return payload

    @staticmethod
    def normalize_logic_snapshot(payload: Dict[str, Any], action: str, params: Dict[str, Any]) -> None:
        if not action.startswith("logic."):
            return
        snapshot = payload.get("snapshot")
        logic = snapshot.get("logic") if isinstance(snapshot, dict) else None
        if not isinstance(logic, dict):
            return
        requested_burst_count = int(params.get("burst_count", logic.get("burst_count", 1)) or 1)
        logic["requested_burst_count"] = requested_burst_count
        if (
            action == "logic.capture"
            and requested_burst_count <= 1
            and int(params.get("pre_samples", 0) or 0) > 0
            and int(logic.get("burst_count", 1) or 1) == 2
        ):
            logic["internal_burst_count"] = 2
            logic["burst_count"] = 1
            if int(logic.get("burst_found", 0) or 0) <= 1:
                logic["burst_found"] = 0
                logic.pop("burst_samples", None)

    def commands_for(self, action: str, params: Dict[str, Any]) -> List[Dict[str, Any]]:
        if action == "probe":
            return [{"cmd": "hello"}, {"cmd": "status"}, {"cmd": "logic_caps"}]
        if action == "wifi.scan":
            return [{"cmd": "wifi_scan"}, {"__poll_wifi_scan__": True}]
        if action == "logic.caps":
            return [{"cmd": "logic_caps"}]
        if action == "logic.poll":
            commands = [{"cmd": "logic_status"}, {"__read_logic_if_complete__": True}]
            if bool(params.get("restart_if_missed")):
                commands.extend([{"__restart_logic_if_missed__": True}, {"cmd": "logic_status"}])
            else:
                commands.append({"cmd": "logic_status"})
            return commands
        if action == "gpio.read":
            pins = params.get("pins") if isinstance(params.get("pins"), list) else [16, 17]
            pull = str(params.get("pull", "none"))
            commands: List[Dict[str, Any]] = []
            for raw_pin in pins:
                pin = int(raw_pin)
                commands.extend([
                    {"cmd": "channel_release", "id": pin, "__ignore_error__": True},
                    {"cmd": "channel_config", "id": pin, "type": "gpio", "gpio": pin, "direction": "input", "pull": pull},
                    {"cmd": "channel_start", "id": pin},
                    {"cmd": "gpio_read", "id": pin},
                    {"cmd": "channel_release", "id": pin, "__ignore_error__": True},
                ])
            return commands
        if action == "logic.configure":
            config = self.logic_config_command(params)
            return self.logic_release_commands(config) + [{"__release_logic_pin_owners__": config}, {"cmd": "logic_caps"}, config, {"cmd": "logic_status"}]
        if action == "logic.capture":
            config = self.logic_config_command(params)
            stimulus = params.get("stimulus") if isinstance(params.get("stimulus"), dict) else None
            nonblocking = bool(params.get("nonblocking"))
            commands = self.logic_release_commands(config) + [{"__release_logic_pin_owners__": config}, {"cmd": "logic_caps"}, config]
            if isinstance(stimulus, dict):
                commands.append({"__prepare_stimulus__": stimulus})
            commands.append({"cmd": "logic_start"})
            if isinstance(stimulus, dict):
                commands.append({"__run_stimulus__": stimulus})
            if nonblocking and not isinstance(stimulus, dict):
                commands.append({"cmd": "logic_status"})
                return commands
            commands.extend([{"__poll_logic__": True, "__wait_forever__": self.logic_waits_for_trigger(config)}, {"__read_logic_chunks__": True}, {"cmd": "logic_status"}])
            return commands
        if action == "logic.stop":
            return [{"cmd": "logic_stop", "__ignore_error__": True}, {"cmd": "logic_release", "__ignore_error__": True}, {"cmd": "logic_status", "__ignore_error__": True}]
        if action == "uart.configure":
            channel = int(params.get("id", 1))
            tx = int(params.get("tx", 0))
            rx = int(params.get("rx", 1))
            return [{"cmd": "channel_release", "id": channel, "__ignore_error__": True}, {"cmd": "channel_config", "id": channel, "type": "uart", "instance": int(params.get("instance", 0)), "tx": tx, "rx": rx, "baud": int(params.get("baud", 115200))}, {"cmd": "channel_start", "id": channel}, {"cmd": "status"}]
        if action == "uart.write":
            write = dict(params)
            write["id"] = int(params.get("id", 1))
            return [{"__uart_write__": write}, {"cmd": "status"}]
        if action == "i2c.configure":
            channel = int(params.get("id", 3))
            sda = int(params.get("sda", 4))
            scl = int(params.get("scl", 5))
            return [{"cmd": "channel_release", "id": channel, "__ignore_error__": True}, {"cmd": "channel_config", "id": channel, "type": "i2c", "instance": int(params.get("instance", 0)), "sda": sda, "scl": scl, "baud": int(params.get("baud", 100000))}, {"cmd": "channel_start", "id": channel}, {"cmd": "status"}]
        if action == "i2c.transfer":
            transfer = dict(params)
            transfer["id"] = int(params.get("id", 3))
            return [{"__i2c_transfer__": transfer}, {"cmd": "status"}]
        if action == "spi.configure":
            channel = int(params.get("id", 2))
            sck = int(params.get("sck", 2))
            mosi = int(params.get("mosi", 3))
            miso = int(params.get("miso", 0))
            cs = int(params.get("cs", 1))
            return [{"cmd": "channel_release", "id": channel, "__ignore_error__": True}, {"cmd": "channel_config", "id": channel, "type": "spi", "instance": int(params.get("instance", 0)), "sck": sck, "mosi": mosi, "miso": miso, "cs": cs, "baud": int(params.get("baud", 1000000))}, {"cmd": "channel_start", "id": channel}, {"cmd": "status"}]
        if action == "spi.transfer":
            channel = int(params.get("id", 2))
            transfer = dict(params)
            transfer["id"] = channel
            return [{"__spi_transfer__": transfer}, {"cmd": "status"}]
        if action == "channel.release":
            channel = int(params.get("id", 0))
            return [{"cmd": "channel_release", "id": channel, "__ignore_error__": True}, {"cmd": "channels"}, {"cmd": "pins"}, {"cmd": "status"}]
        raise ValueError(f"unsupported operation action: {action}")

    @staticmethod
    def logic_config_command(params: Dict[str, Any]) -> Dict[str, Any]:
        command: Dict[str, Any] = {
            "cmd": "logic_config",
            "pin_base": int(params.get("pin_base", 16)),
            "pin_count": int(params.get("pin_count", 4)),
            "sample_rate": int(params.get("sample_rate", 1000000)),
            "samples": int(params.get("samples", 2048)),
            "pull": str(params.get("pull", "none")),
        }
        for field in ("pre_samples", "post_samples", "search_samples", "burst_count"):
            if field in params and params[field] is not None:
                command[field] = int(params[field])
        pin_pulls = params.get("pin_pulls")
        if isinstance(pin_pulls, dict):
            command["pin_pulls"] = {str(key): str(value) for key, value in pin_pulls.items()}

        trigger_type = str(params.get("trigger_type", ""))
        trigger_mode = str(params.get("trigger_mode", ""))
        if trigger_type == "pattern" or trigger_mode == "pattern":
            command["trigger_mode"] = "pattern"
            if "trigger_mask" in params and params["trigger_mask"] is not None:
                command["trigger_mask"] = parse_int(params["trigger_mask"])
            if "trigger_value" in params and params["trigger_value"] is not None:
                command["trigger_value"] = parse_int(params["trigger_value"])
        elif "trigger_pin" in params and params["trigger_pin"] is not None:
            command["trigger_pin"] = int(params["trigger_pin"])
            if "trigger_mode" in params and params["trigger_mode"] is not None:
                command["trigger_mode"] = str(params["trigger_mode"])
            elif trigger_type in {"rising", "falling"}:
                command["trigger_mode"] = trigger_type
            elif trigger_type in {"level-high", "level-low"}:
                command["trigger_mode"] = "level"
            if "trigger_level" in params:
                command["trigger_level"] = bool(params.get("trigger_level", True))
            elif trigger_type == "level-low":
                command["trigger_level"] = False
            elif trigger_type in {"level-high", "level"}:
                command["trigger_level"] = True
        has_trigger = command.get("trigger_pin") is not None or command.get("trigger_mode") == "pattern"
        if not has_trigger:
            if "post_samples" in params and params.get("pre_samples"):
                command["samples"] = max(1, int(params.get("post_samples", command["samples"]) or command["samples"]))
            command.pop("pre_samples", None)
            command.pop("post_samples", None)
            command.pop("search_samples", None)
        return command

    @staticmethod
    def logic_release_commands(config: Dict[str, Any]) -> List[Dict[str, Any]]:
        return [{"cmd": "logic_stop", "__ignore_error__": True}, {"cmd": "logic_release", "__ignore_error__": True}]

    def release_logic_pin_owners(self, config: Dict[str, Any], lines: List[str], documents: List[Dict[str, Any]]) -> None:
        self.release_pin_owners(self.logic_pin_range(config), lines, documents)

    def release_pin_owners(self, pins_to_release: List[int], lines: List[str], documents: List[Dict[str, Any]]) -> None:
        result = self.transact({"cmd": "pins"}, timeout=3.0)
        lines.extend(result["lines"])
        documents.extend(result["documents"])
        target_pins = {int(pin) for pin in pins_to_release}
        owners: set[int] = set()
        release_logic_owner = False
        for document in result["documents"]:
            pins = document.get("pins")
            if not isinstance(pins, list):
                continue
            for item in pins:
                if not isinstance(item, dict):
                    continue
                gpio = int(item.get("gpio", -1))
                owner = int(item.get("owner", 0) or 0)
                if gpio in target_pins and owner == 1000:
                    release_logic_owner = True
                elif gpio in target_pins and owner != 0:
                    owners.add(owner)
        if release_logic_owner:
            for command in ({"cmd": "logic_stop"}, {"cmd": "logic_release"}):
                try:
                    released = self.transact(command, timeout=timeout_for(command))
                except Exception:
                    continue
                lines.extend(released["lines"])
                documents.extend(released["documents"])
        for owner in sorted(owners):
            try:
                released = self.transact({"cmd": "channel_release", "id": owner}, timeout=1.0)
            except Exception:
                continue
            self.forget_channel(owner)
            lines.extend(released["lines"])
            documents.extend(released["documents"])

    @staticmethod
    def logic_pin_range(config: Dict[str, Any]) -> List[int]:
        pin_base = int(config.get("pin_base", 16))
        pin_count = int(config.get("pin_count", 4))
        return list(range(pin_base, pin_base + max(1, pin_count)))

    @staticmethod
    def logic_waits_for_trigger(config: Dict[str, Any]) -> bool:
        return (
            str(config.get("trigger_mode", "")) in {"level", "rising", "falling", "pattern"}
            or config.get("trigger_pin") is not None
            or int(config.get("burst_count", 1) or 1) > 1
        )

    def poll_logic_complete(
        self,
        lines: List[str],
        documents: List[Dict[str, Any]],
        wait_forever: bool = False,
        generation: Optional[int] = None,
    ) -> None:
        deadline = None if wait_forever else time.monotonic() + 8.0
        while deadline is None or time.monotonic() < deadline:
            if generation is not None and generation != self.current_logic_generation():
                raise RuntimeError("logic capture was replaced by a newer request")
            result = self.transact({"cmd": "logic_status"}, timeout=2.5)
            lines.extend(result["lines"])
            documents.extend(result["documents"])
            logic = extract_logic(result["documents"])
            if isinstance(logic, dict):
                if logic.get("complete"):
                    return
                if wait_forever and logic.get("running") is False:
                    if generation is not None and generation != self.current_logic_generation():
                        raise RuntimeError("logic capture was replaced by a newer request")
                    if logic.get("configured"):
                        restart = self.transact({"cmd": "logic_start"}, timeout=timeout_for({"cmd": "logic_start"}))
                        lines.extend(restart["lines"])
                        documents.extend(restart["documents"])
                        time.sleep(0.02)
                        continue
                    raise RuntimeError("logic capture was stopped before trigger")
            time.sleep(0.02 if wait_forever else 0.2)
        raise TimeoutError("logic capture did not complete before timeout")

    def read_logic_chunks(self, lines: List[str], documents: List[Dict[str, Any]]) -> None:
        logic = extract_logic(documents) or {}
        total_words = logic_word_count(logic)
        if total_words <= 0:
            result = self.transact({"cmd": "logic_read", "offset_words": 0, "count_words": 0}, timeout=timeout_for({"cmd": "logic_read"}))
            lines.extend(result["lines"])
            documents.extend(result["documents"])
            return

        offset = 0
        chunk_words = 64
        while offset < total_words:
            count = min(chunk_words, total_words - offset)
            command = {"cmd": "logic_read", "offset_words": offset, "count_words": count}
            try:
                result = self.transact(command, timeout=max(8.0, timeout_for(command)))
            except Exception:
                if count > 16:
                    chunk_words = 16
                    continue
                raise
            lines.extend(result["lines"])
            documents.extend(result["documents"])
            response = result.get("response")
            if isinstance(response, dict) and response.get("ok") is False:
                message = str(response.get("msg", "command failed"))
                if "upload sink blocked" in message and count > 16:
                    chunk_words = 16
                    continue
                raise RuntimeError(message)
            offset += count

    def prepare_stimulus(self, stimulus: Dict[str, Any], lines: List[str], documents: List[Dict[str, Any]]) -> None:
        action = str(stimulus.get("action", ""))
        if action not in {"spi.transfer", "spi_xfer", "uart.write", "channel_write", "i2c.transfer", "i2c_xfer"}:
            raise RuntimeError(f"unsupported capture stimulus: {action or '<empty>'}")
        if not bool(stimulus.get("configure", True)):
            return

        if action in {"spi.transfer", "spi_xfer"}:
            channel = int(stimulus.get("id", 2))
            sck = int(stimulus.get("sck", 2))
            mosi = int(stimulus.get("mosi", 3))
            miso = int(stimulus.get("miso", 0))
            cs = int(stimulus.get("cs", 1))
            self.configure_spi_channel(stimulus, lines, documents, "stimulus setup failed")
            return

        if action in {"uart.write", "channel_write"}:
            channel = int(stimulus.get("id", 1))
            self.configure_uart_channel({**stimulus, "id": channel}, lines, documents, "stimulus setup failed")
            return

        if action in {"i2c.transfer", "i2c_xfer"}:
            channel = int(stimulus.get("id", 3))
            self.configure_i2c_channel({**stimulus, "id": channel}, lines, documents, "stimulus setup failed")

    def run_setup_commands(
        self,
        setup_commands: List[Dict[str, Any]],
        lines: List[str],
        documents: List[Dict[str, Any]],
        error_message: str,
    ) -> None:
        for command in setup_commands:
            result = self.transact(command, timeout=timeout_for(command))
            lines.extend(result["lines"])
            documents.extend(result["documents"])
            response = result.get("response")
            if isinstance(response, dict) and response.get("ok") is False and command["cmd"] != "channel_release":
                raise RuntimeError(str(response.get("msg", error_message)))

    def run_stimulus(self, stimulus: Dict[str, Any], lines: List[str], documents: List[Dict[str, Any]]) -> None:
        delay_ms = int(stimulus.get("delay_ms", 3) or 0)
        if delay_ms > 0:
            time.sleep(delay_ms / 1000.0)
        action = str(stimulus.get("action", ""))
        if action not in {"spi.transfer", "spi_xfer", "uart.write", "channel_write", "i2c.transfer", "i2c_xfer"}:
            raise RuntimeError(f"unsupported capture stimulus: {action or '<empty>'}")
        if action in {"spi.transfer", "spi_xfer"}:
            command = {
                "cmd": "spi_xfer",
                "id": int(stimulus.get("id", 2)),
                "hex": str(stimulus.get("hex", "9f000000")),
                "read_len": int(stimulus.get("read_len", 4) or 0),
            }
        elif action in {"uart.write", "channel_write"}:
            command = {
                "cmd": "channel_write",
                "id": int(stimulus.get("id", 1)),
                "hex": str(stimulus.get("hex", "55")),
            }
        else:
            command = {
                "cmd": "i2c_xfer",
                "id": int(stimulus.get("id", 3)),
                "addr": parse_int(stimulus.get("addr", "0x50")),
                "write": str(stimulus.get("write", "00")),
                "read_len": int(stimulus.get("read_len", 0) or 0),
            }
        result = self.transact(command, timeout=timeout_for(command))
        lines.extend(result["lines"])
        documents.extend(result["documents"])
        response = result.get("response")
        if isinstance(response, dict) and response.get("ok") is False and not bool(stimulus.get("allow_error")):
            raise RuntimeError(str(response.get("msg", "stimulus transfer failed")))

    def uart_write(self, params: Dict[str, Any], lines: List[str], documents: List[Dict[str, Any]]) -> None:
        channel = int(params.get("id", 1))
        command = {"cmd": "channel_write", "id": channel, "hex": str(params.get("hex", ""))}
        signature = self.uart_signature(params)
        cache_key = f"uart:{channel}"
        if self.channel_signatures.get(cache_key) == signature:
            try:
                result = self.transact(command, timeout=timeout_for(command))
                lines.extend(result["lines"])
                documents.extend(result["documents"])
                response = result.get("response")
                if isinstance(response, dict) and response.get("ok") is not False:
                    return
            except Exception:
                pass
            self.channel_signatures.pop(cache_key, None)

        self.configure_uart_channel(params, lines, documents, "uart setup failed")
        result = self.transact(command, timeout=timeout_for(command))
        lines.extend(result["lines"])
        documents.extend(result["documents"])
        response = result.get("response")
        if isinstance(response, dict) and response.get("ok") is False:
            raise RuntimeError(str(response.get("msg", "uart write failed")))

    def i2c_transfer(self, params: Dict[str, Any], lines: List[str], documents: List[Dict[str, Any]]) -> None:
        channel = int(params.get("id", 3))
        command = {
            "cmd": "i2c_xfer",
            "id": channel,
            "addr": parse_int(params.get("addr", "0x50")),
            "write": str(params.get("write", "")),
            "read_len": int(params.get("read_len", 0)),
        }
        signature = self.i2c_signature(params)
        cache_key = f"i2c:{channel}"
        if self.channel_signatures.get(cache_key) == signature:
            try:
                result = self.transact(command, timeout=timeout_for(command))
                lines.extend(result["lines"])
                documents.extend(result["documents"])
                response = result.get("response")
                if isinstance(response, dict) and response.get("ok") is not False:
                    return
            except Exception:
                pass
            self.channel_signatures.pop(cache_key, None)

        self.configure_i2c_channel(params, lines, documents, "i2c setup failed")
        result = self.transact(command, timeout=timeout_for(command))
        lines.extend(result["lines"])
        documents.extend(result["documents"])
        response = result.get("response")
        if isinstance(response, dict) and response.get("ok") is False:
            raise RuntimeError(str(response.get("msg", "i2c transfer failed")))

    def spi_transfer(self, params: Dict[str, Any], lines: List[str], documents: List[Dict[str, Any]]) -> None:
        channel = int(params.get("id", 2))
        transfer = {
            "cmd": "spi_xfer",
            "id": channel,
            "hex": str(params.get("hex", "")),
            "read_len": int(params.get("read_len", 0)),
        }
        sck = int(params.get("sck", 2))
        mosi = int(params.get("mosi", 3))
        miso = int(params.get("miso", 0))
        cs = int(params.get("cs", 1))
        signature = self.spi_signature(params)
        cache_key = f"spi:{channel}"
        has_explicit_bus_config = any(key in params for key in ("instance", "sck", "mosi", "miso", "cs", "baud"))
        if self.channel_signatures.get(cache_key) == signature or (
            self.channel_signatures.get(cache_key) is not None and not has_explicit_bus_config
        ):
            try:
                result = self.transact(transfer, timeout=timeout_for(transfer))
                lines.extend(result["lines"])
                documents.extend(result["documents"])
                response = result.get("response")
                if isinstance(response, dict) and response.get("ok") is not False:
                    return
            except Exception:
                pass
            self.channel_signatures.pop(cache_key, None)

        self.configure_spi_channel(
            {"id": channel, "instance": params.get("instance", 0), "sck": sck, "mosi": mosi, "miso": miso, "cs": cs, "baud": params.get("baud", 1000000)},
            lines,
            documents,
            "spi transfer setup failed",
        )
        result = self.transact(transfer, timeout=timeout_for(transfer))
        lines.extend(result["lines"])
        documents.extend(result["documents"])
        response = result.get("response")
        if isinstance(response, dict) and response.get("ok") is False:
            raise RuntimeError(str(response.get("msg", "spi transfer failed")))

    def configure_uart_channel(
        self,
        params: Dict[str, Any],
        lines: List[str],
        documents: List[Dict[str, Any]],
        error_message: str,
    ) -> None:
        channel = int(params.get("id", 1))
        setup_commands = [
            {"cmd": "channel_release", "id": channel},
            {
                "cmd": "channel_config",
                "id": channel,
                "type": "uart",
                "instance": int(params.get("instance", 1)),
                "tx": int(params.get("tx", 8)),
                "rx": int(params.get("rx", 9)),
                "baud": int(params.get("baud", 115200)),
            },
            {"cmd": "channel_start", "id": channel},
        ]
        self.run_setup_commands(setup_commands, lines, documents, error_message)
        self.remember_uart_channel(params)

    def configure_i2c_channel(
        self,
        params: Dict[str, Any],
        lines: List[str],
        documents: List[Dict[str, Any]],
        error_message: str,
    ) -> None:
        channel = int(params.get("id", 3))
        setup_commands = [
            {"cmd": "channel_release", "id": channel},
            {
                "cmd": "channel_config",
                "id": channel,
                "type": "i2c",
                "instance": int(params.get("instance", 0)),
                "sda": int(params.get("sda", 4)),
                "scl": int(params.get("scl", 5)),
                "baud": int(params.get("baud", 100000)),
            },
            {"cmd": "channel_start", "id": channel},
        ]
        self.run_setup_commands(setup_commands, lines, documents, error_message)
        self.remember_i2c_channel(params)

    def configure_spi_channel(
        self,
        params: Dict[str, Any],
        lines: List[str],
        documents: List[Dict[str, Any]],
        error_message: str,
    ) -> None:
        channel = int(params.get("id", 2))
        setup_commands = [
            {"cmd": "channel_release", "id": channel},
            {
                "cmd": "channel_config",
                "id": channel,
                "type": "spi",
                "instance": int(params.get("instance", 0)),
                "sck": int(params.get("sck", 2)),
                "mosi": int(params.get("mosi", 3)),
                "miso": int(params.get("miso", 0)),
                "cs": int(params.get("cs", 1)),
                "baud": int(params.get("baud", 1000000)),
            },
            {"cmd": "channel_start", "id": channel},
        ]
        self.run_setup_commands(setup_commands, lines, documents, error_message)
        self.remember_spi_channel(params)

    @staticmethod
    def uart_signature(params: Dict[str, Any]) -> tuple[Any, ...]:
        return (
            int(params.get("instance", 1)),
            int(params.get("tx", 8)),
            int(params.get("rx", 9)),
            int(params.get("baud", 115200)),
        )

    @staticmethod
    def i2c_signature(params: Dict[str, Any]) -> tuple[Any, ...]:
        return (
            int(params.get("instance", 0)),
            int(params.get("sda", 4)),
            int(params.get("scl", 5)),
            int(params.get("baud", 100000)),
        )

    @staticmethod
    def spi_signature(params: Dict[str, Any]) -> tuple[Any, ...]:
        return (
            int(params.get("instance", 0)),
            int(params.get("sck", 2)),
            int(params.get("mosi", 3)),
            int(params.get("miso", 0)),
            int(params.get("cs", 1)),
            int(params.get("baud", 1000000)),
        )

    def remember_spi_channel(self, params: Dict[str, Any]) -> None:
        channel = int(params.get("id", 2))
        self.channel_signatures[f"spi:{channel}"] = self.spi_signature(params)

    def remember_uart_channel(self, params: Dict[str, Any]) -> None:
        channel = int(params.get("id", 1))
        self.channel_signatures[f"uart:{channel}"] = self.uart_signature(params)

    def remember_i2c_channel(self, params: Dict[str, Any]) -> None:
        channel = int(params.get("id", 3))
        self.channel_signatures[f"i2c:{channel}"] = self.i2c_signature(params)

    def forget_channel(self, channel: int) -> None:
        for protocol in ("uart", "i2c", "spi"):
            self.channel_signatures.pop(f"{protocol}:{int(channel)}", None)

    def poll_wifi_scan(self, lines: List[str], documents: List[Dict[str, Any]]) -> None:
        deadline = time.monotonic() + 12.0
        saw_status = False
        while time.monotonic() < deadline:
            try:
                result = self.transact({"cmd": "status"}, timeout=3.0)
            except TimeoutError:
                if wifi_scan_has_results(documents):
                    return
                time.sleep(0.4)
                continue
            lines.extend(result["lines"])
            documents.extend(result["documents"])
            wifi = extract_wifi(result["documents"])
            if isinstance(wifi, dict):
                saw_status = True
                scan = wifi.get("scan")
                if not isinstance(scan, dict) or scan.get("results") or not scan.get("active", False):
                    return
            time.sleep(0.4)
        if not saw_status:
            raise TimeoutError("Wi-Fi scan did not return a status update before timeout")

    def monitor_transport(self) -> Dict[str, str]:
        if self.args.tcp:
            return {"transport": "Wi-Fi TCP", "endpoint": self.args.tcp}
        if self.args.serial:
            return {"transport": "USB CDC", "endpoint": self.args.serial}
        return {"transport": "RP2350 Monitor"}

    def snapshot_from(self, documents: List[Dict[str, Any]], action: str) -> Dict[str, Any]:
        transport = self.monitor_transport()
        snapshot: Dict[str, Any] = {
            "device": {"board": "Pico 2 W", **transport},
            "lastResponse": action,
            "events": [doc for doc in documents if doc.get("type") == "event"],
        }
        for doc in documents:
            if doc.get("cmd") == "hello":
                snapshot["device"] = {
                    "board": doc.get("board", "Pico 2 W"),
                    "firmware": doc.get("version", "-"),
                    **transport,
                }
            if doc.get("version") and not snapshot["device"].get("firmware"):
                snapshot["device"]["firmware"] = doc.get("version")
            if doc.get("wifi") and isinstance(doc["wifi"], dict):
                snapshot["wifi_status"] = doc["wifi"]
                snapshot["wifi"] = doc["wifi"].get("scan", {}).get("results", [])
            if "channels" in doc and isinstance(doc["channels"], list):
                snapshot["channels"] = doc["channels"]
            if doc.get("buffers") and isinstance(doc["buffers"], dict):
                snapshot["buffers"] = doc["buffers"]
            if doc.get("logic_caps") and isinstance(doc["logic_caps"], dict):
                snapshot["logic_caps"] = doc["logic_caps"]
            if doc.get("logic") and isinstance(doc["logic"], dict):
                existing_logic = snapshot.get("logic") if isinstance(snapshot.get("logic"), dict) else {}
                existing_words = existing_logic.get("words") if isinstance(existing_logic.get("words"), list) else None
                preserved = {
                    key: existing_logic[key]
                    for key in ("sample_offset", "burst_samples")
                    if key in existing_logic and key not in doc["logic"]
                }
                existing_logic.update(doc["logic"])
                existing_logic.update(preserved)
                if existing_words is not None:
                    existing_logic["words"] = existing_words
                snapshot["logic"] = existing_logic
            if doc.get("type") == "logic_meta":
                logic = snapshot.setdefault("logic", {})
                for key in (
                    "capture_id",
                    "pin_base",
                    "pin_count",
                    "sample_rate",
                    "samples",
                    "record_bits",
                    "sample_offset",
                    "pre_samples",
                    "post_samples",
                    "trigger_found",
                    "trigger_sample",
                    "trigger_pin",
                    "trigger_mode",
                    "trigger_mask",
                    "trigger_value",
                    "burst_count",
                    "burst_found",
                    "burst_samples",
                ):
                    if key in doc:
                        logic[key] = doc[key]
            if doc.get("type") == "logic":
                logic = snapshot.setdefault("logic", {})
                logic.update({
                    "complete": True,
                    "capture_id": doc.get("capture_id"),
                    "pin_base": doc.get("pin_base"),
                    "pin_count": doc.get("pin_count"),
                    "sample_rate": doc.get("sample_rate"),
                    "samples": doc.get("samples"),
                    "record_bits": doc.get("record_bits", 32),
                })
                if not isinstance(logic.get("words"), list):
                    logic["words"] = []
                logic["words"].extend(words_from_hex(str(doc.get("hex", ""))))
            if doc.get("cmd") == "gpio_read":
                channel = doc.get("id") or doc.get("channel")
                if channel is not None:
                    snapshot.setdefault("gpio", []).append({"pin": int(channel), "level": bool(doc.get("level"))})
            if doc.get("type") == "event" and doc.get("proto") == "gpio" and doc.get("channel") is not None:
                hex_value = str(doc.get("hex", ""))
                if hex_value:
                    snapshot.setdefault("gpio", []).append({
                        "pin": int(doc["channel"]),
                        "level": int(hex_value[:2], 16) != 0,
                    })
        return snapshot


class HandlerFactory:
    def __init__(self, bridge: Bridge) -> None:
        self.bridge = bridge

    def build(self) -> type[SimpleHTTPRequestHandler]:
        bridge = self.bridge

        class Handler(SimpleHTTPRequestHandler):
            def __init__(self, *handler_args: Any, **handler_kwargs: Any) -> None:
                super().__init__(*handler_args, directory=str(ROOT), **handler_kwargs)

            def end_headers(self) -> None:
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Access-Control-Allow-Headers", "Content-Type")
                self.send_header("Access-Control-Allow-Methods", "GET,POST,OPTIONS")
                self.send_header("Cache-Control", "no-store")
                super().end_headers()

            def do_OPTIONS(self) -> None:  # noqa: N802
                self.send_response(204)
                self.end_headers()

            def do_GET(self) -> None:  # noqa: N802
                if self.path == "/api/health":
                    self.write_json({"ok": True, "service": "rp2350-monitor-bridge"})
                    return
                super().do_GET()

            def do_POST(self) -> None:  # noqa: N802
                if self.path != "/api/operation":
                    self.send_error(404)
                    return
                try:
                    length = int(self.headers.get("Content-Length", "0"))
                    payload = json.loads(self.rfile.read(length) or b"{}")
                    response = bridge.run_operation(payload.get("operation", payload))
                except Exception as exc:  # noqa: BLE001
                    response = {
                        "ok": False,
                        "error": str(exc),
                        "lines": [],
                        "documents": [],
                        "snapshot": {"lastResponse": str(exc)},
                    }
                self.write_json(response)

            def write_json(self, payload: Dict[str, Any]) -> None:
                body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        return Handler


def parse_tcp_endpoint(endpoint: str) -> tuple[str, int]:
    if ":" not in endpoint:
        return endpoint, 4242
    host, port_text = endpoint.rsplit(":", 1)
    return host, int(port_text)


def parse_int(value: Any) -> int:
    if isinstance(value, int):
        return value
    text = str(value).strip()
    if text.lower().startswith("0x"):
        return int(text[2:], 16)
    return int(text)


def timeout_for(command: Dict[str, Any]) -> float:
    name = str(command.get("cmd", ""))
    if name == "wifi_connect":
        return 18.0
    if name == "logic_read":
        return 45.0
    if name in {"logic_stop", "logic_release"}:
        return 1.0
    if name in {"wifi_scan", "logic_start"}:
        return 5.0
    return 3.0


def extract_logic(documents: Iterable[Dict[str, Any]]) -> Optional[Dict[str, Any]]:
    for document in reversed(list(documents)):
        logic = document.get("logic")
        if isinstance(logic, dict):
            return logic
    return None


def logic_word_count(logic: Dict[str, Any]) -> int:
    for key in ("words", "word_count", "total_words", "count_words"):
        value = logic.get(key)
        if isinstance(value, int):
            return max(0, value)
        if isinstance(value, list):
            return len(value)
    samples = int(logic.get("samples", 0) or 0)
    pin_count = max(1, int(logic.get("pin_count", 1) or 1))
    record_bits = int(logic.get("record_bits", 32) or 32)
    record_bits = max(pin_count, record_bits - (record_bits % pin_count))
    if samples <= 0:
        return 0
    return (samples * pin_count + record_bits - 1) // record_bits


def extract_wifi(documents: Iterable[Dict[str, Any]]) -> Optional[Dict[str, Any]]:
    for document in reversed(list(documents)):
        wifi = document.get("wifi")
        if isinstance(wifi, dict):
            return wifi
    return None


def wifi_scan_has_results(documents: Iterable[Dict[str, Any]]) -> bool:
    wifi = extract_wifi(documents)
    if not isinstance(wifi, dict):
        return False
    scan = wifi.get("scan")
    return isinstance(scan, dict) and bool(scan.get("results"))


def words_from_hex(hex_text: str) -> List[int]:
    raw = bytes.fromhex(hex_text)
    return [
        int.from_bytes(raw[index:index + 4], "little")
        for index in range(0, len(raw), 4)
        if len(raw[index:index + 4]) == 4
    ]


def main() -> None:
    args = parse_args()
    bridge = Bridge(args)
    handler = HandlerFactory(bridge).build()
    server = ThreadingHTTPServer((args.host, args.port), handler)
    print(f"RP2350 Monitor UI bridge: http://{args.host}:{args.port}/", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("RP2350 Monitor UI bridge stopped", file=sys.stderr)


if __name__ == "__main__":
    main()
