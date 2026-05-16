#!/usr/bin/env python3
"""Offline helpers for RP2350 Monitor logic-analyzer captures."""

from __future__ import annotations

import csv
import json
import math
import statistics
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Iterable, Iterator, List, Optional, TextIO


JsonDoc = Dict[str, Any]


class LogicDecodeError(RuntimeError):
    pass


@dataclass
class LogicCapture:
    capture_id: int
    pin_base: int
    pin_count: int
    sample_rate: int
    samples: int
    record_bits: int
    words: List[int]
    channel_names: Dict[int, str] = field(default_factory=dict)
    sample_offset: int = 0

    @classmethod
    def from_jsonl(cls, path: str, capture_id: Optional[int] = None) -> "LogicCapture":
        docs = list(read_jsonl(path))
        chunks = [doc for doc in docs if doc.get("type") == "logic"]
        if not chunks:
            raise LogicDecodeError("no type=logic records found in capture file")
        if capture_id is None:
            capture_id = max(int(doc.get("capture_id", 0)) for doc in chunks)
        chunks = [doc for doc in chunks if int(doc.get("capture_id", -1)) == capture_id]
        if not chunks:
            raise LogicDecodeError(f"capture_id {capture_id} was not found")

        first = chunks[0]
        pin_base = int(first["pin_base"])
        pin_count = int(first["pin_count"])
        sample_rate = int(first["sample_rate"])
        samples = int(first["samples"])
        record_bits = int(first.get("record_bits", bits_packed_per_word(pin_count)))
        channel_names = collect_channel_names(docs)
        expected_words = math.ceil((samples * pin_count) / record_bits)

        words: List[Optional[int]] = [None] * expected_words
        for doc in sorted(chunks, key=lambda item: int(item.get("offset_words", 0))):
            offset = int(doc["offset_words"])
            count = int(doc["words"])
            raw = bytes.fromhex(str(doc["hex"]))
            if len(raw) != count * 4:
                raise LogicDecodeError(
                    f"logic chunk at word {offset} has {len(raw)} bytes, expected {count * 4}"
                )
            if offset < 0 or offset + count > expected_words:
                raise LogicDecodeError(f"logic chunk at word {offset} exceeds expected capture size")
            for index in range(count):
                start = index * 4
                words[offset + index] = int.from_bytes(raw[start : start + 4], "little")

        missing = [index for index, value in enumerate(words) if value is None]
        if missing:
            raise LogicDecodeError(f"capture is missing {len(missing)} words, first missing word {missing[0]}")

        return cls(
            capture_id=capture_id,
            pin_base=pin_base,
            pin_count=pin_count,
            sample_rate=sample_rate,
            samples=samples,
            record_bits=record_bits,
            words=[int(value) for value in words],
            channel_names=channel_names,
        )

    @property
    def duration_us(self) -> float:
        if self.sample_rate <= 0:
            return 0.0
        return self.samples * 1_000_000.0 / self.sample_rate

    def sample_time_us(self, sample: int) -> float:
        return (sample + self.sample_offset) * 1_000_000.0 / self.sample_rate

    def absolute_sample(self, sample: int) -> int:
        return sample + self.sample_offset

    def resolve_pin(self, pin: int) -> int:
        if self.pin_base <= pin < self.pin_base + self.pin_count:
            return pin - self.pin_base
        if 0 <= pin < self.pin_count:
            return pin
        raise LogicDecodeError(
            f"pin {pin} is outside captured range GPIO{self.pin_base}..GPIO{self.pin_base + self.pin_count - 1}"
        )

    def gpio_for_index(self, pin_index: int) -> int:
        return self.pin_base + pin_index

    def channel_label(self, pin_index: int) -> str:
        gpio = self.gpio_for_index(pin_index)
        return self.channel_names.get(gpio, f"gpio{gpio}")

    def level_at_index(self, sample: int, pin_index: int) -> int:
        if sample < 0:
            sample = 0
        if sample >= self.samples:
            sample = self.samples - 1
        bit_index = pin_index + sample * self.pin_count
        word_index = bit_index // self.record_bits
        shift = (bit_index % self.record_bits) + 32 - self.record_bits
        return 1 if (self.words[word_index] & (1 << shift)) else 0

    def level(self, sample: int, pin: int) -> int:
        return self.level_at_index(sample, self.resolve_pin(pin))

    def sliced(self, start_sample: Optional[int], end_sample: Optional[int]) -> "LogicCapture":
        if start_sample is None and end_sample is None:
            return self
        start = 0 if start_sample is None else int(start_sample)
        end = self.samples if end_sample is None else int(end_sample)
        if start < 0 or end < 0 or start >= end or end > self.samples:
            raise LogicDecodeError(f"invalid sample window {start}..{end}, capture has {self.samples} samples")
        if start == 0 and end == self.samples:
            return self

        samples = end - start
        record_bits = bits_packed_per_word(self.pin_count)
        expected_words = math.ceil((samples * self.pin_count) / record_bits)
        words = [0] * expected_words
        for rel_sample in range(samples):
            for pin_index in range(self.pin_count):
                if not self.level_at_index(start + rel_sample, pin_index):
                    continue
                bit_index = pin_index + rel_sample * self.pin_count
                word_index = bit_index // record_bits
                shift = (bit_index % record_bits) + 32 - record_bits
                words[word_index] |= 1 << shift

        return LogicCapture(
            capture_id=self.capture_id,
            pin_base=self.pin_base,
            pin_count=self.pin_count,
            sample_rate=self.sample_rate,
            samples=samples,
            record_bits=record_bits,
            words=words,
            channel_names=dict(self.channel_names),
            sample_offset=self.sample_offset + start,
        )

    def iter_edges(self, pin: int) -> Iterator[JsonDoc]:
        pin_index = self.resolve_pin(pin)
        last = self.level_at_index(0, pin_index)
        for sample in range(1, self.samples):
            value = self.level_at_index(sample, pin_index)
            if value != last:
                yield {
                    "type": "logic_edge",
                    "capture_id": self.capture_id,
                    "sample": self.absolute_sample(sample),
                    "t_us": self.sample_time_us(sample),
                    "gpio": self.gpio_for_index(pin_index),
                    "name": self.channel_label(pin_index),
                    "edge": "rising" if value else "falling",
                    "level": value,
                }
                last = value


def bits_packed_per_word(pin_count: int) -> int:
    return 32 - (32 % pin_count)


def read_jsonl(path: str) -> Iterator[JsonDoc]:
    with open(path, "r", encoding="utf-8") as handle:
        for line_no, line in enumerate(handle, 1):
            line = line.strip()
            if not line:
                continue
            try:
                doc = json.loads(line)
            except json.JSONDecodeError as exc:
                raise LogicDecodeError(f"{path}:{line_no}: invalid JSON: {exc}") from exc
            if isinstance(doc, dict):
                yield doc


def collect_channel_names(docs: Iterable[JsonDoc]) -> Dict[int, str]:
    names: Dict[int, str] = {}
    for doc in docs:
        if doc.get("type") != "logic_settings":
            continue
        raw = doc.get("channel_names")
        if isinstance(raw, dict):
            for key, value in raw.items():
                names[int(str(key), 0)] = str(value)
        channels = doc.get("channels")
        if isinstance(channels, list):
            for item in channels:
                if not isinstance(item, dict) or "name" not in item:
                    continue
                gpio = item.get("gpio", item.get("pin", item.get("index")))
                if gpio is not None:
                    names[int(gpio)] = str(item["name"])
    return names


def write_json_docs(docs: Iterable[JsonDoc], out: TextIO) -> None:
    for doc in docs:
        out.write(json.dumps(doc, separators=(",", ":")) + "\n")
    out.flush()


def capture_summary(capture: LogicCapture) -> Iterator[JsonDoc]:
    yield {
        "type": "logic_summary",
        "capture_id": capture.capture_id,
        "pin_base": capture.pin_base,
        "pin_count": capture.pin_count,
        "sample_rate": capture.sample_rate,
        "samples": capture.samples,
        "sample_start": capture.sample_offset,
        "sample_end": capture.sample_offset + capture.samples,
        "duration_us": capture.duration_us,
        "record_bits": capture.record_bits,
        "words": len(capture.words),
    }
    for pin_index in range(capture.pin_count):
        yield measure_pin(capture, pin_index)


def measure_pin(capture: LogicCapture, pin_index: int) -> JsonDoc:
    level = capture.level_at_index(0, pin_index)
    high_samples = 0
    low_samples = 0
    rising: List[int] = []
    falling: List[int] = []
    high_runs: List[int] = []
    low_runs: List[int] = []
    run_start = 0
    for sample in range(capture.samples):
        current = capture.level_at_index(sample, pin_index)
        if current:
            high_samples += 1
        else:
            low_samples += 1
        if sample == 0:
            continue
        if current != level:
            run_len = sample - run_start
            if level:
                high_runs.append(run_len)
                falling.append(sample)
            else:
                low_runs.append(run_len)
                rising.append(sample)
            level = current
            run_start = sample
    tail_run = capture.samples - run_start
    if level:
        high_runs.append(tail_run)
    else:
        low_runs.append(tail_run)

    periods = [b - a for a, b in zip(rising, rising[1:])]
    median_period = statistics.median(periods) if periods else 0
    return {
        "type": "logic_measure",
        "capture_id": capture.capture_id,
        "gpio": capture.gpio_for_index(pin_index),
        "name": capture.channel_label(pin_index),
        "high_samples": high_samples,
        "low_samples": low_samples,
        "duty_cycle": high_samples / capture.samples if capture.samples else 0.0,
        "rising_edges": len(rising),
        "falling_edges": len(falling),
        "avg_high_us": avg_samples_to_us(capture, high_runs),
        "avg_low_us": avg_samples_to_us(capture, low_runs),
        "median_period_us": median_period * 1_000_000.0 / capture.sample_rate if median_period else 0.0,
        "median_frequency_hz": capture.sample_rate / median_period if median_period else 0.0,
    }


def avg_samples_to_us(capture: LogicCapture, runs: List[int]) -> float:
    if not runs:
        return 0.0
    return (sum(runs) / len(runs)) * 1_000_000.0 / capture.sample_rate


def decode_uart(
    capture: LogicCapture,
    rx_pin: int,
    baud: int,
    data_bits: int = 8,
    parity: str = "none",
    stop_bits: float = 1.0,
    invert: bool = False,
) -> Iterator[JsonDoc]:
    pin_index = capture.resolve_pin(rx_pin)
    bit_samples = capture.sample_rate / baud
    if bit_samples < 3:
        yield warning("uart", f"sample rate gives only {bit_samples:.2f} samples per bit")
    parity_bits = 0 if parity == "none" else 1
    frame_bits = 1 + data_bits + parity_bits + stop_bits

    def bit(sample: int) -> int:
        value = capture.level_at_index(sample, pin_index)
        return value ^ int(invert)

    sample = 1
    prev = bit(0)
    while sample < capture.samples:
        current = bit(sample)
        if prev == 1 and current == 0:
            start = sample
            center = int(round(start + 0.5 * bit_samples))
            if center >= capture.samples or bit(center) != 0:
                sample += 1
                prev = current
                continue
            value = 0
            valid = True
            for index in range(data_bits):
                pos = int(round(start + (1.5 + index) * bit_samples))
                if pos >= capture.samples:
                    valid = False
                    break
                value |= bit(pos) << index
            parity_error = False
            if valid and parity_bits:
                pos = int(round(start + (1.5 + data_bits) * bit_samples))
                if pos >= capture.samples:
                    valid = False
                else:
                    parity_bit = bit(pos)
                    ones = value.bit_count() + parity_bit
                    if parity == "even":
                        parity_error = (ones % 2) != 0
                    elif parity == "odd":
                        parity_error = (ones % 2) == 0
            stop_error = False
            if valid:
                stop_center = 1.5 + data_bits + parity_bits
                pos = int(round(start + stop_center * bit_samples))
                if pos >= capture.samples or bit(pos) != 1:
                    stop_error = True
            end = min(capture.samples - 1, int(round(start + frame_bits * bit_samples)))
            doc = {
                "type": "logic_decode",
                "proto": "uart",
                "capture_id": capture.capture_id,
                "gpio": capture.gpio_for_index(pin_index),
                "name": capture.channel_label(pin_index),
                "start_sample": capture.absolute_sample(start),
                "end_sample": capture.absolute_sample(end),
                "start_us": capture.sample_time_us(start),
                "end_us": capture.sample_time_us(end),
                "baud": baud,
                "data_bits": data_bits,
                "value": value if valid else None,
                "hex": f"{value:02x}" if valid and data_bits <= 8 else (f"{value:x}" if valid else ""),
                "char": chr(value) if valid and 32 <= value < 127 else "",
                "parity_error": parity_error,
                "frame_error": (not valid) or stop_error,
            }
            yield doc
            sample = max(sample + 1, end)
            prev = bit(sample - 1)
            continue
        prev = current
        sample += 1


def decode_spi(
    capture: LogicCapture,
    sck_pin: int,
    mosi_pin: Optional[int],
    miso_pin: Optional[int],
    cs_pin: Optional[int],
    mode: int,
    cs_active_high: bool,
    word_bits: int,
    bit_order: str,
) -> Iterator[JsonDoc]:
    if mosi_pin is None and miso_pin is None:
        raise LogicDecodeError("SPI decode requires --mosi-pin and/or --miso-pin")
    sck = capture.resolve_pin(sck_pin)
    mosi = capture.resolve_pin(mosi_pin) if mosi_pin is not None else None
    miso = capture.resolve_pin(miso_pin) if miso_pin is not None else None
    cs = capture.resolve_pin(cs_pin) if cs_pin is not None else None
    cpol = 1 if mode in (2, 3) else 0
    cpha = 1 if mode in (1, 3) else 0
    active_level = 1 if cs_active_high else 0

    def is_active(sample: int) -> bool:
        return True if cs is None else capture.level_at_index(sample, cs) == active_level

    mosi_bits: List[int] = []
    miso_bits: List[int] = []
    frame = 0
    prev_sck = capture.level_at_index(0, sck)
    was_active = is_active(0)
    for sample in range(1, capture.samples):
        active = is_active(sample)
        if not active:
            if was_active and (mosi_bits or miso_bits):
                yield spi_word_doc(capture, frame, sample, mosi_bits, miso_bits, word_bits, bit_order, partial=True)
                frame += 1
                mosi_bits.clear()
                miso_bits.clear()
            was_active = False
            prev_sck = capture.level_at_index(sample, sck)
            continue
        current_sck = capture.level_at_index(sample, sck)
        if current_sck != prev_sck:
            leading = prev_sck == cpol and current_sck != cpol
            if (cpha == 0 and leading) or (cpha == 1 and not leading):
                if mosi is not None:
                    mosi_bits.append(capture.level_at_index(sample, mosi))
                if miso is not None:
                    miso_bits.append(capture.level_at_index(sample, miso))
                bit_count = max(len(mosi_bits), len(miso_bits))
                if bit_count and bit_count % word_bits == 0:
                    yield spi_word_doc(capture, frame, sample, mosi_bits, miso_bits, word_bits, bit_order, partial=False)
                    frame += 1
                    mosi_bits.clear()
                    miso_bits.clear()
        was_active = True
        prev_sck = current_sck
    if mosi_bits or miso_bits:
        yield spi_word_doc(capture, frame, capture.samples - 1, mosi_bits, miso_bits, word_bits, bit_order, partial=True)


def spi_word_doc(
    capture: LogicCapture,
    frame: int,
    sample: int,
    mosi_bits: List[int],
    miso_bits: List[int],
    word_bits: int,
    bit_order: str,
    partial: bool,
) -> JsonDoc:
    doc: JsonDoc = {
        "type": "logic_decode",
        "proto": "spi",
        "capture_id": capture.capture_id,
        "frame": frame,
        "sample": capture.absolute_sample(sample),
        "t_us": capture.sample_time_us(sample),
        "bits": max(len(mosi_bits), len(miso_bits)),
        "partial": partial,
    }
    if mosi_bits:
        value = bits_to_int(mosi_bits, bit_order)
        doc["mosi"] = value
        doc["mosi_hex"] = format_word(value, len(mosi_bits), word_bits)
    if miso_bits:
        value = bits_to_int(miso_bits, bit_order)
        doc["miso"] = value
        doc["miso_hex"] = format_word(value, len(miso_bits), word_bits)
    return doc


def bits_to_int(bits: List[int], order: str) -> int:
    value = 0
    if order == "msb":
        for bit in bits:
            value = (value << 1) | bit
    else:
        for index, bit in enumerate(bits):
            value |= bit << index
    return value


def format_word(value: int, bit_count: int, word_bits: int) -> str:
    width = max(1, math.ceil(min(bit_count, word_bits) / 4))
    return f"{value:0{width}x}"


def decode_i2c(capture: LogicCapture, sda_pin: int, scl_pin: int) -> Iterator[JsonDoc]:
    sda = capture.resolve_pin(sda_pin)
    scl = capture.resolve_pin(scl_pin)
    active = False
    first_byte = True
    bits: List[int] = []
    bit_start = 0
    for sample in range(1, capture.samples):
        prev_sda = capture.level_at_index(sample - 1, sda)
        prev_scl = capture.level_at_index(sample - 1, scl)
        cur_sda = capture.level_at_index(sample, sda)
        cur_scl = capture.level_at_index(sample, scl)
        if prev_sda == 1 and cur_sda == 0 and cur_scl == 1:
            if active and bits:
                yield warning("i2c", "repeated START discarded a partial byte", sample, capture)
            active = True
            first_byte = True
            bits.clear()
            bit_start = sample
            yield i2c_event(capture, "start", sample)
            continue
        if active and prev_sda == 0 and cur_sda == 1 and cur_scl == 1:
            if bits:
                yield warning("i2c", "STOP discarded a partial byte", sample, capture)
                bits.clear()
            yield i2c_event(capture, "stop", sample)
            active = False
            continue
        if active and prev_scl == 0 and cur_scl == 1:
            if not bits:
                bit_start = sample
            bits.append(cur_sda)
            if len(bits) == 9:
                value = bits_to_int(bits[:8], "msb")
                ack = bits[8] == 0
                if first_byte:
                    yield {
                        "type": "logic_decode",
                        "proto": "i2c",
                        "capture_id": capture.capture_id,
                        "event": "address",
                        "start_sample": capture.absolute_sample(bit_start),
                        "end_sample": capture.absolute_sample(sample),
                        "start_us": capture.sample_time_us(bit_start),
                        "end_us": capture.sample_time_us(sample),
                        "address": value >> 1,
                        "rw": "read" if (value & 1) else "write",
                        "raw": value,
                        "ack": ack,
                    }
                    first_byte = False
                else:
                    yield {
                        "type": "logic_decode",
                        "proto": "i2c",
                        "capture_id": capture.capture_id,
                        "event": "data",
                        "start_sample": capture.absolute_sample(bit_start),
                        "end_sample": capture.absolute_sample(sample),
                        "start_us": capture.sample_time_us(bit_start),
                        "end_us": capture.sample_time_us(sample),
                        "value": value,
                        "hex": f"{value:02x}",
                        "ack": ack,
                    }
                bits.clear()


def i2c_event(capture: LogicCapture, event: str, sample: int) -> JsonDoc:
    return {
        "type": "logic_decode",
        "proto": "i2c",
        "capture_id": capture.capture_id,
        "event": event,
        "sample": capture.absolute_sample(sample),
        "t_us": capture.sample_time_us(sample),
    }


def warning(proto: str, msg: str, sample: Optional[int] = None, capture: Optional[LogicCapture] = None) -> JsonDoc:
    doc: JsonDoc = {"type": "logic_warning", "proto": proto, "msg": msg}
    if sample is not None:
        doc["sample"] = capture.absolute_sample(sample) if capture is not None else sample
    if capture is not None and sample is not None:
        doc["t_us"] = capture.sample_time_us(sample)
        doc["capture_id"] = capture.capture_id
    return doc


def export_csv(capture: LogicCapture, output: str) -> None:
    with open(output, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["sample", "t_us"] + [capture.channel_label(i) for i in range(capture.pin_count)])
        for sample in range(capture.samples):
            writer.writerow(
                [capture.absolute_sample(sample), f"{capture.sample_time_us(sample):.6f}"]
                + [capture.level_at_index(sample, pin) for pin in range(capture.pin_count)]
            )


def export_vcd(capture: LogicCapture, output: str) -> None:
    identifiers = vcd_identifiers(capture.pin_count)
    with open(output, "w", encoding="utf-8") as handle:
        handle.write("$date\n    generated by rpmon_logic.py\n$end\n")
        handle.write("$version\n    RP2350 Monitor logic export\n$end\n")
        handle.write("$timescale 1 ns $end\n")
        handle.write("$scope module rpmon $end\n")
        for pin_index, ident in enumerate(identifiers):
            handle.write(f"$var wire 1 {ident} {vcd_signal_name(capture, pin_index)} $end\n")
        handle.write("$upscope $end\n$enddefinitions $end\n")
        start_ns = round(capture.sample_offset * 1_000_000_000 / capture.sample_rate)
        handle.write(f"#{start_ns}\n")
        levels = [capture.level_at_index(0, pin) for pin in range(capture.pin_count)]
        for ident, level in zip(identifiers, levels):
            handle.write(f"{level}{ident}\n")
        for sample in range(1, capture.samples):
            changes = []
            for pin_index, ident in enumerate(identifiers):
                level = capture.level_at_index(sample, pin_index)
                if level != levels[pin_index]:
                    levels[pin_index] = level
                    changes.append((ident, level))
            if changes:
                timestamp_ns = round(capture.absolute_sample(sample) * 1_000_000_000 / capture.sample_rate)
                handle.write(f"#{timestamp_ns}\n")
                for ident, level in changes:
                    handle.write(f"{level}{ident}\n")


def vcd_identifiers(count: int) -> List[str]:
    alphabet = [chr(code) for code in range(33, 127)]
    identifiers: List[str] = []
    for index in range(count):
        value = index
        ident = ""
        while True:
            ident += alphabet[value % len(alphabet)]
            value //= len(alphabet)
            if value == 0:
                break
        identifiers.append(ident)
    return identifiers


def vcd_signal_name(capture: LogicCapture, pin_index: int) -> str:
    label = capture.channel_label(pin_index)
    safe = "".join(ch if ch.isalnum() or ch == "_" else "_" for ch in label.strip())
    if not safe:
        safe = f"gpio{capture.gpio_for_index(pin_index)}"
    if safe[0].isdigit():
        safe = f"gpio{capture.gpio_for_index(pin_index)}_{safe}"
    if not safe.startswith(f"gpio{capture.gpio_for_index(pin_index)}"):
        safe = f"gpio{capture.gpio_for_index(pin_index)}_{safe}"
    return safe


def decode_command(args: Any) -> int:
    try:
        capture = LogicCapture.from_jsonl(args.input, args.capture_id).sliced(args.start_sample, args.end_sample)
        docs: Iterable[JsonDoc]
        if args.decoder == "summary":
            docs = capture_summary(capture)
        elif args.decoder == "edges":
            pins = args.pin if args.pin else list(range(capture.pin_base, capture.pin_base + capture.pin_count))
            docs = (edge for pin in pins for edge in capture.iter_edges(pin))
        elif args.decoder == "uart":
            require(args.rx_pin is not None, "UART decode requires --rx-pin")
            docs = decode_uart(capture, args.rx_pin, args.baud, args.data_bits, args.parity, args.stop_bits, args.invert)
        elif args.decoder == "spi":
            require(args.sck_pin is not None, "SPI decode requires --sck-pin")
            docs = decode_spi(
                capture,
                args.sck_pin,
                args.mosi_pin,
                args.miso_pin,
                args.cs_pin,
                args.mode,
                args.cs_active_high,
                args.word_bits,
                args.bit_order,
            )
        elif args.decoder == "i2c":
            require(args.sda_pin is not None and args.scl_pin is not None, "I2C decode requires --sda-pin and --scl-pin")
            docs = decode_i2c(capture, args.sda_pin, args.scl_pin)
        else:
            raise LogicDecodeError(f"unsupported decoder {args.decoder}")

        if args.output:
            with open(args.output, "w", encoding="utf-8") as handle:
                write_json_docs(docs, handle)
        else:
            write_json_docs(docs, sys.stdout)
        return 0
    except LogicDecodeError as exc:
        print(json.dumps({"type": "logic_error", "msg": str(exc)}, separators=(",", ":")), file=sys.stderr)
        return 2


def export_command(args: Any) -> int:
    try:
        capture = LogicCapture.from_jsonl(args.input, args.capture_id).sliced(args.start_sample, args.end_sample)
        if args.format == "csv":
            export_csv(capture, args.output)
        elif args.format == "vcd":
            export_vcd(capture, args.output)
        else:
            raise LogicDecodeError(f"unsupported export format {args.format}")
        print(json.dumps({"type": "logic_export", "format": args.format, "output": args.output}, separators=(",", ":")))
        return 0
    except LogicDecodeError as exc:
        print(json.dumps({"type": "logic_error", "msg": str(exc)}, separators=(",", ":")), file=sys.stderr)
        return 2


def require(condition: bool, message: str) -> None:
    if not condition:
        raise LogicDecodeError(message)
