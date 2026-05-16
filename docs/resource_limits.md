# Hardware Resource Limits

This firmware supports concurrent protocol channels when the requested hardware
resources do not overlap. The firmware rejects invalid combinations with
`ok:false` JSON responses instead of silently reusing a peripheral or pin.

## Native Peripheral Limits

Pico 2 W / RP2350 exposes these native protocol blocks to the firmware:

- UART: 2 instances, `instance:0` and `instance:1`.
- SPI: 2 instances, `instance:0` and `instance:1`.
- I2C: 2 instances, `instance:0` and `instance:1`.
- GPIO: exposed header pins `0..22` and `26..28`.
- High-speed logic analyzer: one active PIO2/DMA capture engine, using one PIO
  state machine, one DMA channel, and a dedicated 32,768-word SRAM buffer.
- CAN: reserved in the protocol model, not implemented in this firmware version.

The channel table has `kMaxChannels = 8` slots. A valid concurrent setup can mix
UART, SPI, I2C, and GPIO channels up to that table size, subject to native
peripheral and pin ownership limits.

The logic analyzer is outside the eight-channel table because it is a bulk
capture instrument, not a byte-stream protocol channel. It still uses the same
pin ownership rules, so a GPIO cannot be used by UART/SPI/I2C/GPIO and logic
capture at the same time.

## Rejection Cases

The control API returns a failed response when a request exceeds hardware or
firmware limits:

- Reusing `uart0`, `uart1`, `spi0`, `spi1`, `i2c0`, or `i2c1` from another
  channel returns a message such as `spi0 is already assigned to another channel`.
- Selecting a pin that cannot serve the requested alternate function returns
  `invalid UART/SPI/I2C pin mapping`.
- Selecting a pin already owned by another channel returns `pins already in use`
  or `GPIO pin already in use or not exposed`.
- Creating more than eight configured channels returns `channel table full`.
- `type:"can"` currently returns a reserved-driver error.
- I2C transfers to a non-responding address return an explicit bus error, for
  example `I2C write failed: -1`.
- Logic analyzer captures that do not fit SRAM return a message such as
  `logic capture requires 65536 words, max is 32768`.
- Logic analyzer pin ranges must be contiguous exposed GPIOs and cannot overlap
  other channel ownership.
- Logic analyzer `pull` must be `none`, `up`, or `down`; invalid values return
  `invalid logic pull mode`.
- Logic analyzer `pin_pulls` values must be `none`, `up`, or `down`, and keys
  must point inside the captured GPIO range.
- Pattern trigger masks and values are relative to the captured contiguous pin
  range and cannot exceed `pin_count` bits.
- `burst_count` is capped by `kLogicBurstMarksMax = 16`.
- `search_samples` must fit the same 32,768-word SRAM capture buffer.
- If the PIO2 state machine, PIO instruction memory, or a DMA channel cannot be
  claimed, `logic_start` returns `ok:false` with the specific resource name.

Use the `pins` command to inspect current pin ownership and `channels` to inspect
configured protocol instances. Use `release --id N` to remove a channel and free
its pins.

## Buffer Isolation

Protocol data is buffered in two layers:

- Global event queue: 128 recent status/data JSON lines, preserving a unified
  timeline across Wi-Fi and USB clients.
- Per-channel data queue: 16 recent data events per active channel slot, so a
  burst on one channel does not erase every other channel's recent data history.

`buffer_status` reports both global counters and per-channel depth/drop counters.
`events_read --channel N` replays the per-channel data queue for channel `N`.
Without `--channel`, `events_read` replays the global queue.

High-speed logic analyzer captures use a separate fixed buffer:

- `kLogicCaptureWords = 32768`.
- Buffer size is 131,072 bytes.
- Upload chunks are capped at `kLogicUploadChunkBytes = 512`.
- Bulk `type:"logic"` lines are sent over the same USB CDC or Wi-Fi TCP link as
  other JSON lines, but they are not copied into the telemetry event queues.
- `logic_caps` reports the active analyzer limits, including exposed contiguous
  GPIO ranges, sample-rate ceiling, SRAM buffer size, upload chunk size,
  supported capture modes, trigger modes, pull modes, burst marker capacity,
  host decoders, host exports, and reserved features.

This keeps high-speed captures from evicting ordinary UART/SPI/I2C/GPIO event
history. It also means hosts must finish `logic_read` and store the resulting
JSONL if they need the full capture after another `logic_start`.

Host-side decoding does not consume RP2350 SRAM. `logic_decode` loads the raw
capture JSONL on the computer and emits decoded JSONL for burst markers,
UART/SPI/I2C, edge lists, and timing summaries. Very large CSV/VCD exports are
bounded by host disk space rather than Pico memory.

The firmware still cannot overcome physical bandwidth limits. Hosts should keep
USB or TCP reads active and write JSONL to disk when capturing sustained traffic.
Any non-zero `dropped_events` or per-channel `dropped_events` means the capture
has a gap and the host should mark the analysis as incomplete.

## Current Protocol Behavior

- UART is passive RX monitoring plus TX injection.
- SPI and I2C are host-initiated transaction engines in this version. Passive
  bus sniffing is reserved for future PIO drivers.
- GPIO supports input sampling, output control, and input-change events.
- Logic analyzer supports fixed-rate sampling of multiple contiguous GPIOs into
  SRAM, followed by USB/TCP upload.
- Logic analyzer can apply optional internal pull-up or pull-down bias before
  PIO sampling. `pull` is the default for all captured pins, and `pin_pulls`
  overrides individual GPIOs. The default is `none`, which is safest for driven
  buses.
- Logic analyzer trigger modes are level, rising edge, falling edge, and pattern
  trigger. Pattern matching uses `trigger_mask` and `trigger_value` over the
  captured pin group.
- Pre-trigger and burst modes use PIO/DMA continuous sampling plus firmware-side
  trigger scanning. This preserves packed sample data and emits trigger/burst
  markers, but it is still bounded by the fixed SRAM search window.
