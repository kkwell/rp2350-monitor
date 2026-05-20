import { normalizeOperation } from "./schema.js";

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function pseudoLogicWords(pinCount, samples) {
  const pattern = pinCount >= 4 ? 0x11111111 : 0x55555555;
  const words = Math.max(1, Math.ceil((pinCount * samples) / 32));
  return Array.from({ length: words }, (_, index) => (index % 9 < 4 ? pattern : pattern ^ 0x22222222));
}

export class MockRpmonClient {
  constructor() {
    this.captureId = 1;
    this.levels = new Map([[16, true], [17, false], [18, false], [19, true]]);
  }

  async invoke(operation) {
    await sleep(180);
    const op = normalizeOperation(operation.action, operation.params, operation.ui);
    switch (op.action) {
      case "probe":
        return { ok: true, snapshot: this.snapshot({ lastResponse: "Mock probe complete" }) };
      case "wifi.scan":
        return { ok: true, snapshot: this.snapshot({ wifi: mockWifi(), lastResponse: "Wi-Fi scan complete" }) };
      case "gpio.read":
        for (const pin of op.params.pins ?? []) {
          if (!this.levels.has(pin)) this.levels.set(pin, false);
        }
        return {
          ok: true,
          snapshot: this.snapshot({
            gpio: [...this.levels.entries()].map(([pin, level]) => ({ pin, level })),
            lastResponse: "GPIO levels updated"
          })
        };
      case "logic.configure":
        return { ok: true, snapshot: this.snapshot({ logic: logicState(op.params, false), lastResponse: "Logic configured" }) };
      case "logic.capture": {
        const logic = logicState(op.params, true);
        logic.capture_id = this.captureId++;
        logic.words = pseudoLogicWords(logic.pin_count, logic.samples);
        return { ok: true, snapshot: this.snapshot({ logic, lastResponse: "Logic capture complete" }) };
      }
      default:
        return {
          ok: true,
          snapshot: this.snapshot({
            lastResponse: `${op.action} accepted by mock client`,
            events: [{ type: "event", proto: op.action.split(".")[0], dir: "mock", hex: "01020304" }]
          })
        };
    }
  }

  snapshot(patch = {}) {
    return {
      device: { board: "Pico 2 W", firmware: "0.5.0", transport: "USB CDC", endpoint: "/dev/cu.usbmodemXXXX" },
      wifi: mockWifi(),
      gpio: [...this.levels.entries()].map(([pin, level]) => ({ pin, level })),
      logic: logicState({ pin_base: 16, pin_count: 4, sample_rate: 1000000, samples: 2048 }, false),
      events: [],
      lastResponse: "Ready",
      ...patch
    };
  }
}

export class HostBridgeClient {
  constructor(bridge) {
    this.bridge = bridge;
  }

  async invoke(operation) {
    if (!this.bridge || typeof this.bridge.invoke !== "function") {
      throw new Error("Host bridge is not available");
    }
    return this.bridge.invoke(operation);
  }
}

export class WebSocketRpmonClient {
  constructor(url) {
    this.url = url;
    this.nextId = 1;
    this.pending = new Map();
    this.socket = new WebSocket(url);
    this.socket.addEventListener("message", (event) => {
      const message = JSON.parse(event.data);
      const ticket = this.pending.get(message.id);
      if (!ticket) return;
      this.pending.delete(message.id);
      if (message.ok === false) ticket.reject(new Error(message.error ?? "RP2350 operation failed"));
      else ticket.resolve(message);
    });
  }

  async invoke(operation) {
    if (this.socket.readyState === WebSocket.CONNECTING) {
      await new Promise((resolve, reject) => {
        this.socket.addEventListener("open", resolve, { once: true });
        this.socket.addEventListener("error", reject, { once: true });
      });
    }
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.socket.send(JSON.stringify({ id, operation }));
    });
  }
}

export class HttpRpmonClient {
  constructor(endpoint) {
    this.endpoint = endpoint;
  }

  async invoke(operation) {
    const response = await fetch(this.endpoint, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ operation })
    });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    return response.json();
  }
}

export function selectClient() {
  const params = new URLSearchParams(location.search);
  const apiURL = params.get("api");
  const wsURL = params.get("ws");
  if (window.embedLabsRpmon) return new HostBridgeClient(window.embedLabsRpmon);
  if (apiURL) return new HttpRpmonClient(apiURL);
  if (wsURL) return new WebSocketRpmonClient(wsURL);
  return new MockRpmonClient();
}

function mockWifi() {
  return [
    { ssid: "konghome", rssi: -45, channel: 11, auth: 5 },
    { ssid: "home_Guest", rssi: -55, channel: 11, auth: 5 },
    { ssid: "CMCC-3uPq", rssi: -77, channel: 8, auth: 7 },
    { ssid: "13-308", rssi: -69, channel: 11, auth: 5 }
  ];
}

function logicState(params, complete) {
  return {
    configured: true,
    complete,
    running: false,
    capture_id: 0,
    pin_base: Number(params.pin_base ?? 16),
    pin_count: Number(params.pin_count ?? 4),
    sample_rate: Number(params.sample_rate ?? 1000000),
    samples: Number(params.samples ?? 2048),
    pre_samples: Number(params.pre_samples ?? 0),
    post_samples: Number(params.post_samples ?? params.samples ?? 2048),
    selected_pins: params.selected_pins ?? [16, 17, 18, 19],
    trigger_type: params.trigger_type ?? "none",
    trigger_mode: params.trigger_mode,
    trigger_pin: params.trigger_pin,
    trigger_level: params.trigger_level,
    record_bits: 32,
    words: []
  };
}
