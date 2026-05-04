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
- CAN: reserved in the protocol model, not implemented in this firmware version.

The channel table has `kMaxChannels = 8` slots. A valid concurrent setup can mix
UART, SPI, I2C, and GPIO channels up to that table size, subject to native
peripheral and pin ownership limits.

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

The firmware still cannot overcome physical bandwidth limits. Hosts should keep
USB or TCP reads active and write JSONL to disk when capturing sustained traffic.
Any non-zero `dropped_events` or per-channel `dropped_events` means the capture
has a gap and the host should mark the analysis as incomplete.

## Current Protocol Behavior

- UART is passive RX monitoring plus TX injection.
- SPI and I2C are host-initiated transaction engines in this version. Passive
  bus sniffing is reserved for future PIO drivers.
- GPIO supports input sampling, output control, and input-change events.
