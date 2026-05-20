# RP2350 Monitor Control Protocol

This document is the contract for external host tools that control the Pico 2 W
firmware over USB CDC or Wi-Fi TCP.

## Discovery Contract

RP2350-Monitor is the self-discovery firmware baseline for Pico/RP2350 GUI and
AI tooling. Firmware variants must remain protocol-compatible with this
contract. Do not ship a feature-specific UF2 that removes discovery commands or
transports.

The minimum discovery surface is:

- `hello`: returns firmware `version`, `board`, and available `links`
- `status`: returns firmware, Wi-Fi, channel, buffer, logic, and probe state
- `logic_caps`: returns analyzer ranges, limits, trigger modes, pulls, decoder
  capabilities, and compatibility flags
- `probe_caps`: returns CMSIS-DAP/SWD debug probe capabilities and AI command
  names
- `pins`, `channels`, `events_read`: support UI ownership and telemetry views
- USB CDC, Wi-Fi TCP port `4242`, setup AP recovery, and HTTP `/api/status`

Host tools should verify
`hello -> status -> logic_caps/probe_caps -> pins/channels` before enabling
GPIO, UART, I2C, SPI, Wi-Fi, logic-analyzer, or debug-probe workflows.

## Transports

- USB CDC serial: 115200 baud is used by the reference CLI, but the firmware
  accepts newline-delimited text from the USB stdio endpoint.
- USB CMSIS-DAP v2 bulk: OpenOCD/GDB debug transport. This is independent from
  the CDC JSONL control stream.
- Wi-Fi TCP: connect to port `4242` on the Pico station IP or on the setup AP
  address `192.168.4.1`.
- HTTP setup UI: port `80`, with machine-readable status at `GET /api/status`.

USB and TCP use the same application protocol: one compact JSON object per line,
terminated by `\n`. A host should keep reading after command responses because
protocol traffic and status events are also delivered on the same stream.

## Message Types

Command:

```json
{"cmd":"hello"}
```

Command response:

```json
{"type":"resp","ok":true,"cmd":"hello","msg":"ready","version":"0.9.0","board":"pico2_w","links":["wifi","usb","cmsis-dap"]}
```

Status event:

```json
{"type":"status","seq":4,"ts_us":1234567,"component":"wifi","msg":"station status changed","station_status":"up","ip":"192.168.1.50"}
```

Protocol data event:

```json
{"type":"event","seq":12,"ts_us":1234567,"channel":1,"proto":"uart","dir":"rx","len":3,"offset":0,"hex":"010203"}
```

Logic analyzer bulk data line:

```json
{"type":"logic","capture_id":1,"offset_words":0,"words":128,"pin_base":16,"pin_count":4,"sample_rate":10000000,"samples":1024,"record_bits":32,"encoding":"u32-le-packed","hex":"00000000"}
```

`hex` is lowercase/uppercase insensitive on input. Output is contiguous
hexadecimal bytes without spaces. Maximum payload per command is 128 bytes.

## Wi-Fi Commands

`wifi_scan` starts an asynchronous 2.4 GHz Wi-Fi scan. Read `status` afterward
to get `wifi.scan.results`.

```json
{"cmd":"wifi_scan"}
```

`wifi_set` stores credentials into one of three profile slots. `slot` is `0`,
`1`, or `2`; omitted by old clients means slot `0`.

```json
{"cmd":"wifi_set","slot":0,"ssid":"ChinaNet-3CFC","password":"12345678","save":true}
```

`wifi_connect` connects the active profile or the requested slot. If connection
fails, the setup AP is restored and `wifi.profiles[n].last_error` records the
failure for the selected slot.

```json
{"cmd":"wifi_connect","slot":0}
```

`wifi_clear` removes a saved profile from a slot.

```json
{"cmd":"wifi_clear","slot":1}
```

`wifi_ap` switches back to setup AP mode.

```json
{"cmd":"wifi_ap"}
```

Relevant `status` Wi-Fi shape:

```json
{
  "wifi": {
    "ap_active": true,
    "ap_ssid": "RP2350-Monitor-007BB2",
    "ap_ip": "192.168.4.1",
    "active_profile": 0,
    "ssid_configured": true,
    "ssid": "ChinaNet-3CFC",
    "station_status": "down",
    "station_ip": "0.0.0.0",
    "pending_action": "none",
    "profiles": [
      {"slot":0,"valid":true,"active":true,"ssid":"ChinaNet-3CFC","last_error":""}
    ],
    "scan": {
      "active": false,
      "results": [
        {"ssid":"ChinaNet-3CFC","rssi":-42,"channel":6,"auth":7}
      ]
    }
  }
}
```

## General Commands

`status` returns firmware, HTTP, Wi-Fi, and configured channel state.

```json
{"cmd":"status"}
```

The response includes a `buffers` object with telemetry queue health:

```json
{
  "version": "0.9.0",
  "board": "pico2_w",
  "buffers": {
    "event_capacity": 128,
    "channel_event_capacity": 16,
    "event_line_max": 512,
    "event_depth": 7,
    "event_max_depth": 7,
    "oldest_seq": 1,
    "newest_seq": 7,
    "total_events": 7,
    "data_events": 2,
    "data_bytes": 28,
    "dropped_events": 0,
    "dropped_bytes": 0,
    "overflow_notices": 0,
    "channels": [
      {"id":4,"depth":3,"max_depth":3,"oldest_seq":12,"newest_seq":20,"dropped_events":0,"dropped_bytes":0}
    ]
  }
}
```

`buffer_status` returns only the `buffers` object.

```json
{"cmd":"buffer_status"}
```

`events_read` replays recent buffered status/event lines, then returns a command
response. `count` is capped at 64. `since_seq` is optional. `channel` is
optional; when present, replay comes from that channel's isolated data queue.

```json
{"cmd":"events_read","count":64,"since_seq":120}
{"cmd":"events_read","channel":4,"count":16}
```

`pins` returns exposed GPIO ownership.

```json
{"cmd":"pins"}
```

`channels` returns only configured protocol channels.

```json
{"cmd":"channels"}
```

The `status` response also includes a `logic` object for the high-speed PIO
capture engine:

```json
{
  "logic": {
    "configured": true,
    "running": false,
    "complete": true,
    "capture_id": 3,
    "pin_base": 16,
    "pin_count": 4,
    "sample_rate": 10000000,
    "samples": 1024,
    "words": 128,
    "record_bits": 32,
    "trigger_pin": -1,
    "trigger_mode": "level",
    "trigger_level": true,
    "buffer_words_max": 32768,
    "buffer_bytes": 131072,
    "chunk_bytes": 512
  }
}
```

The `status` response also includes a `probe` object for CMSIS-DAP/SWD state:

```json
{
  "probe": {
    "configured": true,
    "active": false,
    "claimed": false,
    "pins": {"swclk": 2, "swdio": 3, "reset": 1},
    "swclk_khz": 1000,
    "reset_level": 1,
    "dap_packets": 0,
    "dap_errors": 0,
    "port_setups": 0,
    "port_setup_failures": 0,
    "last_error": ""
  }
}
```

## Channel Lifecycle

Every hardware protocol uses this sequence:

1. `channel_config`
2. `channel_start`
3. `channel_write`, `spi_xfer`, `i2c_xfer`, `gpio_write`, or `gpio_read`
4. read `event` messages
5. `channel_stop`

Channel IDs are positive integers chosen by the host. Reconfiguring an existing
ID stops the previous driver and releases its pins.

`channel_stop` disables the hardware while keeping the configuration available
for a later `channel_start`. `channel_release` stops the hardware, removes the
channel, and releases its pins for reuse.

```json
{"cmd":"channel_release","id":4}
```

Concurrent channel limits are enforced during `channel_config`. The firmware
rejects duplicate native instances, invalid alternate-function pin mappings,
already-owned GPIOs, and channel-table overflow with `ok:false` responses. See
[resource_limits.md](resource_limits.md) for the exact limits and messages.

Native UART/SPI/I2C pin choices are validated by peripheral instance and signal
role. A host may combine any valid pins from the same instance's signal lists;
the pins do not need to come from one adjacent header group. For example,
`SPI0 sck=2, mosi=7, miso=16` is valid, while `SPI0 sck=2, mosi=11` is rejected
because GPIO11 is a SPI1 MOSI/TX pin. Invalid mappings return signal-specific
messages such as `invalid SPI0 mosi GPIO11; valid mosi GPIOs: 3,7,19`.

## UART

Configure UART:

```json
{"cmd":"channel_config","id":1,"type":"uart","instance":0,"tx":0,"rx":1,"baud":115200,"loopback":false}
{"cmd":"channel_start","id":1}
```

Write bytes:

```json
{"cmd":"channel_write","id":1,"hex":"48656c6c6f"}
```

UART emits `event` frames with `proto:"uart"` and `dir:"tx"` or `dir:"rx"`.
Internal loopback testing is available by setting `"loopback":true`.

## SPI

Configure SPI:

```json
{"cmd":"channel_config","id":2,"type":"spi","instance":0,"sck":2,"mosi":3,"miso":0,"cs":1,"baud":1000000}
{"cmd":"channel_start","id":2}
```

Transfer bytes. If `read_len` is omitted or `0`, the firmware reads the same
number of bytes as the write payload.

```json
{"cmd":"spi_xfer","id":2,"hex":"9f000000","read_len":4}
```

SPI emits `event` frames with `proto:"spi"` and separate `tx`/`rx` directions.
`cs` is a firmware-controlled manual GPIO output in this version. It may be any
free exposed GPIO or omitted, and it must not share the SCK, MOSI, or MISO GPIO.
This first firmware version is a transaction engine; passive SPI sniffing is a
future PIO driver.

## I2C

Configure I2C:

```json
{"cmd":"channel_config","id":3,"type":"i2c","instance":0,"sda":4,"scl":5,"baud":100000}
{"cmd":"channel_start","id":3}
```

Write, read, or write-then-read:

```json
{"cmd":"i2c_xfer","id":3,"addr":80,"write":"00","read_len":16}
```

`addr` is a 7-bit I2C address. I2C emits `event` frames with `proto:"i2c"` and
`dir:"tx"` / `dir:"rx"`. Passive I2C sniffing is reserved for a future PIO
driver.

## GPIO

Configure a general-purpose I/O pin as a protocol channel:

```json
{"cmd":"channel_config","id":4,"type":"gpio","gpio":16,"direction":"output","initial":false}
{"cmd":"channel_start","id":4}
```

`direction` is `input` or `output`. Inputs may set `pull` to `none`, `up`, or
`down`. Outputs ignore pull mode and use `initial` as the first driven level.

Set and read level:

```json
{"cmd":"gpio_write","id":4,"level":true}
{"cmd":"gpio_read","id":4}
```

`gpio_read` responses include the sampled level:

```json
{"type":"resp","ok":true,"cmd":"gpio_read","msg":"gpio level read","level":true}
```

GPIO emits one-byte data events with `proto:"gpio"` and `hex:"00"` or
`hex:"01"`. Directions are:

- `write`: host changed an output level.
- `read`: host sampled a channel level.
- `change`: firmware polling detected an input transition.

Example event:

```json
{"type":"event","seq":31,"ts_us":1234567,"channel":4,"proto":"gpio","dir":"write","len":1,"offset":0,"hex":"01"}
```

## High-Speed Logic Analyzer

The logic analyzer is a separate PIO/DMA capture engine for oscilloscope-style
multi-pin digital sampling. It is not a normal `channel_config` driver because
bulk captures use a dedicated SRAM buffer and are uploaded in larger chunks.

Host software should query capabilities before presenting a capture UI or
selecting sample sizes:

```json
{"cmd":"logic_caps"}
```

Response:

```json
{
  "type": "resp",
  "ok": true,
  "cmd": "logic_caps",
  "logic_caps": {
    "engine": "pio2_dma",
    "pin_ranges": [{"first": 0, "last": 22}, {"first": 26, "last": 28}],
    "contiguous_pins": true,
    "pin_count_max": 23,
    "sample_rate_max": 150000000,
    "buffer_words": 32768,
    "buffer_bytes": 131072,
    "ring_buffer_words": 16384,
    "ring_buffer_bytes": 65536,
    "upload_chunk_bytes": 512,
    "encoding": "u32-le-packed",
    "capture_modes": ["single", "pretrigger"],
    "triggers": ["none", "level", "rising", "falling", "pattern"],
    "pull_modes": ["none", "up", "down"],
    "per_pin_pull": true,
    "pin_pull_field": "pin_pulls",
    "pretrigger_single_fix": true,
    "ring_pretrigger": true,
    "open_ended_trigger_wait": true,
    "ring_dma_mode": "pingpong",
    "ring_dma_halves": 2,
    "pio_simple_trigger": true,
    "pio_pattern_trigger": true,
    "firmware_trigger_scan": false,
    "pattern_mask_bits_max": 23,
    "pattern_mask_full_width": true,
    "burst_marks_max": 1,
    "host_decoders": ["summary", "bursts", "edges", "uart", "spi", "i2c"],
    "host_exports": ["csv", "vcd"],
    "reserved_features": ["hardware_burst_marks", "external_psram", "sigrok_bridge"]
  }
}
```

Configure a capture:

```json
{"cmd":"logic_config","pin_base":16,"pin_count":4,"sample_rate":10000000,"samples":1024,"pull":"down"}
```

`pull` is optional and may be `none`, `up`, or `down`. The default is `none` and
applies to every captured GPIO. `pin_pulls` may override individual GPIOs using
absolute GPIO numbers as object keys:

```json
{"cmd":"logic_config","pin_base":16,"pin_count":4,"sample_rate":10000000,"samples":1024,"pull":"none","pin_pulls":{"16":"up","17":"none","18":"down","19":"none"}}
```

Use internal pulls only to stabilize idle/floating analyzer inputs; external
drivers still define the real bus level. For I2C analysis, external bus pull-ups
are preferred over the Pico's weak internal pull-ups.

Optional trigger. `trigger_mode` may be `level`, `rising`, or `falling`.
`level` uses `trigger_level`; edge modes wait for the opposite level first and
then the requested edge.

```json
{"cmd":"logic_config","pin_base":16,"pin_count":4,"sample_rate":10000000,"samples":1024,"trigger_pin":16,"trigger_level":true}
{"cmd":"logic_config","pin_base":16,"pin_count":4,"sample_rate":10000000,"samples":1024,"trigger_pin":16,"trigger_mode":"rising"}
```

Advanced capture fields:

- `pre_samples`: samples to keep before the first trigger. Non-zero values use
  PIO/DMA circular sampling. The PIO program itself detects the trigger and
  keeps sampling until the requested post-trigger window is complete.
- `post_samples`: samples expected after a trigger. If omitted, the host should
  treat `samples - pre_samples` as the post window.
- `search_samples`: legacy compatibility field. With
  `logic_caps.ring_pretrigger:true`, trigger waits are open-ended and this field
  reports the SRAM-backed ring capacity instead of limiting trigger wait time.
- `scan_budget_samples` / `scan_dropped_samples`: retained compatibility fields.
  They are always `0` when `logic_caps.firmware_trigger_scan:false`.
- `ring_dma_mode`: `pingpong` means triggered pre-capture uses two chained DMA
  halves and a DMA IRQ to reload the completed half. This avoids the old
  single-DMA address-ring behavior while keeping trigger detection in PIO.
- `ring_dma_halves_completed`: diagnostic counter for the active pre-trigger
  ring. It increases while the capture waits for an external trigger.
- `burst_count`: only `1` is accepted in the PIO-only trigger implementation.
  `burst_count > 1` returns `ok:false` until hardware burst timestamp support is
  implemented.
- `trigger_mode:"pattern"`: match a bit pattern in the captured contiguous pin
  group. Bit 0 maps to `pin_base`, bit 1 maps to `pin_base + 1`, and so on.
- `trigger_mask`: relative pin mask for pattern matching.
- `trigger_value`: relative pin value after applying `trigger_mask`.
  With `pattern_mask_full_width:true`, `trigger_mask` must cover the whole
  captured range. To match GP16=0 and GP17=1, configure `pin_base:16`,
  `pin_count:2`, `trigger_mask:3`, `trigger_value:2`.

Pattern/pre-trigger example:

```json
{
  "cmd": "logic_config",
  "pin_base": 16,
  "pin_count": 2,
  "sample_rate": 10000000,
  "samples": 4096,
  "pull": "none",
  "pin_pulls": {"16": "up", "17": "none"},
  "pre_samples": 512,
  "post_samples": 3584,
  "trigger_mode": "pattern",
  "trigger_mask": 3,
  "trigger_value": 2,
  "burst_count": 1
}
```

Start and poll status:

```json
{"cmd":"logic_start"}
{"cmd":"logic_status"}
```

Read captured words. `count_words:0` means "read to the end". For reads from
offset zero, the firmware sends one `type:"logic_meta"` line first, then one or
more `type:"logic"` data lines, then a normal command response.

```json
{"cmd":"logic_read","offset_words":0,"count_words":0}
```

Metadata line:

```json
{
  "type": "logic_meta",
  "capture_id": 7,
  "pin_base": 16,
  "pin_count": 2,
  "sample_rate": 10000000,
  "samples": 4096,
  "record_bits": 32,
  "sample_offset": 1024,
  "pull": "none",
  "pin_pulls": {"16": "up", "17": "none"},
  "pre_samples": 512,
  "post_samples": 3584,
  "trigger_found": true,
  "trigger_sample": 512,
  "trigger_pin": -1,
  "trigger_mode": "pattern",
  "trigger_mask": 3,
  "trigger_value": 2,
  "burst_count": 1,
  "burst_found": 1,
  "burst_samples": [512]
}
```

Release pins and runtime configuration:

```json
{"cmd":"logic_release"}
```

Data encoding:

- Pins must be a contiguous exposed GPIO range starting at `pin_base`.
- Each sample records `pin_count` bits.
- `record_bits = 32 - (32 % pin_count)`.
- The hex payload is little-endian `uint32_t` words copied from the capture
  buffer.
- To decode a pin level, use relative pin index `pin = gpio - pin_base`,
  `sample` is relative to the uploaded window. Add `logic_meta.sample_offset`
  to recover the sample number in the original search stream, then compute
  `bit_index = pin + sample * pin_count`, `word_index = bit_index / record_bits`,
  and bit mask `1u << ((bit_index % record_bits) + 32 - record_bits)`.

This version captures into RAM first and then uploads over USB or TCP. It
does not provide infinite streaming at the configured sample rate; sustained
capture length is bounded by `buffer_words_max`.

`logic_status` and the nested `status.logic` object also include:

- `ring_mode`: true for trigger/pre-trigger captures using the circular
  DMA buffer.
- `pio_trigger_mode`: true when a `level`, `rising`, `falling`, or full-width
  `pattern` trigger is being detected in the PIO sampling program itself. In
  this mode the control CPU does not scan samples to find the trigger.
- `pio_trigger_irq`: true after the PIO trigger program has raised its completion
  IRQ and firmware is waiting for DMA-visible samples to catch up before
  freezing the capture. It should normally be transient; if it stays true, host
  software can call `logic_stop` and report a capture finalization fault.
- `sample_word_mode`: true when the PIO trigger program stores one raw sample
  per SRAM word internally, currently used for pattern trigger. `logic_read`
  still uploads the stable packed `u32-le-packed` encoding.
- `scan_budget_samples` / `scan_dropped_samples`: compatibility fields, always
  `0` when `firmware_trigger_scan:false`.

Host-side logic analysis:

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX logic_capture --pin-base 16 --pin-count 4 --sample-rate 10000000 --samples 4096 --output capture.jsonl --release
python3 tools/rpmon_cli.py logic_decode --input capture.jsonl --decoder summary
python3 tools/rpmon_cli.py logic_decode --input capture.jsonl --decoder bursts
python3 tools/rpmon_cli.py logic_decode --input capture.jsonl --decoder edges --pin 16
python3 tools/rpmon_cli.py logic_decode --input capture.jsonl --decoder uart --rx-pin 16 --baud 115200
python3 tools/rpmon_cli.py logic_decode --input capture.jsonl --decoder spi --cs-pin 16 --sck-pin 17 --mosi-pin 18 --miso-pin 19 --mode 0
python3 tools/rpmon_cli.py logic_decode --input capture.jsonl --decoder i2c --sda-pin 16 --scl-pin 17
python3 tools/rpmon_cli.py logic_export --input capture.jsonl --format csv --output capture.csv
python3 tools/rpmon_cli.py logic_export --input capture.jsonl --format vcd --output capture.vcd
```

`logic_capture` can also read repeatable settings from a JSON file. CLI
arguments override fields from the file. The CLI writes a `type:"logic_settings"`
line into the capture JSONL so later decoders and PC tools can retain labels and
capture intent:

```json
{
  "pin_base": 16,
  "pin_count": 4,
  "sample_rate": 10000000,
  "samples": 4096,
  "pre_samples": 512,
  "post_samples": 3584,
  "burst_count": 1,
  "pull": "down",
  "pin_pulls": {"18": "up", "19": "none"},
  "trigger": {"mode": "pattern", "mask": "0x3", "value": "0x2"},
  "channel_names": {"16": "uart_rx", "17": "uart_tx"}
}
```

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX logic_capture --settings logic_settings.json --output capture.jsonl --release
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX logic_capture --pin-base 16 --pin-count 4 --sample-rate 10000000 --samples 4096 --pull none --pin-pull 16=up --pin-pull 18=down --output capture.jsonl --release
python3 tools/rpmon_cli.py logic_decode --input capture.jsonl --decoder summary --start-sample 100 --end-sample 1100
python3 tools/rpmon_cli.py logic_export --input capture.jsonl --format csv --output region.csv --start-sample 100 --end-sample 1100
```

`logic_decode` emits newline-delimited JSON. Initial built-in decoders cover:

- `summary`: capture metadata and per-pin high/low/edge/frequency statistics.
- `bursts`: trigger/burst marker list from `logic_meta.burst_samples`.
- `edges`: rising/falling edge list for selected pins.
- `uart`: async UART, default 8N1, LSB-first data.
- `spi`: SPI modes 0..3, optional CS, MOSI/MISO, MSB/LSB word assembly.
- `i2c`: START/STOP, 7-bit address, R/W, data bytes, ACK/NACK.

## CMSIS-DAP / SWD Debug Probe

The debug probe exposes two host-facing paths:

- USB CMSIS-DAP v2 bulk interface for OpenOCD/GDB.
- JSONL `probe_*` commands over USB CDC or Wi-Fi TCP for AI/tooling control.

Query probe capability:

```json
{"cmd":"probe_caps"}
```

Response:

```json
{
  "type": "resp",
  "ok": true,
  "cmd": "probe_caps",
  "probe_caps": {
    "engine": "raspberrypi-debugprobe-pio-swd",
    "source": "github.com/raspberrypi/debugprobe",
    "cmsis_dap": true,
    "cmsis_dap_version": "v2",
    "usb_bulk": true,
    "json_packet_bridge": true,
    "swd": true,
    "jtag": false,
    "swo": false,
    "runtime_pins": true,
    "default_pins": {"swclk": 2, "swdio": 3, "reset": 1},
    "swclk_khz_default": 1000,
    "swclk_khz_max": 24000,
    "openocd_transport": "cmsis-dap",
    "ai_commands": ["probe_caps", "probe_status", "probe_config", "probe_release", "probe_reset", "probe_dap"]
  }
}
```

Configure SWD pins. `reset` may be `-1` if nRESET is not connected:

```json
{"cmd":"probe_config","swclk":2,"swdio":3,"reset":1,"swclk_khz":1000}
```

Read status:

```json
{"cmd":"probe_status"}
```

Pulse, assert, or release target reset:

```json
{"cmd":"probe_reset","action":"pulse","pulse_ms":50}
{"cmd":"probe_reset","action":"assert"}
{"cmd":"probe_reset","action":"release"}
```

Release SWD resources and pins:

```json
{"cmd":"probe_release"}
```

Send a raw CMSIS-DAP packet over the monitor JSONL channel:

```json
{"cmd":"probe_dap","packet_hex":"00f0"}
```

`probe_dap` response:

```json
{"type":"resp","ok":true,"cmd":"probe_dap","msg":"DAP packet processed","request_len":2,"response_len":3,"resp_hex":"00..."}
```

The packet bridge is intended for AI scripts and diagnostics. For normal
programming/debugging, use OpenOCD against the USB CMSIS-DAP interface:

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX probe_config --swclk 2 --swdio 3 --reset 1 --swclk-khz 1000
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg
```

Target selection is an OpenOCD responsibility. The monitor firmware supplies
the SWD transport and reset control; it does not embed target flash algorithms.

## CAN Reservation

The command parser accepts `type:"can"` as a reserved protocol name, but the
current firmware returns an error. Future implementations should keep the same
channel lifecycle and either expose an MCP2515-over-SPI driver or a PIO CAN
engine behind the channel abstraction.

## Reference CLI

The bundled host tool is intentionally thin and prints every firmware JSON line
unchanged:

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX status
python3 tools/rpmon_cli.py --tcp 192.168.4.1 wifi_scan
python3 tools/rpmon_cli.py --tcp 192.168.4.1 wifi_set --slot 0 --ssid ChinaNet-3CFC --password 12345678
python3 tools/rpmon_cli.py --tcp 192.168.4.1 wifi_clear --slot 1
python3 tools/rpmon_cli.py --tcp 192.168.4.1 buffer_status
python3 tools/rpmon_cli.py --tcp 192.168.4.1 events_read --count 64
python3 tools/rpmon_cli.py --tcp 192.168.4.1 events_read --channel 4 --count 16
python3 tools/rpmon_cli.py --tcp 192.168.4.1 uart_loopback_test --id 7 --stop
python3 tools/rpmon_cli.py --tcp 192.168.4.1 config_spi --id 2 --instance 0 --sck 2 --mosi 3 --miso 0 --cs 1
python3 tools/rpmon_cli.py --tcp 192.168.4.1 spi_xfer --id 2 --hex 9f000000
python3 tools/rpmon_cli.py --tcp 192.168.4.1 config_i2c --id 3 --instance 0 --sda 4 --scl 5
python3 tools/rpmon_cli.py --tcp 192.168.4.1 i2c_xfer --id 3 --addr 0x50 --write 00 --read-len 16
python3 tools/rpmon_cli.py --tcp 192.168.4.1 config_gpio --id 4 --gpio 16 --direction output --initial 0
python3 tools/rpmon_cli.py --tcp 192.168.4.1 gpio_write --id 4 --level 1
python3 tools/rpmon_cli.py --tcp 192.168.4.1 gpio_read --id 4
python3 tools/rpmon_cli.py --tcp 192.168.4.1 release --id 4
python3 tools/rpmon_cli.py --tcp 192.168.4.1 logic_config --pin-base 16 --pin-count 4 --sample-rate 10000000 --samples 1024
python3 tools/rpmon_cli.py --tcp 192.168.4.1 logic_start
python3 tools/rpmon_cli.py --tcp 192.168.4.1 logic_status
python3 tools/rpmon_cli.py --tcp 192.168.4.1 logic_read --offset-words 0 --count-words 0
python3 tools/rpmon_cli.py --tcp 192.168.4.1 logic_release
python3 tools/rpmon_cli.py --tcp 192.168.4.1 logic_capture --pin-base 16 --pin-count 4 --sample-rate 10000000 --samples 4096 --output capture.jsonl --release
python3 tools/rpmon_cli.py logic_decode --input capture.jsonl --decoder summary
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX probe_caps
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX probe_config --swclk 2 --swdio 3 --reset 1 --swclk-khz 1000
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX probe_reset --action pulse --pulse-ms 50
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX probe_dap --hex 00f0
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX probe_release
```

External tools should follow the same rule: do not scrape human text; consume
the JSON line stream directly.

For buffering, overflow, replay, and host-side JSONL storage rules, see
[reliability.md](reliability.md).
