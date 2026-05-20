# RP2350 AI Operation Contract

Schema version: `embedlabs.rpmon.operation.v1`

```json
{
  "type": "embedlabs.rpmon.operation.v1",
  "action": "logic.capture",
  "params": {
    "pin_base": 16,
    "pin_count": 4,
    "sample_rate": 1000000,
    "samples": 4096,
    "pre_samples": 0,
    "post_samples": 4096,
    "search_samples": 0,
    "burst_count": 1,
    "selected_pins": [16, 17, 18, 19],
    "trigger_type": "none"
  },
  "ui": {
    "show": true,
    "panel": "logic"
  }
}
```

## Supported First-Version Actions

- `probe`
- `wifi.scan`
- `gpio.read`
- `logic.caps`
- `logic.configure`
- `logic.capture`
- `logic.run`
- `logic.stop`
- `logic.export`
- `logic.region-from-cursors`
- `logic.decode`
- `session.export`
- `session.import`
- `evidence.export`
- `uart.configure`
- `uart.write`
- `i2c.configure`
- `i2c.transfer`
- `spi.configure`
- `spi.transfer`

## Host Bridge Responsibility

The future real bridge should translate operations into RP2350-Monitor JSONL:

- `wifi.scan` -> `wifi_scan`, poll `status` until `wifi.scan.active=false`
- `gpio.read` -> configure/start GPIO inputs if needed, then `gpio_read`
- `logic.caps` -> `logic_caps`
- `logic.capture` -> `logic_caps`, `logic_config`, `logic_start`, poll `logic_status`, `logic_read`
  and merge both `type:"logic_meta"` and `type:"logic"` lines into the UI
  snapshot
- `logic.run` / `logic.stop` -> UI/host controlled repeated short-window
  captures. This approximates oscilloscope-style live refresh without requiring
  the current firmware to implement continuous streaming.
- `logic.export` / `logic.decode` -> host-side VCD/CSV/JSON export and summary,
  edge, UART, SPI, or I2C decoding from the currently loaded capture
- `session.export` / `session.import` -> UI-local logic analyzer session save
  and restore. These actions do not touch the board until a later
  `logic.configure`, `logic.capture`, or `logic.run` operation.
- `evidence.export` -> UI-local evidence bundle export for debugging handoff,
  including analyzer session, current capture, logic metadata, decoder
  annotations, latest export content, and status.
- `uart.*` -> `channel_config`, `channel_start`, `channel_write`
- `i2c.*` -> `channel_config`, `channel_start`, `i2c_xfer`
- `spi.*` -> `channel_config`, `channel_start`, `spi_xfer`

The UI must only render snapshots and operation progress. It must not own serial
locks or persistent hardware state.

## Development Bridge

`bridge/rpmon_bridge.py` is the first local bridge implementation. It serves the
static UI and exposes:

```text
POST /api/operation
```

Request:

```json
{
  "operation": {
    "type": "embedlabs.rpmon.operation.v1",
    "action": "gpio.read",
    "params": {
      "pins": [16, 17],
      "pull": "none"
    },
    "ui": {
      "show": true,
      "panel": "logic"
    }
  }
}
```

The bridge talks to the RP2350-Monitor JSONL protocol directly over USB CDC or
Wi-Fi TCP. The macOS GUI can therefore bundle the UI folder and start the bridge
without depending on the development repo. Windows serial access currently
requires `pyserial`; the intended packaged product should provide a native
transport backend through Tauri/Electron or the shared Embed Labs runtime.

## Standalone / Plugin Launch

The logic analyzer can be launched without the macOS GUI:

```sh
bin/embed-labs-logic-analyzer
bin/embed-labs-logic-analyzer --tcp 192.168.4.1:4242
```

Plugins should treat this as the stable user-visible entry point. The launched
page opens on `panel=logic`, exposes `POST /api/operation`, and remains
AI-operable through `window.EmbedLabsPicoMonitor`. USB CDC and Wi-Fi TCP are
mutually exclusive bridge transports for a single session; do not run a second
CLI or bridge against the same endpoint while a capture is active.

## Analyzer Session Schema

Logic analyzer sessions use schema `embedlabs.rpmon.logic-session.v1`. They are
intended for reproducible hardware debugging and AI handoff. The UI can export
and import them through either visible buttons or the AI operation API.

Minimal operation examples:

```json
{
  "type": "embedlabs.rpmon.operation.v1",
  "action": "session.export",
  "params": {},
  "ui": { "show": true, "panel": "logic" }
}
```

```json
{
  "type": "embedlabs.rpmon.operation.v1",
  "action": "session.import",
  "params": {
    "session": {
      "schema": "embedlabs.rpmon.logic-session.v1",
      "channels": [
        { "pin": 16, "name": "GP16", "color": "#42d392", "pull": "up", "invert": false, "trigger": "p0" }
      ],
      "capture": {
        "sample_rate": 1000000,
        "pre_samples_requested": 120,
        "post_samples": 4096,
        "selected_pins": [16],
        "trigger_type": "falling",
        "trigger_pin": 16
      }
    }
  },
  "ui": { "show": true, "panel": "logic" }
}
```

The exported session also includes decoder fields, cursor/zoom/view state, and
replayable `logic.capture` / `logic.decode` operations. Importing a session is
safe to run offline because it only updates UI state.

Evidence bundles use schema `embedlabs.rpmon.logic-evidence.v1` and can be
generated with:

```json
{
  "type": "embedlabs.rpmon.operation.v1",
  "action": "evidence.export",
  "params": {},
  "ui": { "show": true, "panel": "logic" }
}
```

The evidence bundle is intended for bug reports and AI-to-AI handoff. It
contains the session plus the currently loaded capture, decoder annotations,
logic metadata, and a PNG data URL of the waveform canvas.

## Logic Analyzer UX Baseline

The UI follows the workflow shape of `gusmanb/logicanalyzer`: channel matrix,
sample-rate and sample-window setup, edge/pattern/burst trigger planning,
waveform viewer, cursors, measurements, export, and protocol decoder panels.

Current RP2350-Monitor firmware-backed features:

- Contiguous GPIO capture window with selectable visible channels.
- Sample rate and total sample count.
- No trigger, level trigger, rising edge, falling edge, and pattern trigger.
- Pre-trigger/post-trigger windows, trigger search length, and burst marker
  capture through `logic_meta`.
- `logic_caps` capability discovery, input pull bias, channel labels, and
  settings JSON export compatible with the host CLI `logic_capture --settings`
  workflow.
- Region-based decode/export using start/end samples or the current cursor pair.
- Oscilloscope-like live refresh through repeated finite captures.
- Host-side waveform view, cursor measurements, trigger/burst marker overlay,
  edge summary, burst listing, VCD/CSV/JSON export, and UART/SPI/I2C decode
  from a completed capture.
- Protocol decode annotations are rendered directly on the waveform so the user
  and AI can see which sample span produced each decoded UART/SPI/I2C token.
- Analyzer session export/import for repeatable hardware experiments and AI
  task handoff.
- Evidence bundle export for reproducible bug reports and remote debugging.

Displayed but intentionally disabled until firmware support lands:

- Fast-pattern trigger.
- Blast mode.
- Live Sigrok decoder bridge.
