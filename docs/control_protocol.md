# RP2350 Monitor Control Protocol

This document is the contract for external host tools that control the Pico 2 W
firmware over USB CDC or Wi-Fi TCP.

## Transports

- USB CDC serial: 115200 baud is used by the reference CLI, but the firmware
  accepts newline-delimited text from the USB stdio endpoint.
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
{"type":"resp","ok":true,"cmd":"hello","msg":"ready","version":"0.3.0","board":"pico2_w","links":["wifi","usb"]}
```

Status event:

```json
{"type":"status","seq":4,"ts_us":1234567,"component":"wifi","msg":"station status changed","station_status":"up","ip":"192.168.1.50"}
```

Protocol data event:

```json
{"type":"event","seq":12,"ts_us":1234567,"channel":1,"proto":"uart","dir":"rx","len":3,"offset":0,"hex":"010203"}
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
```

External tools should follow the same rule: do not scrape human text; consume
the JSON line stream directly.

For buffering, overflow, replay, and host-side JSONL storage rules, see
[reliability.md](reliability.md).
