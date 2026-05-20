export const panels = ["overview", "logic", "uart", "i2c", "spi", "ai"];

export const operationSchemaVersion = "embedlabs.rpmon.operation.v1";

export const operationTemplates = {
  "probe": {
    type: operationSchemaVersion,
    action: "probe",
    params: {},
    ui: { show: true, panel: "overview" }
  },
  "wifi.scan": {
    type: operationSchemaVersion,
    action: "wifi.scan",
    params: {},
    ui: { show: true, panel: "overview" }
  },
  "logic.caps": {
    type: operationSchemaVersion,
    action: "logic.caps",
    params: {},
    ui: { show: true, panel: "logic" }
  },
  "gpio.read": {
    type: operationSchemaVersion,
    action: "gpio.read",
    params: { pins: [16, 17], pull: "none" },
    ui: { show: true, panel: "logic" }
  },
  "logic.configure": {
    type: operationSchemaVersion,
    action: "logic.configure",
    params: {
      pin_base: 16,
      pin_count: 4,
      sample_rate: 1000000,
      samples: 4096,
      pre_samples: 0,
      post_samples: 4096,
      search_samples: 0,
      burst_count: 1,
      pull: "none",
      selected_pins: [16, 17, 18, 19],
      trigger_type: "none"
    },
    ui: { show: true, panel: "logic" }
  },
  "logic.capture": {
    type: operationSchemaVersion,
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
      pull: "none",
      selected_pins: [16, 17, 18, 19],
      trigger_type: "none",
      auto_read: true
    },
    ui: { show: true, panel: "logic" }
  },
  "logic.run": {
    type: operationSchemaVersion,
    action: "logic.run",
    params: {},
    ui: { show: true, panel: "logic" }
  },
  "logic.stop": {
    type: operationSchemaVersion,
    action: "logic.stop",
    params: {},
    ui: { show: true, panel: "logic" }
  },
  "logic.export": {
    type: operationSchemaVersion,
    action: "logic.export",
    params: { format: "vcd" },
    ui: { show: true, panel: "logic" }
  },
  "logic.region-from-cursors": {
    type: operationSchemaVersion,
    action: "logic.region-from-cursors",
    params: {},
    ui: { show: true, panel: "logic" }
  },
  "session.export": {
    type: operationSchemaVersion,
    action: "session.export",
    params: {},
    ui: { show: true, panel: "logic" }
  },
  "session.import": {
    type: operationSchemaVersion,
    action: "session.import",
    params: { session: null },
    ui: { show: true, panel: "logic" }
  },
  "evidence.export": {
    type: operationSchemaVersion,
    action: "evidence.export",
    params: {},
    ui: { show: true, panel: "logic" }
  },
  "uart.configure": {
    type: operationSchemaVersion,
    action: "uart.configure",
    params: { id: 1, instance: 1, tx: 8, rx: 9, baud: 115200 },
    ui: { show: true, panel: "uart" }
  },
  "uart.write": {
    type: operationSchemaVersion,
    action: "uart.write",
    params: { id: 1, hex: "48656c6c6f" },
    ui: { show: true, panel: "uart" }
  },
  "i2c.configure": {
    type: operationSchemaVersion,
    action: "i2c.configure",
    params: { id: 3, instance: 0, sda: 4, scl: 5, baud: 100000 },
    ui: { show: true, panel: "i2c" }
  },
  "i2c.transfer": {
    type: operationSchemaVersion,
    action: "i2c.transfer",
    params: { id: 3, addr: "0x50", write: "00", read_len: 16 },
    ui: { show: true, panel: "i2c" }
  },
  "spi.configure": {
    type: operationSchemaVersion,
    action: "spi.configure",
    params: { id: 2, instance: 0, sck: 2, mosi: 3, miso: 0, cs: 1, baud: 1000000 },
    ui: { show: true, panel: "spi" }
  },
  "spi.transfer": {
    type: operationSchemaVersion,
    action: "spi.transfer",
    params: { id: 2, hex: "9f000000", read_len: 4 },
    ui: { show: true, panel: "spi" }
  }
};

export function normalizeOperation(action, params = {}, ui = {}) {
  const template = operationTemplates[action] ?? {
    type: operationSchemaVersion,
    action,
    params: {},
    ui: { show: true, panel: "overview" }
  };
  return {
    type: operationSchemaVersion,
    action: template.action,
    params: { ...template.params, ...params },
    ui: { ...template.ui, ...ui }
  };
}

export function operationFromForm(action, read) {
  switch (action) {
    case "gpio.read":
      return normalizeOperation(action, {
        pins: read("gpioPins").split(",").map((item) => Number(item.trim())).filter(Number.isFinite),
        pull: read("gpioPull")
      });
    case "logic.configure":
    case "logic.capture":
      return normalizeOperation(action, {
        pin_base: Number(read("logicPinBase") || 16),
        pin_count: Number(read("logicPinCount") || 4),
        sample_rate: Number(read("logicSampleRate")),
        samples: Number(read("logicSamples") || read("logicPostSamples") || 4096),
        pre_samples: Number(read("logicPreSamples") || 0),
        post_samples: Number(read("logicPostSamples") || read("logicSamples") || 4096),
        search_samples: Number(read("logicSearchSamples") || 0),
        burst_count: Number(read("logicBurstCount") || 1),
        pull: read("logicPull") || "none",
        trigger_type: read("logicTriggerType") || "none",
        trigger_pin: Number(read("logicTriggerPin") || 16),
        pattern_base: Number(read("logicPatternBase") || 16),
        pattern_bits: read("logicPattern") || ""
      });
    case "uart.configure":
      return normalizeOperation(action, {
        instance: Number(read("uartInstance")),
        tx: Number(read("uartTx")),
        rx: Number(read("uartRx")),
        baud: Number(read("uartBaud"))
      });
    case "uart.write":
      return normalizeOperation(action, { hex: read("uartHex") });
    case "i2c.configure":
      return normalizeOperation(action, {
        instance: Number(read("i2cInstance")),
        sda: Number(read("i2cSda")),
        scl: Number(read("i2cScl")),
        baud: Number(read("i2cBaud"))
      });
    case "i2c.transfer":
      return normalizeOperation(action, {
        addr: read("i2cAddress"),
        write: read("i2cWriteHex"),
        read_len: Number(read("i2cReadLen"))
      });
    case "spi.configure":
      return normalizeOperation(action, {
        instance: Number(read("spiInstance")),
        sck: Number(read("spiSck")),
        mosi: Number(read("spiMosi")),
        miso: Number(read("spiMiso")),
        cs: Number(read("spiCs")),
        baud: Number(read("spiBaud"))
      });
    case "spi.transfer":
      return normalizeOperation(action, {
        hex: read("spiHex"),
        read_len: Number(read("spiReadLen"))
      });
    default:
      return normalizeOperation(action);
  }
}
