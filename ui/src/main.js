import { operationFromForm, operationSchemaVersion, operationTemplates, panels } from "./schema.js";
import { selectClient } from "./client.js";

const state = {
  client: selectClient(),
  snapshot: null,
  queue: [],
  activePanel: new URLSearchParams(location.search).get("panel") || "overview"
};

const $ = (id) => document.getElementById(id);
const read = (id) => $(id)?.value ?? "";

function setPanel(panel) {
  const next = panels.includes(panel) ? panel : "overview";
  state.activePanel = next;
  document.querySelectorAll(".tab-button").forEach((button) => {
    button.classList.toggle("active", button.dataset.panel === next);
  });
  document.querySelectorAll(".panel").forEach((panelElement) => {
    panelElement.classList.toggle("active", panelElement.id === `panel-${next}`);
  });
}

async function runOperation(operation) {
  const startedAt = new Date();
  state.queue.unshift({ operation, status: "running", startedAt });
  renderQueue();
  if (operation.ui?.panel) setPanel(operation.ui.panel);
  try {
    const result = await state.client.invoke(operation);
    if (result.snapshot) applySnapshot(result.snapshot);
    state.queue[0] = { operation, status: result.ok === false ? "failed" : "done", result, startedAt, endedAt: new Date() };
    renderQueue();
    return result;
  } catch (error) {
    state.queue[0] = { operation, status: "failed", error: error.message, startedAt, endedAt: new Date() };
    renderQueue();
    renderStatus({ error: error.message });
    throw error;
  }
}

function applySnapshot(snapshot) {
  state.snapshot = { ...(state.snapshot ?? {}), ...snapshot };
  render();
}

function render() {
  const snapshot = state.snapshot ?? {};
  renderStatus();
  renderOverview(snapshot);
  renderWifi(snapshot.wifi ?? []);
  renderGPIO(snapshot.gpio ?? []);
  renderLogic(snapshot.logic);
  renderProtocolLogs(snapshot);
  $("contractView").textContent = JSON.stringify({
    schema: operationSchemaVersion,
    acceptedOperations: Object.keys(operationTemplates),
    aiEntryPoints: [
      "window.EmbedLabsPicoMonitor.runOperation(operation)",
      "window.postMessage({ type: 'embedlabs.rpmon.operation', operation })",
      "WebSocket query parameter: ?ws=ws://127.0.0.1:<port>"
    ]
  }, null, 2);
}

function renderStatus(patch = {}) {
  const snapshot = { ...(state.snapshot ?? {}), ...patch };
  const device = snapshot.device ?? {};
  const items = [
    ["Device", device.board ?? "Pico 2 W"],
    ["Firmware", device.firmware ?? "-"],
    ["Transport", device.transport ?? "USB CDC"],
    ["Last", snapshot.error ?? snapshot.lastResponse ?? "Ready"]
  ];
  $("statusStrip").innerHTML = items.map(([label, value]) => `
    <div class="pill"><span>${label}</span><strong>${escapeHTML(value)}</strong></div>
  `).join("");
}

function renderOverview(snapshot) {
  const device = snapshot.device ?? {};
  const logic = snapshot.logic ?? {};
  const cards = [
    ["Board", device.board ?? "Pico 2 W", "ok"],
    ["Endpoint", device.endpoint ?? read("transportEndpoint"), "neutral"],
    ["Logic", logic.complete ? "Capture complete" : logic.configured ? "Configured" : "Idle", logic.complete ? "ok" : "neutral"],
    ["GPIO", `${(snapshot.gpio ?? []).length} pins`, "neutral"]
  ];
  $("overviewCards").innerHTML = cards.map(([title, value, tone]) => `
    <article class="metric ${tone}">
      <span>${title}</span>
      <strong>${escapeHTML(value)}</strong>
    </article>
  `).join("");
  $("transportDetail").textContent = JSON.stringify(device, null, 2);
}

function renderWifi(networks) {
  $("wifiList").innerHTML = networks.length
    ? networks.map((item) => `
      <div class="wifi-row">
        <strong>${escapeHTML(item.ssid)}</strong>
        <span>${item.rssi} dBm</span>
        <span>CH ${item.channel}</span>
      </div>
    `).join("")
    : `<p class="empty">No scan results yet.</p>`;
}

function renderGPIO(levels) {
  $("gpioLevels").innerHTML = levels.length
    ? levels.map(({ pin, level }) => `
      <div class="level ${level ? "high" : "low"}">
        <span>GP${pin}</span>
        <strong>${level ? "HIGH" : "LOW"}</strong>
      </div>
    `).join("")
    : `<p class="empty">Read GPIO to show live levels.</p>`;
}

function renderLogic(logic) {
  const canvas = $("logicCanvas");
  const ctx = canvas.getContext("2d");
  const width = canvas.width;
  const height = canvas.height;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#10141b";
  ctx.fillRect(0, 0, width, height);
  ctx.strokeStyle = "rgba(255,255,255,0.08)";
  ctx.lineWidth = 1;
  for (let x = 60; x < width; x += 80) {
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, height);
    ctx.stroke();
  }
  if (!logic?.words?.length) {
    ctx.fillStyle = "#8b95a5";
    ctx.font = "16px system-ui";
    ctx.fillText("No capture loaded. Run Capture to draw digital waveforms.", 28, 42);
    return;
  }
  const channels = Math.max(1, logic.pin_count || 1);
  const rowHeight = (height - 40) / channels;
  for (let channel = 0; channel < channels; channel += 1) {
    const gpio = (logic.pin_base ?? 0) + channel;
    const y = 24 + channel * rowHeight;
    ctx.fillStyle = "#cdd6e3";
    ctx.font = "13px ui-monospace, SFMono-Regular, Menlo, monospace";
    ctx.fillText(`GP${gpio}`, 12, y + 20);
    drawTrace(ctx, logic, channel, 64, y + 6, width - 80, rowHeight - 16);
  }
}

function drawTrace(ctx, logic, channel, x, y, width, height) {
  const samples = Math.max(1, logic.samples || 1);
  const step = Math.max(1, Math.floor(samples / 700));
  const highY = y + 8;
  const lowY = y + height - 8;
  ctx.strokeStyle = "#42d392";
  ctx.lineWidth = 2;
  ctx.beginPath();
  let first = true;
  let previousY = lowY;
  for (let sample = 0; sample < samples; sample += step) {
    const px = x + (sample / Math.max(1, samples - 1)) * width;
    const level = logicLevelAt(logic, sample, channel);
    const py = level ? highY : lowY;
    if (first) {
      ctx.moveTo(px, py);
      first = false;
    } else {
      ctx.lineTo(px, previousY);
      ctx.lineTo(px, py);
    }
    previousY = py;
  }
  ctx.stroke();
}

function logicLevelAt(logic, sample, channel) {
  const pinCount = logic.pin_count || 1;
  const recordBits = logic.record_bits || 32;
  const bitIndex = channel + sample * pinCount;
  const wordIndex = Math.floor(bitIndex / recordBits);
  const bitPosition = (bitIndex % recordBits) + 32 - recordBits;
  const word = logic.words?.[wordIndex] ?? 0;
  return Boolean(word & (1 << bitPosition));
}

function renderProtocolLogs(snapshot) {
  const events = snapshot.events ?? [];
  const text = events.length ? events.map((event) => JSON.stringify(event)).join("\n") : "No events yet.";
  $("uartLog").textContent = events.filter((event) => event.proto === "uart").map((event) => JSON.stringify(event)).join("\n") || text;
  $("i2cLog").textContent = events.filter((event) => event.proto === "i2c").map((event) => JSON.stringify(event)).join("\n") || text;
  $("spiLog").textContent = events.filter((event) => event.proto === "spi").map((event) => JSON.stringify(event)).join("\n") || text;
}

function renderQueue() {
  $("aiQueue").textContent = state.queue.map((item) => {
    const duration = item.endedAt ? `${item.endedAt - item.startedAt}ms` : "running";
    return JSON.stringify({ status: item.status, duration, operation: item.operation, error: item.error }, null, 2);
  }).join("\n\n") || "AI operations will appear here.";
}

function escapeHTML(value) {
  return String(value).replace(/[&<>"']/g, (char) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;", "'": "&#39;" }[char]));
}

function wireUI() {
  document.querySelectorAll(".tab-button").forEach((button) => {
    button.addEventListener("click", () => setPanel(button.dataset.panel));
  });
  document.querySelectorAll("[data-action]").forEach((button) => {
    button.addEventListener("click", () => {
      const action = button.dataset.action;
      if (action === "ai.demo") {
        runOperation(operationFromForm("logic.capture", read));
      } else {
        runOperation(operationFromForm(action, read));
      }
    });
  });
  window.addEventListener("message", (event) => {
    const message = event.data ?? {};
    if (message.type !== "embedlabs.rpmon.operation") return;
    runOperation(message.operation);
  });
}

window.EmbedLabsPicoMonitor = {
  runOperation,
  applySnapshot,
  showPanel: setPanel,
  getState: () => state,
  setClient: (client) => { state.client = client; }
};

wireUI();
setPanel(state.activePanel);
applySnapshot(state.client.snapshot ? state.client.snapshot() : {});
if (new URLSearchParams(location.search).get("api") || new URLSearchParams(location.search).get("ws") || window.embedLabsRpmon?.invoke) {
  runOperation(operationFromForm("probe", read)).catch(() => {});
}
