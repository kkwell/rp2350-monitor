#!/usr/bin/env python3
"""RP2350 Monitor hardware loopback regression test.

Default wiring used by this test:
  GP0 -> GP18
  GP1 -> GP19
  GP2 -> GP16
  GP3 -> GP17

The mapping lets native SPI0 on GP0..GP3 be mirrored by the logic analyzer on
GP16..GP19 without GPIO ownership conflicts:
  GP2 SCK  -> GP16
  GP3 MOSI -> GP17
  GP0 MISO -> GP18
  GP1 CS   -> GP19
"""

from __future__ import annotations

import argparse
import glob
import json
import tempfile
import time
from pathlib import Path
from typing import Any, Dict, Optional

from rpmon_cli import SerialTransport, TcpTransport, Transport
from rpmon_logic import LogicCapture


DRIVE_BY_MONITOR = {16: 2, 17: 3, 18: 0, 19: 1}
OUTPUT_IDS = {0: 90, 1: 91, 2: 92, 3: 93}
INPUT_IDS = {16: 100, 17: 101, 18: 102, 19: 103}
SPI_ID = 80


def discover_serial() -> str:
    candidates = sorted(glob.glob("/dev/tty.usbmodem*")) + sorted(glob.glob("/dev/cu.usbmodem*"))
    if not candidates:
        raise SystemExit("no RP2350 USB serial device found")
    return candidates[0]


def connect(args: argparse.Namespace) -> Transport:
    if args.tcp:
        host, _, port = args.tcp.partition(":")
        return TcpTransport(host, int(port or "4242"), args.timeout)
    return SerialTransport(args.serial or discover_serial(), args.baud, args.timeout)


def print_doc(doc: Dict[str, Any]) -> None:
    print(json.dumps(doc, separators=(",", ":")), flush=True)


def transact(
    transport: Transport,
    payload: Dict[str, Any],
    timeout: float,
    ignore_error: bool = False,
) -> tuple[bool, list[Dict[str, Any]]]:
    transport.send_json(payload)
    deadline = time.monotonic() + timeout
    seen: list[Dict[str, Any]] = []
    while time.monotonic() < deadline:
        line = transport.readline(max(0.05, deadline - time.monotonic()))
        if line is None:
            break
        try:
            doc = json.loads(line)
        except json.JSONDecodeError:
            continue
        seen.append(doc)
        if doc.get("type") == "resp" and doc.get("cmd") == payload.get("cmd"):
            ok = bool(doc.get("ok"))
            if not ok and not ignore_error:
                raise RuntimeError(f"{payload.get('cmd')} failed: {doc.get('msg', doc)}")
            return ok, seen
    if ignore_error:
        return False, seen
    raise TimeoutError(f"{payload.get('cmd')} timed out")


def response_doc(docs: list[Dict[str, Any]], cmd: str) -> Dict[str, Any]:
    for doc in reversed(docs):
        if doc.get("type") == "resp" and doc.get("cmd") == cmd:
            return doc
    return {}


def release_all(transport: Transport, timeout: float) -> None:
    transact(transport, {"cmd": "logic_stop"}, timeout, ignore_error=True)
    transact(transport, {"cmd": "logic_release"}, timeout, ignore_error=True)
    for channel_id in [SPI_ID, *OUTPUT_IDS.values(), *INPUT_IDS.values()]:
        transact(transport, {"cmd": "channel_release", "id": channel_id}, timeout, ignore_error=True)


def setup_gpio_loopbacks(transport: Transport, timeout: float) -> None:
    release_all(transport, timeout)
    for drive_gpio, channel_id in OUTPUT_IDS.items():
        transact(
            transport,
            {
                "cmd": "channel_config",
                "id": channel_id,
                "type": "gpio",
                "gpio": drive_gpio,
                "direction": "output",
                "initial": True,
            },
            timeout,
        )
        transact(transport, {"cmd": "channel_start", "id": channel_id}, timeout)
    for monitor_gpio, channel_id in INPUT_IDS.items():
        transact(
            transport,
            {
                "cmd": "channel_config",
                "id": channel_id,
                "type": "gpio",
                "gpio": monitor_gpio,
                "direction": "input",
                "pull": "up",
            },
            timeout,
        )
        transact(transport, {"cmd": "channel_start", "id": channel_id}, timeout)


def release_monitor_inputs(transport: Transport, timeout: float) -> None:
    for channel_id in INPUT_IDS.values():
        transact(transport, {"cmd": "channel_release", "id": channel_id}, timeout, ignore_error=True)
    transact(transport, {"cmd": "logic_stop"}, timeout, ignore_error=True)
    transact(transport, {"cmd": "logic_release"}, timeout, ignore_error=True)


def gpio_write_drive(transport: Transport, drive_gpio: int, level: bool, timeout: float) -> None:
    transact(transport, {"cmd": "gpio_write", "id": OUTPUT_IDS[drive_gpio], "level": level}, timeout)


def gpio_write_monitor(transport: Transport, monitor_gpio: int, level: bool, timeout: float) -> None:
    gpio_write_drive(transport, DRIVE_BY_MONITOR[monitor_gpio], level, timeout)


def set_monitor_levels(transport: Transport, levels: Dict[int, bool], timeout: float) -> None:
    for monitor_gpio, level in levels.items():
        gpio_write_monitor(transport, monitor_gpio, level, timeout)


def gpio_read_monitor(transport: Transport, monitor_gpio: int, timeout: float) -> bool:
    _, docs = transact(transport, {"cmd": "gpio_read", "id": INPUT_IDS[monitor_gpio]}, timeout)
    doc = response_doc(docs, "gpio_read")
    if "level" not in doc:
        raise RuntimeError("gpio_read did not report a level")
    return bool(doc["level"])


def verify_loopback_wiring(transport: Transport, timeout: float) -> None:
    results: Dict[str, bool] = {}
    for monitor_gpio in sorted(DRIVE_BY_MONITOR):
        gpio_write_monitor(transport, monitor_gpio, True, timeout)
        time.sleep(0.01)
        high = gpio_read_monitor(transport, monitor_gpio, timeout)
        gpio_write_monitor(transport, monitor_gpio, False, timeout)
        time.sleep(0.01)
        low = gpio_read_monitor(transport, monitor_gpio, timeout)
        results[f"gp{monitor_gpio}_high"] = high
        results[f"gp{monitor_gpio}_low"] = low
        if not high or low:
            raise AssertionError(f"GPIO loopback failed on GP{monitor_gpio}: high={high}, low={low}")
        gpio_write_monitor(transport, monitor_gpio, True, timeout)
    print_doc({"type": "loopback_gpio", **results})


def wait_complete(
    transport: Transport,
    timeout: float,
    poll_interval: float,
    command_timeout: float,
) -> Dict[str, Any]:
    deadline = time.monotonic() + timeout
    last_logic: Dict[str, Any] = {}
    while time.monotonic() < deadline:
        _, docs = transact(transport, {"cmd": "logic_status"}, timeout=max(1.0, command_timeout))
        logic = next((doc.get("logic") for doc in docs if isinstance(doc.get("logic"), dict)), None)
        if isinstance(logic, dict):
            last_logic = logic
            if logic.get("complete"):
                return logic
            if logic.get("running") is False and not logic.get("complete"):
                raise RuntimeError(f"logic capture stopped before completion: {logic}")
        time.sleep(poll_interval)
    raise TimeoutError(f"logic capture did not complete, last={last_logic}")


def read_capture(transport: Transport, timeout: float) -> LogicCapture:
    _, docs = transact(transport, {"cmd": "logic_read", "offset_words": 0, "count_words": 0}, timeout)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", suffix=".jsonl", delete=False) as handle:
        path = Path(handle.name)
        for doc in docs:
            if doc.get("type") in {"logic_meta", "logic"}:
                handle.write(json.dumps(doc, separators=(",", ":")) + "\n")
    try:
        return LogicCapture.from_jsonl(str(path))
    finally:
        path.unlink(missing_ok=True)


def find_edges(capture: LogicCapture, pin: int) -> list[Dict[str, Any]]:
    return list(capture.iter_edges(pin))


def assert_edge_near_trigger(
    name: str,
    capture: LogicCapture,
    pin: int,
    edge: str,
    tolerance: int = 3,
) -> Dict[str, Any]:
    if not capture.trigger_found or capture.trigger_sample is None:
        raise AssertionError(f"{name}: trigger was not found")
    trigger_relative = capture.trigger_sample
    for item in find_edges(capture, pin):
        relative = int(item["sample"]) - capture.sample_offset
        if item["edge"] == edge and abs(relative - trigger_relative) <= tolerance:
            return item
    raise AssertionError(
        f"{name}: no {edge} edge on GP{pin} near trigger T={trigger_relative}; "
        f"edges={find_edges(capture, pin)[:8]}"
    )


def assert_level_at_trigger(name: str, capture: LogicCapture, pin: int, level: int) -> None:
    if not capture.trigger_found or capture.trigger_sample is None:
        raise AssertionError(f"{name}: trigger was not found")
    actual = capture.level(capture.trigger_sample, pin)
    if actual != level:
        raise AssertionError(f"{name}: GP{pin} level at trigger is {actual}, expected {level}")


def capture_summary(name: str, capture: LogicCapture, status: Dict[str, Any]) -> Dict[str, Any]:
    return {
        "case": name,
        "complete": bool(status.get("complete", True)),
        "samples": capture.samples,
        "pre_samples": capture.pre_samples,
        "post_samples": capture.post_samples,
        "trigger_found": capture.trigger_found,
        "trigger_sample": capture.trigger_sample,
        "burst_count": capture.burst_count,
        "burst_found": int(status.get("burst_found", len(capture.burst_samples))),
        "words": len(capture.words),
        "sample_offset": capture.sample_offset,
        "pio_trigger_mode": status.get("pio_trigger_mode"),
        "pio_trigger_irq": status.get("pio_trigger_irq"),
        "scan_dropped_samples": status.get("scan_dropped_samples"),
    }


def run_capture(
    transport: Transport,
    args: argparse.Namespace,
    name: str,
    config: Dict[str, Any],
    before_start: Optional[Dict[int, bool]] = None,
    after_arm: Optional[Dict[int, bool]] = None,
    arm_delay: Optional[float] = None,
) -> tuple[LogicCapture, Dict[str, Any]]:
    release_monitor_inputs(transport, args.timeout)
    if before_start:
        set_monitor_levels(transport, before_start, args.timeout)
    transact(transport, config, args.timeout)
    transact(transport, {"cmd": "logic_start"}, args.timeout)
    time.sleep(args.arm_delay if arm_delay is None else arm_delay)
    if after_arm:
        set_monitor_levels(transport, after_arm, args.timeout)
    status = wait_complete(transport, args.capture_timeout, args.poll_interval, args.timeout)
    capture = read_capture(transport, args.read_timeout)
    print_doc({"type": "loopback_case", **capture_summary(name, capture, status)})
    return capture, status


def common_logic_config(args: argparse.Namespace, pin_count: int = 4) -> Dict[str, Any]:
    samples = args.pre_samples + args.post_samples
    pulls = {str(pin): "up" for pin in range(16, 16 + pin_count)}
    return {
        "cmd": "logic_config",
        "pin_base": 16,
        "pin_count": pin_count,
        "sample_rate": args.sample_rate,
        "samples": samples,
        "pre_samples": args.pre_samples,
        "post_samples": args.post_samples,
        "pin_pulls": pulls,
        "pull": "none",
    }


def run_logic_tests(transport: Transport, args: argparse.Namespace) -> None:
    no_trigger_config = {
        "cmd": "logic_config",
        "pin_base": 16,
        "pin_count": 4,
        "sample_rate": min(args.sample_rate, 1_000_000),
        "samples": 512,
        "pull": "none",
        "pin_pulls": {"16": "up", "17": "up", "18": "up", "19": "up"},
    }
    run_capture(
        transport,
        args,
        "no_trigger_immediate",
        no_trigger_config,
        before_start={16: True, 17: True, 18: True, 19: True},
        arm_delay=0.0,
    )

    falling_config = {**common_logic_config(args), "trigger_pin": 16, "trigger_mode": "falling"}
    capture, _ = run_capture(
        transport,
        args,
        "falling_gp16_pretrigger_wait",
        falling_config,
        before_start={16: True, 17: True, 18: True, 19: True},
        after_arm={16: False},
        arm_delay=args.open_wait,
    )
    assert_edge_near_trigger("falling_gp16_pretrigger_wait", capture, 16, "falling")

    level_low_config = {
        **common_logic_config(args),
        "trigger_pin": 16,
        "trigger_mode": "level",
        "trigger_level": False,
    }
    capture, _ = run_capture(
        transport,
        args,
        "level_low_gp16_current_low",
        level_low_config,
        before_start={16: False, 17: True, 18: True, 19: True},
        arm_delay=0.02,
    )
    assert_level_at_trigger("level_low_gp16_current_low", capture, 16, 0)

    rising_config = {**common_logic_config(args), "trigger_pin": 16, "trigger_mode": "rising"}
    capture, _ = run_capture(
        transport,
        args,
        "rising_gp16_basic",
        rising_config,
        before_start={16: False, 17: True, 18: True, 19: True},
        after_arm={16: True},
        arm_delay=0.05,
    )
    assert_edge_near_trigger("rising_gp16_basic", capture, 16, "rising")

    pattern_config = {
        **common_logic_config(args, pin_count=2),
        "trigger_mode": "pattern",
        "trigger_mask": 0x3,
        "trigger_value": 0x2,
    }
    capture, _ = run_capture(
        transport,
        args,
        "pattern_gp16_p0_gp17_p1",
        pattern_config,
        before_start={16: True, 17: False, 18: True, 19: True},
        after_arm={16: False, 17: True},
        arm_delay=0.05,
    )
    assert_level_at_trigger("pattern_gp16_p0_gp17_p1", capture, 16, 0)
    assert_level_at_trigger("pattern_gp16_p0_gp17_p1", capture, 17, 1)

    burst_config = {
        "cmd": "logic_config",
        "pin_base": 16,
        "pin_count": 1,
        "sample_rate": 100000,
        "samples": 320,
        "pre_samples": 40,
        "post_samples": 280,
        "pin_pulls": {"16": "up"},
        "pull": "none",
        "trigger_pin": 16,
        "trigger_mode": "falling",
        "burst_count": 2,
    }
    release_monitor_inputs(transport, args.timeout)
    set_monitor_levels(transport, {16: True, 17: True, 18: True, 19: True}, args.timeout)
    ok, docs = transact(transport, burst_config, args.timeout, ignore_error=True)
    if ok:
        raise AssertionError("burst_count>1 must be rejected when firmware trigger scanning is disabled")
    doc = response_doc(docs, "logic_config")
    print_doc({"type": "loopback_case", "case": "burst_count_2_rejected", "ok": False, "msg": doc.get("msg", "")})


def run_spi_mirror_test(transport: Transport, args: argparse.Namespace) -> None:
    release_all(transport, args.timeout)
    transact(
        transport,
        {
            "cmd": "channel_config",
            "id": SPI_ID,
            "type": "spi",
            "instance": 0,
            "sck": 2,
            "mosi": 3,
            "miso": 0,
            "cs": 1,
            "baud": args.spi_baud,
        },
        args.timeout,
    )
    transact(transport, {"cmd": "channel_start", "id": SPI_ID}, args.timeout)
    config = {
        "cmd": "logic_config",
        "pin_base": 16,
        "pin_count": 4,
        "sample_rate": args.sample_rate,
        "samples": args.pre_samples + args.post_samples,
        "pre_samples": args.pre_samples,
        "post_samples": args.post_samples,
        "pin_pulls": {"16": "down", "17": "down", "18": "down", "19": "up"},
        "pull": "none",
        "trigger_pin": 19,
        "trigger_mode": "falling",
    }
    transact(transport, config, args.timeout)
    transact(transport, {"cmd": "logic_start"}, args.timeout)
    time.sleep(args.spi_wait)
    transact(transport, {"cmd": "spi_xfer", "id": SPI_ID, "hex": "a55a3cc3", "read_len": 4}, args.timeout)
    status = wait_complete(transport, args.capture_timeout, args.poll_interval, args.timeout)
    capture = read_capture(transport, args.read_timeout)
    assert_edge_near_trigger("spi0_mirror_gp16_19", capture, 19, "falling")
    if not find_edges(capture, 16):
        raise AssertionError("spi0_mirror_gp16_19: no SCK edges on GP16")
    if not find_edges(capture, 17):
        raise AssertionError("spi0_mirror_gp16_19: no MOSI edges on GP17")
    print_doc(
        {
            "type": "loopback_case",
            **capture_summary("spi0_mirror_gp16_19", capture, status),
            "gp16_edges": find_edges(capture, 16)[:8],
            "gp17_edges": find_edges(capture, 17)[:8],
            "gp19_edges": find_edges(capture, 19)[:8],
        }
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate RP2350 Monitor logic analyzer loopbacks.")
    parser.add_argument("--serial", help="USB CDC device, defaults to /dev/tty.usbmodem*")
    parser.add_argument("--tcp", help="host[:port] for Wi-Fi/TCP control instead of USB CDC")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--capture-timeout", type=float, default=8.0)
    parser.add_argument("--read-timeout", type=float, default=10.0)
    parser.add_argument("--poll-interval", type=float, default=0.02)
    parser.add_argument("--arm-delay", type=float, default=0.05)
    parser.add_argument("--open-wait", type=float, default=2.0)
    parser.add_argument("--spi-wait", type=float, default=1.0)
    parser.add_argument("--sample-rate", type=int, default=10_000_000)
    parser.add_argument("--spi-baud", type=int, default=1_000_000)
    parser.add_argument("--pre-samples", type=int, default=120)
    parser.add_argument("--post-samples", type=int, default=4096)
    parser.add_argument("--json", action="store_true", help="accepted for automation compatibility; output is always JSONL")
    args = parser.parse_args()

    transport = connect(args)
    try:
        for cmd in ("hello", "status", "logic_caps", "pins", "channels"):
            ok, docs = transact(transport, {"cmd": cmd}, args.timeout)
            doc = response_doc(docs, cmd)
            print_doc({"type": f"loopback_{cmd}", "ok": ok, **{k: v for k, v in doc.items() if k != "type"}})
            if cmd == "logic_caps":
                caps = doc.get("logic_caps", {})
                if not caps.get("ring_pretrigger") or not caps.get("open_ended_trigger_wait"):
                    raise AssertionError(f"logic_caps missing ring trigger capabilities: {caps}")
                if caps.get("firmware_trigger_scan") is not False or not caps.get("pio_pattern_trigger"):
                    raise AssertionError(f"logic_caps did not report PIO-only trigger support: {caps}")
                if caps.get("ring_dma_mode") != "pingpong" or caps.get("ring_dma_halves") != 2:
                    raise AssertionError(f"logic_caps did not report ping-pong ring DMA: {caps}")

        setup_gpio_loopbacks(transport, args.timeout)
        verify_loopback_wiring(transport, args.timeout)
        run_logic_tests(transport, args)
        run_spi_mirror_test(transport, args)
        print_doc({"type": "loopback_result", "ok": True})
        return 0
    finally:
        try:
            release_all(transport, args.timeout)
        except Exception:
            pass
        transport.close()


if __name__ == "__main__":
    raise SystemExit(main())
