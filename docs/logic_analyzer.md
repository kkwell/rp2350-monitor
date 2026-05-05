# Logic Analyzer Design Notes

## Reference Takeaways

The `gusmanb/logicanalyzer` project is useful as a product reference because it
treats a Pico-based analyzer as a full workflow:

- PIO captures deterministic digital samples.
- The host application preserves capture settings with the data.
- The host supports protocol decoding, repeated analysis, CSV export, and VCD-like
  waveform workflows.
- Firmware supports multiple trigger styles and higher-level capture modes.

The parts that do not map directly to this project are also clear:

- Its desktop GUI and .NET plugin model are too heavy for a CLI-first tool meant
  for AI/model automation.
- Its hardware assumes dedicated analyzer pin mapping and optional level shifter
  boards; RP2350 Monitor must coexist with configurable UART/SPI/I2C/GPIO
  channels.
- Its high-end modes depend on tight hardware assumptions. This firmware keeps a
  conservative fixed SRAM capture first, then exposes host-side decoders.

## Current RP2350 Monitor Behavior

Firmware responsibilities:

- Claim a contiguous exposed GPIO range.
- Arm PIO2 and DMA for fixed-rate sampling.
- Support no trigger, level trigger, rising edge trigger, and falling edge
  trigger.
- Store packed samples in a fixed 131,072-byte SRAM buffer.
- Upload raw `type:"logic"` chunks over USB CDC or Wi-Fi TCP.

Host responsibilities:

- `logic_capture`: configure, start, wait, upload, and persist raw JSONL.
- `logic_decode --decoder summary`: metadata plus per-pin edge/frequency/duty
  measurements.
- `logic_decode --decoder edges`: edge timeline.
- `logic_decode --decoder uart`: UART bytes and frame/parity errors.
- `logic_decode --decoder spi`: SPI words for modes 0..3.
- `logic_decode --decoder i2c`: START/STOP/address/data/ACK events.
- `logic_export --format csv|vcd`: interchange files for external tools.

## Examples

Capture once and decode UART:

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX logic_capture --pin-base 16 --pin-count 1 --sample-rate 2000000 --samples 8192 --output uart.jsonl --release
python3 tools/rpmon_cli.py logic_decode --input uart.jsonl --decoder uart --rx-pin 16 --baud 115200
```

Decode SPI from a 4-pin capture:

```sh
python3 tools/rpmon_cli.py logic_decode --input spi.jsonl --decoder spi --cs-pin 16 --sck-pin 17 --mosi-pin 18 --miso-pin 19 --mode 0
```

Export for waveform tools:

```sh
python3 tools/rpmon_cli.py logic_export --input capture.jsonl --format vcd --output capture.vcd
```

## Extension Points

- Pattern trigger in firmware using a second PIO state machine.
- Pre-trigger capture using circular DMA and trigger-tail reconstruction.
- Burst capture for repeated trigger windows.
- Optional Sigrok bridge on the host for long-tail protocol decoders.
- Larger buffers on RP2350 boards with external PSRAM.
