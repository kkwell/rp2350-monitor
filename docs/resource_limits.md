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

Native UART/SPI/I2C channels accept the following Pico 2 W exposed GPIO mappings:

| Peripheral | Signal | Supported GPIOs |
| --- | --- | --- |
| UART0 | TX | 0, 2, 12, 14, 16, 18, 28 |
| UART0 | RX | 1, 3, 13, 15, 17, 19 |
| UART1 | TX | 4, 6, 8, 10, 20, 22, 26 |
| UART1 | RX | 5, 7, 9, 11, 21, 27 |
| SPI0 | MISO/RX | 0, 4, 16, 20 |
| SPI0 | CSn | 1, 5, 17, 21 |
| SPI0 | SCK | 2, 6, 18, 22 |
| SPI0 | MOSI/TX | 3, 7, 19 |
| SPI1 | MISO/RX | 8, 12, 28 |
| SPI1 | CSn | 9, 13 |
| SPI1 | SCK | 10, 14, 26 |
| SPI1 | MOSI/TX | 11, 15, 27 |
| I2C0 | SDA | 0, 4, 8, 12, 16, 20, 28 |
| I2C0 | SCL | 1, 5, 9, 13, 17, 21 |
| I2C1 | SDA | 2, 6, 10, 14, 18, 22, 26 |
| I2C1 | SCL | 3, 7, 11, 15, 19, 27 |

Pins are matched by peripheral instance and signal role, not by a required
adjacent board-header group. For example, `SPI0 sck=2, mosi=7, miso=16` is a
valid native SPI0 mapping because each selected pin is valid for that SPI0
signal. `SPI0 sck=2, mosi=11` is rejected because GPIO11 is `SPI1 MOSI/TX`, not
`SPI0 MOSI/TX`.

UART and I2C follow the same rule: TX/RX or SDA/SCL can be chosen from any valid
pin in the same instance's signal lists. They cannot be mixed across instances,
such as `UART0 tx` with a `UART1 rx`, or `I2C0 sda` with `I2C1 scl`.

SPI `cs` is a firmware-controlled manual GPIO output in this version. It may be
omitted or assigned to any free exposed GPIO, and it does not need to be one of
the native `CSn` alternate-function pins listed above. It must not reuse the
same GPIO as SCK, MOSI, or MISO.

The logic analyzer is outside the eight-channel table because it is a bulk
capture instrument, not a byte-stream protocol channel. It still uses the same
pin ownership rules, so a GPIO cannot be used by UART/SPI/I2C/GPIO and logic
capture at the same time.

## Rejection Cases

The control API returns a failed response when a request exceeds hardware or
firmware limits:

- Reusing `uart0`, `uart1`, `spi0`, `spi1`, `i2c0`, or `i2c1` from another
  channel returns a message such as `spi0 is already assigned to another channel`.
- Selecting a pin that cannot serve the requested alternate function returns a
  signal-specific error such as `invalid SPI0 mosi GPIO11; valid mosi GPIOs:
  3,7,19`.
- Selecting an invalid SPI manual CS pin returns a CS-specific error, for
  example `invalid SPI0 cs GPIO2; cs must not share sck/mosi/miso pins`.
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
- `burst_count` must be `1` in the PIO-only trigger implementation. Values
  greater than one are rejected until hardware burst timestamp support is added.
- Triggered `pre_samples + post_samples` windows must fit the circular ring
  capacity. Oversized requests fail during `logic_config`.
- `search_samples` is a legacy compatibility field when
  `logic_caps.ring_pretrigger` is true; it does not limit how long firmware waits
  for a trigger.
- If the PIO2 state machine, PIO instruction memory, or required DMA channels
  cannot be claimed, `logic_start` returns `ok:false` with the specific resource
  name. Pre-trigger ring capture uses two DMA channels.

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
- Triggered pre-trigger captures use a 16,384-word circular DMA ring inside
  that SRAM area. Firmware `0.8.9` implements the ring as two chained DMA halves
  (`logic_caps.ring_dma_mode:"pingpong"`) instead of a single long address-ring
  DMA transfer. `logic_caps.ring_buffer_words` and
  `logic_caps.ring_buffer_bytes` report this active ring capacity.
- Single-pin `level`, `rising`, and `falling` pre-trigger captures use
  `logic_caps.pio_simple_trigger` and detect the trigger inside the PIO sampling
  program. Pattern trigger uses `logic_caps.pio_pattern_trigger` and also runs
  in PIO, but `logic_caps.pattern_mask_full_width` means the mask must cover the
  whole configured `pin_count`.
- `logic_status.pio_trigger_irq` reports the short interval after PIO has
  marked a trigger completion and before DMA-visible samples are ready to freeze
  into the upload window.
- `logic_caps.firmware_trigger_scan` is `false`. `scan_budget_samples` and
  `scan_dropped_samples` remain in status responses for compatibility and are
  reported as `0`; trigger detection is not performed by CPU-side ring scanning.
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
- Pre-trigger modes use PIO/DMA circular sampling plus PIO-side trigger
  detection. Firmware waits open-ended for level, edge, or full-width pattern
  triggers, then freezes the requested pre/post window and uploads it through
  the existing `logic_read` chunks.
