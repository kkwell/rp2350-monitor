# Buffering and Reliability Design

This firmware treats hardware protocol traffic as product data, not debug text.
UART, SPI, I2C, GPIO, and future protocol drivers publish normalized JSON events into
the same live telemetry path plus per-channel replay buffers.

## Memory Budget

Current measured firmware size:

```text
text 487688 bytes
bss  402592 bytes
```

`text` lives in flash. `bss` is static SRAM and includes lwIP buffers, device
state, and the telemetry event queue. RP2350 has enough SRAM headroom for stack,
SDK runtime state, and future channel buffers after this allocation. Long-lived
core objects are static, so the telemetry queue does not consume `main()` stack.

The data path avoids heap allocation. Buffers are fixed-size and bounded at
compile time:

- Event queue: `kEventQueueCapacity = 128` JSON lines.
- Per-channel data queue: `kChannelEventQueueCapacity = 16` JSON lines per
  channel slot.
- Event line size: `kEventLineMax = 512` bytes.
- Hardware payload per event chunk: `kMaxPayloadBytes = 128` bytes.
- Replay response limit: `kEventReplayMax = 64` events per request.
- Logic analyzer capture buffer: `kLogicCaptureWords = 32768`, or 131,072
  bytes of static SRAM.
- Logic analyzer upload chunk: `kLogicUploadChunkBytes = 512` bytes per JSON
  bulk line before hex expansion.

## Event Queues

All status and protocol data events are copied into a global in-memory ring
queue before being sent to USB/TCP live sinks. Protocol data events are also
copied into a per-channel ring queue. The global queue keeps the latest unified
timeline; the per-channel queues keep recent data for each channel even if a
burst from another channel advances the global queue.

Protocol data event shape:

```json
{"type":"event","seq":25,"ts_us":1234567,"channel":1,"proto":"uart","dir":"rx","len":16,"offset":0,"hex":"00112233"}
```

`seq` is monotonic across status and data events. `offset` is non-zero when a
large publish is split into multiple 128-byte chunks.

## Overflow Behavior

The queues are intentionally bounded. If producers exceed host drain speed long
enough to fill a queue, the oldest event in that queue is dropped and the newest
event is accepted. The firmware records global counters and per-channel
counters:

- `buffers.dropped_events`
- `buffers.dropped_bytes`
- `buffers.event_depth`
- `buffers.event_max_depth`
- `buffers.oldest_seq`
- `buffers.newest_seq`
- `buffers.channels[n].dropped_events`
- `buffers.channels[n].dropped_bytes`
- `buffers.channels[n].depth`

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
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX events_read --channel 4 --count 16
```

Global `events_read` streams buffered status/event lines first, then returns a
command response. Channel-scoped `events_read --channel N` streams only data
events from channel `N`'s isolated queue.

```json
{"type":"resp","ok":true,"cmd":"events_read","msg":"events replayed","sent":12,"max":64,"since_seq":120,"channel":-1}
```

Buffer health can be polled explicitly:

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX buffer_status
```

`status` and HTTP `GET /api/status` also include the same `buffers` object.

## High-Speed Capture Path

The logic analyzer path is intentionally separate from the event queues. PIO2
samples a contiguous GPIO group at the requested fixed rate, DMA copies packed
32-bit words into the static capture buffer, and the firmware releases the PIO
state machine and DMA channel when the capture completes. The host then uses
`logic_read` to upload the frozen buffer over USB CDC or Wi-Fi TCP.

This design gives high-speed digital input a deterministic memory ceiling and
prevents large captures from overwriting recent UART/SPI/I2C/GPIO events. It is
a capture-then-upload instrument in this version, not an unbounded live stream.
GPIOs are deinitialized when `logic_release` frees the capture pins, which avoids
leaving released pins in the PIO function after a capture session.

Logic analyzer status is available through `status`, HTTP `GET /api/status`,
and `logic_status`. Important fields:

- `logic.running`: DMA capture is still active.
- `logic.complete`: data is ready for `logic_read`.
- `logic.words`: captured 32-bit word count.
- `logic.buffer_words_max` / `logic.buffer_bytes`: fixed SRAM capacity.
- `logic.record_bits`: number of valid packed bits per 32-bit word.
- `logic.pull`: analyzer input bias for the current configuration, one of
  `none`, `up`, or `down`.

Use `logic_caps` before allocating host-side storage or presenting a capture
configuration UI. It is the stable source for buffer size, upload chunk size,
supported trigger modes, supported pull modes, and reserved features.

## Failure Cases

- USB disconnected: live USB sends are skipped, but events remain in the ring
  until overwritten by newer events.
- TCP client absent or slow: live TCP sends may fail without blocking hardware
  polling; the ring remains available for replay.
- Queue full: oldest events are dropped, counters increase, warning status is
  emitted for global overflow. Per-channel drops are reported in
  `buffers.channels`.
- Oversized data publish: data is split into bounded chunks with `offset`.
- GPIO input changes, reads, and writes are normalized as one-byte data events.
- Oversized logic analyzer request: rejected before capture starts, with the
  required word count and maximum word count in the response.
- Logic analyzer runtime resource conflict: `logic_start` fails if PIO2 or DMA
  cannot be claimed; already claimed GPIOs are rejected during `logic_config`.
- Logic analyzer trigger timeout: host `logic_capture` returns non-zero and
  sends `logic_stop` when the configured level/edge trigger is not observed
  within `--wait-timeout`.
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
4. Watch `buffers.channels[n].dropped_events` for channel-local gaps.
5. Use `events_read --since-seq` and `events_read --channel N` after reconnects.
6. Keep protocol-specific decoders outside the firmware; the firmware provides
   timestamped byte events and reliable health metadata.

For high-speed logic captures, host software should:

1. Use `logic_capture --output capture.jsonl` when a complete raw artifact is
   needed.
2. Call `logic_status` until `complete:true` if controlling the low-level
   commands directly.
3. Use `logic_read --count-words 0` or page by `offset_words`.
4. Store every `type:"logic"` JSON line before decoding.
5. Decode offline with `logic_decode`; keep expensive analysis on the host.
6. Treat a new `logic_start` as destructive to the previous capture buffer.
