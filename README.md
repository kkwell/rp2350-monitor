# RP2350 Monitor

RP2350 Monitor is a Raspberry Pi Pico 2 W firmware and host CLI for configurable hardware protocol monitoring/debugging.

## Scope

Current version:

- Wi-Fi control/data channel on TCP port `4242`
- USB CDC backup control/data channel
- USB CMSIS-DAP v2 SWD debug probe path based on Raspberry Pi official
  `debugprobe`, with OpenOCD/GDB support through the host
- Pico-hosted setup AP: `RP2350-Monitor-xxxxxx`, password `rpmon2350`, IP `192.168.4.1`
- Native UART, SPI, I2C, and GPIO channel layers
- PIO2/DMA high-speed logic analyzer for contiguous GPIO capture
- Machine-readable logic analyzer capability discovery for host software
- Logic analyzer PIO level/edge trigger, full-width pattern trigger, and
  ping-pong DMA pre-trigger windowing without CPU-side trigger scanning
- Logic analyzer global and per-pin internal pull bias controls
- Host-side logic analysis for UART, SPI, I2C, bursts, edges, timing summary, sample windows, CSV, and VCD
- CAN reserved behind a driver interface
- Host CLI that emits newline-delimited JSON for AI/tooling analysis
- Browser-native host UI under `ui/` for logic analyzer, UART, I2C, SPI,
  Wi-Fi, GPIO status, and AI-visible operation replay
- Bounded global and per-channel event buffering with overflow counters and replay
- GitHub Actions CI/release workflows for reproducible UF2 builds

Bluetooth is intentionally not included in this version.

## Self-Discovery Firmware Baseline

All RP2350/Pico feature work must be built on this RP2350-Monitor firmware,
not on a one-off single-purpose UF2. Host software discovers the board and
enables UI/tool features through:

- `hello` with firmware version, board id, and available links
- `status` with Wi-Fi, channel, logic analyzer, debug probe, and buffer state
- `logic_caps` for analyzer ranges, limits, triggers, pulls, and decoders
- `probe_caps` for CMSIS-DAP/SWD probe capability discovery
- `pins`, `channels`, `events_read`, USB CDC, Wi-Fi TCP `4242`, setup AP, and
  HTTP `GET /api/status`

Any firmware change must preserve those discovery paths before adding or fixing
GPIO, UART, I2C, SPI, Wi-Fi, logic-analyzer, or debug-probe behavior. The
minimum regression order is
`hello -> status -> logic_caps/probe_caps -> pins/channels -> target feature`.
If one of those discovery checks fails, the GUI and AI tools may not identify
the hardware and the build is not releasable.

## Build

The project defaults to the DBT-installed Pico SDK and Arm GNU Toolchain paths
if `PICO_SDK_PATH` / `PICO_TOOLCHAIN_PATH` are not set. The Raspberry Pi
debugprobe source is tracked as a git submodule:

```sh
git submodule update --init --recursive
```

```sh
cmake -S . -B build -DPICO_BOARD=pico2_w
cmake --build build
```

The UF2 output is generated under `build/rp2350_monitor.uf2`.

## First Connection Flow

1. Flash `rp2350_monitor.uf2` to the Pico 2 W.
2. Pico starts an AP named `RP2350-Monitor-xxxxxx`.
3. Connect the computer to that AP and use TCP:

```sh
python3 tools/rpmon_cli.py --tcp 192.168.4.1 hello
python3 tools/rpmon_cli.py --tcp 192.168.4.1 status
```

USB can be used instead of Wi-Fi:

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX hello
```

Configure station Wi-Fi and connect:

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX wifi_scan
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX wifi_set --slot 0 --ssid "your-ssid" --password "your-pass"
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX wifi_connect --slot 0
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX wifi_clear --slot 1
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX status
```

Pico 2 W Wi-Fi must use a 2.4 GHz network. If station connection fails, the firmware restores the setup AP and `status` reports `ap_ssid` and `ap_ip` for recovery.
The firmware stores up to three Wi-Fi profiles. Failed saved-profile connection attempts are reported in `wifi.profiles[n].last_error`.

## Host UI

The monitor UI is part of this repository and is released together with the
firmware. It is the canonical UI/bridge source for desktop packaging and AI
debugging flows.

```sh
cd ui
npm run validate
bin/embed-labs-logic-analyzer --serial /dev/cu.usbmodemXXXX --panel logic --lang zh
```

The launcher serves the UI and bridge from this repository, then prints a local
URL such as:

```text
http://127.0.0.1:5178/?api=http://127.0.0.1:5178/api/operation&panel=logic&lang=zh
```

Wi-Fi transport is also supported when the Pico is reachable on the same LAN:

```sh
cd ui
bin/embed-labs-logic-analyzer --tcp 192.168.4.1:4242 --panel logic --lang zh
```

## Channel Examples

UART0 on GPIO0/GPIO1:

```sh
python3 tools/rpmon_cli.py --tcp 192.168.4.1 config_uart --id 1 --instance 0 --tx 0 --rx 1 --baud 115200
python3 tools/rpmon_cli.py --tcp 192.168.4.1 start --id 1
python3 tools/rpmon_cli.py --tcp 192.168.4.1 write --id 1 --hex 48656c6c6f
python3 tools/rpmon_cli.py --tcp 192.168.4.1 monitor
```

Record all incoming JSON lines to disk while monitoring:

```sh
python3 tools/rpmon_cli.py --tcp 192.168.4.1 --log capture.jsonl monitor
```

Inspect and replay buffered firmware events:

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX buffer_status
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX events_read --count 64
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX events_read --channel 4 --count 16
```

UART0 internal loopback test through the control interface:

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX uart_loopback_test --id 7 --stop
python3 tools/rpmon_cli.py --tcp 192.168.4.1 uart_loopback_test --id 7 --stop
```

SPI0 on GPIO2/3/0 with GPIO1 as manual CS:

```sh
python3 tools/rpmon_cli.py --tcp 192.168.4.1 config_spi --id 2 --instance 0 --sck 2 --mosi 3 --miso 0 --cs 1 --baud 1000000
python3 tools/rpmon_cli.py --tcp 192.168.4.1 start --id 2
python3 tools/rpmon_cli.py --tcp 192.168.4.1 spi_xfer --id 2 --hex 9f000000
```

I2C0 on GPIO4/GPIO5:

```sh
python3 tools/rpmon_cli.py --tcp 192.168.4.1 config_i2c --id 3 --instance 0 --sda 4 --scl 5 --baud 100000
python3 tools/rpmon_cli.py --tcp 192.168.4.1 start --id 3
python3 tools/rpmon_cli.py --tcp 192.168.4.1 i2c_xfer --id 3 --addr 0x50 --write 00 --read-len 16
```

GPIO16 as a controllable output:

```sh
python3 tools/rpmon_cli.py --tcp 192.168.4.1 config_gpio --id 4 --gpio 16 --direction output --initial 0
python3 tools/rpmon_cli.py --tcp 192.168.4.1 start --id 4
python3 tools/rpmon_cli.py --tcp 192.168.4.1 gpio_write --id 4 --level 1
python3 tools/rpmon_cli.py --tcp 192.168.4.1 gpio_read --id 4
python3 tools/rpmon_cli.py --tcp 192.168.4.1 release --id 4
```

GPIO17 as an input with an internal pull-up:

```sh
python3 tools/rpmon_cli.py --tcp 192.168.4.1 config_gpio --id 5 --gpio 17 --direction input --pull up
python3 tools/rpmon_cli.py --tcp 192.168.4.1 start --id 5
python3 tools/rpmon_cli.py --tcp 192.168.4.1 gpio_read --id 5
python3 tools/rpmon_cli.py --tcp 192.168.4.1 monitor
```

CMSIS-DAP/SWD debug probe:

```sh
# Default probe pins are GP2=SWCLK, GP3=SWDIO, GP1=nRESET.
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX probe_caps
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX probe_config --swclk 2 --swdio 3 --reset 1 --swclk-khz 1000
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX probe_reset --action pulse --pulse-ms 50
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX probe_status

# Then use the USB CMSIS-DAP interface from OpenOCD with the target MCU config.
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg
```

The monitor firmware supplies the CMSIS-DAP transport; OpenOCD supplies the
target-specific flash algorithm and debug server. See
[docs/debug_probe.md](docs/debug_probe.md) for pin rules, OpenOCD examples, and
AI-facing `probe_*` command details.

High-speed logic capture on GPIO16..GPIO19:

```sh
python3 tools/rpmon_cli.py --tcp 192.168.4.1 logic_caps
python3 tools/rpmon_cli.py --tcp 192.168.4.1 logic_config --pin-base 16 --pin-count 4 --sample-rate 10000000 --samples 1024
python3 tools/rpmon_cli.py --tcp 192.168.4.1 logic_start
python3 tools/rpmon_cli.py --tcp 192.168.4.1 logic_status
python3 tools/rpmon_cli.py --tcp 192.168.4.1 --log logic_capture.jsonl logic_read --offset-words 0 --count-words 0
python3 tools/rpmon_cli.py --tcp 192.168.4.1 logic_release
```

The logic analyzer captures into a fixed 131,072-byte SRAM buffer first, then
uploads `type:"logic"` JSONL chunks over the same USB or TCP control link.
The host CLI can capture and decode in a tooling-friendly flow:

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX logic_capture --pin-base 16 --pin-count 4 --sample-rate 10000000 --samples 4096 --pull down --channel-name 16=uart_rx --output capture.jsonl --release
python3 tools/rpmon_cli.py logic_decode --input capture.jsonl --decoder summary
python3 tools/rpmon_cli.py logic_decode --input capture.jsonl --decoder bursts
python3 tools/rpmon_cli.py logic_decode --input capture.jsonl --decoder uart --rx-pin 16 --baud 115200
python3 tools/rpmon_cli.py logic_decode --input capture.jsonl --decoder spi --cs-pin 16 --sck-pin 17 --mosi-pin 18 --miso-pin 19 --mode 0
python3 tools/rpmon_cli.py logic_decode --input capture.jsonl --decoder i2c --sda-pin 16 --scl-pin 17
python3 tools/rpmon_cli.py logic_export --input capture.jsonl --format vcd --output capture.vcd
```

For repeatable host workflows, store capture settings as JSON and pass
`--settings`. Command-line arguments override fields from the settings file:

```json
{
  "pin_base": 16,
  "pin_count": 2,
  "sample_rate": 10000000,
  "samples": 4096,
  "pre_samples": 512,
  "post_samples": 3584,
  "burst_count": 1,
  "pull": "down",
  "pin_pulls": {"16": "up", "17": "none"},
  "trigger": {"mode": "pattern", "mask": "0x3", "value": "0x2"},
  "channel_names": {"16": "uart_rx", "17": "uart_tx"}
}
```

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX logic_capture --settings logic_settings.json --output capture.jsonl --release
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX logic_capture --pin-base 16 --pin-count 4 --sample-rate 10000000 --samples 4096 --pull none --pin-pull 16=up --pin-pull 18=down --output capture.jsonl --release
python3 tools/rpmon_cli.py logic_decode --input capture.jsonl --decoder summary --start-sample 100 --end-sample 1100
python3 tools/rpmon_cli.py logic_decode --input capture.jsonl --decoder bursts
```

Hardware loopback regression test for trigger validation:

```sh
# Wire GP0->GP18, GP1->GP19, GP2->GP16, and GP3->GP17 before running.
python3 tools/rpmon_logic_loopback_test.py --serial /dev/tty.usbmodemXXXX
```

This drives GP2 as the GP16 stimulus, GP3 as the GP17 stimulus, GP0 as the GP18
stimulus, and GP1 as the GP19 stimulus. It verifies no-trigger capture, falling,
rising, level-low, GP16=P0/GP17=P1 full-width pattern trigger, rejection of
`burst_count>1`, and native SPI0 on GP0..GP3 mirrored by logic inputs
GP16..GP19. The test is intentionally
single-session because USB CDC JSONL must not be driven by the GUI bridge and a
second CLI process at the same time.

Wi-Fi-only logic analyzer validation:

```sh
# Connect the host to RP2350-Monitor-xxxxxx first, or use the board station IP.
python3 tools/rpmon_wifi_logic_test.py --host 192.168.4.1

# Preferred station-mode validation. USB is used only to read the board's
# station IP/status; the functional regression still runs through Wi-Fi TCP.
python3 tools/rpmon_wifi_logic_test.py --serial /dev/tty.usbmodemXXXX
```

This checks HTTP `/api/status`, then runs the same GPIO trigger and SPI mirror
regression through TCP port `4242`. If the board reports station `up` but the
host cannot reach that station IP, the script reports
`station-up-host-unreachable`; this usually means the router guest network has
client isolation enabled or the host is not on a peer-visible LAN.

## Protocol

Control messages are newline-delimited JSON. Responses and captured protocol events are also newline-delimited JSON, which keeps the host side easy to pipe into scripts or models.

See [docs/control_protocol.md](docs/control_protocol.md) for the host control protocol, [docs/logic_analyzer.md](docs/logic_analyzer.md) for capture/decode workflow, [docs/debug_probe.md](docs/debug_probe.md) for CMSIS-DAP/SWD debugging, [docs/resource_limits.md](docs/resource_limits.md) for concurrent hardware limits, [docs/reliability.md](docs/reliability.md) for buffering and failure handling, and [docs/architecture.md](docs/architecture.md) for the firmware layering and extension points.

## Release Builds

GitHub Actions builds the firmware on every push and pull request. Pushes to
`main` also run the release workflow: it reads `RPMON_FW_VERSION` from
`CMakeLists.txt`, builds with Pico SDK `2.2.0`, and creates a GitHub release
with UF2/ELF/MAP/size/checksum assets when tag `v<version>` does not already
exist. Bump `RPMON_FW_VERSION` before merging a new release.
