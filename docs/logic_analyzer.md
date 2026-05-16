# Logic Analyzer Design Notes

## Reference Takeaways

The `gusmanb/logicanalyzer` project is useful as a product reference because it
treats a Pico-based analyzer as a full workflow:

- PIO captures deterministic digital samples.
- The host application preserves capture settings with the data.
- The host supports protocol decoding, repeated analysis, CSV export, and VCD-like
  waveform workflows.
- Firmware supports multiple trigger styles and higher-level capture modes.
- The terminal capture flow can be driven from a capture-settings file, which is
  useful for automated regression tests and PC tooling.

The parts that do not map directly to this project are also clear:

- Its desktop GUI and .NET plugin model are too heavy for a CLI-first tool meant
  for AI/model automation.
- Its hardware assumes dedicated analyzer pin mapping and optional level shifter
  boards; RP2350 Monitor must coexist with configurable UART/SPI/I2C/GPIO
  channels.
- Its high-end modes depend on tight hardware assumptions. This firmware keeps a
  conservative fixed SRAM capture first, then exposes host-side decoders.

Comparison against the local reference project:

| Area | Reference behavior | RP2350 Monitor status |
| --- | --- | --- |
| Capture engine | PIO plus DMA with multiple channel-width modes | PIO2 plus DMA, contiguous exposed GPIO range |
| Capability model | Documented fixed modes in the GUI/app | `logic_caps` returns machine-readable ranges, buffer size, triggers, pulls, decoders, and reserved features |
| Capture settings | Settings file can drive terminal capture | `logic_capture --settings` reads JSON and stores a `logic_settings` line in the output JSONL |
| Channel labels | GUI supports channel names | `--channel-name GPIO=NAME` and settings `channel_names` are preserved; CSV/VCD/summary use labels |
| Region analysis | Viewer can measure selected regions | `logic_decode` and `logic_export` support `--start-sample` / `--end-sample` windows |
| Protocol decode | Sigrok-oriented GUI decoder ecosystem | CLI-native JSON decoders for summary, bursts, edges, UART, SPI, and I2C; Sigrok bridge remains an extension point |
| Trigger modes | Simple/complex trigger plus burst in firmware | No trigger, level, rising, falling, pattern, pre-trigger windowing, and burst markers |

## Current RP2350 Monitor Behavior

Firmware responsibilities:

- Claim a contiguous exposed GPIO range.
- Arm PIO2 and DMA for fixed-rate sampling.
- Support no trigger, level trigger, rising edge trigger, falling edge trigger,
  and pattern trigger.
- Support pre-trigger windows and burst trigger markers through PIO/DMA
  continuous sampling plus firmware trigger scanning.
- Optionally apply `pull:"none"`, `pull:"up"`, or `pull:"down"` to captured
  inputs before PIO takes ownership.
- Store packed samples in a fixed 131,072-byte SRAM buffer.
- Upload raw `type:"logic"` chunks over USB CDC or Wi-Fi TCP.
- Report machine-readable capture limits through `logic_caps`.

Host responsibilities:

- `logic_capture`: configure, start, wait, upload, and persist raw JSONL.
- `logic_capture --settings`: load a repeatable JSON capture profile and embed
  `type:"logic_settings"` metadata in the saved JSONL.
- `logic_decode --decoder summary`: metadata plus per-pin edge/frequency/duty
  measurements.
- `logic_decode --decoder bursts`: trigger and burst marker timeline.
- `logic_decode --decoder edges`: edge timeline.
- `logic_decode --decoder uart`: UART bytes and frame/parity errors.
- `logic_decode --decoder spi`: SPI words for modes 0..3.
- `logic_decode --decoder i2c`: START/STOP/address/data/ACK events.
- `logic_export --format csv|vcd`: interchange files for external tools.
- `logic_decode` / `logic_export --start-sample N --end-sample M`: analyze or
  export a bounded sample window without recapturing.

## Examples

Capture once and decode UART:

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX logic_caps
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX logic_capture --pin-base 16 --pin-count 1 --sample-rate 2000000 --samples 8192 --pull down --channel-name 16=uart_rx --output uart.jsonl --release
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

Settings-file capture:

```json
{
  "pin_base": 16,
  "pin_count": 4,
  "sample_rate": 10000000,
  "samples": 4096,
  "pre_samples": 512,
  "post_samples": 3584,
  "search_samples": 32768,
  "burst_count": 4,
  "pull": "down",
  "trigger": {"mode": "pattern", "mask": "0x3", "value": "0x2"},
  "channel_names": {"16": "uart_rx", "17": "uart_tx", "18": "sck", "19": "mosi"}
}
```

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX logic_capture --settings logic_settings.json --output capture.jsonl --release
python3 tools/rpmon_cli.py logic_decode --input capture.jsonl --decoder summary --start-sample 100 --end-sample 2100
python3 tools/rpmon_cli.py logic_decode --input capture.jsonl --decoder bursts
python3 tools/rpmon_cli.py logic_export --input capture.jsonl --format csv --output region.csv --start-sample 100 --end-sample 2100
```

## Extension Points

- Optional Sigrok bridge on the host for long-tail protocol decoders.
- Larger buffers on RP2350 boards with external PSRAM.
