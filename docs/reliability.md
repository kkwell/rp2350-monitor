# Buffering and Reliability Design

This firmware treats hardware protocol traffic as product data, not debug text.
UART, SPI, I2C, GPIO, and future protocol drivers publish normalized JSON events into
one shared telemetry path.

## Memory Budget

Current measured firmware size:

```text
text 478312 bytes
bss  189788 bytes
```

`text` lives in flash. `bss` is static SRAM and includes lwIP buffers, device
state, and the telemetry event queue. RP2350 has enough SRAM headroom for stack,
SDK runtime state, and future channel buffers after this allocation. Long-lived
core objects are static, so the telemetry queue does not consume `main()` stack.

The data path avoids heap allocation. Buffers are fixed-size and bounded at
compile time:

- Event queue: `kEventQueueCapacity = 128` JSON lines.
- Event line size: `kEventLineMax = 512` bytes.
- Hardware payload per event chunk: `kMaxPayloadBytes = 128` bytes.
- Replay response limit: `kEventReplayMax = 64` events per request.

## Event Queue

All status and protocol data events are copied into an in-memory ring queue
before being sent to USB/TCP live sinks. The queue keeps the latest events even
when no host is connected or when a live sink cannot accept data immediately.

Protocol data event shape:

```json
{"type":"event","seq":25,"ts_us":1234567,"channel":1,"proto":"uart","dir":"rx","len":16,"offset":0,"hex":"00112233"}
```

`seq` is monotonic across status and data events. `offset` is non-zero when a
large publish is split into multiple 128-byte chunks.

## Overflow Behavior

The queue is intentionally bounded. If producers exceed host drain speed long
enough to fill the queue, the oldest event is dropped and the newest event is
accepted. The firmware records:

- `buffers.dropped_events`
- `buffers.dropped_bytes`
- `buffers.event_depth`
- `buffers.event_max_depth`
- `buffers.oldest_seq`
- `buffers.newest_seq`

The first overflow and then every 16 additional drops produce a warning status
event:

```json
{"type":"status","component":"buffer","msg":"event queue overflow","level":"warning","dropped_events":17,"dropped_bytes":2048,"capacity":128}
```

This gives the host a clear audit trail that captured data is incomplete.

## Host Recovery and Storage

Hosts should continuously read from USB or TCP and append every JSON line to a
local JSONL log. The bundled CLI supports this directly:

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX --log capture.jsonl monitor
```

If a host reconnects or suspects missed live traffic, it can pull recent buffered
events:

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX events_read --count 64 --since-seq 120
```

`events_read` streams buffered status/event lines first, then returns a command
response:

```json
{"type":"resp","ok":true,"cmd":"events_read","msg":"events replayed","sent":12,"max":64,"since_seq":120}
```

Buffer health can be polled explicitly:

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX buffer_status
```

`status` and HTTP `GET /api/status` also include the same `buffers` object.

## Failure Cases

- USB disconnected: live USB sends are skipped, but events remain in the ring
  until overwritten by newer events.
- TCP client absent or slow: live TCP sends may fail without blocking hardware
  polling; the ring remains available for replay.
- Queue full: oldest events are dropped, counters increase, warning status is
  emitted.
- Oversized data publish: data is split into bounded chunks with `offset`.
- GPIO input changes, reads, and writes are normalized as one-byte data events.
- Invalid command payloads: command responses return `ok:false` with a clear
  `msg`; they are not added to the telemetry queue.
- Wi-Fi connection failure: AP recovery is started and per-profile
  `last_error` records readable failure text.

## Product Guidance

For high-speed or continuous passive sniffing, the host software should:

1. Use USB or TCP as a continuous JSONL stream.
2. Write every line to disk before doing expensive analysis.
3. Watch `buffers.dropped_events`; any non-zero delta means the capture has a
   gap.
4. Use `events_read --since-seq` after reconnects.
5. Keep protocol-specific decoders outside the firmware; the firmware provides
   timestamped byte events and reliable health metadata.
