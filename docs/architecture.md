# RP2350 Monitor Architecture

## Design Goals

- Keep Wi-Fi and USB as independent control/data links.
- Keep protocol implementations isolated behind a common channel interface.
- Keep pin ownership and alternate-function validation centralized.
- Make host output easy for tools and AI models to consume.
- Leave CAN and PIO protocol engines as clean extension points.

## Firmware Layers

```text
main
├── transport
│   ├── usb_transport        USB CDC newline JSON
│   └── tcp_transport        Wi-Fi TCP newline JSON
├── net
│   ├── wifi_manager         AP mode, station profiles, scan, status
│   └── dhcp_server          minimal AP DHCP server
├── core
│   ├── command_processor    JSON command dispatch
│   ├── channel_manager      channel lifecycle
│   ├── pin_manager          pin ownership and mux validation
│   ├── config_store         persisted Wi-Fi profiles
│   └── event_bus            bounded event buffering and fan-out
└── drivers
    ├── uart_channel         native UART RX/TX monitoring
    ├── spi_channel          native SPI transaction engine
    ├── i2c_channel          native I2C transaction engine
    └── can_channel          reserved placeholder
```

## Data Model

All transports carry newline-delimited JSON.

Command example:

```json
{"cmd":"channel_config","id":1,"type":"uart","instance":0,"tx":0,"rx":1,"baud":115200}
```

Response example:

```json
{"type":"resp","ok":true,"cmd":"channel_config","msg":"channel configured"}
```

Protocol event example:

```json
{"type":"event","seq":12,"ts_us":1234567,"channel":1,"proto":"uart","dir":"rx","len":3,"offset":0,"hex":"010203"}
```

The host CLI prints these lines unchanged and can append them to a JSONL file
with `--log`. This avoids terminal-specific formatting and makes the data stream
suitable for scripts, logs, and AI-assisted analysis.

## Buffering Policy

`EventBus` owns a fixed telemetry ring in static SRAM. All protocol data and
status events enter this queue before USB/TCP live fan-out. If a host is absent
or slow, recent events remain replayable through `events_read`. If the queue
fills, the oldest event is dropped, `buffers.dropped_events` and
`buffers.dropped_bytes` are incremented, and warning status events are emitted.

Current constants:

- `kEventQueueCapacity = 128`
- `kEventLineMax = 512`
- `kMaxPayloadBytes = 128`
- `kEventReplayMax = 64`

The measured static SRAM allocation is documented in
[reliability.md](reliability.md).

## Protocol Channel Contract

Every protocol driver implements:

- `configure`: validate protocol-specific pins and claim them.
- `start`: initialize the RP2350 native peripheral or future PIO engine.
- `stop`: disable hardware and release runtime state.
- `poll`: collect passive receive data where applicable.
- `write`: inject bytes into the channel.
- `transfer`: perform transaction-style protocols such as SPI/I2C.
- `describe_json`: report channel configuration and state.

Native first-version behavior:

- UART: passive RX monitoring plus TX injection.
- SPI: transaction engine using native SPI; passive bus sniffing belongs in a future PIO channel.
- I2C: transaction engine using native I2C; passive bus sniffing belongs in a future PIO channel.
- CAN: reserved until the hardware path is chosen.

## Pin Policy

`PinManager` is the single place that knows:

- Which Pico 2 W GPIOs are externally exposed.
- Which pins can be used for UART/SPI/I2C alternate functions.
- Which channel currently owns each pin.

Protocol drivers should not duplicate pin tables. New protocol layers should add validation helpers here, then claim pins through the same API.

## Wi-Fi Provisioning Flow

Boot behavior:

1. Initialize CYW43 Wi-Fi.
2. Load saved station credentials if present.
3. Start AP mode with SSID `RP2350-Monitor-xxxxxx`.
4. Start DHCP with Pico at `192.168.4.1`.
5. Start TCP control server on port `4242`.

User can configure station Wi-Fi through either USB or AP TCP:

```json
{"cmd":"wifi_scan"}
{"cmd":"wifi_set","slot":0,"ssid":"your-ssid","password":"your-pass","save":true}
{"cmd":"wifi_connect","slot":0}
```

The firmware stores three Wi-Fi profile slots. If a saved profile fails to connect, the AP is restored and the per-slot `last_error` appears in `status`.
After station connection, `status` reports the station IP. USB remains available as a recovery/control path.

## Extension Points

CAN options:

- External MCP2515-style CAN controller behind `SpiChannel`.
- PIO-based CAN experimental RX/TX engine for constrained bit timing.

PIO options:

- Passive SPI/I2C sniffers.
- Custom single-wire or timing-sensitive protocol capture.
- Triggered GPIO edge capture with timestamped events.

When adding PIO engines, keep them under a separate driver module and feed normalized events through `EventBus`.
