# RP2350 Monitor UI

Cross-platform RP2350 hardware workbench for Embed Labs.

This module is intentionally browser-native in the first version. It can run as:

- a normal browser page,
- a Tauri/Electron/VSCode WebView payload,
- a macOS GUI resource opened from `DBT-Agent.app`,
- an AI-visible debugging panel driven by the same operation contract.

## Run

```sh
cd RP2350-Monitor/ui
npm run validate
bin/embed-labs-logic-analyzer --serial /dev/cu.usbmodemXXXX
```

Then open:

```text
http://127.0.0.1:5178/?api=http://127.0.0.1:5178/api/operation
```

The page defaults to a mock client. A real host bridge can be supplied through:

- `window.embedLabsRpmon.invoke(operation)`, or
- `?api=http://127.0.0.1:5178/api/operation`, or
- `?ws=ws://127.0.0.1:<port>` for a future WebSocket bridge.

Development bridge:

```sh
bin/embed-labs-logic-analyzer --serial /dev/cu.usbmodemXXXX --panel logic --lang zh
bin/embed-labs-logic-analyzer --tcp 192.168.4.1:4242 --panel logic --lang zh
```

Open:

```text
http://127.0.0.1:5178/?api=http://127.0.0.1:5178/api/operation
```

Standalone logic-analyzer launcher:

```sh
bin/embed-labs-logic-analyzer
bin/embed-labs-logic-analyzer --serial /dev/cu.usbmodemXXXX
bin/embed-labs-logic-analyzer --tcp 192.168.4.1:4242
bin/embed-labs-logic-analyzer --lang en --panel logic
```

The launcher opens the logic analyzer directly and starts the same local bridge
used by the macOS GUI. This is the preferred plugin entry point: a model or
plugin should launch this tool when the user asks to open the Pico/RP2350 logic
analyzer, then drive the UI through `window.EmbedLabsPicoMonitor` or
`POST /api/operation`.

For `--tcp`, the host must be connected to the board AP or the same LAN as the
board station address. Check `http://<board-ip>/api/status` first when the TCP
bridge connects but commands time out.

The macOS GUI bundles this folder and starts `bridge/rpmon_bridge.py`
automatically when the RP2350 monitor workbench is opened. The bridge is
self-contained and talks to the firmware JSONL protocol directly; it no longer
depends on `RP2350-Monitor/tools/rpmon_cli.py`.

## AI Entry Points

The UI exposes:

```js
window.EmbedLabsPicoMonitor.runOperation(operation)
window.EmbedLabsPicoMonitor.applySnapshot(snapshot)
window.EmbedLabsPicoMonitor.showPanel("logic")
window.EmbedLabsPicoMonitor.exportSession()
window.EmbedLabsPicoMonitor.importSession(session)
window.EmbedLabsPicoMonitor.exportEvidence()
```

For AI-assisted hardware debugging, use the same operation contract as the
buttons. The bridge serializes access to USB CDC or Wi-Fi TCP, so the model
should not open a second CLI session against the same board while this UI is
capturing.

It also accepts:

```js
window.postMessage({
  type: "embedlabs.rpmon.operation",
  operation: {
    type: "embedlabs.rpmon.operation.v1",
    action: "logic.capture",
    params: {
      pin_base: 16,
      pin_count: 4,
      sample_rate: 1000000,
      samples: 4096,
      pre_samples: 0,
      post_samples: 4096,
      search_samples: 0,
      burst_count: 1,
      selected_pins: [16, 17, 18, 19],
      trigger_type: "none"
    },
    ui: {
      show: true,
      panel: "logic"
    }
  }
})
```

## Direction

Keep hardware logic out of UI widgets. User clicks and AI calls should both emit
the same operation contract. The local host bridge owns USB CDC / Wi-Fi TCP
transport, command serialization, device locks, and real RP2350-Monitor protocol
execution. For a true Windows/Linux desktop package, wrap this UI with Tauri or
Electron and replace/extend the Python bridge with the platform-native transport
backend used by the packaged app.

## Analyzer Sessions

The logic analyzer can save and restore a complete UI experiment session, and
can export an evidence bundle for AI-assisted debugging handoff. Use `Save
Session` to download `embed-labs-rp2350-logic-session-*.json`, use `Load
Session` to restore it later, and use `Save Evidence` to save the current
session plus capture data, decoder annotations, logic metadata, and status as
`embed-labs-rp2350-logic-evidence-*.json`. The evidence bundle also embeds the
current waveform canvas as a PNG data URL so another AI thread can inspect the
visual capture without reopening the board.

A session contains:

- visible GPIO channels, labels, colors, pull bias, inversion, and trigger role,
- sample rate, pre-trigger/post-trigger windows, trigger search, burst count,
  trigger type, and pattern fields,
- decoder type and UART/SPI/I2C decoder pins,
- cursor, zoom, and waveform viewport state,
- replayable `logic.capture` and `logic.decode` operation templates for AI use.

AI clients can use the same feature without touching the file picker:

```js
const exported = await window.EmbedLabsPicoMonitor.runOperation({
  type: "embedlabs.rpmon.operation.v1",
  action: "session.export",
  params: {},
  ui: { show: true, panel: "logic" }
});

await window.EmbedLabsPicoMonitor.runOperation({
  type: "embedlabs.rpmon.operation.v1",
  action: "session.import",
  params: { session: exported.snapshot.analyzer_session },
  ui: { show: true, panel: "logic" }
});
```

Session import is UI-local. It restores the analyzer setup but does not access
the board until the user or AI runs configure/capture/run.

Evidence export is also available through `evidence.export` or
`window.EmbedLabsPicoMonitor.exportEvidence()`. It is meant to be attached to
bug reports or passed to another AI thread without requiring the user to repeat
the hardware capture.

## Logic Analyzer Scope

The logic analyzer workbench is designed against the mature
`gusmanb/logicanalyzer` workflow: channel setup, capture settings, trigger
planning, waveform viewing, cursors, measurements, export, and protocol decode.
The current RP2350 firmware can execute contiguous GPIO captures with none,
level, rising-edge, falling-edge, or pattern trigger. The UI now exposes
pre-trigger/post-trigger windows, trigger search length, burst markers, pull
bias, channel labels, region decode/export, marker drawing from `logic_meta`,
and `Settings` JSON compatible with the host CLI `logic_capture --settings`
workflow.

`Run` / `Stop` provides oscilloscope-like live refresh by repeatedly executing
short finite captures and repainting the waveform after each upload. This is not
yet a firmware-level lossless streaming mode; sustained streaming remains a
future host/firmware transport feature.

Channel add/remove is intentionally button-driven: choose one GPIO from the
`Channel Pin` selector, add it to the visible channel list, and remove channels
from their row buttons. User-facing errors use a modal dialog; routine analyzer
state such as ready, waiting for trigger, or capture complete is shown in the
logic workbench status bar.

Still-reserved analyzer extensions are host Sigrok live bridge, external PSRAM
capture depth, blast/fast-pattern trigger modes, and named decoder tracks.
