# RP2350 Monitor

RP2350 Monitor is a Raspberry Pi Pico 2 W firmware and host CLI for configurable hardware protocol monitoring/debugging.

## Scope

Current version:

- Wi-Fi control/data channel on TCP port `4242`
- USB CDC backup control/data channel
- Pico-hosted setup AP: `RP2350-Monitor-xxxxxx`, password `rpmon2350`, IP `192.168.4.1`
- Native UART, SPI, I2C, and GPIO channel layers
- CAN reserved behind a driver interface
- Host CLI that emits newline-delimited JSON for AI/tooling analysis
- Bounded in-firmware event buffering with overflow counters and replay

Bluetooth is intentionally not included in this version.

## Build

The project defaults to the DBT-installed Pico SDK and Arm GNU Toolchain paths if `PICO_SDK_PATH` / `PICO_TOOLCHAIN_PATH` are not set.

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

## Protocol

Control messages are newline-delimited JSON. Responses and captured protocol events are also newline-delimited JSON, which keeps the host side easy to pipe into scripts or models.

See [docs/control_protocol.md](docs/control_protocol.md) for the host control protocol, [docs/reliability.md](docs/reliability.md) for buffering and failure handling, and [docs/architecture.md](docs/architecture.md) for the firmware layering and extension points.
