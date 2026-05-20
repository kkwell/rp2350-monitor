(function () {
  const panels = ["overview", "logic", "uart", "i2c", "spi", "ai"];
  const operationSchemaVersion = "embedlabs.rpmon.operation.v1";
  const exposedPins = [...Array.from({ length: 23 }, (_, index) => index), 26, 27, 28];
  const exposedPinSet = new Set(exposedPins);
  const nativePinMaps = {
    uart: {
      0: { tx: [0, 2, 12, 14, 16, 18, 28], rx: [1, 3, 13, 15, 17, 19] },
      1: { tx: [4, 6, 8, 10, 20, 22, 26], rx: [5, 7, 9, 11, 21, 27] }
    },
    i2c: {
      0: { sda: [0, 4, 8, 12, 16, 20, 28], scl: [1, 5, 9, 13, 17, 21] },
      1: { sda: [2, 6, 10, 14, 18, 22, 26], scl: [3, 7, 11, 15, 19, 27] }
    },
    spi: {
      0: { miso: [0, 4, 16, 20], sck: [2, 6, 18, 22], mosi: [3, 7, 19] },
      1: { miso: [8, 12, 28], sck: [10, 14, 26], mosi: [11, 15, 27] }
    }
  };
  const nativeDefaults = {
    uart: { instance: 1, tx: 8, rx: 9 },
    i2c: { instance: 0, sda: 4, scl: 5 },
    spi: { instance: 0, sck: 2, mosi: 3, miso: 0, cs: 1 }
  };
  const spiPreferredManualCs = { 0: 1, 1: 13 };
  const protocolLimits = { uart: 2, i2c: 2, spi: 2 };
  const protocolInstanceChannelIds = {
    uart: { 1: 1, 0: 4 },
    i2c: { 0: 3, 1: 5 },
    spi: { 0: 2, 1: 6 }
  };
  const protocolDefaultIds = { uart: 1, spi: 2, i2c: 3 };
  const defaultLogicSampleRateMax = 150000000;
  const channelPalette = [
    "#42d392", "#66a6ff", "#f6c85f", "#ff7a7a", "#b48cff", "#50e3c2",
    "#f08ab6", "#9ee37d", "#ffb86b", "#8bd3ff", "#d5f46b", "#ff9f9f"
  ];
  const supportedTriggerTypes = new Set(["none", "level-high", "level-low", "rising", "falling", "pattern"]);
  const translations = {
    en: {
      language: "Language",
      overview: "Overview",
      logicAnalyzer: "Logic Analyzer",
      aiSession: "AI Session",
      logicTitle: "Logic Analyzer",
      logicSubtitle: "RP2350 PIO/DMA capture with desktop-style channel setup, trigger planning, cursors, measurements, export, and decoders.",
      fit: "Fit",
      run: "Run",
      stop: "Stop",
      configure: "Configure",
      capture: "Capture",
      waiting: "Waiting",
      export: "Export",
      saveSession: "Save Session",
      loadSession: "Load Session",
      saveEvidence: "Save Evidence",
      sampleRate: "Sample Rate",
      preSamples: "Pre Samples",
      postSamples: "Post Samples",
      logicPull: "Default Bias",
      sampleMode: "Sample Mode",
      singleCapture: "Single capture",
      repeatedWindows: "Repeated windows",
      longStream: "Long stream (host bridge required)",
      zoomIn: "Zoom In",
      zoomOut: "Zoom Out",
      cursorA: "Cursor A",
      cursorB: "Cursor B",
      channels: "Channels",
      noneShort: "None",
      invert: "Invert",
      triggerModes: "Trigger Window",
      trigger: "Trigger",
      triggerGP: "Trigger GP",
      channelPin: "Channel Pin",
      channelPull: "Bias",
      channelInvert: "Invert",
      channelTrigger: "Trigger",
      addChannel: "Add",
      clearChannels: "Clear",
      removeChannel: "Remove",
      triggerIgnore: "Ignore",
      triggerHigh: "High",
      triggerLow: "Low",
      triggerRise: "Rise",
      triggerFall: "Fall",
      triggerPatternHigh: "P1",
      triggerPatternLow: "P0",
      errorTitle: "Operation failed",
      ok: "OK",
      levelHigh: "Level High",
      levelLow: "Level Low",
      risingEdge: "Rising Edge",
      fallingEdge: "Falling Edge",
      patternTrigger: "Pattern",
      fastPatternRequired: "Fast Pattern (firmware required)",
      blastRequired: "Blast (firmware required)",
      patternBase: "Pattern Base",
      patternBits: "Pattern Bits",
      searchSamples: "Search Samples",
      burstCount: "Burst Marks",
      gpioLive: "GPIO Live",
      read: "Read",
      pins: "Pins",
      decodeExport: "Protocol Decode / Export",
      decode: "Decode",
      copyExport: "Copy Export",
      useCursors: "Use Cursors",
      settings: "Settings",
      regionStart: "Start",
      regionEnd: "End",
      regionEndPlaceholder: "end",
      decoder: "Decoder",
      autoBaud: "Auto",
      summary: "Summary",
      bursts: "Bursts",
      edges: "Edges",
      sigrokBridge: "Sigrok bridge",
      device: "Device",
      deviceChannel: "Interface",
      addDevice: "Add",
      closeDevice: "Close",
      active: "Active",
      inactive: "Closed",
      noFreeHardware: "All hardware instances for this protocol are already listed.",
      firmware: "Firmware",
      transport: "Transport",
      probe: "Probe",
      mode: "Mode",
      scan: "Scan",
      networkSetup: "Network Setup",
      apMode: "AP Mode",
      savedWifi: "Saved Wi-Fi",
      profileSlot: "Profile Slot",
      ssid: "SSID",
      password: "Password",
      saveWifi: "Save",
      saveAndConnect: "Save + Connect",
      connectSaved: "Connect Saved",
      clearSlot: "Clear Slot",
      hotspotConfig: "Hotspot Setup",
      hotspotActive: "AP hotspot is active",
      hotspotInactive: "AP hotspot is off",
      hotspotHint: "Connect a phone or another device to this hotspot and open the setup page.",
      apSsid: "AP SSID",
      apIp: "AP IP",
      apPassword: "AP Password",
      setupUrl: "Setup URL",
      stationNetwork: "Station Network",
      profileEmpty: "Empty",
      profileActive: "Active",
      profileSaved: "Saved",
      wifiSaved: "Wi-Fi profile saved",
      wifiSavedConnected: "Wi-Fi profile saved and connected",
      wifiConnected: "Saved Wi-Fi connected",
      wifiCleared: "Wi-Fi profile cleared",
      apStarted: "AP setup mode started",
      ssidRequired: "SSID is required.",
      pull: "Pull",
      up: "Up",
      down: "Down",
      write: "Write",
      transfer: "Transfer",
      spiCaptureWiring: "To view SPI waveforms, capture GP16=SCK, GP17=MOSI, GP18=MISO, GP19=CS and wire the selected native SPI pins to those monitor pins.",
      bridgeOffline: "Local RP2350 bridge is not reachable. Start the monitor bridge and retry.",
      instance: "Instance",
      baud: "Baud",
      address: "Address",
      readLen: "Read Len",
      writeHex: "Write HEX",
      transferHex: "Transfer HEX",
      aiQueue: "AI Operation Queue",
      demoOperation: "Demo Operation",
      contract: "Contract",
      last: "Last",
      ready: "Ready",
      liveCapture: "Live capture",
      board: "Board",
      endpoint: "Endpoint",
      logic: "Logic",
      gpio: "GPIO",
      captureComplete: "Capture complete",
      captureNoEdges: "Capture complete: no level changes in the visible channels.",
      captureNoEdgesHint: "No edges in this window. For manual signal changes, use a trigger or lower the sample rate / increase samples.",
      levelTriggerNoEdgesHint: "High/Low are level conditions; they can fire on an already-stable level. Use Rise/Fall to capture the actual transition.",
      configured: "Configured",
      idle: "Idle",
      noScanResults: "No scan results yet.",
      high: "HIGH",
      low: "LOW",
      readGPIOHint: "Read GPIO to show live levels.",
      selected: "selected",
      hardwareWindow: "hardware window",
      selection: "selection",
      sparse: "Sparse",
      contiguous: "Contiguous",
      selectAtLeastOne: "Select at least one channel before capture.",
      sparseNote: "Sparse selections are displayed like a desktop analyzer, but current firmware captures the enclosing contiguous GPIO range.",
      directNote: "Current selection maps directly to one firmware capture range.",
      triggerExpandedNote: "Trigger GP was included in the hardware capture window; only selected channels are displayed.",
      patternMultiHint: "Pattern trigger is the multi-channel trigger mode: bit 0 maps to Pattern Base, then the following GPIOs.",
      unsupportedPin: "is not exposed; split the capture or choose one contiguous exposed range.",
      noCapture: "No capture loaded. Configure, Capture, or Run to draw digital waveforms.",
      waitingForTrigger: "Waiting for trigger or capture upload. Triggered captures draw after the condition is met.",
      openEndedTriggerNoPre: "Open-ended trigger wait uses the post-trigger window only.",
      captureInProgress: "Capturing. Waveforms will draw after data is uploaded.",
      configuredNoCapture: "Configuration is ready. Run or Capture to acquire a waveform.",
      channelAdded: "Channel added.",
      channelRemoved: "Channel removed.",
      channelsCleared: "All channels removed.",
      cursorHint: "Cursor A/B and edge measurements appear after capture.",
      rate: "Rate",
      samples: "Samples",
      window: "Window",
      runState: "Run",
      memory: "Memory",
      maxSamples: "Max",
      stopped: "stopped",
      live: "live",
      bUnset: "B unset",
      logicStopped: "Logic run stopped",
      runStarted: "Logic live run started",
      runStopped: "Logic live run stopped",
      wifiScanComplete: "Wi-Fi scan complete",
      probeComplete: "Mock probe complete",
      logicCapsLoaded: "Logic capabilities loaded",
      noCaptureDecode: "No capture is available to decode.",
      noCaptureExport: "No logic capture is available to export.",
      exportGenerated: "Logic export generated",
      noExportContent: "No export content is available.",
      exportCopied: "Export copied",
      decodeComplete: "Logic decode complete",
      regionFromCursors: "Region set from cursors",
      invalidRegion: "Invalid sample region.",
      settingsGenerated: "Capture settings generated",
      noEdges: "no edges",
      noUART: "No UART frames decoded.",
      noSPI: "No SPI bytes decoded.",
      noI2C: "No I2C frames decoded.",
      sigrokHint: "Sigrok integration is a host-side extension point. Export VCD/CSV for now.",
      triggerFirmwareRequired: "trigger is shown for workflow planning but requires a firmware extension before capture.",
      triggerRequiredForAdvanced: "Pre-trigger or multi-burst capture requires an edge, level, or pattern trigger.",
      preSampleNeedsTrigger: "Pre Samples is ignored until a channel trigger is armed.",
      preSampleNeedsTriggerForCapture: "Pre Samples only works with a channel trigger. This capture will run immediately without pre-trigger samples.",
      preSampleIgnoredShort: "Pre Samples ignored: no trigger",
      statusNoEdgesShort: "Capture complete: no edges",
      invalidPatternBits: "Pattern bits must contain only 0 or 1.",
      invalidPatternWindow: "Pattern must fit inside the captured GPIO window.",
      singleEdgeTriggerOnly: "Only one edge trigger can be armed. Use High/Low channel triggers for multi-channel pattern matching.",
      noBursts: "No trigger or burst markers were recorded.",
      triggerGuideNone: "No trigger: capture starts immediately.",
      triggerGuideCurrent: "current",
      triggerGuideUnknown: "current level unknown; use Read GPIO for a live check",
      triggerGuideHigh: "captures when this pin is HIGH",
      triggerGuideLow: "captures when this pin is LOW",
      triggerGuideRising: "rising edge needs LOW first, then HIGH",
      triggerGuideFalling: "falling edge needs HIGH first, then LOW",
      triggerGuidePattern: "Pattern compares the selected relative bits",
      presetManual: "Logic",
      presetUART: "UART",
      presetI2C: "I2C",
      presetSPI: "SPI",
      presetApplied: "Preset applied",
      protocolPresets: "Protocol presets",
      protocolPresetHint: "Preset only fills channels, trigger, and decoder fields; it is not a separate hardware trigger mode.",
      triggerSourceHint: "Set multi-pin triggers in the channel list. High/Low/P1/P0 combine into a pattern trigger; Rise/Fall can arm one channel at a time.",
      triggerNoArmedChannels: "No channel trigger: capture starts immediately.",
      armedTriggers: "Armed triggers",
      configLockedWhileRunning: "Capture is running. Stop it before changing configuration.",
      preSampleArmed: "Pre Samples keeps samples before the trigger point.",
      preSampleWaiting: "Waiting for the trigger; waveform is drawn after the trigger is captured.",
      sessionSaved: "Analyzer session saved.",
      sessionLoaded: "Analyzer session loaded.",
      invalidSession: "The selected analyzer session is not valid.",
      evidenceSaved: "Analyzer evidence bundle saved."
      ,systemState: "System State",
      runtimeResources: "Runtime Resources",
      activeInterfaces: "Active Interfaces",
      connection: "Connection",
      wifiStation: "Wi-Fi Station",
      wifiAP: "Wi-Fi AP",
      stationIP: "Station IP",
      channelCount: "Channels",
      eventBuffer: "Event Buffer",
      logicEngine: "Logic Engine",
      captureBuffer: "Capture Buffer",
      noActiveInterfaces: "No active protocol channels.",
      uartAssistant: "UART Serial Assistant",
      i2cAssistant: "I2C Bus Assistant",
      spiAssistant: "SPI Bus Assistant",
      openPort: "Open",
      openBus: "Open Bus",
      send: "Send",
      frame: "Frame",
      sendMode: "Send Mode",
      payloadMode: "Payload Mode",
      displayMode: "Display",
      textMode: "Text",
      binaryMode: "Binary",
      terminalMode: "Terminal",
      decodedMode: "Decoded",
      lineEnding: "Line Ending",
      sendPayload: "Send Payload",
      writePayload: "Write Payload",
      transferPayload: "Transfer Payload",
      localEcho: "Local echo sent data",
      clearLog: "Clear Log",
      terminal: "Terminal",
      transferType: "Transfer Type",
      writeRead: "Write + Read",
      writeOnly: "Write only",
      readOnly: "Read only",
      i2cTransactions: "Transactions",
      spiTransactions: "Transactions",
      aiPurpose: "AI Integration",
      generateExample: "Generate Example",
      aiPurposeOneTitle: "Why This Page Exists",
      aiPurposeOneBody: "This is the operation contract used by plugins and models. It is not a separate product demo; it lets AI operate the same hardware tools as the user.",
      aiPurposeTwoTitle: "How AI Uses It",
      aiPurposeTwoBody: "AI can call runOperation, configure captures, decode protocols, export evidence, and keep the UI visible so the user can inspect every step.",
      aiPurposeThreeTitle: "Boundary",
      aiPurposeThreeBody: "The UI never invents hardware behavior. It emits a normalized operation; the local bridge owns USB/Wi-Fi transport and firmware protocol execution.",
      bytesTx: "TX",
      bytesRx: "RX",
      frames: "Frames",
      noEvents: "No events yet."
      ,aiQueueEmpty: "AI operations will appear here.",
      operationDetail: "Operation Detail",
      fixedPins: "Fixed Pins",
      nativeMapping: "Native Mapping",
      txPin: "TX Pin",
      rxPin: "RX Pin",
      sdaPin: "SDA Pin",
      sclPin: "SCL Pin",
      sckPin: "SCK Pin",
      mosiPin: "MOSI Pin",
      misoPin: "MISO Pin",
      csPin: "CS Pin",
      custom: "Custom",
      customBaud: "Custom Baud",
      call: "Call",
      startedAt: "Started",
      duration: "Duration",
      parameters: "Parameters",
      result: "Result",
      error: "Error",
      selectAICall: "Select an operation from the left list."
    },
    zh: {
      language: "语言",
      overview: "总览",
      logicAnalyzer: "逻辑分析仪",
      aiSession: "AI 会话",
      logicTitle: "逻辑分析仪",
      logicSubtitle: "面向 RP2350 PIO/DMA 的示波器式采集工作台：通道、触发、游标、测量、导出和协议解码集中在一页。",
      fit: "适配",
      run: "运行",
      stop: "停止",
      configure: "配置",
      capture: "单次采集",
      waiting: "等待中",
      export: "导出",
      saveSession: "保存会话",
      loadSession: "载入会话",
      saveEvidence: "保存证据",
      sampleRate: "采样率",
      preSamples: "预采样",
      postSamples: "后采样",
      logicPull: "默认偏置",
      sampleMode: "采样模式",
      singleCapture: "单次采集",
      repeatedWindows: "连续短窗口",
      longStream: "长流模式（需桥接支持）",
      zoomIn: "放大",
      zoomOut: "缩小",
      cursorA: "游标 A",
      cursorB: "游标 B",
      channels: "通道",
      noneShort: "无",
      invert: "反选",
      triggerModes: "触发窗口",
      trigger: "触发",
      triggerGP: "触发 GP",
      channelPin: "通道引脚",
      channelPull: "偏置",
      channelInvert: "反相",
      channelTrigger: "触发",
      addChannel: "添加",
      clearChannels: "清空",
      removeChannel: "删除",
      triggerIgnore: "忽略",
      triggerHigh: "高",
      triggerLow: "低",
      triggerRise: "升",
      triggerFall: "降",
      triggerPatternHigh: "P1",
      triggerPatternLow: "P0",
      errorTitle: "操作失败",
      ok: "确定",
      levelHigh: "高电平",
      levelLow: "低电平",
      risingEdge: "上升沿",
      fallingEdge: "下降沿",
      patternTrigger: "Pattern",
      fastPatternRequired: "Fast Pattern（需固件）",
      blastRequired: "Blast（需固件）",
      patternBase: "Pattern 起始",
      patternBits: "Pattern 位",
      searchSamples: "搜索样本",
      burstCount: "Burst 标记",
      gpioLive: "GPIO 实时",
      read: "读取",
      pins: "引脚",
      decodeExport: "协议解码 / 导出",
      decode: "解码",
      copyExport: "复制导出",
      useCursors: "使用游标",
      settings: "设置",
      regionStart: "起始",
      regionEnd: "结束",
      regionEndPlaceholder: "结束",
      decoder: "解码器",
      autoBaud: "自动",
      summary: "摘要",
      bursts: "Burst 标记",
      edges: "边沿",
      sigrokBridge: "Sigrok 桥接",
      device: "设备",
      deviceChannel: "接口",
      addDevice: "新增",
      closeDevice: "关闭",
      active: "已打开",
      inactive: "未打开",
      noFreeHardware: "该协议的硬件实例都已经列出，不能继续新增。",
      firmware: "固件",
      transport: "连接",
      probe: "探测",
      mode: "模式",
      scan: "扫描",
      networkSetup: "网络配置",
      apMode: "AP 模式",
      savedWifi: "已保存 Wi-Fi",
      profileSlot: "配置槽位",
      ssid: "SSID",
      password: "密码",
      saveWifi: "保存",
      saveAndConnect: "保存并连接",
      connectSaved: "连接已保存",
      clearSlot: "清除槽位",
      hotspotConfig: "热点配网",
      hotspotActive: "AP 热点已开启",
      hotspotInactive: "AP 热点未开启",
      hotspotHint: "手机或其它设备连接该热点后，打开配置页面即可配网。",
      apSsid: "AP SSID",
      apIp: "AP IP",
      apPassword: "AP 密码",
      setupUrl: "配置地址",
      stationNetwork: "联网 Wi-Fi",
      profileEmpty: "空",
      profileActive: "当前",
      profileSaved: "已保存",
      wifiSaved: "Wi-Fi 配置已保存",
      wifiSavedConnected: "Wi-Fi 配置已保存并连接",
      wifiConnected: "已连接保存的 Wi-Fi",
      wifiCleared: "Wi-Fi 配置已清除",
      apStarted: "AP 配网模式已开启",
      ssidRequired: "请填写 SSID。",
      pull: "上下拉",
      up: "上拉",
      down: "下拉",
      write: "写入",
      transfer: "传输",
      spiCaptureWiring: "需要看 SPI 波形时，逻辑分析仪采 GP16=SCK、GP17=MOSI、GP18=MISO、GP19=CS，并把当前选择的原生 SPI 引脚接到这些监听脚。",
      bridgeOffline: "本地 RP2350 桥接服务不可用，请先启动监控桥接服务后重试。",
      instance: "实例",
      baud: "波特率",
      address: "地址",
      readLen: "读取长度",
      writeHex: "写入 HEX",
      transferHex: "传输 HEX",
      aiQueue: "AI 操作队列",
      demoOperation: "演示操作",
      contract: "协议",
      last: "状态",
      ready: "就绪",
      liveCapture: "实时采集",
      board: "开发板",
      endpoint: "端点",
      logic: "逻辑",
      gpio: "GPIO",
      captureComplete: "采集完成",
      captureNoEdges: "采集完成：当前通道未检测到电平变化。",
      captureNoEdgesHint: "本次窗口内没有边沿。手动接线变化请使用触发模式，或降低采样率/增加样本数。",
      levelTriggerNoEdgesHint: "高/低是电平条件，信号已处于目标电平时会立即命中；要看实际跳变请使用升/降触发。",
      configured: "已配置",
      idle: "空闲",
      noScanResults: "暂无扫描结果。",
      high: "高",
      low: "低",
      readGPIOHint: "读取 GPIO 后显示实时电平。",
      selected: "已选",
      hardwareWindow: "硬件窗口",
      selection: "选择",
      sparse: "稀疏",
      contiguous: "连续",
      selectAtLeastOne: "至少选择一个采集通道。",
      sparseNote: "稀疏选择会按桌面逻辑分析仪展示；当前固件实际采集包含这些通道的连续 GPIO 窗口。",
      directNote: "当前选择可以直接映射到一个固件采集窗口。",
      triggerExpandedNote: "触发 GP 已自动纳入硬件采集窗口；波形仍只显示用户选择的通道。",
      patternMultiHint: "Pattern 是多通道触发模式：bit0 对应 Pattern 起始，后续位依次对应连续 GPIO。",
      unsupportedPin: "不是已开放引脚；请拆分采集或选择连续开放范围。",
      noCapture: "暂无采集数据。点击配置、单次采集或运行后显示数字波形。",
      waitingForTrigger: "正在等待触发或采样上传。触发条件满足后才会绘制波形。",
      openEndedTriggerNoPre: "开放等待触发时只采集触发后窗口。",
      captureInProgress: "正在采集，数据上传完成后绘制波形。",
      configuredNoCapture: "配置已就绪。点击运行或单次采集后获取波形。",
      channelAdded: "已添加通道。",
      channelRemoved: "已删除通道。",
      channelsCleared: "已清空所有通道。",
      cursorHint: "采集后显示游标 A/B 和边沿测量。",
      rate: "采样率",
      samples: "样本",
      window: "窗口",
      runState: "运行",
      memory: "内存",
      maxSamples: "最大样本",
      stopped: "停止",
      live: "实时",
      bUnset: "B 未设",
      logicStopped: "逻辑采集已停止",
      runStarted: "实时逻辑采集已启动",
      runStopped: "实时逻辑采集已停止",
      wifiScanComplete: "Wi-Fi 扫描完成",
      probeComplete: "模拟探测完成",
      logicCapsLoaded: "逻辑分析仪能力已读取",
      noCaptureDecode: "没有可解码的采集数据。",
      noCaptureExport: "没有可导出的逻辑采集数据。",
      exportGenerated: "逻辑导出已生成",
      noExportContent: "没有可复制的导出内容。",
      exportCopied: "导出内容已复制",
      decodeComplete: "逻辑解码完成",
      regionFromCursors: "已按游标设置区域",
      invalidRegion: "采样区域无效。",
      settingsGenerated: "采集设置已生成",
      noEdges: "无边沿",
      noUART: "未解码到 UART 帧。",
      noSPI: "未解码到 SPI 字节。",
      noI2C: "未解码到 I2C 帧。",
      sigrokHint: "Sigrok 集成属于主机侧扩展点。当前可先导出 VCD/CSV。",
      triggerFirmwareRequired: "触发模式当前只作为规划控件展示，执行采集前需要固件支持。",
      triggerRequiredForAdvanced: "预触发或多 Burst 采集需要选择边沿、电平或 Pattern 触发。",
      preSampleNeedsTrigger: "预采样会在配置通道触发后生效；未配置触发时本次会忽略预采样。",
      preSampleNeedsTriggerForCapture: "预采样只在配置通道触发后生效。本次会按普通立即采集执行，不保留触发前样本。",
      preSampleIgnoredShort: "预采样已忽略：未设置触发",
      statusNoEdgesShort: "采集完成：无电平变化",
      invalidPatternBits: "Pattern 位只能包含 0 或 1。",
      invalidPatternWindow: "Pattern 必须落在本次硬件采集 GPIO 窗口内。",
      singleEdgeTriggerOnly: "边沿触发一次只能选择一个通道。多通道组合触发请使用高/低电平 Pattern。",
      noBursts: "本次采集没有记录触发或 Burst 标记。",
      triggerGuideNone: "无触发：点击采集后立即开始。",
      triggerGuideCurrent: "当前",
      triggerGuideUnknown: "当前电平未知，可先点击 GPIO 读取做一次现场检查",
      triggerGuideHigh: "该引脚为高电平时会触发",
      triggerGuideLow: "该引脚为低电平时会触发",
      triggerGuideRising: "上升沿需要先为低电平，再变为高电平",
      triggerGuideFalling: "下降沿需要先为高电平，再变为低电平",
      triggerGuidePattern: "Pattern 会比较连续采集窗口内的相对 bit",
      presetManual: "普通逻辑",
      presetUART: "UART",
      presetI2C: "I2C",
      presetSPI: "SPI",
      presetApplied: "已应用预设",
      protocolPresets: "协议预设",
      protocolPresetHint: "预设只快速填写通道、触发和解码参数，不是单独的硬件触发模式。",
      triggerSourceHint: "多通道触发在通道列表的“触发”列设置。高/低/P1/P0 会组合为 Pattern 触发；升/降一次只允许一个通道。",
      triggerNoArmedChannels: "未配置通道触发：采集会立即开始。",
      armedTriggers: "已配置触发",
      configLockedWhileRunning: "采集正在运行，请先停止后再修改配置。",
      preSampleArmed: "预采样会保留触发点之前的样本。",
      preSampleWaiting: "正在等待触发；命中触发后才会绘制波形。",
      sessionSaved: "分析仪会话已保存。",
      sessionLoaded: "分析仪会话已载入。",
      invalidSession: "选择的分析仪会话文件无效。",
      evidenceSaved: "分析仪证据包已保存。"
      ,systemState: "系统状态",
      runtimeResources: "运行资源",
      activeInterfaces: "活动接口",
      connection: "连接状态",
      wifiStation: "Wi-Fi STA",
      wifiAP: "Wi-Fi AP",
      stationIP: "STA IP",
      channelCount: "通道数",
      eventBuffer: "事件缓冲",
      logicEngine: "逻辑引擎",
      captureBuffer: "采集缓冲",
      noActiveInterfaces: "当前没有活动协议通道。",
      uartAssistant: "UART 串口助手",
      i2cAssistant: "I2C 总线助手",
      spiAssistant: "SPI 总线助手",
      openPort: "打开串口",
      openBus: "打开总线",
      send: "发送",
      frame: "帧格式",
      sendMode: "发送模式",
      payloadMode: "载荷模式",
      displayMode: "显示方式",
      textMode: "文本",
      binaryMode: "二进制",
      terminalMode: "终端",
      decodedMode: "解析",
      lineEnding: "行尾",
      sendPayload: "发送内容",
      writePayload: "写入内容",
      transferPayload: "传输内容",
      localEcho: "回显已发送数据",
      clearLog: "清空日志",
      terminal: "终端",
      transferType: "传输类型",
      writeRead: "先写后读",
      writeOnly: "只写",
      readOnly: "只读",
      i2cTransactions: "事务记录",
      spiTransactions: "事务记录",
      aiPurpose: "AI 集成",
      generateExample: "生成示例",
      aiPurposeOneTitle: "这个页面的作用",
      aiPurposeOneBody: "这里是插件和大模型使用的统一操作协议，不是单独示例功能；AI 通过它调用与用户相同的硬件调试能力。",
      aiPurposeTwoTitle: "AI 如何使用",
      aiPurposeTwoBody: "AI 可以调用 runOperation 配置采集、执行协议解码、导出证据，并让界面保持可见，方便用户检查每一步。",
      aiPurposeThreeTitle: "边界规则",
      aiPurposeThreeBody: "界面不臆造硬件行为，只产生规范化操作；本地桥接负责 USB/Wi-Fi 链路和固件协议执行。",
      bytesTx: "发送",
      bytesRx: "接收",
      frames: "帧",
      noEvents: "暂无事件。"
      ,aiQueueEmpty: "AI 操作会显示在这里。",
      operationDetail: "调用详情",
      fixedPins: "固定引脚",
      nativeMapping: "原生映射",
      txPin: "TX 引脚",
      rxPin: "RX 引脚",
      sdaPin: "SDA 引脚",
      sclPin: "SCL 引脚",
      sckPin: "SCK 引脚",
      mosiPin: "MOSI 引脚",
      misoPin: "MISO 引脚",
      csPin: "CS 引脚",
      custom: "自定义",
      customBaud: "自定义波特率",
      call: "调用",
      startedAt: "开始时间",
      duration: "耗时",
      parameters: "参数",
      result: "结果",
      error: "错误",
      selectAICall: "从左侧列表选择一次调用查看详情。"
    }
  };
  const localActions = new Set([
    "logic.fit",
    "logic.run",
    "logic.stop",
    "logic.zoom-in",
    "logic.zoom-out",
    "logic.cursor-a",
    "logic.cursor-b",
    "logic.region-from-cursors",
    "logic.decode",
    "logic.copy-export",
    "session.save",
    "session.load",
    "evidence.save",
    "ai.demo"
  ]);

  const operationTemplates = {
    "probe": { action: "probe", params: {}, ui: { show: true, panel: "overview" } },
    "wifi.scan": { action: "wifi.scan", params: {}, ui: { show: true, panel: "overview" } },
    "wifi.save": { action: "wifi.save", params: { slot: 0, ssid: "", password: "", save: true }, ui: { show: true, panel: "overview" } },
    "wifi.save-connect": { action: "wifi.save-connect", params: { slot: 0, ssid: "", password: "", save: true }, ui: { show: true, panel: "overview" } },
    "wifi.connect": { action: "wifi.connect", params: { slot: 0 }, ui: { show: true, panel: "overview" } },
    "wifi.clear": { action: "wifi.clear", params: { slot: 0 }, ui: { show: true, panel: "overview" } },
    "wifi.ap": { action: "wifi.ap", params: {}, ui: { show: true, panel: "overview" } },
    "logic.caps": { action: "logic.caps", params: {}, ui: { show: true, panel: "logic" } },
    "gpio.read": { action: "gpio.read", params: { pins: [16, 17], pull: "none" }, ui: { show: true, panel: "logic" } },
    "logic.configure": {
      action: "logic.configure",
      params: { pin_base: 16, pin_count: 4, sample_rate: 1000000, samples: 4096, pre_samples: 0, post_samples: 4096, search_samples: 0, burst_count: 1, selected_pins: [16, 17, 18, 19] },
      ui: { show: true, panel: "logic" }
    },
    "logic.capture": {
      action: "logic.capture",
      params: { pin_base: 16, pin_count: 4, sample_rate: 1000000, samples: 4096, pre_samples: 0, post_samples: 4096, search_samples: 0, burst_count: 1, selected_pins: [16, 17, 18, 19], auto_read: true },
      ui: { show: true, panel: "logic" }
    },
    "logic.run": { action: "logic.run", params: {}, ui: { show: true, panel: "logic" } },
    "logic.stop": { action: "logic.stop", params: {}, ui: { show: true, panel: "logic" } },
    "logic.export": { action: "logic.export", params: { format: "vcd" }, ui: { show: true, panel: "logic" } },
    "logic.region-from-cursors": { action: "logic.region-from-cursors", params: {}, ui: { show: true, panel: "logic" } },
    "session.export": { action: "session.export", params: {}, ui: { show: true, panel: "logic" } },
    "session.import": { action: "session.import", params: {}, ui: { show: true, panel: "logic" } },
    "evidence.export": { action: "evidence.export", params: {}, ui: { show: true, panel: "logic" } },
    "uart.configure": { action: "uart.configure", params: { id: 1, instance: 1, tx: 8, rx: 9, baud: 115200 }, ui: { show: true, panel: "uart" } },
    "uart.write": { action: "uart.write", params: { id: 1, hex: "48656c6c6f" }, ui: { show: true, panel: "uart" } },
    "i2c.configure": { action: "i2c.configure", params: { id: 3, instance: 0, sda: 4, scl: 5, baud: 100000 }, ui: { show: true, panel: "i2c" } },
    "i2c.transfer": { action: "i2c.transfer", params: { id: 3, addr: "0x50", write: "00", read_len: 16 }, ui: { show: true, panel: "i2c" } },
    "spi.configure": { action: "spi.configure", params: { id: 2, instance: 0, sck: 2, mosi: 3, miso: 0, cs: 1, baud: 1000000 }, ui: { show: true, panel: "spi" } },
    "spi.transfer": { action: "spi.transfer", params: { id: 2, hex: "9f000000", read_len: 4 }, ui: { show: true, panel: "spi" } }
  };

  const state = {
    client: selectClient(),
    snapshot: null,
    queue: [],
    activePanel: new URLSearchParams(location.search).get("panel") || "overview",
    channels: makeDefaultChannels(),
    protocolDevices: makeDefaultProtocolDevices(),
    protocolSelection: { uart: 1, i2c: 3, spi: 2 },
    zoom: 1,
    viewStart: 0,
    cursorA: 0,
    cursorB: null,
    cursorTarget: "a",
    canvasDrag: null,
    cursorDrag: null,
    overviewDrag: null,
    suppressCanvasClick: false,
    suppressOverviewClick: false,
    capturePending: false,
    pendingCaptureToken: null,
    capturePollTimer: null,
    lastCaptureKey: "",
    userViewLocked: false,
    logicRunActive: false,
    logicRunIteration: 0,
    language: new URLSearchParams(location.search).get("lang") || localStorage.getItem("embedlabs.rpmon.lang") || "zh",
    appliedLanguage: "",
    activePreset: "",
    logicStatus: "",
    logicStatusTone: "",
    cursorColors: { a: "#f6c85f", b: "#ff7a7a" },
    lastExport: "",
    decoderAnnotations: [],
    notices: [],
    selectedQueueIndex: 0
  };

  const $ = (id) => document.getElementById(id);
  const read = (id) => $(id)?.value ?? "";

  if (!translations[state.language]) state.language = "zh";

  function normalizeOperation(action, params = {}, ui = {}) {
    const template = operationTemplates[action] ?? { action, params: {}, ui: { show: true, panel: "overview" } };
    return {
      type: operationSchemaVersion,
      action: template.action,
      params: { ...template.params, ...params },
      ui: { ...template.ui, ...ui }
    };
  }

  function operationFromForm(action) {
    switch (action) {
      case "wifi.save":
      case "wifi.save-connect": {
        const ssid = read("wifiSsid").trim();
        if (!ssid) throw new Error(t("ssidRequired"));
        return normalizeOperation(action, {
          slot: Number(read("wifiSlot") || 0),
          ssid,
          password: read("wifiPassword"),
          save: true
        }, { panel: "overview" });
      }
      case "wifi.connect":
      case "wifi.clear":
        return normalizeOperation(action, { slot: Number(read("wifiSlot") || 0) }, { panel: "overview" });
      case "gpio.read":
        return normalizeOperation(action, {
          pins: read("gpioPins").split(",").map((item) => Number(item.trim())).filter(Number.isFinite),
          pull: read("gpioPull")
        });
      case "logic.configure":
        return normalizeOperation(action, logicParamsFromForm({ strictPreTrigger: false }), { panel: "logic" });
      case "logic.capture": {
        const params = logicParamsFromForm({ strictPreTrigger: true });
        if (captureWaitsForTrigger(params)) {
          params.nonblocking = true;
          const caps = state.snapshot?.logic_caps || {};
          const supportsOpenEndedPreTrigger = Boolean(caps.ring_pretrigger && caps.open_ended_trigger_wait);
          if (!supportsOpenEndedPreTrigger && Number(params.pre_samples || 0) > 0) {
            params.pre_samples_open_ended = Number(params.pre_samples);
            params.pre_samples = 0;
            params.samples = Math.max(1, Number(params.post_samples || params.samples || 4096));
          }
        }
        return normalizeOperation(action, params, { panel: "logic" });
      }
      case "logic.export":
        return normalizeOperation(action, { format: read("logicExportFormat") || "vcd" }, { panel: "logic" });
      case "uart.configure":
        syncSelectedProtocolFromControls("uart");
        return normalizeOperation(action, protocolConfigFromForm("uart"));
      case "uart.write":
        syncSelectedProtocolFromControls("uart");
        return normalizeOperation(action, { ...protocolConfigFromForm("uart"), hex: protocolPayloadHex("uartPayload", read("uartSendMode"), read("uartLineEnding")) });
      case "i2c.configure":
        syncSelectedProtocolFromControls("i2c");
        return normalizeOperation(action, protocolConfigFromForm("i2c"));
      case "i2c.transfer": {
        syncSelectedProtocolFromControls("i2c");
        const type = read("i2cTransferType") || "write-read";
        const write = type === "read" ? "" : protocolPayloadHex("i2cPayload", read("i2cPayloadMode"), "none");
        const readLen = type === "write" ? 0 : Number(read("i2cReadLen"));
        return normalizeOperation(action, { ...protocolConfigFromForm("i2c"), addr: read("i2cAddress"), write, read_len: readLen });
      }
      case "spi.configure":
        syncSelectedProtocolFromControls("spi");
        return normalizeOperation(action, protocolConfigFromForm("spi"));
      case "spi.transfer": {
        syncSelectedProtocolFromControls("spi");
        return normalizeOperation(action, spiTransferParamsFromForm());
      }
      default:
        return normalizeOperation(action);
    }
  }

  function spiTransferParamsFromForm() {
    const selected = selectedProtocolDevice("spi");
    return {
      id: selected.id,
      instance: protocolInstanceValue("spi", selected),
      sck: Number(read("spiSck") || 2),
      mosi: Number(read("spiMosi") || 3),
      miso: Number(read("spiMiso") || 0),
      cs: Number(read("spiCs") || 1),
      baud: Number(read("spiBaud") || 1000000),
      hex: protocolPayloadHex("spiPayload", read("spiPayloadMode"), "none"),
      read_len: Number(read("spiReadLen"))
    };
  }

  function logicParamsFromForm(options = {}) {
    const selectedPins = selectedChannelPins();
    if (!selectedPins.length) {
      throw new Error(t("selectAtLeastOne"));
    }
    const channelConfigs = selectedChannelConfigs();
    const preSamplesRequested = positiveInteger(read("logicPreSamples"), 0);
    let preSamples = preSamplesRequested;
    const postSamples = positiveInteger(read("logicPostSamples"), 4096);
    const searchSamples = positiveInteger(read("logicSearchSamples"), 0);
    const burstMax = Math.max(1, Number(state.snapshot?.logic_caps?.burst_marks_max ?? defaultLogicCaps().burst_marks_max ?? 16));
    const burstCount = clamp(positiveInteger(read("logicBurstCount"), 1), 1, burstMax);
    const sampleRateRequested = positiveInteger(read("logicSampleRate"), 1000000);
    const sampleRateMax = logicSampleRateMax();
    const sampleRate = clamp(sampleRateRequested, 1, sampleRateMax);
    const channelTrigger = channelTriggerFromConfigs(channelConfigs);
    const triggerType = channelTrigger?.trigger_type || read("logicTriggerType") || "none";
    if (!supportedTriggerTypes.has(triggerType)) {
      throw new Error(`${triggerType} ${t("triggerFirmwareRequired")}`);
    }
    if (triggerType === "none" && preSamplesRequested > 0) {
      preSamples = 0;
    }
    if (triggerType === "none" && burstCount > 1) {
      throw new Error(t("triggerRequiredForAdvanced"));
    }
    if (sampleRate !== sampleRateRequested) setValue("logicSampleRate", sampleRate);
    let triggerPin = null;
    let pattern = null;
    let hardwarePins = [...selectedPins];
    if (channelTrigger?.mode === "pattern") {
      hardwarePins = [...new Set(hardwarePins.concat(channelTrigger.pins))].sort((a, b) => a - b);
    } else if (channelTrigger?.mode === "single") {
      triggerPin = channelTrigger.pin;
      if (!hardwarePins.includes(triggerPin)) {
        hardwarePins.push(triggerPin);
        hardwarePins.sort((a, b) => a - b);
      }
    } else if (triggerType === "pattern") {
      pattern = parsePatternInput(selectedPins[0]);
      hardwarePins = [...new Set(hardwarePins.concat(pattern.pins))].sort((a, b) => a - b);
    } else if (triggerType !== "none") {
      triggerPin = positiveInteger(read("logicTriggerPin"), selectedPins[0]);
      if (!exposedPinSet.has(triggerPin)) {
        throw new Error(`GP${triggerPin} ${t("unsupportedPin")}`);
      }
      if (!hardwarePins.includes(triggerPin)) {
        hardwarePins.push(triggerPin);
        hardwarePins.sort((a, b) => a - b);
      }
    }
    const hardwareWindow = hardwareWindowForPins(hardwarePins);
    const pullConfig = pullConfigFor(channelConfigs, hardwareWindow.base);
    const visiblePins = [...selectedPins];
    const addVisiblePin = (pin) => {
      if (Number.isFinite(pin) && !visiblePins.includes(pin)) visiblePins.push(pin);
    };
    if (triggerPin !== null) addVisiblePin(triggerPin);
    if (channelTrigger?.mode === "pattern") channelTrigger.pins.forEach(addVisiblePin);
    if (pattern) pattern.pins.forEach(addVisiblePin);
    visiblePins.sort((a, b) => a - b);
    const params = {
      pin_base: hardwareWindow.base,
      pin_count: hardwareWindow.count,
      sample_rate: sampleRate,
      samples: Math.max(1, preSamples + postSamples),
      pre_samples: preSamples,
      post_samples: postSamples,
      search_samples: searchSamples,
      burst_count: burstCount,
      selected_pins: selectedPins,
      visible_pins: visiblePins,
      capture_channels: channelConfigs.map(({ pin, name, color, pull, invert, trigger }) => ({ pin, name, color, pull, invert, trigger })),
      sample_mode: read("logicSampleMode") || "single",
      pull: read("logicPull") || "none",
      pin_pulls: pullConfig.pinPulls,
      trigger_type: triggerType,
      sample_rate_requested: sampleRateRequested,
      sample_rate_limited: sampleRate !== sampleRateRequested,
      sample_rate_max: sampleRateMax,
      pre_samples_requested: preSamplesRequested,
      pre_samples_armed: triggerType !== "none" || preSamplesRequested === 0,
      pre_samples_ignored: triggerType === "none" && preSamplesRequested > 0,
      trigger_included_in_window: triggerPin !== null && !selectedPins.includes(triggerPin),
      pattern_included_in_window: pattern ? pattern.pins.some((pin) => !selectedPins.includes(pin)) : false,
      pin_configs: channelConfigs.map(({ pin, name, color, pull, invert, trigger }) => ({ pin, name, color, pull, invert, trigger }))
    };

    if (pullConfig.uniform) {
      params.pull = pullConfig.uniform;
    } else if (pullConfig.enabled) {
      params.pull = "none";
    }
    if (hardwareWindow.sparse) {
      params.host_note = `Current firmware captures contiguous GP${hardwareWindow.base}..GP${hardwareWindow.base + hardwareWindow.count - 1}; unselected pins are hidden in the viewer.`;
    }
    if (channelTrigger?.mode === "pattern") {
      const patternMask = patternMaskValueFromConditions(channelTrigger.conditions, hardwareWindow.base, hardwareWindow.count);
      params.trigger_mode = "pattern";
      params.trigger_mask = patternMask.mask;
      params.trigger_value = patternMask.value;
      params.pattern_base = hardwareWindow.base;
      params.pattern_bits = patternMask.label;
    } else if (triggerType === "pattern") {
      const patternMask = patternMaskValue(pattern, hardwareWindow.base, hardwareWindow.count);
      params.trigger_mode = "pattern";
      params.trigger_mask = patternMask.mask;
      params.trigger_value = patternMask.value;
      params.pattern_base = pattern.base;
      params.pattern_bits = pattern.bits;
    } else if (triggerType !== "none") {
      params.trigger_pin = triggerPin;
      if (triggerType === "rising" || triggerType === "falling") {
        params.trigger_mode = triggerType;
      } else {
        params.trigger_mode = "level";
        params.trigger_level = triggerType === "level-high";
      }
    }
    return params;
  }

  function parsePatternInput(fallbackBase = 16) {
    const base = positiveInteger(read("logicPatternBase"), fallbackBase);
    const bits = String(read("logicPattern") || "").replace(/[\s_]/g, "");
    if (!bits || !/^[01]+$/.test(bits)) {
      throw new Error(t("invalidPatternBits"));
    }
    const maxBits = Math.max(1, Number(state.snapshot?.logic_caps?.pattern_mask_bits_max ?? defaultLogicCaps().pattern_mask_bits_max ?? 23));
    if (bits.length > maxBits) {
      throw new Error(`${t("invalidPatternWindow")} ${bits.length}/${maxBits}`);
    }
    const pins = Array.from({ length: bits.length }, (_, index) => base + index);
    for (const pin of pins) {
      if (!exposedPinSet.has(pin)) throw new Error(`GP${pin} ${t("unsupportedPin")}`);
    }
    return { base, bits, pins };
  }

  function patternMaskValue(pattern, pinBase, pinCount) {
    const offset = pattern.base - pinBase;
    if (offset < 0 || offset + pattern.bits.length > pinCount) {
      throw new Error(t("invalidPatternWindow"));
    }
    let mask = 0;
    let value = 0;
    for (let index = 0; index < pattern.bits.length; index += 1) {
      const bit = offset + index;
      mask += 2 ** bit;
      if (pattern.bits[index] === "1") value += 2 ** bit;
    }
    return { mask, value };
  }

  function patternMaskValueFromConditions(conditions, pinBase, pinCount) {
    let mask = 0;
    let value = 0;
    const triggerByPin = new Map((conditions || []).map((condition) => [Number(condition.pin), condition.trigger]));
    const label = [];
    for (let pin = pinBase; pin < pinBase + pinCount; pin += 1) {
      const trigger = triggerByPin.get(pin);
      if (!trigger) {
        label.push("x");
        continue;
      }
      const bit = pin - pinBase;
      mask += 2 ** bit;
      const wantsHigh = trigger === "high" || trigger === "p1";
      label.push(wantsHigh ? "1" : "0");
      if (wantsHigh) value += 2 ** bit;
    }
    return { mask, value, label: label.join("") };
  }

  function selectedChannelConfigs() {
    return state.channels
      .filter((channel) => channel.selected)
      .sort((a, b) => a.pin - b.pin)
      .map((channel) => ({
        pin: channel.pin,
        name: channel.name || `GP${channel.pin}`,
        color: channel.color || "#42d392",
        pull: channel.pull || "none",
        invert: Boolean(channel.invert),
        trigger: channel.trigger || "ignore"
      }));
  }

  function pullConfigFor(configs, pinBase) {
    const pinPulls = {};
    const pulls = new Set();
    for (const config of configs) {
      const pull = config.pull || "none";
      pulls.add(pull);
      pinPulls[String(config.pin)] = pull;
    }
    return {
      enabled: true,
      pinPulls,
      uniform: pulls.size === 1 ? [...pulls][0] : ""
    };
  }

  function channelTriggerFromConfigs(configs) {
    const armed = configs.filter((config) => config.trigger && config.trigger !== "ignore");
    if (!armed.length) return null;
    const edge = armed.filter((config) => config.trigger === "rise" || config.trigger === "fall");
    if (edge.length) {
      if (armed.length > 1) throw new Error(t("singleEdgeTriggerOnly"));
      return {
        mode: "single",
        pin: edge[0].pin,
        trigger_type: edge[0].trigger === "rise" ? "rising" : "falling"
      };
    }
    if (armed.length === 1 && (armed[0].trigger === "high" || armed[0].trigger === "low")) {
      return {
        mode: "single",
        pin: armed[0].pin,
        trigger_type: armed[0].trigger === "high" ? "level-high" : "level-low"
      };
    }
    const window = hardwareWindowForPins(armed.map((config) => config.pin));
    let mask = 0;
    let value = 0;
    const label = [];
    for (let pin = window.base; pin < window.base + window.count; pin += 1) {
      const config = armed.find((item) => item.pin === pin);
      const bit = pin - window.base;
      if (!config) {
        label.push("x");
        continue;
      }
      mask += 2 ** bit;
      const wantsHigh = config.trigger === "high" || config.trigger === "p1";
      label.push(wantsHigh ? "1" : "0");
      if (wantsHigh) value += 2 ** bit;
    }
    return {
      mode: "pattern",
      trigger_type: "pattern",
      pins: armed.map((config) => config.pin),
      conditions: armed.map((config) => ({ pin: config.pin, trigger: config.trigger })),
      mask,
      value,
      label: label.join("")
    };
  }

  function selectClient() {
    const params = new URLSearchParams(location.search);
    const apiURL = params.get("api");
    const wsURL = params.get("ws");
    if (window.embedLabsRpmon?.invoke) return { invoke: (operation) => window.embedLabsRpmon.invoke(operation) };
    if (apiURL) {
      return {
        invoke: async (operation) => {
          const response = await fetch(apiURL, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ operation })
          });
          if (!response.ok) throw new Error(`HTTP ${response.status}`);
          return response.json();
        }
      };
    }
    if (wsURL) return new WebSocketRpmonClient(wsURL);
    return new MockRpmonClient();
  }

  function WebSocketRpmonClient(url) {
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

  WebSocketRpmonClient.prototype.invoke = async function (operation) {
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
  };

  function MockRpmonClient() {
    this.captureId = 1;
    this.levels = new Map([[16, true], [17, false], [18, false], [19, true]]);
  }

  MockRpmonClient.prototype.invoke = async function (operation) {
    await new Promise((resolve) => setTimeout(resolve, 160));
    const op = normalizeOperation(operation.action, operation.params, operation.ui);
    if (op.action === "probe") return { ok: true, snapshot: this.snapshot({ lastResponse: "Mock probe complete" }) };
    if (op.action === "wifi.scan") return { ok: true, snapshot: this.snapshot({ wifi: mockWifi(), lastResponse: "Wi-Fi scan complete" }) };
    if (op.action === "wifi.save") return { ok: true, snapshot: this.snapshot({ wifi_status: mockWifiStatus({ ssid: op.params.ssid, station_status: "saved" }), lastResponse: t("wifiSaved") }) };
    if (op.action === "wifi.save-connect") return { ok: true, snapshot: this.snapshot({ wifi_status: mockWifiStatus({ ssid: op.params.ssid, station_status: "up", station_ip: "192.168.3.97", ap_active: false }), lastResponse: t("wifiSavedConnected") }) };
    if (op.action === "wifi.connect") return { ok: true, snapshot: this.snapshot({ wifi_status: mockWifiStatus({ station_status: "up", station_ip: "192.168.3.97", ap_active: false }), lastResponse: t("wifiConnected") }) };
    if (op.action === "wifi.clear") return { ok: true, snapshot: this.snapshot({ wifi_status: mockWifiStatus({ ssid_configured: false, ssid: "", station_status: "down", station_ip: "0.0.0.0" }), lastResponse: t("wifiCleared") }) };
    if (op.action === "wifi.ap") return { ok: true, snapshot: this.snapshot({ wifi_status: mockWifiStatus({ ap_active: true, station_status: "down", station_ip: "0.0.0.0" }), lastResponse: t("apStarted") }) };
    if (op.action === "logic.caps") return { ok: true, snapshot: this.snapshot({ logic_caps: defaultLogicCaps(), lastResponse: "Logic capabilities loaded" }) };
    if (op.action === "gpio.read") {
      for (const pin of op.params.pins ?? []) {
        if (!this.levels.has(pin)) this.levels.set(pin, false);
      }
      return { ok: true, snapshot: this.snapshot({ gpio: [...this.levels.entries()].map(([pin, level]) => ({ pin, level })), lastResponse: "GPIO levels updated" }) };
    }
    if (op.action === "logic.capture") {
      const logic = logicState(op.params, true);
      logic.capture_id = this.captureId++;
      logic.words = pseudoLogicWords(logic.pin_count, logic.samples);
      return { ok: true, snapshot: this.snapshot({ logic, lastResponse: "Logic capture complete" }) };
    }
    if (op.action === "logic.configure") return { ok: true, snapshot: this.snapshot({ logic: logicState(op.params, false), lastResponse: "Logic configured" }) };
    return { ok: true, snapshot: this.snapshot({ lastResponse: `${op.action} accepted by mock client`, events: [{ type: "event", proto: op.action.split(".")[0], dir: "mock", hex: "01020304" }] }) };
  };

  MockRpmonClient.prototype.snapshot = function (patch = {}) {
    return {
      device: { board: "Pico 2 W", firmware: "0.5.0", transport: "USB CDC", endpoint: "/dev/cu.usbmodemXXXX" },
      wifi_status: mockWifiStatus(),
      wifi: mockWifi(),
      gpio: [...this.levels.entries()].map(([pin, level]) => ({ pin, level })),
      logic: logicState({ pin_base: 16, pin_count: 4, sample_rate: 1000000, samples: 4096, selected_pins: [16, 17, 18, 19] }, false),
      logic_caps: defaultLogicCaps(),
      channels: [],
      buffers: { event_depth: 0, event_capacity: 128, total_events: 0, dropped_events: 0 },
      events: [],
      lastResponse: "Ready",
      ...patch
    };
  };

  async function runOperation(operation) {
    if (!operation?.action) throw new Error("Operation action is required.");
    const startedAt = new Date();
    const queueItem = { operation, status: "running", startedAt };
    const waitsForTrigger = operation.action === "logic.capture" && captureWaitsForTrigger(operation.params);
    const captureToken = operation.action === "logic.capture" ? `${Date.now()}:${Math.random()}` : null;
    state.queue.unshift(queueItem);
    state.selectedQueueIndex = 0;
    renderQueue();
    if (operation.ui?.panel && !operation.params?.continuous_run) setPanel(operation.ui.panel);
    if (operation.action === "logic.configure") {
      clearCapturePoll();
      state.capturePending = false;
      state.pendingCaptureToken = null;
    }
    if (operation.action === "logic.stop") {
      clearCapturePoll();
      state.logicRunActive = false;
      state.capturePending = false;
      state.pendingCaptureToken = null;
      renderRunControls();
    }
    if (operation.action === "logic.capture") {
      const continuousRun = Boolean(operation.params?.continuous_run);
      state.pendingCaptureToken = captureToken;
      state.capturePending = !continuousRun;
      const waitingMessage = continuousRun
        ? `${t("liveCapture")} #${operation.params?.run_index ?? state.logicRunIteration + 1}`
        : waitsForTrigger ? pendingTriggerMessage(operation.params) : t("captureInProgress");
      const displayWaitingMessage = operation.params?.pre_samples_open_ended
        ? `${waitingMessage} ${t("openEndedTriggerNoPre")}`
        : waitingMessage;
      const runIndex = Number(operation.params?.run_index ?? 1);
      const shouldClearForCapture = !continuousRun || waitsForTrigger || runIndex <= 1 || !state.snapshot?.logic?.words?.length;
      if (shouldClearForCapture) {
        applySnapshot({
          logic: { ...operation.params, configured: true, running: true, complete: false, words: [] },
          lastResponse: displayWaitingMessage
        });
      } else if (!continuousRun) {
        applySnapshot({ lastResponse: displayWaitingMessage });
      }
      if (!continuousRun || waitsForTrigger) {
        setLogicStatus(displayWaitingMessage, waitsForTrigger ? "" : "ok");
      }
      renderRunControls();
    }
    try {
      const result = await runLocalOperationIfNeeded(operation);
      const finalResult = result ?? await state.client.invoke(operation);
      if (finalResult.ok !== false) {
        markProtocolOperationSuccess(operation.action);
      }
      const staleCapture = captureToken && state.pendingCaptureToken !== captureToken;
      const keepPollingCapture = Boolean(
        captureToken
        && !staleCapture
        && !operation.params?.continuous_run
        && operation.params?.nonblocking
        && finalResult.ok !== false
        && captureWaitsForTrigger(operation.params)
        && !finalResult.snapshot?.logic?.complete
        && !finalResult.snapshot?.logic?.words?.length
      );
      if (captureToken && !staleCapture && !operation.params?.continuous_run && !keepPollingCapture) {
        clearCapturePoll();
        state.capturePending = false;
        state.pendingCaptureToken = null;
      }
      if (finalResult.snapshot?.logic) {
        if (operation.action.startsWith("logic.")) {
          finalResult.snapshot = {
            ...finalResult.snapshot,
            logic: { ...operation.params, ...finalResult.snapshot.logic }
          };
        } else {
          finalResult.snapshot = { ...finalResult.snapshot };
          delete finalResult.snapshot.logic;
        }
      }
      if (staleCapture) {
        Object.assign(queueItem, { status: "done", result: finalResult, startedAt, endedAt: new Date() });
        renderQueue();
        return finalResult;
      }
      if (finalResult.snapshot) applySnapshot(finalResult.snapshot);
      if (keepPollingCapture) {
        const waitingMessage = operation.params?.pre_samples_open_ended
          ? `${pendingTriggerMessage(operation.params)} ${t("openEndedTriggerNoPre")}`
          : pendingTriggerMessage(operation.params);
        setLogicStatus(waitingMessage, "");
        startCapturePoll(captureToken, operation.params);
      } else if (finalResult.ok === false) {
        const message = finalResult.error || finalResult.snapshot?.lastResponse || "Operation failed";
        if (operation.action.startsWith("logic.")) setLogicStatus(message, "warning");
        showErrorDialog(message);
      } else {
        let message = localizedStatusText(finalResult.snapshot?.lastResponse ?? operation.action);
        const preIgnoredMessage = operation.action === "logic.capture" && operation.params?.pre_samples_ignored
          ? t("preSampleNeedsTriggerForCapture")
          : "";
        if (operation.action === "logic.capture" && operation.params?.pre_samples_ignored) {
          message = `${preIgnoredMessage} ${message}`;
        }
        if (operation.action === "logic.capture" && !operation.params?.continuous_run && state.snapshot?.logic?.words?.length && !logicHasVisibleEdges(state.snapshot.logic)) {
          const noEdgeHint = isLevelTriggeredCapture(state.snapshot.logic) ? t("levelTriggerNoEdgesHint") : t("captureNoEdgesHint");
          message = `${preIgnoredMessage ? `${preIgnoredMessage} ` : ""}${t("captureNoEdges")} ${logicEdgeSummary(state.snapshot.logic)}. ${noEdgeHint}`;
        }
        if (operation.params?.continuous_run) {
          setLogicStatus(`${t("liveCapture")} #${state.logicRunIteration}`, "ok");
        } else if (operation.action.startsWith("logic.")) {
          setLogicStatus(message, "ok");
        } else if (!state.capturePending) {
          setLogicStatus(message, "");
        } else {
          applySnapshot({ lastResponse: message });
        }
      }
      Object.assign(queueItem, { status: finalResult.ok === false ? "failed" : "done", result: finalResult, startedAt, endedAt: new Date() });
      renderQueue();
      return finalResult;
    } catch (error) {
      const message = userFacingErrorMessage(error);
      const staleCapture = captureToken && state.pendingCaptureToken !== captureToken;
      if (captureToken && !staleCapture && !operation.params?.continuous_run) {
        clearCapturePoll();
        state.capturePending = false;
        state.pendingCaptureToken = null;
      }
      Object.assign(queueItem, { status: "failed", error: message, startedAt, endedAt: new Date() });
      renderQueue();
      if (staleCapture) {
        Object.assign(queueItem, { status: "done", error: "", startedAt, endedAt: new Date() });
        renderQueue();
        return { ok: true, snapshot: { lastResponse: t("captureComplete") } };
      }
      renderStatus({ error: message });
      if (operation.params?.continuous_run) {
        setLogicStatus(message, "warning");
      } else if (operation.action.startsWith("logic.")) {
        setLogicStatus(message, "warning");
        showErrorDialog(message);
      } else {
        showErrorDialog(message);
      }
      throw error;
    }
  }

  async function startLogicRun() {
    if (state.logicRunActive) return;
    state.logicRunActive = true;
    state.logicRunIteration = 0;
    const mode = $("logicSampleMode");
    if (mode) mode.value = "repeated";
    setPanel("logic");
    setLogicStatus(t("runStarted"), "ok");
    render();

    while (state.logicRunActive) {
      let operation;
      try {
        const params = logicParamsFromForm();
        params.sample_mode = "repeated";
        params.continuous_run = true;
        params.run_index = state.logicRunIteration + 1;
        operation = normalizeOperation("logic.capture", params, { panel: state.activePanel });
      } catch (error) {
        stopLogicRun(error.message);
        break;
      }

      try {
        state.logicRunIteration += 1;
        await runOperation(operation);
      } catch (error) {
        stopLogicRun(error.message);
        break;
      }

      if (captureWaitsForTrigger(operation.params)) {
        stopLogicRun(t("captureComplete"));
        break;
      }
      if (!state.logicRunActive) break;
      await delay(logicRunDelayMilliseconds(operation.params));
    }
    render();
  }

  function captureWaitsForTrigger(params = {}) {
    const triggerType = String(params.trigger_type || "");
    const triggerMode = String(params.trigger_mode || "");
    return (triggerType && triggerType !== "none") || ["level", "rising", "falling", "pattern"].includes(triggerMode) || Number(params.burst_count ?? 1) > 1;
  }

  function clearCapturePoll() {
    if (state.capturePollTimer) {
      clearTimeout(state.capturePollTimer);
      state.capturePollTimer = null;
    }
  }

  function startCapturePoll(token, params = {}) {
    clearCapturePoll();
    const poll = async () => {
      if (state.pendingCaptureToken !== token || !state.capturePending) return;
      try {
        const result = await state.client.invoke(normalizeOperation("logic.poll", { restart_if_missed: true }, { panel: "logic" }));
        if (state.pendingCaptureToken !== token || !state.capturePending) return;
        if (result.snapshot) applySnapshot(result.snapshot);
        const logic = result.snapshot?.logic;
        const complete = Boolean(logic?.complete || logic?.words?.length);
        if (result.ok === false) {
          setLogicStatus(result.error || result.snapshot?.lastResponse || "logic poll failed", "warning");
          state.capturePollTimer = setTimeout(poll, 600);
          return;
        }
        if (complete) {
          clearCapturePoll();
          state.capturePending = false;
          state.pendingCaptureToken = null;
          const noEdges = logic?.words?.length && !logicHasVisibleEdges(enrichLogic(logic));
          const message = noEdges
            ? `${t("captureNoEdges")} ${logicEdgeSummary(enrichLogic(logic))}. ${isLevelTriggeredCapture(logic) ? t("levelTriggerNoEdgesHint") : t("captureNoEdgesHint")}`
            : t("captureComplete");
          setLogicStatus(message, noEdges ? "warning" : "ok");
          renderRunControls();
          renderConfigLockState();
          return;
        }
        const waitingMessage = params?.pre_samples_open_ended
          ? `${pendingTriggerMessage(params)} ${t("openEndedTriggerNoPre")}`
          : pendingTriggerMessage(params);
        setLogicStatus(waitingMessage, "");
        state.capturePollTimer = setTimeout(poll, 35);
      } catch (error) {
        if (state.pendingCaptureToken !== token || !state.capturePending) return;
        setLogicStatus(userFacingErrorMessage(error), "warning");
        state.capturePollTimer = setTimeout(poll, 120);
      }
    };
    state.capturePollTimer = setTimeout(poll, 20);
  }

  function pendingTriggerMessage(params = {}) {
    const triggerType = String(params.trigger_type || params.trigger_mode || "none");
    if (triggerType === "pattern" || params.trigger_mode === "pattern") {
      return `${t("preSampleWaiting")} ${t("trigger")}: ${localizedTrigger("pattern")}`;
    }
    if (params.trigger_pin !== undefined && params.trigger_pin !== null && triggerType !== "none") {
      return `${t("preSampleWaiting")} ${t("trigger")} GP${params.trigger_pin}: ${localizedTrigger(triggerType)}`;
    }
    return t("waitingForTrigger");
  }

  function stopLogicRun(message = t("logicStopped")) {
    state.logicRunActive = false;
    const mode = $("logicSampleMode");
    if (mode && mode.value === "repeated") mode.value = "single";
    setLogicStatus(message);
    applySnapshot({ lastResponse: message });
  }

  async function runLocalOperationIfNeeded(operation) {
    if (operation.action === "logic.run") {
      startLogicRun().catch((error) => stopLogicRun(error.message));
      return { ok: true, snapshot: { lastResponse: t("runStarted") } };
    }
    if (operation.action === "logic.stop") {
      state.capturePending = false;
      state.pendingCaptureToken = null;
      clearCapturePoll();
      stopLogicRun();
      await state.client.invoke(normalizeOperation("logic.stop", {}, { panel: "logic" })).catch(() => {});
      return { ok: true, snapshot: { lastResponse: t("runStopped") } };
    }
    if (operation.action === "logic.export") {
      const format = operation.params?.format || "vcd";
      const content = exportLogic(format);
      state.lastExport = content;
      showDecoderMessage(content || t("noCaptureExport"));
      const message = format === "settings" ? t("settingsGenerated") : `${t("exportGenerated")} (${format})`;
      setLogicStatus(content ? message : t("noCaptureExport"), content ? "ok" : "warning");
      return { ok: Boolean(content), snapshot: { lastResponse: content ? message : t("noCaptureExport") } };
    }
    if (operation.action === "logic.decode") {
      const output = decodeCurrentCapture();
      showDecoderMessage(output);
      state.decoderAnnotations = Array.isArray(output?.annotations) ? output.annotations : [];
      renderLogic(state.snapshot?.logic);
      setLogicStatus(t("decodeComplete"), "ok");
      return { ok: true, snapshot: { lastResponse: t("decodeComplete") } };
    }
    if (operation.action === "session.export") {
      return { ok: true, snapshot: { lastResponse: t("sessionSaved"), analyzer_session: buildAnalyzerSession() } };
    }
    if (operation.action === "session.import") {
      applyAnalyzerSession(operation.params?.session);
      setLogicStatus(t("sessionLoaded"), "ok");
      return { ok: true, snapshot: { lastResponse: t("sessionLoaded") } };
    }
    if (operation.action === "evidence.export") {
      const evidence = buildAnalyzerEvidence();
      return { ok: true, snapshot: { lastResponse: t("evidenceSaved"), analyzer_evidence: evidence } };
    }
    if (operation.action === "logic.copy-export") {
      if (!state.lastExport) state.lastExport = exportLogic(read("logicExportFormat") || "vcd");
      if (navigator.clipboard && state.lastExport) await navigator.clipboard.writeText(state.lastExport);
      showDecoderMessage(state.lastExport || t("noExportContent"));
      setLogicStatus(state.lastExport ? t("exportCopied") : t("noExportContent"), state.lastExport ? "ok" : "warning");
      return { ok: Boolean(state.lastExport), snapshot: { lastResponse: state.lastExport ? t("exportCopied") : t("noExportContent") } };
    }
    return null;
  }

  function handleLocalAction(action) {
    if (action === "logic.fit") {
      state.zoom = 1;
      state.viewStart = 0;
      state.userViewLocked = false;
      renderLogic(state.snapshot?.logic);
      return;
    }
    if (action === "session.save") {
      saveAnalyzerSessionFile();
      return;
    }
    if (action === "session.load") {
      $("sessionLoadInput")?.click();
      return;
    }
    if (action === "evidence.save") {
      saveAnalyzerEvidenceFile();
      return;
    }
    if (action === "logic.run") {
      startLogicRun().catch((error) => stopLogicRun(error.message));
      return;
    }
    if (action === "logic.stop") {
      state.capturePending = false;
      state.pendingCaptureToken = null;
      clearCapturePoll();
      stopLogicRun();
      state.client.invoke(normalizeOperation("logic.stop", {}, { panel: "logic" })).catch(() => {});
      return;
    }
    if (action === "logic.zoom-in") {
      state.zoom = clamp(state.zoom * 1.6, 1, 64);
      state.userViewLocked = true;
      renderLogic(state.snapshot?.logic);
      return;
    }
    if (action === "logic.zoom-out") {
      state.zoom = clamp(state.zoom / 1.6, 1, 64);
      state.userViewLocked = true;
      renderLogic(state.snapshot?.logic);
      return;
    }
    if (action === "logic.cursor-a") {
      state.cursorTarget = "a";
      state.cursorA = cursorDefaultSample();
      renderLogic(state.snapshot?.logic);
      return;
    }
    if (action === "logic.cursor-b") {
      state.cursorTarget = "b";
      state.cursorB = cursorDefaultSample(0.65);
      renderLogic(state.snapshot?.logic);
      return;
    }
    if (action === "logic.region-from-cursors") {
      const logic = state.snapshot?.logic;
      if (!logic?.words?.length || state.cursorB === null || state.cursorB === undefined) {
        setLogicStatus(t("cursorHint"));
        return;
      }
      const start = clamp(Math.min(state.cursorA, state.cursorB), 0, Math.max(0, logic.samples - 1));
      const end = clamp(Math.max(state.cursorA, state.cursorB), start + 1, logic.samples);
      $("logicRegionStart").value = String(Math.round(start));
      $("logicRegionEnd").value = String(Math.round(end));
      setLogicStatus(`${t("regionFromCursors")}: ${Math.round(start)}..${Math.round(end)}`, "ok");
      renderLogic(logic);
      return;
    }
    if (action === "logic.decode" || action === "logic.copy-export") {
      runOperation(normalizeOperation(action, {}, { panel: "logic" })).catch(() => {});
      return;
    }
    if (action === "ai.demo") {
      const operation = normalizeOperation("logic.capture", operationTemplates["logic.capture"].params, { show: true, panel: "logic" });
      state.queue.unshift({ operation, status: "example", startedAt: new Date(), endedAt: new Date() });
      state.selectedQueueIndex = 0;
      renderQueue();
    }
  }

  function applySnapshot(snapshot) {
    const next = { ...(state.snapshot ?? {}), ...snapshot };
    if (Array.isArray(snapshot.events)) {
      const previousEvents = Array.isArray(state.snapshot?.events) ? state.snapshot.events : [];
      next.events = snapshot.events.length ? previousEvents.concat(snapshot.events).slice(-500) : previousEvents;
    }
    if (snapshot.logic) {
      next.logic = enrichLogic(snapshot.logic);
      const words = Array.isArray(next.logic.words) ? next.logic.words : [];
      const captureKey = words.length
        ? [
          next.logic.capture_id ?? "",
          next.logic.samples ?? "",
          words.length,
          words[0] ?? 0,
          words[words.length - 1] ?? 0
        ].join(":")
        : "";
      if (captureKey && captureKey !== state.lastCaptureKey) {
        state.decoderAnnotations = [];
        if (next.logic.trigger_found && !state.userViewLocked) {
          state.zoom = triggerFocusedZoom(next.logic);
          state.viewStart = triggerFocusedViewStart(next.logic);
          state.userViewLocked = false;
        } else if (!state.userViewLocked) {
          state.viewStart = 0;
        }
        state.lastCaptureKey = captureKey;
      }
    }
    state.snapshot = next;
    mergeProtocolDevicesFromSnapshot(next);
    render();
  }

  function enrichLogic(logic) {
    const next = { ...logic };
    next.pin_base = Number(next.pin_base ?? 16);
    next.pin_count = Number(next.pin_count ?? 1);
    next.sample_rate = Number(next.sample_rate ?? read("logicSampleRate") ?? 1000000);
    next.samples = Number(next.samples ?? Math.max(1, positiveInteger(read("logicPreSamples"), 0) + positiveInteger(read("logicPostSamples"), 4096)));
    next.record_bits = Number(next.record_bits ?? (32 - (32 % Math.max(1, next.pin_count))));
    if (!Array.isArray(next.selected_pins)) {
      const selected = selectedChannelPins();
      next.selected_pins = selected.length ? selected.filter((pin) => pin >= next.pin_base && pin < next.pin_base + next.pin_count) : pinsInRange(next.pin_base, next.pin_count);
    }
    if (!Array.isArray(next.words)) next.words = [];
    if (!Array.isArray(next.burst_samples)) next.burst_samples = [];
    return next;
  }

  function setPanel(panel) {
    const next = panels.includes(panel) ? panel : "overview";
    state.activePanel = next;
    document.querySelectorAll(".tab-button").forEach((button) => button.classList.toggle("active", button.dataset.panel === next));
    document.querySelectorAll(".panel").forEach((panelElement) => panelElement.classList.toggle("active", panelElement.id === `panel-${next}`));
  }

  function t(key) {
    return translations[state.language]?.[key] ?? translations.en[key] ?? key;
  }

  function localizedStatusText(value) {
    const map = {
      Ready: "ready",
      "Logic capture complete": "captureComplete",
      "logic.capture": "captureComplete",
      "logic.configure": "configured",
      "probe": "probe",
      "wifi.scan": "wifiScanComplete",
      "Logic configured": "configured",
      "GPIO levels updated": "gpio",
      "Wi-Fi scan complete": "wifiScanComplete",
      "Mock probe complete": "probeComplete",
      "Logic capabilities loaded": "logicCapsLoaded"
    };
    return map[value] ? t(map[value]) : value;
  }

  function userFacingErrorMessage(error) {
    const message = String(error?.message || error || "");
    if (/failed to fetch|fetch failed|econnrefused|networkerror/i.test(message)) {
      return t("bridgeOffline");
    }
    return message;
  }

  function localizedTrigger(value) {
    const map = {
      none: "noneShort",
      "level-high": "levelHigh",
      "level-low": "levelLow",
      rising: "risingEdge",
      falling: "fallingEdge",
      pattern: "patternTrigger",
      "fast-pattern": "fastPatternRequired",
      blast: "blastRequired"
    };
    return map[value] ? t(map[value]) : (value || t("noneShort"));
  }

  function applyLanguage() {
    document.documentElement.lang = state.language === "zh" ? "zh-CN" : "en";
    const selector = $("languageSelect");
    if (selector && selector.value !== state.language) selector.value = state.language;
    document.querySelectorAll("[data-i18n]").forEach((node) => {
      const key = node.dataset.i18n;
      if (key) node.textContent = t(key);
    });
    document.querySelectorAll("[data-i18n-placeholder]").forEach((node) => {
      const key = node.dataset.i18nPlaceholder;
      if (key) node.setAttribute("placeholder", t(key));
    });
    document.querySelectorAll("[data-i18n-title]").forEach((node) => {
      const key = node.dataset.i18nTitle;
      if (key) node.setAttribute("title", t(key));
    });
    state.appliedLanguage = state.language;
  }

  function render() {
    const snapshot = state.snapshot ?? {};
    if (state.appliedLanguage !== state.language) applyLanguage();
    renderStatus();
    renderOverview(snapshot);
    renderWifi(snapshot.wifi ?? []);
    renderGPIO(snapshot.gpio ?? []);
    renderProtocolDeviceControls();
    renderProtocolPinControls();
    renderTriggerControls();
    renderDecoderControls();
    renderPresetButtons();
    renderTriggerArmingSummary();
    renderChannelMatrix();
    renderRunControls();
    renderConfigLockState();
    renderLogic(snapshot.logic);
    renderLogicStatusBar(snapshot);
    renderProtocolLogs(snapshot);
    renderQueue();
  }

  function renderValueSelect(select, values, preferredValue, labelFor = (value) => String(value)) {
    if (!select) return;
    const normalized = values.map((value) => String(value));
    const previous = normalized.includes(select.value) ? select.value : "";
    const currentValues = Array.from(select.options).map((option) => option.value);
    if (currentValues.join(",") !== normalized.join(",")) {
      select.innerHTML = values.map((value) => `<option value="${value}">${escapeHTML(labelFor(value))}</option>`).join("");
    }
    const preferred = String(preferredValue);
    select.value = previous || (normalized.includes(preferred) ? preferred : normalized[0] || "");
  }

  function renderProtocolPinControls() {
    ensureSelectedProtocolDevices();
    const uartDevice = selectedProtocolDevice("uart");
    const uartInstance = protocolInstanceValue("uart", uartDevice);
    const uartMap = nativePinMaps.uart[uartInstance] || nativePinMaps.uart[nativeDefaults.uart.instance];
    renderPinSelect($("uartTx"), uartMap.tx, uartDevice?.tx ?? nativeDefaults.uart.tx);
    renderPinSelect($("uartRx"), uartMap.rx, uartDevice?.rx ?? nativeDefaults.uart.rx);
    setText("uartPinSummary", `UART${uartInstance} TX GP${read("uartTx")} / RX GP${read("uartRx")}`);

    const i2cDevice = selectedProtocolDevice("i2c");
    const i2cInstance = protocolInstanceValue("i2c", i2cDevice);
    const i2cMap = nativePinMaps.i2c[i2cInstance] || nativePinMaps.i2c[nativeDefaults.i2c.instance];
    renderPinSelect($("i2cSda"), i2cMap.sda, i2cDevice?.sda ?? nativeDefaults.i2c.sda);
    renderPinSelect($("i2cScl"), i2cMap.scl, i2cDevice?.scl ?? nativeDefaults.i2c.scl);
    setText("i2cPinSummary", `I2C${i2cInstance} SDA GP${read("i2cSda")} / SCL GP${read("i2cScl")}`);

    const spiDevice = selectedProtocolDevice("spi");
    const spiInstance = protocolInstanceValue("spi", spiDevice);
    const spiMap = nativePinMaps.spi[spiInstance] || nativePinMaps.spi[nativeDefaults.spi.instance];
    renderPinSelect($("spiSck"), spiMap.sck, spiDevice?.sck ?? nativeDefaults.spi.sck);
    renderPinSelect($("spiMosi"), spiMap.mosi, spiDevice?.mosi ?? nativeDefaults.spi.mosi);
    renderPinSelect($("spiMiso"), spiMap.miso, spiDevice?.miso ?? nativeDefaults.spi.miso);
    const spiDataPins = new Set([Number(read("spiSck")), Number(read("spiMosi")), Number(read("spiMiso"))]);
    const manualCsPins = exposedPins.filter((pin) => !spiDataPins.has(pin));
    renderPinSelect($("spiCs"), manualCsPins, spiDevice?.cs ?? nativeDefaults.spi.cs);
    setText("spiPinSummary", `SPI${spiInstance} SCK GP${read("spiSck")} / MOSI GP${read("spiMosi")} / MISO GP${read("spiMiso")} / CS GP${read("spiCs")}`);
  }

  function renderProtocolDeviceControls() {
    ["uart", "i2c", "spi"].forEach((protocol) => {
      const select = $(`${protocol}DeviceSelect`);
      if (!select) return;
      const devices = state.protocolDevices[protocol] || [];
      const selected = selectedProtocolDevice(protocol);
      const selectedActive = activeProtocolChannel(protocol, selected.id);
      const options = devices.map((device) => `<option value="${device.id}">${escapeHTML(protocolDeviceLabel(protocol, device))}</option>`);
      if (select.innerHTML !== options.join("")) {
        select.innerHTML = options.join("");
      }
      select.value = String(selected.id);
      const table = $(`${protocol}DeviceTable`);
      if (table) {
        table.innerHTML = devices.map((device) => {
          const active = activeProtocolChannel(protocol, device.id);
          const isSelected = Number(device.id) === Number(selected.id);
          const detail = active ? protocolChannelDetail(protocol, active) : protocolChannelDetail(protocol, device);
          return `
            <div class="protocol-device-row ${isSelected ? "selected" : ""}" data-protocol-device-row="${protocol}" data-device-id="${device.id}">
              <button type="button" class="protocol-device-main" data-protocol-device-row="${protocol}" data-device-id="${device.id}">
                <strong>${escapeHTML(protocolDeviceLabel(protocol, device))}</strong>
                <span class="status-pill ${active ? "ok" : ""}">${active ? t("active") : t("inactive")}</span>
                <em title="${escapeHTML(detail)}">${escapeHTML(detail)}</em>
              </button>
              <button type="button" data-protocol-release-row="${protocol}" data-device-id="${device.id}" title="${escapeHTML(t("closeDevice"))}" aria-label="${escapeHTML(t("closeDevice"))}">${t("closeDevice")}</button>
            </div>
          `;
        }).join("");
      }
      const status = $(`${protocol}DeviceStatus`);
      if (status) {
        const detail = selectedActive ? protocolChannelDetail(protocol, selectedActive) : protocolChannelDetail(protocol, selected);
        status.innerHTML = `<span class="status-pill ${selectedActive ? "ok" : ""}">${selectedActive ? t("active") : t("inactive")}</span><strong>${escapeHTML(detail)}</strong>`;
      }
      const addButton = document.querySelector(`[data-action-local="${protocol}.addDevice"]`);
      if (addButton) {
        const full = devices.length >= protocolLimits[protocol];
        addButton.disabled = full;
        addButton.title = full ? t("noFreeHardware") : "";
      }
    });
  }

  function setText(id, value) {
    const node = $(id);
    if (node) node.textContent = value;
  }

  function renderTriggerControls() {
    const triggerType = read("logicTriggerType") || "none";
    const patternMode = triggerType === "pattern";
    const triggerPin = $("logicTriggerPin");
    const patternBase = $("logicPatternBase");
    const patternBits = $("logicPattern");
    const selectedPins = selectedChannelPins();
    renderPinSelect(triggerPin, exposedPins, selectedPins[0] ?? 16);
    renderPinSelect(patternBase, exposedPins, selectedPins[0] ?? 16);
    if (triggerPin) triggerPin.disabled = triggerType === "none" || patternMode;
    if (patternBase) patternBase.disabled = !patternMode;
    if (patternBits) patternBits.disabled = !patternMode;
  }

  function renderDecoderControls() {
    const logic = state.snapshot?.logic;
    const visiblePins = logic?.words?.length ? visiblePinsFor(logic) : selectedChannelPins();
    const pins = visiblePins.length ? visiblePins : selectedChannelPins().length ? selectedChannelPins() : [16, 17, 18, 19];
    const decoder = read("decoderType") || "summary";
    const spiLoopback = decoder === "spi" && [16, 17, 18, 19].every((pin) => pins.includes(pin));
    const spiNative = decoder === "spi" && [0, 1, 2, 3].every((pin) => pins.includes(pin));
    const spiMapping = spiLoopback
      ? { data: 17, clock: 16, cs: 19 }
      : spiNative
        ? { data: 3, clock: 2, cs: 1 }
        : null;
    renderPinSelect($("decoderDataPin"), pins, spiMapping?.data ?? pins[0] ?? 16, Boolean(spiMapping));
    renderPinSelect($("decoderClockPin"), pins, spiMapping?.clock ?? pins[1] ?? pins[0] ?? 17, Boolean(spiMapping));
    renderPinSelect($("decoderCsPin"), pins, spiMapping?.cs ?? pins[2] ?? pins[0] ?? 18, Boolean(spiMapping));
    const fieldVisibility = {
      data: ["uart", "i2c", "spi"].includes(decoder),
      clock: ["uart", "i2c", "spi"].includes(decoder),
      cs: decoder === "spi",
      baud: decoder === "uart",
      "spi-mode": decoder === "spi"
    };
    document.querySelectorAll("[data-decoder-field]").forEach((node) => {
      const key = node.dataset.decoderField;
      node.classList.toggle("hidden", !fieldVisibility[key]);
    });
    const dataLabel = $("decoderDataLabel");
    const clockLabel = $("decoderClockLabel");
    if (dataLabel) {
      dataLabel.textContent = decoder === "uart" ? "RX" : decoder === "i2c" ? "SDA" : decoder === "spi" ? "MOSI" : "Data";
    }
    if (clockLabel) {
      clockLabel.textContent = decoder === "uart" ? "TX" : decoder === "i2c" ? "SCL" : decoder === "spi" ? "SCK" : "Clock";
    }
  }

  function renderPresetButtons() {
    document.querySelectorAll("[data-preset]").forEach((button) => {
      button.classList.toggle("active", Boolean(state.activePreset) && button.dataset.preset === state.activePreset);
      button.setAttribute("title", t("protocolPresetHint"));
    });
  }

  function renderTriggerArmingSummary() {
    const container = $("triggerArmingSummary");
    if (!container) return;
    const armed = selectedChannelConfigs().filter((config) => config.trigger && config.trigger !== "ignore");
    const preSamples = positiveInteger(read("logicPreSamples"), 0);
    const triggerKind = (() => {
      if (!armed.length) return t("triggerNoArmedChannels");
      const hasEdge = armed.some((config) => config.trigger === "rise" || config.trigger === "fall");
      if (hasEdge && armed.length === 1) return `${t("trigger")}: ${triggerLabel(armed[0].trigger)} GP${armed[0].pin}`;
      return `${t("trigger")}: ${t("patternTrigger")} (${armed.length} ${t("channels")})`;
    })();
    const chips = armed.map((config) => `<span class="trigger-chip">GP${config.pin} ${escapeHTML(triggerLabel(config.trigger))}</span>`).join("");
    const preSampleText = preSamples > 0
      ? (armed.length ? `${t("preSamples")}: ${preSamples}. ${t("preSampleArmed")}` : t("preSampleNeedsTrigger"))
      : "";
    container.innerHTML = `
      <strong>${t("armedTriggers")}</strong>
      <div>${escapeHTML(triggerKind)}</div>
      <div>${escapeHTML(t("triggerSourceHint"))}</div>
      ${preSampleText ? `<div>${escapeHTML(preSampleText)}</div>` : ""}
      ${chips ? `<div class="trigger-chip-list">${chips}</div>` : ""}
    `;
  }

  function triggerLabel(value) {
    const map = {
      high: "triggerHigh",
      low: "triggerLow",
      rise: "triggerRise",
      fall: "triggerFall",
      p1: "triggerPatternHigh",
      p0: "triggerPatternLow"
    };
    return t(map[value] || "triggerIgnore");
  }

  function renderPinSelect(select, pins, preferredPin, forcePreferred = false) {
    if (!select) return;
    const values = pins.map((pin) => String(pin));
    const previous = values.includes(select.value) ? select.value : "";
    const currentValues = Array.from(select.options).map((option) => option.value);
    if (currentValues.join(",") !== values.join(",")) {
      select.innerHTML = pins.map((pin) => `<option value="${pin}">GP${pin}</option>`).join("");
    }
    const preferred = String(preferredPin);
    select.value = forcePreferred && values.includes(preferred)
      ? preferred
      : previous || (values.includes(preferred) ? preferred : values[0]);
  }

  function renderRunControls() {
    const runButton = $("logicRunButton");
    const stopButton = $("logicStopButton");
    const captureButton = document.querySelector('[data-action="logic.capture"]');
    if (runButton) {
      runButton.classList.toggle("active-run", state.logicRunActive);
      runButton.textContent = state.logicRunActive ? `${t("run")} ${state.logicRunIteration}` : t("run");
      runButton.disabled = state.logicRunActive || state.capturePending;
    }
    if (stopButton) {
      stopButton.disabled = !(state.logicRunActive || state.capturePending);
    }
    if (captureButton) {
      captureButton.textContent = state.logicRunActive ? t("capture") : state.capturePending ? t("waiting") : t("capture");
      captureButton.disabled = state.logicRunActive || state.capturePending;
    }
  }

  function renderConfigLockState() {
    const locked = Boolean(state.logicRunActive || state.capturePending);
    const controls = document.querySelectorAll([
      ".analyzer-toolbar input",
      ".analyzer-toolbar select",
      ".channel-panel button",
      ".channel-panel input",
      ".channel-panel select",
      ".trigger-panel button",
      ".trigger-panel input",
      ".trigger-panel select",
      ".analyzer-decoder button",
      ".analyzer-decoder input",
      ".analyzer-decoder select"
    ].join(","));
    controls.forEach((control) => {
      if (locked) {
        if (control.dataset.lockOriginalDisabled === undefined) {
          control.dataset.lockOriginalDisabled = control.disabled ? "1" : "0";
          control.dataset.lockOriginalTitle = control.getAttribute("title") || "";
        }
        control.disabled = true;
        control.setAttribute("title", t("configLockedWhileRunning"));
      } else if (control.dataset.lockOriginalDisabled !== undefined) {
        control.disabled = control.dataset.lockOriginalDisabled === "1";
        const title = control.dataset.lockOriginalTitle || "";
        if (title) control.setAttribute("title", title);
        else control.removeAttribute("title");
        delete control.dataset.lockOriginalDisabled;
        delete control.dataset.lockOriginalTitle;
      }
    });
  }

  function renderLogicStatusBar(snapshot = state.snapshot ?? {}) {
    const bar = $("logicStatusBar");
    if (!bar) return;
    const preSampleMessage = preSampleStatusMessage();
    let message = state.logicStatus || preSampleMessage || localizedStatusText(snapshot.lastResponse ?? "");
    if (!message) {
      const logic = snapshot.logic;
      message = !logic?.words?.length ? emptyLogicMessage(logic) : t("ready");
    }
    bar.className = `logic-status-bar ${state.logicStatusTone || ""}`.trim();
    const fullMessage = message || t("ready");
    bar.textContent = compactLogicStatusText(fullMessage);
    bar.setAttribute("title", fullMessage);
    bar.setAttribute("aria-label", fullMessage);
  }

  function setLogicStatus(message, tone = "") {
    state.logicStatus = message || "";
    state.logicStatusTone = tone;
    renderLogicStatusBar();
  }

  function preSampleStatusMessage() {
    const preSamples = positiveInteger(read("logicPreSamples"), 0);
    if (preSamples <= 0) return "";
    const armed = selectedChannelConfigs().some((config) => config.trigger && config.trigger !== "ignore");
    return armed ? `${t("preSamples")}: ${preSamples}. ${t("preSampleArmed")}` : t("preSampleNeedsTrigger");
  }

  function compactLogicStatusText(message) {
    const text = String(message || "");
    if (!text) return "";
    if (text.includes(t("captureNoEdges"))) return t("statusNoEdgesShort");
    if (text.includes(t("preSampleNeedsTriggerForCapture"))) return t("preSampleIgnoredShort");
    if (text.includes(t("preSampleNeedsTrigger"))) return t("preSampleIgnoredShort");
    const limit = state.language === "zh" ? 42 : 76;
    return text.length > limit ? `${text.slice(0, limit - 1)}...` : text;
  }

  function showErrorDialog(message) {
    const modal = $("errorModal");
    const body = $("errorModalMessage");
    if (body) body.textContent = message || t("errorTitle");
    if (modal) modal.classList.remove("hidden");
  }

  function renderStatus(patch = {}) {
    const snapshot = { ...(state.snapshot ?? {}), ...patch };
    const device = snapshot.device ?? {};
    const rawLast = snapshot.error ?? snapshot.lastResponse ?? t("ready");
    const last = state.logicRunActive ? `${t("liveCapture")} #${state.logicRunIteration}` : localizedStatusText(rawLast);
    const items = [[t("device"), device.board ?? "Pico 2 W"], [t("firmware"), device.firmware ?? "-"], [t("transport"), device.transport ?? "USB CDC"], [t("last"), last]];
    $("statusStrip").innerHTML = items.map(([label, value]) => `<div class="pill"><span>${label}</span><strong>${escapeHTML(value)}</strong></div>`).join("");
  }

  function renderOverview(snapshot) {
    const device = snapshot.device ?? {};
    const logic = snapshot.logic ?? {};
    const wifiStatus = snapshot.wifi_status ?? {};
    const buffers = snapshot.buffers ?? {};
    const channels = Array.isArray(snapshot.channels) ? snapshot.channels : [];
    const transportMode = $("transportMode");
    const transportEndpoint = $("transportEndpoint");
    if (transportMode && device.transport) {
      transportMode.value = String(device.transport).includes("Wi-Fi") ? "Wi-Fi TCP" : "USB CDC";
    }
    if (transportEndpoint && (device.endpoint || wifiStatus.station_ip)) {
      transportEndpoint.value = device.endpoint || wifiStatus.station_ip;
    }
    const cards = [
      [t("board"), device.board ?? "Pico 2 W", "ok"],
      [t("firmware"), device.firmware ?? snapshot.version ?? "-", "neutral"],
      [t("transport"), device.transport ?? (wifiStatus.station_ip ? "Wi-Fi TCP" : "USB CDC"), wifiStatus.station_ip ? "ok" : "neutral"],
      [t("stationIP"), wifiStatus.station_ip ?? device.endpoint ?? read("transportEndpoint"), wifiStatus.station_ip ? "ok" : "neutral"],
      [t("logic"), logic.complete ? t("captureComplete") : logic.configured ? t("configured") : t("idle"), logic.complete ? "ok" : "neutral"],
      [t("channelCount"), `${channels.length || 0}`, channels.length ? "ok" : "neutral"]
    ];
    $("overviewCards").innerHTML = cards.map(([title, value, tone]) => `<article class="metric ${tone}"><span>${title}</span><strong>${escapeHTML(value)}</strong></article>`).join("");
    const stationTone = wifiStatus.station_status === "up" ? "ok" : wifiStatus.ssid_configured ? "warn" : "neutral";
    $("overviewHealth").innerHTML = [
      [t("connection"), device.transport ?? "RP2350 Monitor", "ok"],
      [t("wifiStation"), wifiStatus.station_status ?? "-", stationTone],
      [t("wifiAP"), wifiStatus.ap_active ? "active" : "off", wifiStatus.ap_active ? "warn" : "neutral"],
      [t("logicEngine"), logic.running ? t("live") : logic.complete ? t("captureComplete") : logic.configured ? t("configured") : t("idle"), logic.running || logic.complete ? "ok" : "neutral"],
      [t("eventBuffer"), buffers.event_depth !== undefined ? `${buffers.event_depth}/${buffers.event_capacity ?? "-"}` : "-", "neutral"],
      [t("captureBuffer"), snapshot.logic_caps?.buffer_bytes ? formatBytes(snapshot.logic_caps.buffer_bytes) : "-", "neutral"]
    ].map(([label, value, tone]) => `<div class="health-item ${tone}"><strong>${escapeHTML(value)}</strong><span>${escapeHTML(label)}</span></div>`).join("");
    $("transportDetail").innerHTML = detailRows([
      [t("mode"), device.transport ?? "-"],
      [t("endpoint"), device.endpoint ?? wifiStatus.station_ip ?? read("transportEndpoint")],
      [t("wifiStation"), wifiStatus.ssid ? `${wifiStatus.ssid} / ${wifiStatus.station_status ?? "-"}` : "-"],
      [t("stationIP"), wifiStatus.station_ip ?? "-"]
    ]);
    $("overviewRuntime").innerHTML = detailRows([
      [t("logicEngine"), snapshot.logic_caps?.engine ?? "-"],
      [t("sampleRate"), logic.sample_rate ? formatFrequency(logic.sample_rate) : "-"],
      [t("samples"), logic.samples ?? "-"],
      [t("memory"), snapshot.logic_caps?.buffer_bytes ? formatBytes(snapshot.logic_caps.buffer_bytes) : "-"],
      [t("eventBuffer"), buffers.total_events !== undefined ? `${buffers.total_events} total / ${buffers.dropped_events ?? 0} dropped` : "-"],
      [t("last"), localizedStatusText(snapshot.lastResponse ?? t("ready"))]
    ]);
    $("overviewInterfaces").innerHTML = channels.length
      ? channels.map((channel) => `<div class="detail-row"><strong>${escapeHTML(channel.type ?? "channel")} #${escapeHTML(channel.id ?? "-")}</strong><span>${escapeHTML(formatChannelDetail(channel))}</span></div>`).join("")
      : `<p class="empty">${t("noActiveInterfaces")}</p>`;
  }

  function detailRows(rows) {
    return rows.map(([label, value]) => `<div class="detail-row"><strong>${escapeHTML(value ?? "-")}</strong><span>${escapeHTML(label)}</span></div>`).join("");
  }

  function formatChannelDetail(channel) {
    if (!channel || typeof channel !== "object") return "-";
    if (channel.type === "uart") return `UART${channel.instance ?? 0} TX GP${channel.tx} RX GP${channel.rx} ${channel.baud ?? ""}`;
    if (channel.type === "i2c") return `I2C${channel.instance ?? 0} SDA GP${channel.sda} SCL GP${channel.scl} ${channel.baud ?? ""}`;
    if (channel.type === "spi") return `SPI${channel.instance ?? 0} SCK GP${channel.sck} MOSI GP${channel.mosi} MISO GP${channel.miso} CS GP${channel.cs} ${channel.baud ?? ""}`;
    if (channel.type === "gpio") return `GPIO${channel.gpio} ${channel.direction ?? ""} ${channel.level === undefined ? "" : channel.level ? "HIGH" : "LOW"}`;
    return JSON.stringify(channel);
  }

  function renderWifi(networks) {
    const list = Array.isArray(networks) ? networks : [];
    $("wifiList").innerHTML = list.length ? list.map((item) => `<div class="wifi-row"><strong>${escapeHTML(item.ssid)}</strong><span>${item.rssi} dBm</span><span>CH ${item.channel}</span></div>`).join("") : `<p class="empty">${t("noScanResults")}</p>`;
  }

  function renderGPIO(levels) {
    $("gpioLevels").innerHTML = levels.length ? levels.map(({ pin, level }) => `<div class="level ${level ? "high" : "low"}"><span>GP${pin}</span><strong>${level ? t("high") : t("low")}</strong></div>`).join("") : `<p class="empty">${t("readGPIOHint")}</p>`;
  }

  function renderChannelMatrix() {
    const active = document.activeElement;
    if (state.logicRunActive && active?.dataset?.channelName !== undefined) return;
    const summary = selectedChannelSummary();
    $("logicChannelSummary").innerHTML = `
      <div><strong>${summary.count}</strong><span>${t("selected")}</span></div>
      <div><strong>${escapeHTML(summary.range)}</strong><span>${t("hardwareWindow")}</span></div>
      <div><strong>${summary.sparse ? t("sparse") : t("contiguous")}</strong><span>${t("selection")}</span></div>
    `;
    const triggerType = read("logicTriggerType") || "none";
    const triggerGuide = triggerGuideFromForm();
    const baseNote = triggerType === "pattern" ? `${t("patternMultiHint")} ${summary.note}` : summary.note;
    const capabilityNote = $("logicCapabilityNote");
    if (capabilityNote) capabilityNote.textContent = triggerGuide ? `${baseNote} ${triggerGuide}` : baseNote;
    renderChannelAddSelect();
    const channels = state.channels.filter((channel) => channel.selected);
    $("logicChannelMatrix").innerHTML = channels.length ? `
      <div class="channel-table-head">
        <span>GP</span>
        <span>${t("channels")}</span>
        <span>${t("channelPull")}</span>
        <span>${t("channelTrigger")}</span>
        <span>${t("gpioLive")}</span>
        <span>${t("channelInvert")}</span>
        <span></span>
        <span></span>
      </div>
    ` + channels.map((channel) => {
      const live = channelLiveSummary(channel.pin);
      const pull = channel.pull || "none";
      const trigger = channel.trigger || "ignore";
      return `
      <div class="channel-card ${channel.selected ? "selected" : ""}">
        <span class="channel-check">GP${channel.pin}</span>
        <input data-channel-name="${channel.pin}" value="${escapeHTML(channel.name)}" />
        <select data-channel-pull="${channel.pin}" title="${t("channelPull")}">
          <option value="none" ${pull === "none" ? "selected" : ""}>${t("noneShort")}</option>
          <option value="up" ${pull === "up" ? "selected" : ""}>${t("up")}</option>
          <option value="down" ${pull === "down" ? "selected" : ""}>${t("down")}</option>
        </select>
        <select data-channel-trigger="${channel.pin}" title="${t("channelTrigger")}">
          <option value="ignore" ${trigger === "ignore" ? "selected" : ""}>${t("triggerIgnore")}</option>
          <option value="high" ${trigger === "high" ? "selected" : ""}>${t("triggerHigh")}</option>
          <option value="low" ${trigger === "low" ? "selected" : ""}>${t("triggerLow")}</option>
          <option value="rise" ${trigger === "rise" ? "selected" : ""}>${t("triggerRise")}</option>
          <option value="fall" ${trigger === "fall" ? "selected" : ""}>${t("triggerFall")}</option>
          <option value="p1" ${trigger === "p1" ? "selected" : ""}>${t("triggerPatternHigh")}</option>
          <option value="p0" ${trigger === "p0" ? "selected" : ""}>${t("triggerPatternLow")}</option>
        </select>
        <span class="channel-live ${live.levelClass}">${escapeHTML(live.text)}</span>
        <input type="checkbox" class="channel-invert" data-channel-invert="${channel.pin}" ${channel.invert ? "checked" : ""} title="${t("channelInvert")}" />
        <input type="color" data-channel-color="${channel.pin}" value="${channel.color}" />
        <button type="button" class="channel-remove" data-channel-remove="${channel.pin}" title="${t("removeChannel")}">×</button>
      </div>
    `;
    }).join("") : `<p class="empty">${t("selectAtLeastOne")}</p>`;
  }

  function renderChannelAddSelect() {
    const select = $("logicAddPinSelect");
    if (!select) return;
    const selected = new Set(selectedChannelPins());
    const available = exposedPins.filter((pin) => !selected.has(pin));
    const previous = Number(select.value);
    select.innerHTML = available.map((pin) => `<option value="${pin}">GP${pin}</option>`).join("");
    if (available.includes(previous)) select.value = String(previous);
    select.disabled = available.length === 0;
    const addButton = $("logicAddChannel");
    if (addButton) addButton.disabled = available.length === 0;
  }

  function addSelectedChannel() {
    const pin = Number(read("logicAddPinSelect"));
    if (!Number.isFinite(pin) || !exposedPinSet.has(pin)) return;
    state.activePreset = "";
    const channel = channelFor(pin);
    if (channel) {
      resetChannelToDefaults(channel, true);
    }
    setLogicStatus(t("channelAdded"), "ok");
    render();
  }

  function removeChannel(pin) {
    state.activePreset = "";
    const channel = channelFor(pin);
    if (channel) resetChannelToDefaults(channel, false);
    setLogicStatus(t("channelRemoved"));
    render();
  }

  function clearChannels() {
    state.activePreset = "";
    state.channels.forEach((channel) => resetChannelToDefaults(channel, false));
    setLogicStatus(t("channelsCleared"), "warning");
    render();
  }

  function resetChannelToDefaults(channel, selected = false) {
    if (!channel) return;
    const index = exposedPins.indexOf(channel.pin);
    channel.name = `GP${channel.pin}`;
    channel.selected = Boolean(selected);
    channel.color = channelPalette[(index >= 0 ? index : 0) % channelPalette.length];
    channel.pull = "none";
    channel.invert = false;
    channel.trigger = "ignore";
  }

  function applyLogicPreset(name) {
    const presets = {
      manual: {
        pins: [16, 17],
        sampleRate: 100000,
        preSamples: 120,
        postSamples: 32768,
        pull: "none",
        triggerType: "none",
        decoder: "summary"
      },
      uart: {
        pins: [Number(read("uartRx") || 1)],
        sampleRate: 1000000,
        preSamples: 120,
        postSamples: 8192,
        pull: "up",
        triggerType: "falling",
        triggerPin: Number(read("uartRx") || 1),
        decoder: "uart",
        dataPin: Number(read("uartRx") || 1),
        baud: Number(read("uartBaud") || 115200)
      },
      i2c: {
        pins: [Number(read("i2cSda") || 4), Number(read("i2cScl") || 5)],
        sampleRate: 1000000,
        preSamples: 120,
        postSamples: 8192,
        pull: "up",
        triggerType: "falling",
        triggerPin: Number(read("i2cSda") || 4),
        decoder: "i2c",
        dataPin: Number(read("i2cSda") || 4),
        clockPin: Number(read("i2cScl") || 5)
      },
      spi: {
        pins: [16, 17, 18, 19],
        names: { 16: "SCK", 17: "MOSI", 18: "MISO", 19: "CS" },
        sampleRate: 5000000,
        preSamples: 120,
        postSamples: 16384,
        pull: "none",
        triggerType: "falling",
        triggerPin: 19,
        decoder: "spi",
        dataPin: 17,
        clockPin: 16,
        csPin: 19
      }
    };
    const preset = presets[name];
    if (!preset) return;
    const pins = [...new Set(preset.pins.filter((pin) => exposedPinSet.has(pin)))].sort((a, b) => a - b);
    if (!pins.length) {
      showErrorDialog(t("selectAtLeastOne"));
      return;
    }
    state.activePreset = name;
    state.channels.forEach((channel) => {
      channel.selected = pins.includes(channel.pin);
      if (channel.selected) {
        channel.name = preset.names?.[channel.pin] || `GP${channel.pin}`;
        channel.pull = preset.pull || "none";
        channel.trigger = "ignore";
      }
    });
    const triggerChannel = channelFor(preset.triggerPin);
    if (triggerChannel?.selected && preset.triggerType === "falling") triggerChannel.trigger = "fall";
    if (triggerChannel?.selected && preset.triggerType === "rising") triggerChannel.trigger = "rise";
    setValue("logicSampleRate", preset.sampleRate);
    setValue("logicPreSamples", preset.preSamples ?? 120);
    setValue("logicPostSamples", preset.postSamples);
    setValue("logicSearchSamples", 0);
    setValue("logicBurstCount", 1);
    setValue("logicPull", preset.pull || "none");
    setValue("logicSampleMode", "single");
    setValue("logicTriggerType", preset.triggerType || "none");
    if (preset.triggerPin !== undefined) setValue("logicTriggerPin", preset.triggerPin);
    setValue("decoderType", preset.decoder || "summary");
    if (preset.dataPin !== undefined) setValue("decoderDataPin", preset.dataPin);
    if (preset.clockPin !== undefined) setValue("decoderClockPin", preset.clockPin);
    if (preset.csPin !== undefined) setValue("decoderCsPin", preset.csPin);
    if (preset.baud !== undefined) setValue("decoderBaud", preset.baud);
    setLogicStatus(`${t("presetApplied")}: ${presetLabel(name)}`, "ok");
    render();
  }

  function setValue(id, value) {
    const node = $(id);
    if (node) node.value = String(value);
  }

  function presetLabel(name) {
    const map = { manual: "presetManual", uart: "presetUART", i2c: "presetI2C", spi: "presetSPI" };
    return t(map[name] || name);
  }

  function buildAnalyzerSession(options = {}) {
    let captureParams;
    try {
      captureParams = logicParamsFromForm({ strictPreTrigger: false });
    } catch (error) {
      if (!options.allowEmpty) throw error;
      captureParams = {
        pin_base: 16,
        pin_count: 1,
        sample_rate: positiveInteger(read("logicSampleRate"), 1000000),
        samples: positiveInteger(read("logicPostSamples"), 4096),
        pre_samples: 0,
        post_samples: positiveInteger(read("logicPostSamples"), 4096),
        search_samples: positiveInteger(read("logicSearchSamples"), 0),
        burst_count: positiveInteger(read("logicBurstCount"), 1),
        pull: read("logicPull") || "none",
        selected_pins: [],
        trigger_type: "none",
        channel_configs: []
      };
    }
    const captureOperation = normalizeOperation("logic.capture", captureParams, { panel: "logic", show: true });
    return {
      schema: "embedlabs.rpmon.logic-session.v1",
      created_at: new Date().toISOString(),
      language: state.language,
      active_preset: state.activePreset || "",
      channels: state.channels
        .filter((channel) => channel.selected)
        .sort((a, b) => a.pin - b.pin)
        .map(({ pin, name, color, pull, invert, trigger }) => ({ pin, name, color, pull, invert: Boolean(invert), trigger })),
      capture: captureOperation.params,
      decoder: {
        type: read("decoderType") || "summary",
        data_pin: Number(read("decoderDataPin") || 16),
        clock_pin: Number(read("decoderClockPin") || 17),
        cs_pin: Number(read("decoderCsPin") || 18),
        baud: read("decoderBaud") || 115200,
        spi_mode: Number(read("decoderSpiMode") || 0),
        region_start: read("logicRegionStart") || "",
        region_end: read("logicRegionEnd") || ""
      },
      view: {
        zoom: state.zoom,
        view_start: state.viewStart,
        cursor_a: state.cursorA,
        cursor_b: state.cursorB,
        cursor_target: state.cursorTarget
      },
      ai_replay: {
        capture_operation: captureOperation,
        decode_operation: normalizeOperation("logic.decode", {}, { panel: "logic", show: true })
      }
    };
  }

  function applyAnalyzerSession(session) {
    if (!session || typeof session !== "object" || session.schema !== "embedlabs.rpmon.logic-session.v1") {
      throw new Error(t("invalidSession"));
    }
    const channels = Array.isArray(session.channels) ? session.channels : [];
    if (!channels.length) throw new Error(t("invalidSession"));
    state.channels.forEach((channel) => {
      channel.selected = false;
      channel.trigger = "ignore";
    });
    channels.forEach((item) => {
      const pin = Number(item.pin);
      if (!exposedPinSet.has(pin)) return;
      const channel = channelFor(pin);
      if (!channel) return;
      channel.selected = true;
      channel.name = String(item.name || `GP${pin}`);
      channel.color = String(item.color || channel.color || "#42d392");
      channel.pull = String(item.pull || "none");
      channel.invert = Boolean(item.invert);
      channel.trigger = String(item.trigger || "ignore");
    });
    const capture = session.capture && typeof session.capture === "object" ? session.capture : {};
    setValue("logicSampleRate", capture.sample_rate ?? 1000000);
    setValue("logicPreSamples", capture.pre_samples_requested ?? capture.pre_samples ?? 120);
    setValue("logicPostSamples", capture.post_samples ?? capture.samples ?? 4096);
    setValue("logicSearchSamples", capture.search_samples ?? 0);
    setValue("logicBurstCount", capture.burst_count ?? 1);
    setValue("logicPull", capture.pull ?? "none");
    setValue("logicSampleMode", capture.sample_mode ?? "single");
    setValue("logicTriggerType", capture.trigger_type ?? "none");
    if (capture.trigger_pin !== undefined) setValue("logicTriggerPin", capture.trigger_pin);
    if (capture.pattern_base !== undefined) setValue("logicPatternBase", capture.pattern_base);
    if (capture.pattern_bits !== undefined) setValue("logicPattern", capture.pattern_bits);

    const decoder = session.decoder && typeof session.decoder === "object" ? session.decoder : {};
    setValue("decoderType", decoder.type ?? "summary");
    setValue("decoderDataPin", decoder.data_pin ?? 16);
    setValue("decoderClockPin", decoder.clock_pin ?? 17);
    setValue("decoderCsPin", decoder.cs_pin ?? 18);
    setValue("decoderBaud", decoder.baud ?? 115200);
    setValue("decoderSpiMode", decoder.spi_mode ?? 0);
    setValue("logicRegionStart", decoder.region_start ?? "");
    setValue("logicRegionEnd", decoder.region_end ?? "");

    const view = session.view && typeof session.view === "object" ? session.view : {};
    state.zoom = clamp(Number(view.zoom ?? 1), 1, 64);
    state.viewStart = Math.max(0, Number(view.view_start ?? 0));
    state.cursorA = Math.max(0, Number(view.cursor_a ?? 0));
    state.cursorB = view.cursor_b === null || view.cursor_b === undefined ? null : Math.max(0, Number(view.cursor_b));
    state.cursorTarget = view.cursor_target === "b" ? "b" : "a";
    state.activePreset = String(session.active_preset || "");
    state.userViewLocked = state.zoom > 1;
    state.decoderAnnotations = [];
    setPanel("logic");
    render();
  }

  function buildAnalyzerEvidence() {
    const logic = state.snapshot?.logic ?? null;
    const meta = state.snapshot?.logic_meta ?? null;
    const canvas = $("logicCanvas");
    return {
      schema: "embedlabs.rpmon.logic-evidence.v1",
      created_at: new Date().toISOString(),
      session: buildAnalyzerSession({ allowEmpty: true }),
      device: state.snapshot?.device ?? null,
      firmware: state.snapshot?.firmware ?? null,
      transport: state.snapshot?.transport ?? null,
      logic,
      logic_meta: meta,
      decoder_annotations: state.decoderAnnotations,
      waveform_image: canvas ? canvas.toDataURL("image/png") : "",
      last_export_format: read("logicExportFormat") || "",
      last_export: state.lastExport || "",
      last_response: state.snapshot?.lastResponse ?? "",
      status: {
        text: state.logicStatus,
        tone: state.logicStatusTone,
        capture_pending: state.capturePending,
        run_active: state.logicRunActive
      }
    };
  }

  function saveAnalyzerSessionFile() {
    let session;
    try {
      session = buildAnalyzerSession();
    } catch (error) {
      showErrorDialog(error.message);
      setLogicStatus(error.message, "warning");
      return;
    }
    const blob = new Blob([JSON.stringify(session, null, 2)], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = `embed-labs-rp2350-logic-session-${new Date().toISOString().replace(/[:.]/g, "-")}.json`;
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
    URL.revokeObjectURL(url);
    setLogicStatus(t("sessionSaved"), "ok");
  }

  function saveAnalyzerEvidenceFile() {
    let evidence;
    try {
      evidence = buildAnalyzerEvidence();
    } catch (error) {
      showErrorDialog(error.message);
      setLogicStatus(error.message, "warning");
      return;
    }
    const blob = new Blob([JSON.stringify(evidence, null, 2)], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = `embed-labs-rp2350-logic-evidence-${new Date().toISOString().replace(/[:.]/g, "-")}.json`;
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
    URL.revokeObjectURL(url);
    setLogicStatus(t("evidenceSaved"), "ok");
  }

  async function loadAnalyzerSessionFile(file) {
    if (!file) return;
    try {
      const session = JSON.parse(await file.text());
      applyAnalyzerSession(session);
      setLogicStatus(t("sessionLoaded"), "ok");
    } catch (error) {
      showErrorDialog(error.message || t("invalidSession"));
      setLogicStatus(error.message || t("invalidSession"), "warning");
    }
  }

  function channelLiveSummary(pin) {
    const logic = state.snapshot?.logic;
    if (!logic?.words?.length || !pinInLogic(logic, pin)) {
      const live = gpioLevelFor(pin);
      return { text: live.text, levelClass: live.level === null ? "" : live.level ? "high" : "low" };
    }
    const channel = pin - logic.pin_base;
    const lastSample = Math.max(0, Number(logic.samples ?? 1) - 1);
    const level = displayLevelAt(logic, lastSample, channel);
    const summary = summarizePin(logic, pin);
    return {
      text: `${level ? "H" : "L"} ${summary.edges}e ${summary.highPercent.toFixed(0)}%`,
      levelClass: level ? "high" : "low"
    };
  }

  function triggerGuideFromForm() {
    let params;
    try {
      params = logicParamsFromForm();
    } catch {
      return "";
    }
    const triggerType = String(params.trigger_type || "none");
    if (triggerType === "none") return t("triggerGuideNone");
    if (triggerType === "pattern" || params.trigger_mode === "pattern") return t("triggerGuidePattern");
    if (params.trigger_pin === undefined || params.trigger_pin === null) return "";
    const level = knownLevelForPin(params.trigger_pin);
    const current = level.level === null ? t("triggerGuideUnknown") : `${t("triggerGuideCurrent")} ${level.level ? t("high") : t("low")}`;
    const guideMap = {
      "level-high": "triggerGuideHigh",
      "level-low": "triggerGuideLow",
      rising: "triggerGuideRising",
      falling: "triggerGuideFalling"
    };
    const guide = t(guideMap[triggerType] || "triggerGuidePattern");
    const joiner = state.language === "zh" ? "；" : "; ";
    return `GP${params.trigger_pin}: ${current}${joiner}${guide}`;
  }

  function knownLevelForPin(pin) {
    const live = gpioLevelFor(pin);
    if (live.level !== null) return live;
    const logic = state.snapshot?.logic;
    if (logic?.words?.length && pinInLogic(logic, pin)) {
      const channel = Number(pin) - Number(logic.pin_base ?? 0);
      const sample = Math.max(0, Number(logic.samples ?? 1) - 1);
      const level = displayLevelAt(logic, sample, channel);
      return { level, text: level ? "H" : "L" };
    }
    return { level: null, text: t("idle") };
  }

  function gpioLevelFor(pin) {
    const levels = Array.isArray(state.snapshot?.gpio) ? state.snapshot.gpio : [];
    const latest = [...levels].reverse().find((item) => Number(item.pin) === Number(pin));
    if (!latest) return { level: null, text: t("idle") };
    const level = Boolean(latest.level);
    return { level, text: level ? "H" : "L" };
  }

  function renderLogic(logic) {
    renderMetrics(logic);
    const canvas = $("logicCanvas");
    const overview = $("logicOverview");
    const pinsForHeight = logic?.words?.length ? visiblePinsFor(logic) : selectedChannelPins();
    const desiredHeight = Math.max(320, 52 + Math.max(1, pinsForHeight.length) * 58);
    const scrollHostHeight = canvas.parentElement?.clientHeight || 0;
    canvas.style.height = `${Math.max(desiredHeight, scrollHostHeight)}px`;
    const { ctx, width, height } = prepareCanvasForDisplay(canvas);
    ctx.clearRect(0, 0, width, height);
    fillCanvasBackground(ctx, width, height);

    if (!logic?.words?.length) {
      drawIdlePreview(ctx, width, height, logic);
      drawOverview(null);
      renderMeasurements(logic);
      return;
    }

    const visiblePins = visiblePinsFor(logic);
    const layout = logicDrawLayout(width, height, visiblePins.length);
    const { left, axisTop, top, plotWidth, plotHeight, rowHeight } = layout;
    const view = currentView(logic);
    drawGrid(ctx, left, axisTop, top, plotWidth, plotHeight, logic, view);
    drawRegion(ctx, logic, left, top, plotWidth, plotHeight, view);

    visiblePins.forEach((pin, index) => {
      const channel = channelFor(pin);
      const y = top + index * rowHeight;
      drawChannelBackground(ctx, index, 0, y, width, rowHeight);
      drawChannelLabel(ctx, pin, y, rowHeight);
      drawLevelGuide(ctx, left, y + 4, plotWidth, rowHeight - 10);
      drawTrace(ctx, logic, pin - logic.pin_base, left, y + 4, plotWidth, rowHeight - 10, channel?.color || "#42d392", view);
    });
    drawDecoderAnnotations(ctx, logic, visiblePins, left, top, plotWidth, plotHeight, view, rowHeight);
    drawMarkers(ctx, logic, left, top, plotWidth, plotHeight, view);
    drawCursors(ctx, logic, left, top, plotWidth, plotHeight, view);
    drawOverview(logic);
    renderMeasurements(logic);
  }

  function prepareCanvasForDisplay(canvas) {
    const dpr = Math.max(1, window.devicePixelRatio || 1);
    const rect = canvas.getBoundingClientRect();
    const parent = canvas.parentElement;
    const measuredWidth = parent?.clientWidth || rect.width || canvas.clientWidth || 1120;
    const measuredHeight = rect.height || parent?.clientHeight || canvas.clientHeight || 320;
    const cssWidth = Math.max(320, Math.floor(measuredWidth));
    const cssHeight = Math.max(1, Math.floor(measuredHeight));
    const targetWidth = Math.max(1, Math.round(cssWidth * dpr));
    const targetHeight = Math.max(1, Math.round(cssHeight * dpr));
    if (canvas.width !== targetWidth || canvas.height !== targetHeight) {
      canvas.width = targetWidth;
      canvas.height = targetHeight;
    }
    canvas.style.width = "100%";
    canvas.style.height = `${cssHeight}px`;
    canvas.__logicalWidth = cssWidth;
    canvas.__logicalHeight = cssHeight;
    canvas.__pixelRatio = targetWidth / cssWidth;
    const ctx = canvas.getContext("2d");
    ctx.imageSmoothingEnabled = false;
    ctx.setTransform(targetWidth / cssWidth, 0, 0, targetHeight / cssHeight, 0, 0);
    return { ctx, width: cssWidth, height: cssHeight, dpr };
  }

  function logicDrawLayout(width, height, pinCount) {
    const left = 80;
    const axisTop = 18;
    const top = 54;
    const right = 18;
    const bottom = 22;
    const rows = Math.max(1, Number(pinCount) || 1);
    const availableHeight = Math.max(44, height - top - bottom);
    const rowHeight = Math.max(44, availableHeight / rows);
    const plotHeight = rowHeight * rows;
    return {
      left,
      axisTop,
      top,
      right,
      bottom,
      rowHeight,
      plotWidth: Math.max(1, width - left - right),
      plotHeight
    };
  }

  function drawIdlePreview(ctx, width, height, logic) {
    const pins = selectedChannelPins();
    const layout = logicDrawLayout(width, height, pins.length);
    const { left, axisTop, top, plotWidth, plotHeight, rowHeight } = layout;
    const previewLogic = {
      sample_rate: Number(logic?.sample_rate ?? read("logicSampleRate") ?? 1000000),
      samples: Number(logic?.samples ?? Math.max(1, positiveInteger(read("logicPreSamples"), 0) + positiveInteger(read("logicPostSamples"), 4096)))
    };
    const view = currentView(previewLogic);
    drawGrid(ctx, left, axisTop, top, plotWidth, plotHeight, previewLogic, view);
    pins.forEach((pin, index) => {
      const y = top + index * rowHeight;
      drawChannelBackground(ctx, index, 0, y, width, rowHeight);
      drawChannelLabel(ctx, pin, y, rowHeight);
      drawLevelGuide(ctx, left, y + 4, plotWidth, rowHeight - 10);
    });
  }

  function emptyLogicMessage(logic) {
    if (state.logicRunActive || state.capturePending) return t("waitingForTrigger");
    if (logic?.configured) return t("configuredNoCapture");
    return t("noCapture");
  }

  function fillCanvasBackground(ctx, width, height) {
    ctx.fillStyle = "#0b1018";
    ctx.fillRect(0, 0, width, height);
    ctx.strokeStyle = "rgba(255,255,255,0.08)";
    ctx.strokeRect(0.5, 0.5, width - 1, height - 1);
  }

  function drawChannelBackground(ctx, index, x, y, width, height) {
    ctx.fillStyle = index % 2 === 0 ? "rgba(255,255,255,0.025)" : "rgba(255,255,255,0.045)";
    ctx.fillRect(x, y, width, height);
    ctx.strokeStyle = "rgba(255,255,255,0.06)";
    ctx.beginPath();
    ctx.moveTo(x, y + height + 0.5);
    ctx.lineTo(x + width, y + height + 0.5);
    ctx.stroke();
  }

  function drawChannelLabel(ctx, pin, y, rowHeight) {
    const channel = channelFor(pin);
    const live = gpioLevelFor(pin);
    const label = channel?.name && channel.name !== `GP${pin}` ? `${channel.name}` : `GP${pin}`;
    ctx.fillStyle = "#cdd6e3";
    ctx.font = "12px ui-monospace, SFMono-Regular, Menlo, monospace";
    ctx.fillText(`GP${pin}`, 10, y + 18);
    ctx.fillStyle = "#8b95a5";
    ctx.font = "10px ui-monospace, SFMono-Regular, Menlo, monospace";
    ctx.fillText(label.slice(0, 10), 10, y + 34);
    ctx.fillStyle = live.level ? "#42d392" : "#8b95a5";
    ctx.fillText(live.text, 10, y + Math.min(rowHeight - 8, 50));
  }

  function drawLevelGuide(ctx, x, y, width, height) {
    const highY = y + 7;
    const lowY = y + height - 7;
    ctx.strokeStyle = "rgba(255,255,255,0.08)";
    ctx.lineWidth = 1;
    ctx.setLineDash([3, 5]);
    ctx.beginPath();
    ctx.moveTo(x, highY);
    ctx.lineTo(x + width, highY);
    ctx.moveTo(x, lowY);
    ctx.lineTo(x + width, lowY);
    ctx.stroke();
    ctx.setLineDash([]);
    ctx.fillStyle = "#536071";
    ctx.font = "10px ui-monospace, SFMono-Regular, Menlo, monospace";
    ctx.fillText("H", x - 14, highY + 3);
    ctx.fillText("L", x - 14, lowY + 3);
  }

  function drawStartPoint(ctx, x, y, level, label) {
    ctx.fillStyle = level === null ? "#8b95a5" : level ? "#42d392" : "#667085";
    ctx.beginPath();
    ctx.arc(x, y, 4, 0, Math.PI * 2);
    ctx.fill();
    ctx.font = "10px ui-monospace, SFMono-Regular, Menlo, monospace";
    ctx.fillText(label, x + 8, y - 6);
  }

  function drawGrid(ctx, left, axisTop, plotTop, width, height, logic, view) {
    ctx.fillStyle = "rgba(11,16,24,0.92)";
    ctx.fillRect(0, 0, left + width + 22, plotTop - 1);
    ctx.strokeStyle = "rgba(255,255,255,0.12)";
    ctx.beginPath();
    ctx.moveTo(left, plotTop - 0.5);
    ctx.lineTo(left + width, plotTop - 0.5);
    ctx.stroke();
    ctx.strokeStyle = "rgba(255,255,255,0.08)";
    ctx.lineWidth = 1;
    const lines = Math.max(4, Math.min(8, Math.floor(width / 135)));
    for (let i = 0; i <= lines; i += 1) {
      const x = left + (i / lines) * width;
      ctx.beginPath();
      ctx.moveTo(x, plotTop);
      ctx.lineTo(x, plotTop + height);
      ctx.stroke();
      if (i === lines) continue;
      const sample = Math.round(view.start + (i / lines) * view.span);
      const timeUs = (sample / Math.max(1, logic.sample_rate)) * 1000000;
      ctx.fillStyle = "#667085";
      ctx.font = "11px ui-monospace, SFMono-Regular, Menlo, monospace";
      ctx.fillText(formatAxisTime(timeUs), x + 3, axisTop);
    }
  }

  function formatAxisTime(timeUs) {
    const value = Number(timeUs);
    if (!Number.isFinite(value)) return "-";
    if (Math.abs(value) >= 1000000) return `${formatNumber(value / 1000000)}s`;
    if (Math.abs(value) >= 1000) return `${formatNumber(value / 1000)}ms`;
    return `${formatNumber(value)}us`;
  }

  function drawRegion(ctx, logic, left, top, width, height, view) {
    let region;
    try {
      region = sampleWindowFromForm(logic);
    } catch {
      return;
    }
    if (!region.active) return;
    const visibleStart = Math.max(region.start, view.start);
    const visibleEnd = Math.min(region.end, view.start + view.span);
    if (visibleEnd <= visibleStart) return;
    const x = left + ((visibleStart - view.start) / Math.max(1, view.span)) * width;
    const w = ((visibleEnd - visibleStart) / Math.max(1, view.span)) * width;
    ctx.fillStyle = "rgba(246, 200, 95, 0.13)";
    ctx.fillRect(x, top, Math.max(1, w), height);
    ctx.strokeStyle = "rgba(246, 200, 95, 0.55)";
    ctx.strokeRect(x + 0.5, top + 0.5, Math.max(1, w), height - 1);
  }

  function drawMarkers(ctx, logic, left, top, width, height, view) {
    const triggerSample = logic.trigger_found ? markerRelativeSample(logic, logic.trigger_sample) : NaN;
    if (logic.trigger_found) {
      drawSampleMarker(ctx, "T", triggerSample, "#8bd3ff", left, top, width, height, view);
    }
    const requestedBurstCount = Number(logic.requested_burst_count ?? logic.burst_count ?? 1);
    if (!Number.isFinite(requestedBurstCount) || requestedBurstCount <= 1) return;
    const burstSamples = Array.isArray(logic.burst_samples) ? logic.burst_samples : [];
    burstSamples.slice(0, 16).forEach((sample, index) => {
      const burstSample = markerRelativeSample(logic, sample);
      if (Number.isFinite(triggerSample) && Math.abs(burstSample - triggerSample) <= 1) return;
      drawSampleMarker(ctx, `B${index + 1}`, burstSample, "#b48cff", left, top, width, height, view);
    });
  }

  function drawDecoderAnnotations(ctx, logic, visiblePins, left, top, width, height, view, rowHeight) {
    const annotations = (state.decoderAnnotations || [])
      .filter((item) => item && Number.isFinite(Number(item.sample)))
      .filter((item) => item.sample >= view.start && item.sample <= view.start + view.span)
      .slice(0, 96);
    if (!annotations.length) return;
    const pinToIndex = new Map(visiblePins.map((pin, index) => [Number(pin), index]));
    ctx.save();
    ctx.font = "10px ui-monospace, SFMono-Regular, Menlo, monospace";
    annotations.forEach((item, index) => {
      const pin = Number(item.pin);
      const laneIndex = pinToIndex.has(pin) ? pinToIndex.get(pin) : Math.min(visiblePins.length - 1, Math.max(0, index % Math.max(1, visiblePins.length)));
      const x = left + ((Number(item.sample) - view.start) / Math.max(1, view.span)) * width;
      const laneTop = top + laneIndex * rowHeight;
      const y = laneTop + 19 + (index % 2) * 17;
      const color = decoderToneColor(item.tone || item.protocol);
      ctx.strokeStyle = color;
      ctx.lineWidth = 1;
      ctx.setLineDash([2, 4]);
      ctx.beginPath();
      ctx.moveTo(x, laneTop + 4);
      ctx.lineTo(x, Math.min(top + height, laneTop + rowHeight - 6));
      ctx.stroke();
      ctx.setLineDash([]);

      const label = String(item.label || "").slice(0, 18);
      const detail = String(item.detail || "").slice(0, 26);
      const text = detail ? `${label} ${detail}` : label;
      const textWidth = Math.min(180, ctx.measureText(text).width + 12);
      const bx = clamp(x + 5, left + 2, left + width - textWidth - 2);
      ctx.fillStyle = "rgba(11, 16, 24, 0.9)";
      roundRect(ctx, bx, y - 13, textWidth, 16, 5);
      ctx.fill();
      ctx.strokeStyle = color;
      ctx.stroke();
      ctx.fillStyle = color;
      ctx.fillText(text, bx + 6, y - 2);
    });
    ctx.restore();
  }

  function decoderToneColor(tone) {
    const key = String(tone || "").toLowerCase();
    if (key.includes("uart")) return "#66a6ff";
    if (key.includes("spi")) return "#b48cff";
    if (key.includes("i2c")) return "#42d392";
    if (key.includes("edge")) return "#f6c85f";
    if (key.includes("stop") || key.includes("nack")) return "#ff7a7a";
    return "#8bd3ff";
  }

  function roundRect(ctx, x, y, width, height, radius) {
    const r = Math.min(radius, width / 2, height / 2);
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.lineTo(x + width - r, y);
    ctx.quadraticCurveTo(x + width, y, x + width, y + r);
    ctx.lineTo(x + width, y + height - r);
    ctx.quadraticCurveTo(x + width, y + height, x + width - r, y + height);
    ctx.lineTo(x + r, y + height);
    ctx.quadraticCurveTo(x, y + height, x, y + height - r);
    ctx.lineTo(x, y + r);
    ctx.quadraticCurveTo(x, y, x + r, y);
    ctx.closePath();
  }

  function drawSampleMarker(ctx, label, sample, color, left, top, width, height, view) {
    if (!Number.isFinite(sample)) return;
    if (sample < view.start || sample > view.start + view.span) return;
    const x = left + ((sample - view.start) / Math.max(1, view.span)) * width;
    ctx.strokeStyle = color;
    ctx.lineWidth = 1.2;
    ctx.setLineDash([4, 4]);
    ctx.beginPath();
    ctx.moveTo(x, top);
    ctx.lineTo(x, top + height);
    ctx.stroke();
    ctx.setLineDash([]);
    ctx.fillStyle = color;
    ctx.font = "11px ui-monospace, SFMono-Regular, Menlo, monospace";
    ctx.fillText(label, x + 4, top + 40);
  }

  function markerRelativeSample(logic, sample) {
    const numeric = Number(sample);
    if (!Number.isFinite(numeric)) return NaN;
    const samples = Math.max(0, Number(logic?.samples ?? 0));
    const offset = Number(logic?.sample_offset ?? logic?.capture_start_sample ?? 0);
    if (numeric >= 0 && (samples <= 0 || numeric < samples)) return numeric;
    if (Number.isFinite(offset) && offset > 0 && numeric >= offset) return numeric - offset;
    return numeric;
  }

  function triggerFocusedViewStart(logic) {
    const trigger = markerRelativeSample(logic, logic.trigger_sample);
    if (!Number.isFinite(trigger)) return 0;
    const samples = Math.max(1, Number(logic.samples ?? 1));
    const span = Math.max(16, samples / Math.max(1, state.zoom));
    const preSamples = Math.max(0, Number(logic.pre_samples ?? 0));
    const lead = preSamples > 0 ? Math.min(preSamples, span * 0.25) : span * 0.25;
    return clamp(trigger - lead, 0, Math.max(0, samples - span));
  }

  function triggerFocusedZoom(logic) {
    const samples = Math.max(1, Number(logic.samples ?? 1));
    const preSamples = Math.max(0, Number(logic.pre_samples ?? 0));
    const focusSpan = preSamples > 0
      ? Math.max(64, preSamples * 4)
      : Math.max(64, Math.floor(samples * 0.12));
    const span = clamp(focusSpan, 16, samples);
    return Math.max(1, samples / span);
  }

  function drawTrace(ctx, logic, channelIndex, x, y, width, height, color, view) {
    const samples = Math.max(1, logic.samples || 1);
    const start = Math.max(0, Math.floor(view.start));
    const end = Math.min(samples - 1, Math.ceil(view.start + view.span));
    const visibleSamples = Math.max(1, end - start + 1);
    const step = visibleSamples <= 120000 ? 1 : Math.max(1, Math.floor(visibleSamples / 120000));
    const highY = y + 7;
    const lowY = y + height - 7;
    ctx.strokeStyle = color;
    ctx.lineWidth = 2;
    ctx.beginPath();
    let first = true;
    let previousY = lowY;
    let firstY = lowY;
    let firstLevel = false;
    for (let sample = start; sample <= end; sample += step) {
      const px = x + ((sample - view.start) / Math.max(1, view.span)) * width;
      const level = displayLevelAt(logic, sample, channelIndex);
      const py = level ? highY : lowY;
      if (first) {
        ctx.moveTo(px, py);
        firstY = py;
        firstLevel = level;
        first = false;
      } else {
        ctx.lineTo(px, previousY);
        ctx.lineTo(px, py);
      }
      previousY = py;
    }
    ctx.stroke();
    drawStartPoint(ctx, x, firstY, firstLevel, `s${start} ${firstLevel ? "H" : "L"}`);
  }

  function drawCursors(ctx, logic, left, top, width, height, view) {
    const cursors = [["A", state.cursorA, state.cursorColors.a || "#f6c85f"], ["B", state.cursorB, state.cursorColors.b || "#ff7a7a"]];
    for (const [label, sample, color] of cursors) {
      if (sample === null || sample === undefined) continue;
      if (sample < view.start || sample > view.start + view.span) continue;
      const x = left + ((sample - view.start) / Math.max(1, view.span)) * width;
      ctx.strokeStyle = color;
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.moveTo(x, top);
      ctx.lineTo(x, top + height);
      ctx.stroke();
      ctx.fillStyle = color;
      ctx.font = "12px ui-monospace, SFMono-Regular, Menlo, monospace";
      ctx.fillText(label, x + 4, top + 26);
    }
    if (logic.trigger_pin !== undefined) {
      ctx.fillStyle = "#8bd3ff";
      ctx.font = "12px ui-monospace, SFMono-Regular, Menlo, monospace";
      ctx.fillText(`${t("trigger")} GP${logic.trigger_pin}`, left + 8, top + height - 8);
    }
  }

  function drawOverview(logic) {
    const canvas = $("logicOverview");
    canvas.style.height = "34px";
    canvas.style.maxHeight = "34px";
    canvas.style.minHeight = "34px";
    const { ctx, width, height } = prepareCanvasForDisplay(canvas);
    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = "#0f131a";
    ctx.fillRect(0, 0, width, height);
    ctx.strokeStyle = "rgba(255,255,255,0.08)";
    ctx.strokeRect(0.5, 0.5, width - 1, height - 1);
    if (!logic?.words?.length) return;
    const pins = visiblePinsFor(logic);
    const rowHeight = height / Math.max(1, pins.length);
    pins.forEach((pin, index) => {
      drawMiniTrace(ctx, logic, pin - logic.pin_base, 8, index * rowHeight + 2, width - 16, Math.max(2, rowHeight - 4), channelFor(pin)?.color || "#42d392");
    });
    try {
      const region = sampleWindowFromForm(logic);
      if (region.active) {
        const x = 8 + (region.start / Math.max(1, logic.samples)) * (width - 16);
        const w = ((region.end - region.start) / Math.max(1, logic.samples)) * (width - 16);
        ctx.fillStyle = "rgba(246, 200, 95, 0.16)";
        ctx.fillRect(x, 2, Math.max(1, w), height - 4);
      }
    } catch {
      // Invalid region input is surfaced when the user decodes or exports.
    }
    const view = currentView(logic);
    const viewX = 8 + (view.start / Math.max(1, logic.samples)) * (width - 16);
    const viewW = (view.span / Math.max(1, logic.samples)) * (width - 16);
    ctx.strokeStyle = "#f6c85f";
    ctx.lineWidth = 2;
    ctx.strokeRect(viewX, 2, Math.max(10, viewW), height - 4);
    ctx.lineWidth = 1;
  }

  function drawMiniTrace(ctx, logic, channelIndex, x, y, width, height, color) {
    const samples = Math.max(1, Number(logic.samples || 1));
    const step = Math.max(1, Math.floor(samples / Math.max(80, width)));
    const highY = y + Math.max(1, height * 0.22);
    const lowY = y + Math.max(2, height * 0.78);
    ctx.save();
    ctx.strokeStyle = color;
    ctx.lineWidth = 1;
    ctx.beginPath();
    let first = true;
    let previousY = lowY;
    for (let sample = 0; sample < samples; sample += step) {
      const px = x + (sample / Math.max(1, samples - 1)) * width;
      const py = displayLevelAt(logic, sample, channelIndex) ? highY : lowY;
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
    ctx.restore();
  }

  function renderMetrics(logic) {
    const params = (() => {
      try { return logicParamsFromForm(); } catch { return null; }
    })();
    const active = logic || params || {};
    const rate = Number(active.sample_rate ?? 0);
    const samples = Number(active.samples ?? 0);
    const durationMs = rate > 0 ? (samples / rate) * 1000 : 0;
    const memoryBytes = Math.ceil(((Number(active.pin_count ?? 1) * samples) / 8));
    const maxSamples = logicMaxSamplesFor(Number(active.pin_count ?? 1));
    const maxMemory = Number(state.snapshot?.logic_caps?.buffer_bytes ?? 0);
    const items = [
      [t("rate"), rate ? formatFrequency(rate) : "-"],
      [t("samples"), samples ? formatNumber(samples) : "-"],
      [t("maxSamples"), maxSamples ? formatNumber(maxSamples) : "-"],
      [t("window"), durationMs ? `${formatNumber(durationMs)} ms` : "-"],
      [t("pins"), active.pin_count ? `GP${active.pin_base}..GP${Number(active.pin_base) + Number(active.pin_count) - 1}` : "-"],
      [t("trigger"), triggerMetric(active)],
      [t("runState"), state.logicRunActive ? `${t("live")} #${state.logicRunIteration}` : t("stopped")],
      [t("memory"), memoryBytes ? formatMemoryPair(memoryBytes, maxMemory) : "-"]
    ];
    $("logicMetrics").innerHTML = items.map(([label, value]) => `<div class="metric small"><span>${label}</span><strong>${escapeHTML(value)}</strong></div>`).join("");
  }

  function triggerMetric(active) {
    const label = localizedTrigger(active.trigger_type || active.trigger_mode);
    const parts = [label];
    if (active.trigger_found) {
      const rel = markerRelativeSample(active, active.trigger_sample);
      if (Number.isFinite(rel)) parts.push(`@${formatNumber(rel)}`);
    }
    if (Number(active.burst_count ?? 1) > 1 || Number(active.burst_found ?? 0) > 0) {
      parts.push(`${Number(active.burst_found ?? 0)}/${Number(active.burst_count ?? 1)}`);
    }
    return parts.join(" ");
  }

  function renderMeasurements(logic) {
    if (!logic?.words?.length) {
      $("logicMeasurements").textContent = "";
      return;
    }
    let analysisLogic = logic;
    let region = null;
    try {
      region = sampleWindowFromForm(logic);
      analysisLogic = slicedLogicForRegion(logic);
    } catch {
      // Region errors are reported on decode/export actions.
    }
    const rate = Math.max(1, logic.sample_rate || 1);
    const delta = state.cursorB === null ? null : Math.abs(state.cursorB - state.cursorA);
    const deltaText = delta === null ? t("bUnset") : `${formatNumber((delta / rate) * 1000000)} us / ${formatNumber(delta)} ${t("samples")}`;
    const regionText = region?.active ? ` | ${t("regionStart")}=${region.start} ${t("regionEnd")}=${region.end}` : "";
    const triggerText = logic.trigger_found ? ` | T=${formatNumber(markerRelativeSample(logic, logic.trigger_sample))}` : "";
    $("logicMeasurements").textContent = `A=${Math.round(state.cursorA)} B=${state.cursorB === null ? "-" : Math.round(state.cursorB)} Delta=${deltaText}${triggerText}${regionText}`;
  }

  function renderProtocolLogs(snapshot) {
    const events = snapshot.events ?? [];
    const selectedUart = selectedProtocolDevice("uart");
    const selectedI2c = selectedProtocolDevice("i2c");
    const selectedSpi = selectedProtocolDevice("spi");
    const uartEvents = events.filter((event) => event.proto === "uart" && Number(event.channel) === Number(selectedUart?.id));
    const i2cEvents = events.filter((event) => event.proto === "i2c" && Number(event.channel) === Number(selectedI2c?.id));
    const spiEvents = events.filter((event) => event.proto === "spi" && Number(event.channel) === Number(selectedSpi?.id));
    renderProtocolPanel("uart", uartEvents, read("uartDisplayMode") || "terminal");
    renderProtocolPanel("i2c", i2cEvents, read("i2cDisplayMode") || "decoded");
    renderProtocolPanel("spi", spiEvents, read("spiDisplayMode") || "decoded");
  }

  function renderProtocolPanel(protocol, events, mode) {
    const log = $(`${protocol}Log`);
    const stats = $(`${protocol}Stats`);
    if (!log) return;
    const lines = events.length ? events.map((event, index) => formatProtocolEvent(event, mode, index + 1)) : [t("noEvents")];
    log.textContent = lines.join("\n");
    if (stats) {
      const txBytes = events.filter((event) => event.dir === "tx").reduce((sum, event) => sum + bytesFromHex(event.hex).length, 0);
      const rxBytes = events.filter((event) => event.dir === "rx").reduce((sum, event) => sum + bytesFromHex(event.hex).length, 0);
      stats.innerHTML = `<span>${t("frames")}: ${events.length}</span><span>${t("bytesTx")}: ${txBytes}</span><span>${t("bytesRx")}: ${rxBytes}</span>`;
    }
  }

  function clearProtocolLog(protocol) {
    const current = Array.isArray(state.snapshot?.events) ? state.snapshot.events : [];
    const selected = selectedProtocolDevice(protocol);
    state.snapshot = {
      ...(state.snapshot ?? {}),
      events: current.filter((event) => !(event.proto === protocol && Number(event.channel) === Number(selected?.id))),
      lastResponse: t("clearLog")
    };
    render();
  }

  function formatProtocolEvent(event, mode, index) {
    if (mode === "json") return JSON.stringify(event);
    const dir = String(event.dir || "event").toUpperCase();
    const channel = event.channel !== undefined ? `#${event.channel}` : "";
    const hex = String(event.hex || "");
    const payload = formatProtocolBytes(hex, mode);
    if (mode === "terminal" && event.proto === "uart") {
      return `[${index}] ${dir}${channel} ${payload}`;
    }
    if (mode === "hex") {
      return `[${index}] ${dir}${channel} ${formatProtocolBytes(hex, "hex")}`;
    }
    if (mode === "binary") {
      return `[${index}] ${dir}${channel} ${formatProtocolBytes(hex, "binary")}`;
    }
    return `[${index}] ${dir}${channel}\n${payload || JSON.stringify(event)}`;
  }

  function renderQueue() {
    const list = $("aiQueue");
    const detail = $("aiDetail");
    if (!list || !detail) return;
    if (!state.queue.length) {
      list.innerHTML = `<p class="empty">${t("aiQueueEmpty")}</p>`;
      detail.innerHTML = `<p class="empty">${t("selectAICall")}</p>`;
      return;
    }
    const selected = clamp(Number(state.selectedQueueIndex || 0), 0, state.queue.length - 1);
    state.selectedQueueIndex = selected;
    list.innerHTML = state.queue.map((item, index) => {
      const operation = item.operation ?? {};
      const duration = item.endedAt ? `${item.endedAt - item.startedAt}ms` : "running";
      const statusClass = item.status === "failed" ? "failed" : "";
      return `
        <button type="button" class="ai-call-row ${index === selected ? "active" : ""}" data-ai-index="${index}">
          <span class="ai-call-status ${statusClass}">${escapeHTML(item.status || "-")}</span>
          <span class="ai-call-main">
            <strong>${escapeHTML(operation.action || "-")}</strong>
            <span>${escapeHTML(item.startedAt ? item.startedAt.toLocaleTimeString() : "-")} / ${escapeHTML(duration)}</span>
          </span>
          <span>#${state.queue.length - index}</span>
        </button>
      `;
    }).join("");
    renderAIDetail(state.queue[selected]);
  }

  function renderAIDetail(item) {
    const detail = $("aiDetail");
    if (!detail) return;
    if (!item) {
      detail.innerHTML = `<p class="empty">${t("selectAICall")}</p>`;
      return;
    }
    const duration = item.endedAt ? `${item.endedAt - item.startedAt}ms` : "running";
    detail.innerHTML = [
      detailSection(t("call"), [
        [t("startedAt"), item.startedAt ? item.startedAt.toLocaleString() : "-"],
        [t("duration"), duration],
        ["Status", item.status || "-"],
        ["Action", item.operation?.action || "-"]
      ]),
      detailSection(t("parameters"), item.operation?.params ?? {}),
      item.error ? detailSection(t("error"), item.error) : "",
      item.result ? detailSection(t("result"), summarizeResultForDetail(item.result)) : ""
    ].join("");
  }

  function detailSection(title, content) {
    if (Array.isArray(content)) {
      return `<section class="ai-detail-section"><strong>${escapeHTML(title)}</strong>${content.map(([label, value]) => `<span>${escapeHTML(label)}: ${escapeHTML(value)}</span>`).join("")}</section>`;
    }
    return `<section class="ai-detail-section"><strong>${escapeHTML(title)}</strong><pre>${escapeHTML(typeof content === "string" ? content : JSON.stringify(content, null, 2))}</pre></section>`;
  }

  function summarizeResultForDetail(result) {
    if (!result || typeof result !== "object") return result;
    const snapshot = result.snapshot || {};
    return {
      ok: result.ok,
      error: result.error,
      device: snapshot.device,
      lastResponse: snapshot.lastResponse,
      events: Array.isArray(snapshot.events) ? snapshot.events.slice(-8) : [],
      logic: snapshot.logic ? {
        configured: snapshot.logic.configured,
        running: snapshot.logic.running,
        complete: snapshot.logic.complete,
        capture_id: snapshot.logic.capture_id,
        pin_base: snapshot.logic.pin_base,
        pin_count: snapshot.logic.pin_count,
        sample_rate: snapshot.logic.sample_rate,
        samples: snapshot.logic.samples
      } : undefined
    };
  }

  function makeDefaultChannels() {
    return exposedPins.map((pin, index) => ({
      pin,
      name: `GP${pin}`,
      selected: pin >= 16 && pin <= 19,
      color: channelPalette[index % channelPalette.length],
      pull: "none",
      invert: false,
      trigger: "ignore"
    }));
  }

  function makeDefaultProtocolDevices() {
    return {
      uart: [{ id: 1, ...nativeDefaults.uart, baud: 115200, opened: false, ...defaultProtocolPageState("uart") }],
      i2c: [{ id: 3, ...nativeDefaults.i2c, baud: 100000, opened: false, ...defaultProtocolPageState("i2c") }],
      spi: [{ id: 2, ...nativeDefaults.spi, baud: 1000000, opened: false, ...defaultProtocolPageState("spi") }]
    };
  }

  function defaultProtocolPageState(protocol) {
    if (protocol === "uart") {
      return { sendMode: "text", lineEnding: "none", displayMode: "terminal", payloadHex: "48656c6c6f" };
    }
    if (protocol === "i2c") {
      return { addr: "0x50", transferType: "write-read", readLen: 16, payloadMode: "hex", displayMode: "decoded", payloadHex: "00" };
    }
    return { readLen: 4, payloadMode: "hex", displayMode: "decoded", payloadHex: "9f000000" };
  }

  function ensureSelectedProtocolDevices() {
    ["uart", "i2c", "spi"].forEach((protocol) => {
      const devices = state.protocolDevices[protocol] || [];
      if (!devices.length) {
        const next = makeDefaultProtocolDevices()[protocol][0];
        state.protocolDevices[protocol] = [next];
        state.protocolSelection[protocol] = next.id;
      }
      if (!devices.some((device) => device.id === state.protocolSelection[protocol])) {
        state.protocolSelection[protocol] = state.protocolDevices[protocol][0].id;
      }
    });
  }

  function selectedProtocolDevice(protocol) {
    ensureSelectedProtocolDevices();
    const devices = state.protocolDevices[protocol] || [];
    return devices.find((device) => device.id === state.protocolSelection[protocol]) || devices[0];
  }

  function protocolInstanceValue(protocol, device = selectedProtocolDevice(protocol)) {
    const instances = Object.keys(nativePinMaps[protocol] || {}).map(Number).sort((a, b) => a - b);
    const fallback = nativeDefaults[protocol]?.instance ?? instances[0] ?? 0;
    const value = Number(device?.instance ?? fallback);
    return instances.includes(value) ? value : fallback;
  }

  function protocolConfigFromForm(protocol) {
    const selected = selectedProtocolDevice(protocol);
    if (protocol === "uart") {
      return { id: selected.id, instance: protocolInstanceValue("uart", selected), tx: Number(read("uartTx")), rx: Number(read("uartRx")), baud: uartBaudValue() };
    }
    if (protocol === "i2c") {
      return { id: selected.id, instance: protocolInstanceValue("i2c", selected), sda: Number(read("i2cSda")), scl: Number(read("i2cScl")), baud: Number(read("i2cBaud")) };
    }
    return { id: selected.id, instance: protocolInstanceValue("spi", selected), sck: Number(read("spiSck")), mosi: Number(read("spiMosi")), miso: Number(read("spiMiso")), cs: Number(read("spiCs")), baud: Number(read("spiBaud")) };
  }

  function protocolPageStateFromForm(protocol) {
    if (protocol === "uart") {
      return {
        sendMode: read("uartSendMode") || "text",
        lineEnding: read("uartLineEnding") || "none",
        displayMode: read("uartDisplayMode") || "terminal",
        payloadHex: safeProtocolPayloadHex("uartPayload", read("uartSendMode") || "text", read("uartLineEnding") || "none", "uartHex")
      };
    }
    if (protocol === "i2c") {
      return {
        addr: read("i2cAddress") || "0x50",
        transferType: read("i2cTransferType") || "write-read",
        readLen: Number(read("i2cReadLen") || 16),
        payloadMode: read("i2cPayloadMode") || "hex",
        displayMode: read("i2cDisplayMode") || "decoded",
        payloadHex: safeProtocolPayloadHex("i2cPayload", read("i2cPayloadMode") || "hex", "none", "i2cWriteHex")
      };
    }
    return {
      readLen: Number(read("spiReadLen") || 4),
      payloadMode: read("spiPayloadMode") || "hex",
      displayMode: read("spiDisplayMode") || "decoded",
      payloadHex: safeProtocolPayloadHex("spiPayload", read("spiPayloadMode") || "hex", "none", "spiHex")
    };
  }

  function syncSelectedProtocolFromControls(protocol) {
    const selected = selectedProtocolDevice(protocol);
    Object.assign(selected, protocolConfigFromForm(protocol), protocolPageStateFromForm(protocol));
  }

  function syncSelectedProtocolPageState(protocol) {
    const selected = selectedProtocolDevice(protocol);
    if (!selected) return;
    Object.assign(selected, protocolPageStateFromForm(protocol));
  }

  function markProtocolOperationSuccess(action) {
    const protocol = String(action || "").split(".")[0];
    if (!["uart", "i2c", "spi"].includes(protocol)) return;
    const selected = selectedProtocolDevice(protocol);
    if (!selected) return;
    Object.assign(selected, protocolConfigFromForm(protocol), protocolPageStateFromForm(protocol), { opened: true });
  }

  function selectProtocolDevice(protocol, id) {
    const numericId = Number(id);
    if (!Number.isFinite(numericId)) return;
    try {
      syncSelectedProtocolFromControls(protocol);
    } catch {
      syncSelectedProtocolPageState(protocol);
    }
    state.protocolSelection[protocol] = numericId;
    populateProtocolForm(protocol, selectedProtocolDevice(protocol));
    render();
  }

  function populateProtocolForm(protocol, device) {
    if (!device) return;
    if (protocol === "uart") {
      renderProtocolPinControls();
      setValue("uartTx", device.tx);
      setValue("uartRx", device.rx);
      setUartBaudValue(device.baud || 115200);
      setValue("uartSendMode", device.sendMode || "text");
      setValue("uartLineEnding", device.lineEnding || "none");
      setValue("uartDisplayMode", device.displayMode || "terminal");
      restoreProtocolPayload("uartPayload", "uartSendMode", "uartHex", device.payloadHex || "48656c6c6f");
    } else if (protocol === "i2c") {
      renderProtocolPinControls();
      setValue("i2cSda", device.sda);
      setValue("i2cScl", device.scl);
      setValue("i2cBaud", device.baud || 100000);
      setValue("i2cAddress", device.addr || "0x50");
      setValue("i2cTransferType", device.transferType || "write-read");
      setValue("i2cReadLen", device.readLen ?? 16);
      setValue("i2cPayloadMode", device.payloadMode || "hex");
      setValue("i2cDisplayMode", device.displayMode || "decoded");
      restoreProtocolPayload("i2cPayload", "i2cPayloadMode", "i2cWriteHex", device.payloadHex || "00");
    } else if (protocol === "spi") {
      renderProtocolPinControls();
      setValue("spiSck", device.sck);
      setValue("spiMosi", device.mosi);
      setValue("spiMiso", device.miso);
      setValue("spiCs", device.cs);
      setValue("spiBaud", device.baud || 1000000);
      setValue("spiReadLen", device.readLen ?? 4);
      setValue("spiPayloadMode", device.payloadMode || "hex");
      setValue("spiDisplayMode", device.displayMode || "decoded");
      restoreProtocolPayload("spiPayload", "spiPayloadMode", "spiHex", device.payloadHex || "9f000000");
    }
  }

  function addProtocolDevice(protocol) {
    const devices = state.protocolDevices[protocol] || [];
    const usedInstances = new Set(devices.map((device) => Number(device.instance)));
    const instance = [0, 1].find((value) => !usedInstances.has(value));
    if (instance === undefined || devices.length >= protocolLimits[protocol]) {
      showErrorDialog(t("noFreeHardware"));
      return;
    }
    const occupied = occupiedProtocolPins();
    const defaults = defaultProtocolConfigFor(protocol, instance, occupied);
    const device = { id: protocolChannelIdForInstance(protocol, instance), instance, ...defaults, opened: false, ...defaultProtocolPageState(protocol) };
    state.protocolDevices[protocol].push(device);
    state.protocolSelection[protocol] = device.id;
    populateProtocolForm(protocol, device);
    render();
  }

  function releaseSelectedProtocolDevice(protocol) {
    const selected = selectedProtocolDevice(protocol);
    if (!selected) return;
    const devices = state.protocolDevices[protocol] || [];
    if (devices.length > 1) {
      const selectedIndex = devices.findIndex((device) => Number(device.id) === Number(selected.id));
      state.protocolDevices[protocol] = devices.filter((device) => Number(device.id) !== Number(selected.id));
      const fallback = state.protocolDevices[protocol][Math.max(0, selectedIndex - 1)] || state.protocolDevices[protocol][0];
      if (fallback) {
        state.protocolSelection[protocol] = fallback.id;
        populateProtocolForm(protocol, fallback);
      }
      render();
    } else {
      selected.opened = false;
      render();
    }
    runOperation(normalizeOperation("channel.release", { id: selected.id, protocol }, { panel: protocol })).catch(() => {});
  }

  function mergeProtocolDevicesFromSnapshot(snapshot) {
    const channels = Array.isArray(snapshot?.channels) ? snapshot.channels : [];
    for (const channel of channels) {
      if (!["uart", "i2c", "spi"].includes(channel.type)) continue;
      const devices = state.protocolDevices[channel.type];
      const existing = devices.find((device) => Number(device.id) === Number(channel.id));
      if (existing && existing.opened) {
        Object.assign(existing, protocolDeviceFromChannel(channel));
      }
    }
  }

  function protocolDeviceFromChannel(channel) {
    const base = { id: Number(channel.id), instance: Number(channel.instance ?? 0), baud: Number(channel.baud ?? 0), opened: true, ...defaultProtocolPageState(channel.type) };
    if (channel.type === "uart") return { ...base, tx: Number(channel.tx), rx: Number(channel.rx), loopback: Boolean(channel.loopback) };
    if (channel.type === "i2c") return { ...base, sda: Number(channel.sda), scl: Number(channel.scl) };
    return { ...base, sck: Number(channel.sck), mosi: Number(channel.mosi), miso: Number(channel.miso), cs: Number(channel.cs) };
  }

  function activeProtocolChannel(protocol, id) {
    const device = (state.protocolDevices[protocol] || []).find((item) => Number(item.id) === Number(id));
    if (!device?.opened) return null;
    const channels = Array.isArray(state.snapshot?.channels) ? state.snapshot.channels : [];
    return channels.find((channel) => channel.type === protocol && Number(channel.id) === Number(id) && channel.active !== false);
  }

  function protocolDeviceLabel(protocol, device) {
    return `${protocol.toUpperCase()}${device.instance} #${device.id}`;
  }

  function protocolChannelIdForInstance(protocol, instance) {
    return Number(protocolInstanceChannelIds[protocol]?.[Number(instance)] ?? nextProtocolChannelId());
  }

  function protocolChannelDetail(protocol, channel) {
    if (!channel) return "-";
    if (protocol === "uart") return `TX GP${channel.tx} / RX GP${channel.rx} / ${channel.baud || 115200}`;
    if (protocol === "i2c") return `SDA GP${channel.sda} / SCL GP${channel.scl} / ${channel.baud || 100000}`;
    return `SCK GP${channel.sck} / MOSI GP${channel.mosi} / MISO GP${channel.miso} / CS GP${channel.cs} / ${channel.baud || 1000000}`;
  }

  function protocolFromControlId(id) {
    if (id.startsWith("uart")) return "uart";
    if (id.startsWith("i2c")) return "i2c";
    if (id.startsWith("spi")) return "spi";
    return "";
  }

  function occupiedProtocolPins() {
    const pins = new Set();
    for (const channel of Array.isArray(state.snapshot?.channels) ? state.snapshot.channels : []) {
      const device = (state.protocolDevices[channel.type] || []).find((item) => Number(item.id) === Number(channel.id));
      if (device?.opened) {
        protocolPins(channel).forEach((pin) => pins.add(pin));
      }
    }
    for (const devices of Object.values(state.protocolDevices)) {
      devices.forEach((device) => protocolPins(device).forEach((pin) => pins.add(pin)));
    }
    return pins;
  }

  function protocolPins(config) {
    const fields = ["tx", "rx", "sda", "scl", "sck", "mosi", "miso", "cs"];
    return fields.map((field) => Number(config[field])).filter((pin) => Number.isFinite(pin));
  }

  function defaultProtocolConfigFor(protocol, instance, occupied) {
    if (protocol === "uart") {
      const map = nativePinMaps.uart[instance];
      const pins = choosePinPair(map.tx, map.rx, occupied);
      return { tx: pins[0], rx: pins[1], baud: 115200 };
    }
    if (protocol === "i2c") {
      const map = nativePinMaps.i2c[instance];
      const pins = choosePinPair(map.sda, map.scl, occupied);
      return { sda: pins[0], scl: pins[1], baud: 100000 };
    }
    const map = nativePinMaps.spi[instance];
    const triple = choosePins([map.sck, map.mosi, map.miso], occupied);
    const preferredCs = spiPreferredManualCs[instance] ?? nativeDefaults.spi.cs;
    const csCandidates = [preferredCs, ...exposedPins.filter((pin) => pin !== preferredCs)];
    const cs = csCandidates.find((pin) => !occupied.has(pin) && !triple.includes(pin)) ?? preferredCs;
    return { sck: triple[0], mosi: triple[1], miso: triple[2], cs, baud: 1000000 };
  }

  function choosePinPair(first, second, occupied) {
    return choosePins([first, second], occupied);
  }

  function choosePins(groups, occupied) {
    const selected = [];
    for (const group of groups) {
      const pin = group.find((candidate) => !occupied.has(candidate) && !selected.includes(candidate)) ?? group.find((candidate) => !selected.includes(candidate)) ?? group[0];
      selected.push(pin);
    }
    return selected;
  }

  function nextProtocolChannelId() {
    const used = new Set([1, 2, 3]);
    for (const devices of Object.values(state.protocolDevices)) {
      devices.forEach((device) => used.add(Number(device.id)));
    }
    for (const channel of Array.isArray(state.snapshot?.channels) ? state.snapshot.channels : []) {
      used.add(Number(channel.id));
    }
    for (let id = 4; id < 64; id += 1) {
      if (!used.has(id)) return id;
    }
    return Date.now() % 10000;
  }

  function selectedChannelPins() {
    return state.channels.filter((channel) => channel.selected).map((channel) => channel.pin).sort((a, b) => a - b);
  }

  function selectedChannelSummary() {
    const pins = selectedChannelPins();
    if (!pins.length) return { count: 0, range: t("noneShort"), sparse: false, note: t("selectAtLeastOne") };
    try {
      let hardwarePins = [...pins];
      const triggerType = read("logicTriggerType") || "none";
      const triggerPin = positiveInteger(read("logicTriggerPin"), pins[0]);
      let triggerExpanded = false;
      if (triggerType === "pattern") {
        const pattern = parsePatternInput(pins[0]);
        for (const pin of pattern.pins) {
          if (!hardwarePins.includes(pin)) {
            hardwarePins.push(pin);
            triggerExpanded = true;
          }
        }
        hardwarePins.sort((a, b) => a - b);
      } else if (triggerType !== "none" && supportedTriggerTypes.has(triggerType) && exposedPinSet.has(triggerPin) && !hardwarePins.includes(triggerPin)) {
        hardwarePins.push(triggerPin);
        hardwarePins.sort((a, b) => a - b);
        triggerExpanded = true;
      }
      const window = hardwareWindowForPins(hardwarePins);
      const range = `GP${window.base}..GP${window.base + window.count - 1}`;
      const note = triggerExpanded ? t("triggerExpandedNote") : window.sparse ? t("sparseNote") : t("directNote");
      return { count: pins.length, range, sparse: window.sparse, note };
    } catch (error) {
      return { count: pins.length, range: "invalid", sparse: true, note: error.message };
    }
  }

  function hardwareWindowForPins(pins) {
    const min = Math.min(...pins);
    const max = Math.max(...pins);
    for (let pin = min; pin <= max; pin += 1) {
      if (!exposedPinSet.has(pin)) {
        throw new Error(`GP${pin} ${t("unsupportedPin")}`);
      }
    }
    return { base: min, count: max - min + 1, sparse: pins.length !== max - min + 1 };
  }

  function logicMaxSamplesFor(pinCount) {
    const pins = Math.max(1, Number(pinCount) || 1);
    const caps = state.snapshot?.logic_caps ?? defaultLogicCaps();
    const words = Number(caps.buffer_words ?? 0);
    if (!words) return 0;
    const recordBits = 32 - (32 % pins);
    return Math.floor((words * recordBits) / pins);
  }

  function logicSampleRateMax() {
    const caps = state.snapshot?.logic_caps ?? defaultLogicCaps();
    const max = Number(caps.sample_rate_max ?? defaultLogicSampleRateMax);
    return Number.isFinite(max) && max > 0 ? Math.floor(max) : defaultLogicSampleRateMax;
  }

  function pinsInRange(base, count) {
    return Array.from({ length: Math.max(0, count) }, (_, index) => base + index).filter((pin) => exposedPinSet.has(pin));
  }

  function visiblePinsFor(logic) {
    const selected = Array.isArray(logic.visible_pins) && logic.visible_pins.length
      ? logic.visible_pins
      : Array.isArray(logic.selected_pins) && logic.selected_pins.length
        ? logic.selected_pins
        : pinsInRange(logic.pin_base ?? 16, logic.pin_count ?? 1);
    return selected.filter((pin) => pin >= logic.pin_base && pin < logic.pin_base + logic.pin_count);
  }

  function channelFor(pin) {
    return state.channels.find((channel) => channel.pin === Number(pin));
  }

  function currentView(logic) {
    const samples = Math.max(1, Number(logic?.samples ?? 1));
    const span = Math.max(16, samples / Math.max(1, state.zoom));
    const start = clamp(state.viewStart, 0, Math.max(0, samples - span));
    state.viewStart = start;
    return { start, span };
  }

  function logicCanvasGeometry(canvas = $("logicCanvas")) {
    const rect = canvas.getBoundingClientRect();
    const logicalWidth = Number(canvas.__logicalWidth || rect.width || canvas.clientWidth || canvas.width || 1);
    const logicalHeight = Number(canvas.__logicalHeight || rect.height || canvas.clientHeight || canvas.height || 1);
    const scaleX = rect.width ? logicalWidth / rect.width : 1;
    const scaleY = rect.height ? logicalHeight / rect.height : 1;
    const left = 80;
    const right = 22;
    return {
      rect,
      scaleX,
      scaleY,
      left,
      right,
      plotWidth: Math.max(1, logicalWidth - left - right)
    };
  }

  function logicCanvasPoint(event, canvas = $("logicCanvas")) {
    const geometry = logicCanvasGeometry(canvas);
    return {
      x: (event.clientX - geometry.rect.left) * geometry.scaleX,
      y: (event.clientY - geometry.rect.top) * geometry.scaleY,
      geometry
    };
  }

  function sampleFromCanvasEvent(event, logic) {
    const point = logicCanvasPoint(event);
    const view = currentView(logic);
    return Math.round(clamp(((point.x - point.geometry.left) / point.geometry.plotWidth) * view.span + view.start, 0, logic.samples - 1));
  }

  function cursorHitFromCanvasEvent(event, logic) {
    if (!logic?.words?.length) return "";
    const point = logicCanvasPoint(event);
    const view = currentView(logic);
    const candidates = [
      ["a", state.cursorA],
      ["b", state.cursorB]
    ].filter(([, sample]) => sample !== null && sample !== undefined);
    let best = "";
    let bestDistance = 999;
    for (const [key, sample] of candidates) {
      if (sample < view.start || sample > view.start + view.span) continue;
      const x = point.geometry.left + ((sample - view.start) / Math.max(1, view.span)) * point.geometry.plotWidth;
      const distance = Math.abs(point.x - x);
      if (distance < bestDistance) {
        best = key;
        bestDistance = distance;
      }
    }
    return bestDistance <= 10 ? best : "";
  }

  function overviewCanvasGeometry(canvas = $("logicOverview")) {
    const rect = canvas.getBoundingClientRect();
    const logicalWidth = Number(canvas.__logicalWidth || rect.width || canvas.clientWidth || canvas.width || 1);
    const scaleX = rect.width ? logicalWidth / rect.width : 1;
    const left = 8;
    const right = 8;
    return {
      rect,
      scaleX,
      left,
      right,
      plotWidth: Math.max(1, logicalWidth - left - right)
    };
  }

  function setViewFromOverviewEvent(event, logic) {
    const canvas = $("logicOverview");
    const geometry = overviewCanvasGeometry(canvas);
    const x = (event.clientX - geometry.rect.left) * geometry.scaleX;
    const sample = clamp(((x - geometry.left) / geometry.plotWidth) * Math.max(1, logic.samples || 1), 0, Math.max(0, logic.samples - 1));
      const view = currentView(logic);
      const maxStart = Math.max(0, Number(logic.samples || 1) - view.span);
      state.viewStart = clamp(sample - view.span / 2, 0, maxStart);
      state.userViewLocked = true;
      renderLogic(logic);
  }

  function cursorDefaultSample(fraction = 0.35) {
    const logic = state.snapshot?.logic;
    const samples = Math.max(1, Number(logic?.samples ?? 1));
    return Math.round(samples * fraction);
  }

  function logicLevelAt(logic, sample, channel) {
    const pinCount = Math.max(1, logic.pin_count || 1);
    const recordBits = logic.record_bits || (32 - (32 % pinCount));
    const bitIndex = channel + sample * pinCount;
    const wordIndex = Math.floor(bitIndex / recordBits);
    const bitPosition = (bitIndex % recordBits) + 32 - recordBits;
    const word = Number(logic.words?.[wordIndex] ?? 0) >>> 0;
    return Boolean(Math.floor(word / (2 ** bitPosition)) & 1);
  }

  function displayLevelAt(logic, sample, channel) {
    const level = logicLevelAt(logic, sample, channel);
    const pin = Number(logic.pin_base ?? 0) + Number(channel);
    const config = pinConfigFor(logic, pin);
    return config?.invert ? !level : level;
  }

  function logicHasVisibleEdges(logic) {
    if (!logic?.words?.length || !logic.samples) return false;
    const pins = visiblePinsFor(logic);
    const sampleLimit = Math.max(1, Number(logic.samples || 1));
    for (const pin of pins) {
      const channelIndex = pin - Number(logic.pin_base ?? 0);
      let previous = displayLevelAt(logic, 0, channelIndex);
      for (let sample = 1; sample < sampleLimit; sample += 1) {
        const current = displayLevelAt(logic, sample, channelIndex);
        if (current !== previous) return true;
        previous = current;
      }
    }
    return false;
  }

  function firstVisibleEdgeSample(logic) {
    if (!logic?.words?.length || !logic.samples) return NaN;
    const pins = visiblePinsFor(logic);
    const sampleLimit = Math.max(1, Number(logic.samples || 1));
    let firstEdge = Infinity;
    for (const pin of pins) {
      const channelIndex = pin - Number(logic.pin_base ?? 0);
      let previous = displayLevelAt(logic, 0, channelIndex);
      for (let sample = 1; sample < sampleLimit; sample += 1) {
        const current = displayLevelAt(logic, sample, channelIndex);
        if (current !== previous) {
          firstEdge = Math.min(firstEdge, sample);
          break;
        }
        previous = current;
      }
    }
    return Number.isFinite(firstEdge) ? firstEdge : NaN;
  }

  function focusLogicAroundSample(logic, sample, preferredSpan = 4096) {
    if (!logic?.samples || !Number.isFinite(sample)) return;
    const samples = Math.max(1, Number(logic.samples || 1));
    const span = clamp(Number(preferredSpan) || 4096, 64, samples);
    state.zoom = clamp(samples / span, 1, 64);
    const focusedSpan = samples / Math.max(1, state.zoom);
    state.viewStart = clamp(sample - focusedSpan * 0.18, 0, Math.max(0, samples - focusedSpan));
    state.userViewLocked = false;
  }

  function isLevelTriggeredCapture(logic) {
    const type = String(logic?.trigger_type || "");
    const mode = String(logic?.trigger_mode || "");
    return type === "level-high" || type === "level-low" || mode === "level";
  }

  function logicEdgeSummary(logic) {
    if (!logic?.words?.length) return "";
    const edges = visiblePinsFor(logic)
      .map((pin) => `GP${pin}:${summarizePin(logic, pin).edges}e`)
      .join(" ");
    const windowMs = (Number(logic.samples ?? 0) / Math.max(1, Number(logic.sample_rate ?? 1))) * 1000;
    return `${edges} ${t("window")}:${formatDurationMs(windowMs)}`;
  }

  function pinConfigFor(logic, pin) {
    const configs = Array.isArray(logic?.pin_configs) ? logic.pin_configs : [];
    const explicit = configs.find((config) => Number(config.pin) === Number(pin));
    if (explicit) return explicit;
    const base = channelFor(pin);
    const pinPulls = logic?.pin_pulls && typeof logic.pin_pulls === "object" ? logic.pin_pulls : {};
    if (Object.prototype.hasOwnProperty.call(pinPulls, String(pin))) {
      return { ...(base ?? { pin }), pull: String(pinPulls[String(pin)] || "none") };
    }
    return base;
  }

  function sampleWindowFromForm(logic) {
    const samples = Math.max(1, Number(logic?.samples ?? 1));
    const rawStart = String(read("logicRegionStart") ?? "").trim();
    const rawEnd = String(read("logicRegionEnd") ?? "").trim();
    const start = rawStart ? positiveInteger(rawStart, -1) : 0;
    const end = rawEnd ? positiveInteger(rawEnd, -1) : samples;
    if (start < 0 || end <= start || end > samples) {
      throw new Error(t("invalidRegion"));
    }
    return { start, end, active: start !== 0 || end !== samples };
  }

  function slicedLogicForRegion(logic) {
    const region = sampleWindowFromForm(logic);
    if (!region.active) return logic;
    const pinCount = Math.max(1, logic.pin_count || 1);
    const recordBits = 32 - (32 % pinCount);
    const samples = region.end - region.start;
    const words = Array.from({ length: Math.ceil((samples * pinCount) / recordBits) }, () => 0);
    for (let relSample = 0; relSample < samples; relSample += 1) {
      for (let channel = 0; channel < pinCount; channel += 1) {
        if (!logicLevelAt(logic, region.start + relSample, channel)) continue;
        const bitIndex = channel + relSample * pinCount;
        const wordIndex = Math.floor(bitIndex / recordBits);
        const bitPosition = (bitIndex % recordBits) + 32 - recordBits;
        words[wordIndex] = (words[wordIndex] + (2 ** bitPosition)) >>> 0;
      }
    }
    return {
      ...logic,
      samples,
      sample_offset: Number(logic.sample_offset ?? 0) + region.start,
      trigger_found: Boolean(logic.trigger_found && Number.isFinite(markerRelativeSample(logic, logic.trigger_sample) - region.start) && markerRelativeSample(logic, logic.trigger_sample) - region.start >= 0 && markerRelativeSample(logic, logic.trigger_sample) - region.start < samples),
      trigger_sample: markerRelativeSample(logic, logic.trigger_sample) - region.start,
      burst_samples: (Array.isArray(logic.burst_samples) ? logic.burst_samples : [])
        .map((sample) => markerRelativeSample(logic, sample) - region.start)
        .filter((sample) => Number.isFinite(sample) && sample >= 0 && sample < samples),
      record_bits: recordBits,
      words
    };
  }

  function summarizePin(logic, pin) {
    const channel = pin - logic.pin_base;
    let previous = displayLevelAt(logic, 0, channel);
    let edges = 0;
    let high = previous ? 1 : 0;
    for (let sample = 1; sample < logic.samples; sample += 1) {
      const level = displayLevelAt(logic, sample, channel);
      if (level !== previous) edges += 1;
      if (level) high += 1;
      previous = level;
    }
    return { edges, highPercent: (high / Math.max(1, logic.samples)) * 100 };
  }

  function edgeList(logic, pin, limit = 300) {
    const channel = pin - logic.pin_base;
    const rate = Math.max(1, logic.sample_rate || 1);
    const rows = [];
    let previous = displayLevelAt(logic, 0, channel);
    for (let sample = 1; sample < logic.samples && rows.length < limit; sample += 1) {
      const level = displayLevelAt(logic, sample, channel);
      const absoluteSample = sample + Number(logic.sample_offset ?? 0);
      if (level !== previous) rows.push({ sample: absoluteSample, time_us: (absoluteSample / rate) * 1000000, edge: level ? "rising" : "falling" });
      previous = level;
    }
    return rows;
  }

  function decodeCurrentCapture() {
    const rawLogic = state.snapshot?.logic;
    if (!rawLogic?.words?.length) return decoderResult(t("noCaptureDecode"));
    const logic = slicedLogicForRegion(rawLogic);
    const regionShift = Number(logic.sample_offset ?? 0) - Number(rawLogic.sample_offset ?? 0);
    const finish = (result) => shiftDecoderResult(result, regionShift);
    const decoder = read("decoderType") || "summary";
    if (decoder === "summary") {
      return finish(decoderResult(visiblePinsFor(logic).map((pin) => {
        const summary = summarizePin(logic, pin);
        return `GP${pin}: edges=${summary.edges}, high=${summary.highPercent.toFixed(2)}%`;
      }).join("\n")));
    }
    if (decoder === "bursts") return finish(decodeBursts(logic));
    if (decoder === "edges") {
      const annotations = [];
      const text = visiblePinsFor(logic).map((pin) => {
        const edges = edgeList(logic, pin, 80).map((edge) => {
          annotations.push({
            protocol: "edge",
            tone: "edge",
            pin,
            sample: edge.sample - Number(logic.sample_offset ?? 0),
            label: edge.edge === "rising" ? "↑" : "↓",
            detail: `GP${pin}`
          });
          return `  ${edge.edge.padEnd(7)} sample=${edge.sample} time=${edge.time_us.toFixed(3)}us`;
        }).join("\n");
        return `GP${pin}\n${edges || "  " + t("noEdges")}`;
      }).join("\n\n");
      return finish(decoderResult(text, annotations));
    }
    if (decoder === "uart") {
      const rxPin = Number(read("decoderDataPin"));
      const txPin = Number(read("decoderClockPin"));
      return finish(decodeUARTDuplex(logic, rxPin, txPin, decoderBaudValue(logic, rxPin)));
    }
    if (decoder === "spi") return finish(decodeSPI(logic, Number(read("decoderClockPin")), Number(read("decoderDataPin")), Number(read("decoderCsPin")), Number(read("decoderSpiMode"))));
    if (decoder === "i2c") return finish(decodeI2C(logic, Number(read("decoderDataPin")), Number(read("decoderClockPin"))));
    return finish(decoderResult(t("sigrokHint")));
  }

  function decodeBursts(logic) {
    const rate = Math.max(1, Number(logic.sample_rate ?? 1));
    const rows = [];
    const annotations = [];
    const start = Number(logic.sample_offset ?? 0);
    const samples = Math.max(0, Number(logic.samples ?? 0));
    const inWindow = (sample) => Number.isFinite(sample) && sample >= 0 && sample < samples;
    if (logic.trigger_found) {
      const relative = markerRelativeSample(logic, logic.trigger_sample);
      if (inWindow(relative)) {
        const absolute = start + relative;
        rows.push(`trigger sample=${absolute} rel=${relative} time=${formatNumber((absolute / rate) * 1000000)}us`);
        annotations.push({ protocol: "bursts", tone: "trigger", pin: Number(logic.trigger_pin ?? visiblePinsFor(logic)[0]), sample: relative, label: "T", detail: "trigger" });
      }
    }
    const requestedBurstCount = Number(logic.requested_burst_count ?? logic.burst_count ?? 1);
    if (Number.isFinite(requestedBurstCount) && requestedBurstCount > 1) {
      const triggerRelative = logic.trigger_found ? markerRelativeSample(logic, logic.trigger_sample) : NaN;
      const burstSamples = Array.isArray(logic.burst_samples) ? logic.burst_samples : [];
      burstSamples.forEach((raw, index) => {
        const relative = markerRelativeSample(logic, raw);
        if (!inWindow(relative)) return;
        if (Number.isFinite(triggerRelative) && Math.abs(relative - triggerRelative) <= 1) return;
        const absolute = start + relative;
        rows.push(`burst${index + 1} sample=${absolute} rel=${relative} time=${formatNumber((absolute / rate) * 1000000)}us`);
        annotations.push({ protocol: "bursts", tone: "burst", pin: visiblePinsFor(logic)[0], sample: relative, label: `B${index + 1}`, detail: "burst" });
      });
    }
    return decoderResult(rows.length ? rows.join("\n") : t("noBursts"), annotations);
  }

  function decoderBaudValue(logic, rxPin) {
    const raw = read("decoderBaud");
    if (raw !== "auto") return Math.max(1, Number(raw || 115200));
    return estimateUARTBaud(logic, rxPin) || 115200;
  }

  function estimateUARTBaud(logic, rxPin) {
    if (!pinInLogic(logic, rxPin)) return 0;
    const channel = rxPin - logic.pin_base;
    const sampleRate = Math.max(1, Number(logic.sample_rate || 1));
    const runs = [];
    let previous = logicLevelAt(logic, 0, channel);
    let runStart = 0;
    for (let sample = 1; sample < logic.samples; sample += 1) {
      const level = logicLevelAt(logic, sample, channel);
      if (level === previous) continue;
      const run = sample - runStart;
      if (run > 1) runs.push(run);
      runStart = sample;
      previous = level;
      if (runs.length >= 512) break;
    }
    if (!runs.length) return 0;
    runs.sort((a, b) => a - b);
    const baseRun = runs[Math.min(runs.length - 1, Math.floor(runs.length * 0.18))];
    if (!baseRun) return 0;
    const rawBaud = sampleRate / baseRun;
    const common = [9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600];
    return common.reduce((best, item) => Math.abs(item - rawBaud) < Math.abs(best - rawBaud) ? item : best, common[0]);
  }

  function decodeUARTDuplex(logic, rxPin, txPin, baud) {
    if (txPin === rxPin || !Number.isFinite(txPin)) return decodeUART(logic, rxPin, baud, "RX");
    const rx = decodeUART(logic, rxPin, baud, "RX");
    const tx = decodeUART(logic, txPin, baud, "TX");
    return decoderResult(`${rx.text}\n\n${tx.text}`, [...(rx.annotations || []), ...(tx.annotations || [])]);
  }

  function decodeUART(logic, rxPin, baud, lineLabel = "RX") {
    if (!pinInLogic(logic, rxPin)) return decoderResult(`${lineLabel} GP${rxPin} ${state.language === "zh" ? "不在本次采集范围内。" : "is not in this capture."}`);
    const channel = rxPin - logic.pin_base;
    const samplesPerBit = logic.sample_rate / Math.max(1, baud);
    const bytes = [];
    const frames = [];
    let previous = true;
    for (let sample = 1; sample < logic.samples - Math.ceil(samplesPerBit * 10); sample += 1) {
      const level = logicLevelAt(logic, sample, channel);
      if (previous && !level) {
        let value = 0;
        for (let bit = 0; bit < 8; bit += 1) {
          const bitSample = Math.round(sample + samplesPerBit * (1.5 + bit));
          if (logicLevelAt(logic, bitSample, channel)) value |= (1 << bit);
        }
        bytes.push(value);
        frames.push({ value, sample, endSample: Math.round(sample + samplesPerBit * 10) });
        sample += Math.floor(samplesPerBit * 9);
      }
      previous = level;
      if (bytes.length >= 256) break;
    }
    if (!bytes.length) return decoderResult(t("noUART"));
    const annotations = frames.map((frame) => ({
      protocol: "uart",
      tone: "uart",
      pin: rxPin,
      sample: frame.sample,
      endSample: frame.endSample,
      label: `0x${frame.value.toString(16).padStart(2, "0")}`,
      detail: frame.value >= 32 && frame.value <= 126 ? `${lineLabel} '${String.fromCharCode(frame.value)}'` : lineLabel
    }));
    return decoderResult(`UART ${lineLabel} GP${rxPin} ${baud} baud\nHEX: ${bytes.map((value) => value.toString(16).padStart(2, "0")).join(" ")}\nASCII: ${bytes.map((value) => value >= 32 && value <= 126 ? String.fromCharCode(value) : ".").join("")}`, annotations);
  }

  function decodeSPI(logic, sckPin, dataPin, csPin, mode) {
    if (![sckPin, dataPin, csPin].every((pin) => pinInLogic(logic, pin))) return decoderResult(state.language === "zh" ? "SPI 解码引脚未全部包含在本次采集中。" : "SPI decoder pins are not all present in this capture.");
    const sck = sckPin - logic.pin_base;
    const data = dataPin - logic.pin_base;
    const cs = csPin - logic.pin_base;
    const sampleOnRising = mode === 0 || mode === 3;
    const bytes = [];
    const frames = [];
    let current = 0;
    let bits = 0;
    let prevSck = logicLevelAt(logic, 0, sck);
    let wasActive = !logicLevelAt(logic, 0, cs);
    let byteStart = 0;
    for (let sample = 1; sample < logic.samples; sample += 1) {
      const active = !logicLevelAt(logic, sample, cs);
      const nowSck = logicLevelAt(logic, sample, sck);
      const edge = !prevSck && nowSck ? "rising" : prevSck && !nowSck ? "falling" : "";
      const sampleEdge = (sampleOnRising && edge === "rising") || (!sampleOnRising && edge === "falling");
      if (sampleEdge && (active || wasActive)) {
        if (bits === 0) byteStart = sample;
        current = (current << 1) | (logicLevelAt(logic, sample, data) ? 1 : 0);
        bits += 1;
        if (bits === 8) {
          bytes.push(current);
          frames.push({ value: current, sample: byteStart, endSample: sample });
          current = 0;
          bits = 0;
        }
      }
      if (!active) {
        current = 0;
        bits = 0;
        prevSck = nowSck;
        wasActive = false;
        continue;
      }
      prevSck = nowSck;
      wasActive = true;
      if (bytes.length >= 512) break;
    }
    if (!bytes.length) return decoderResult(spiDecodeFailureText(logic, sckPin, dataPin, csPin));
    const annotations = frames.map((frame) => ({
      protocol: "spi",
      tone: "spi",
      pin: dataPin,
      sample: frame.sample,
      endSample: frame.endSample,
      label: `0x${frame.value.toString(16).padStart(2, "0")}`,
      detail: `MOSI GP${dataPin}`
    }));
    return decoderResult(`SPI mode ${mode}\n${bytes.map((value) => value.toString(16).padStart(2, "0")).join(" ")}`, annotations);
  }

  function spiDecodeFailureText(logic, sckPin, dataPin, csPin) {
    const sckSummary = summarizePin(logic, sckPin);
    const csSummary = summarizePin(logic, csPin);
    const dataSummary = summarizePin(logic, dataPin);
    const csChannel = csPin - logic.pin_base;
    let csActiveSamples = 0;
    for (let sample = 0; sample < Number(logic.samples || 0); sample += 1) {
      if (!logicLevelAt(logic, sample, csChannel)) csActiveSamples += 1;
    }
    const zh = state.language === "zh";
    const lines = [
      t("noSPI"),
      zh
        ? `当前映射：SCK GP${sckPin} / MOSI GP${dataPin} / CS GP${csPin}`
        : `Current mapping: SCK GP${sckPin} / MOSI GP${dataPin} / CS GP${csPin}`,
      zh
        ? `边沿统计：SCK ${sckSummary.edges}，MOSI ${dataSummary.edges}，CS ${csSummary.edges}，CS 有效样本 ${csActiveSamples}`
        : `Edges: SCK ${sckSummary.edges}, MOSI ${dataSummary.edges}, CS ${csSummary.edges}, CS active samples ${csActiveSamples}`
    ];
    if (sckSummary.edges === 0) {
      lines.push(zh ? `没有看到 SCK 边沿，请检查 GP2->GP${sckPin} 监听线。` : `No SCK edges were captured. Check the GP2->GP${sckPin} monitor wire.`);
    } else if (csActiveSamples === 0) {
      lines.push(zh ? `CS 没有进入有效低电平，请检查 GP1->GP${csPin} 监听线或片选极性。` : `CS never went active low. Check the GP1->GP${csPin} monitor wire or chip-select polarity.`);
    } else {
      lines.push(zh ? "已看到部分信号，但不足 8 个有效 SCK 采样位；请检查 SPI Mode、采样率、窗口长度和接线。" : "Some signal was captured, but fewer than 8 valid SCK sample bits were found. Check SPI mode, sample rate, window length, and wiring.");
    }
    return lines.join("\n");
  }

  function decodeI2C(logic, sdaPin, sclPin) {
    if (![sdaPin, sclPin].every((pin) => pinInLogic(logic, pin))) return decoderResult(state.language === "zh" ? "I2C 解码引脚未全部包含在本次采集中。" : "I2C decoder pins are not both present in this capture.");
    const sda = sdaPin - logic.pin_base;
    const scl = sclPin - logic.pin_base;
    const rows = [];
    const annotations = [];
    let prevSda = logicLevelAt(logic, 0, sda);
    let prevScl = logicLevelAt(logic, 0, scl);
    let byte = 0;
    let bits = 0;
    const offset = Number(logic.sample_offset ?? 0);
    for (let sample = 1; sample < logic.samples && rows.length < 300; sample += 1) {
      const nowSda = logicLevelAt(logic, sample, sda);
      const nowScl = logicLevelAt(logic, sample, scl);
      if (prevSda && !nowSda && nowScl) {
        rows.push(`START @ ${sample + offset}`);
        annotations.push({ protocol: "i2c", tone: "i2c", pin: sdaPin, sample, label: "S", detail: "START" });
      }
      if (!prevSda && nowSda && nowScl) {
        rows.push(`STOP  @ ${sample + offset}`);
        annotations.push({ protocol: "i2c", tone: "stop", pin: sdaPin, sample, label: "P", detail: "STOP" });
      }
      if (!prevScl && nowScl) {
        byte = (byte << 1) | (nowSda ? 1 : 0);
        bits += 1;
        if (bits === 9) {
          const value = byte >> 1;
          const ack = (byte & 1) ? "NACK" : "ACK";
          rows.push(`BYTE  0x${value.toString(16).padStart(2, "0")} ACK=${ack} @ ${sample + offset}`);
          annotations.push({ protocol: "i2c", tone: ack === "ACK" ? "i2c" : "nack", pin: sdaPin, sample, label: `0x${value.toString(16).padStart(2, "0")}`, detail: ack });
          byte = 0;
          bits = 0;
        }
      }
      prevSda = nowSda;
      prevScl = nowScl;
    }
    return decoderResult(rows.length ? rows.join("\n") : t("noI2C"), annotations);
  }

  function decoderResult(text, annotations = []) {
    return { text: text || "", annotations };
  }

  function shiftDecoderResult(result, shift) {
    const payload = typeof result === "object" && result !== null ? result : decoderResult(result);
    if (!shift || !Array.isArray(payload.annotations)) return payload;
    return {
      ...payload,
      annotations: payload.annotations.map((item) => ({
        ...item,
        sample: Number(item.sample) + shift,
        endSample: Number.isFinite(Number(item.endSample)) ? Number(item.endSample) + shift : item.endSample
      }))
    };
  }

  function logicSettingsFromForm() {
    const params = logicParamsFromForm();
    const settings = {
      pin_base: params.pin_base,
      pin_count: params.pin_count,
      sample_rate: params.sample_rate,
      samples: params.samples,
      pre_samples: params.pre_samples ?? 0,
      post_samples: params.post_samples ?? params.samples,
      search_samples: params.search_samples ?? 0,
      burst_count: params.burst_count ?? 1,
      pull: params.pull ?? "none",
      pin_pulls: params.pin_pulls ?? {},
      channel_names: Object.fromEntries((params.capture_channels ?? []).map((channel) => [String(channel.pin), channel.name])),
    };
    if (params.trigger_mode === "pattern") {
      settings.trigger = {
        mode: "pattern",
        mask: `0x${Number(params.trigger_mask ?? 0).toString(16)}`,
        value: `0x${Number(params.trigger_value ?? 0).toString(16)}`,
        pattern_base: params.pattern_base,
        pattern_bits: params.pattern_bits
      };
    } else if (params.trigger_pin !== undefined) {
      settings.trigger = {
        pin: params.trigger_pin,
        mode: params.trigger_mode ?? "level",
        level: params.trigger_level ?? true
      };
    }
    return settings;
  }

  function exportLogic(format) {
    if (format === "settings") return JSON.stringify(logicSettingsFromForm(), null, 2);
    const rawLogic = state.snapshot?.logic;
    if (!rawLogic?.words?.length) return "";
    const logic = slicedLogicForRegion(rawLogic);
    if (format === "json") return JSON.stringify(logic, null, 2);
    if (format === "csv") return exportCSV(logic);
    return exportVCD(logic);
  }

  function exportCSV(logic) {
    const pins = visiblePinsFor(logic);
    const rate = Math.max(1, logic.sample_rate || 1);
    const header = ["sample", "time_s"].concat(pins.map((pin) => `GP${pin}`)).join(",");
    const rows = [header];
    for (let sample = 0; sample < logic.samples; sample += 1) {
      const absoluteSample = sample + Number(logic.sample_offset ?? 0);
      rows.push([absoluteSample, (absoluteSample / rate).toFixed(9)].concat(pins.map((pin) => displayLevelAt(logic, sample, pin - logic.pin_base) ? "1" : "0")).join(","));
    }
    return rows.join("\n");
  }

  function exportVCD(logic) {
    const pins = visiblePinsFor(logic);
    const symbols = "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const rate = Math.max(1, logic.sample_rate || 1);
    const lines = ["$date", `  ${new Date().toISOString()}`, "$end", "$version", "  Embed Labs RP2350 Monitor", "$end", "$timescale 1 ns $end", "$scope module logic $end"];
    pins.forEach((pin, index) => lines.push(`$var wire 1 ${symbols[index]} GP${pin} $end`));
    lines.push("$upscope $end", "$enddefinitions $end", "#0");
    const previous = new Map();
    for (let sample = 0; sample < logic.samples; sample += 1) {
      const absoluteSample = sample + Number(logic.sample_offset ?? 0);
      const timeNs = Math.round((absoluteSample / rate) * 1000000000);
      const changes = [];
      pins.forEach((pin, index) => {
        const level = displayLevelAt(logic, sample, pin - logic.pin_base) ? "1" : "0";
        if (previous.get(pin) !== level) {
          previous.set(pin, level);
          changes.push(`${level}${symbols[index]}`);
        }
      });
      if (changes.length) lines.push(`#${timeNs}`, ...changes);
    }
    return lines.join("\n");
  }

  function pinInLogic(logic, pin) {
    return Number.isFinite(pin) && pin >= logic.pin_base && pin < logic.pin_base + logic.pin_count;
  }

  function showDecoderMessage(text) {
    const payload = typeof text === "object" && text !== null ? text : { text };
    const annotations = Array.isArray(payload.annotations) ? payload.annotations : [];
    const header = annotations.length
      ? `${state.language === "zh" ? "图形注释" : "Waveform annotations"}: ${annotations.length}\n`
      : "";
    $("decoderOutput").textContent = `${header}${payload.text || ""}`;
  }

  function mockWifi() {
    return [{ ssid: "konghome", rssi: -45, channel: 11, auth: 5 }, { ssid: "home_Guest", rssi: -55, channel: 11, auth: 5 }, { ssid: "CMCC-3uPq", rssi: -77, channel: 8, auth: 7 }, { ssid: "13-308", rssi: -69, channel: 11, auth: 5 }];
  }

  function mockWifiStatus(patch = {}) {
    const ssid = patch.ssid ?? "konghome";
    return {
      ap_active: false,
      ap_ssid: "RP2350-Monitor-007BB2",
      ap_ip: "192.168.4.1",
      active_profile: 0,
      ssid_configured: Boolean(ssid),
      ssid,
      station_status: "up",
      station_ip: "192.168.3.97",
      pending_action: "none",
      profiles: [
        { slot: 0, valid: Boolean(ssid), active: true, ssid, last_error: "" },
        { slot: 1, valid: false, active: false, ssid: "", last_error: "" },
        { slot: 2, valid: false, active: false, ssid: "", last_error: "" }
      ],
      scan: { active: false, results: mockWifi() },
      ...patch
    };
  }

  function defaultLogicCaps() {
    return {
      engine: "pio2_dma",
      pin_ranges: [{ first: 0, last: 22 }, { first: 26, last: 28 }],
      contiguous_pins: true,
      pin_count_max: 23,
      sample_rate_max: 150000000,
      buffer_words: 32768,
      buffer_bytes: 131072,
      upload_chunk_bytes: 512,
      encoding: "u32-le-packed",
      capture_modes: ["single", "pretrigger", "burst"],
      triggers: ["none", "level", "rising", "falling", "pattern"],
      pull_modes: ["none", "up", "down"],
      pattern_mask_bits_max: 23,
      burst_marks_max: 16,
      host_decoders: ["summary", "bursts", "edges", "uart", "spi", "i2c"],
      host_exports: ["csv", "vcd"],
      reserved_features: ["external_psram", "sigrok_bridge"]
    };
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
      samples: Number(params.samples ?? 4096),
      pre_samples: Number(params.pre_samples ?? 0),
      post_samples: Number(params.post_samples ?? params.samples ?? 4096),
      search_samples: Number(params.search_samples ?? 0),
      trigger_type: params.trigger_type ?? "none",
      trigger_mode: params.trigger_mode,
      trigger_pin: params.trigger_pin,
      trigger_level: params.trigger_level,
      trigger_mask: params.trigger_mask,
      trigger_value: params.trigger_value,
      trigger_found: complete && (params.trigger_type ?? "none") !== "none",
      trigger_sample: Number(params.pre_samples ?? 0),
      sample_offset: 0,
      burst_count: Number(params.burst_count ?? 1),
      burst_found: complete && Number(params.burst_count ?? 1) > 1 ? Math.min(Number(params.burst_count ?? 1), 4) : 0,
      burst_samples: complete && Number(params.burst_count ?? 1) > 1 ? Array.from({ length: Math.min(Number(params.burst_count ?? 1), 4) }, (_, index) => Number(params.pre_samples ?? 0) + index * Math.max(1, Math.floor(Number(params.post_samples ?? params.samples ?? 4096) / Math.max(1, Number(params.burst_count ?? 1))))) : [],
      pull: params.pull ?? "none",
      selected_pins: params.selected_pins ?? pinsInRange(Number(params.pin_base ?? 16), Number(params.pin_count ?? 4)),
      record_bits: 32 - (32 % Math.max(1, Number(params.pin_count ?? 4))),
      words: []
    };
  }

  function pseudoLogicWords(pinCount, samples) {
    const count = Math.max(1, Number(pinCount));
    const totalSamples = Math.max(1, Number(samples));
    const recordBits = 32 - (32 % count);
    const words = Array.from({ length: Math.ceil((count * totalSamples) / recordBits) }, () => 0);
    for (let sample = 0; sample < totalSamples; sample += 1) {
      for (let channel = 0; channel < count; channel += 1) {
        const period = 8 * (channel + 1);
        const level = Math.floor(sample / period) % 2 === 0;
        if (!level) continue;
        const bitIndex = channel + sample * count;
        const wordIndex = Math.floor(bitIndex / recordBits);
        const bitPosition = (bitIndex % recordBits) + 32 - recordBits;
        words[wordIndex] = (words[wordIndex] + (2 ** bitPosition)) >>> 0;
      }
    }
    return words;
  }

  function protocolPayloadHex(textareaId, mode, lineEnding = "none") {
    const source = read(textareaId);
    let hex = "";
    if (mode === "binary") {
      const bits = source.replace(/[\s_]/g, "");
      if (!bits) return "";
      if (!/^[01]+$/.test(bits) || bits.length % 8 !== 0) {
        throw new Error(state.language === "zh" ? "二进制内容必须是 8bit 对齐的 0/1 序列。" : "Binary payload must be an 8-bit aligned 0/1 sequence.");
      }
      for (let index = 0; index < bits.length; index += 8) {
        hex += Number.parseInt(bits.slice(index, index + 8), 2).toString(16).padStart(2, "0");
      }
    } else if (mode === "hex") {
      hex = source.replace(/(?:0x|0X)/g, "").replace(/[^a-fA-F0-9]/g, "");
      if (hex.length % 2 !== 0) {
        throw new Error(state.language === "zh" ? "HEX 内容必须是偶数字符。" : "HEX payload must contain an even number of digits.");
      }
    } else {
      const suffix = lineEnding === "crlf" ? "\r\n" : lineEnding === "cr" ? "\r" : lineEnding === "lf" ? "\n" : "";
      hex = Array.from(new TextEncoder().encode(source + suffix)).map((value) => value.toString(16).padStart(2, "0")).join("");
    }
    return hex.toLowerCase();
  }

  function safeProtocolPayloadHex(textareaId, mode, lineEnding = "none", hiddenId = "") {
    const textarea = $(textareaId);
    try {
      return protocolPayloadHex(textareaId, mode, lineEnding);
    } catch {
      return (textarea?.dataset.payloadHex || (hiddenId ? read(hiddenId) : "") || "").toLowerCase();
    }
  }

  function uartBaudValue() {
    const preset = read("uartBaudPreset");
    const raw = preset === "custom" ? read("uartBaud") : preset;
    const baud = Number(raw);
    if (!Number.isFinite(baud) || baud <= 0) {
      throw new Error(state.language === "zh" ? "波特率必须是有效正整数。" : "Baud rate must be a valid positive integer.");
    }
    return Math.floor(baud);
  }

  function syncUartBaudUI() {
    const preset = read("uartBaudPreset");
    const wrap = $("uartCustomBaudWrap");
    if (wrap) wrap.classList.toggle("is-hidden", preset !== "custom");
    if (preset && preset !== "custom") {
      const input = $("uartBaud");
      if (input) input.value = preset;
    }
  }

  function setUartBaudValue(baud) {
    const value = String(baud || 115200);
    const preset = $("uartBaudPreset");
    if (preset) {
      const hasPreset = Array.from(preset.options).some((option) => option.value === value);
      preset.value = hasPreset ? value : "custom";
    }
    const input = $("uartBaud");
    if (input) input.value = value;
    syncUartBaudUI();
  }

  function protocolPayloadDisplay(hex, mode) {
    if (mode === "hex") return formatProtocolBytes(hex, "hex");
    if (mode === "binary") return formatProtocolBytes(hex, "binary");
    return formatProtocolBytes(hex, "terminal");
  }

  function setProtocolPayloadHex(textarea, hiddenId, hex) {
    if (!textarea) return;
    textarea.dataset.payloadHex = String(hex || "").toLowerCase();
    const hidden = hiddenId ? $(hiddenId) : null;
    if (hidden) hidden.value = textarea.dataset.payloadHex;
  }

  function restoreProtocolPayload(textareaId, modeId, hiddenId, hex) {
    const textarea = $(textareaId);
    if (!textarea) return;
    const mode = read(modeId) || "text";
    const payloadHex = String(hex || "").toLowerCase();
    textarea.dataset.payloadMode = mode;
    textarea.dataset.payloadInvalid = "false";
    textarea.dataset.payloadSynthetic = mode === "text" && hasNonTerminalBytes(payloadHex) ? "true" : "false";
    setProtocolPayloadHex(textarea, hiddenId, payloadHex);
    textarea.value = protocolPayloadDisplay(payloadHex, mode);
    sanitizeProtocolPayload(textarea, mode);
  }

  function updateProtocolPayloadCache(textareaId, modeId, lineEndingId = "", hiddenId = "") {
    const textarea = $(textareaId);
    if (!textarea) return;
    const mode = read(modeId) || "text";
    sanitizeProtocolPayload(textarea, mode);
    try {
      setProtocolPayloadHex(textarea, hiddenId, protocolPayloadHex(textareaId, mode, lineEndingId ? read(lineEndingId) : "none"));
      textarea.dataset.payloadInvalid = "false";
      textarea.dataset.payloadSynthetic = "false";
    } catch {
      textarea.dataset.payloadInvalid = "true";
    }
  }

  function hasNonTerminalBytes(hex) {
    return bytesFromHex(hex).some((value) => value !== 10 && value !== 13 && (value < 32 || value > 126));
  }

  function syncProtocolPayloadMode(textareaId, modeId, lineEndingId = "", hiddenId = "") {
    const textarea = $(textareaId);
    const modeNode = $(modeId);
    if (!textarea || !modeNode) return;
    const nextMode = modeNode.value || "text";
    const previousMode = textarea.dataset.payloadMode || nextMode;
    let hex = textarea.dataset.payloadHex || (hiddenId ? read(hiddenId) : "");
    if (previousMode !== nextMode) {
      try {
        if (previousMode === "text" && textarea.dataset.payloadSynthetic === "true") {
          hex = textarea.dataset.payloadHex || (hiddenId ? read(hiddenId) : "");
        } else {
          hex = protocolPayloadHex(textareaId, previousMode, lineEndingId ? read(lineEndingId) : "none");
        }
      } catch {
        hex = textarea.dataset.payloadHex || (hiddenId ? read(hiddenId) : "");
      }
      textarea.value = protocolPayloadDisplay(hex, nextMode);
      setProtocolPayloadHex(textarea, hiddenId, hex);
      textarea.dataset.payloadInvalid = "false";
      textarea.dataset.payloadSynthetic = nextMode === "text" && hasNonTerminalBytes(hex) ? "true" : "false";
    } else {
      updateProtocolPayloadCache(textareaId, modeId, lineEndingId, hiddenId);
    }
    textarea.dataset.payloadMode = nextMode;
    sanitizeProtocolPayload(textarea, nextMode);
  }

  function refreshProtocolPayloadDisplay(textareaId, modeId, hiddenId = "") {
    const textarea = $(textareaId);
    if (!textarea) return;
    const mode = read(modeId) || "text";
    const hex = textarea.dataset.payloadHex || (hiddenId ? read(hiddenId) : "");
    textarea.value = protocolPayloadDisplay(hex, mode);
    sanitizeProtocolPayload(textarea, mode);
  }

  function sanitizeProtocolPayload(textarea, mode) {
    if (!textarea) return;
    if (mode === "hex") {
      const clean = textarea.value.replace(/(?:0x|0X)/g, "").replace(/[^a-fA-F0-9\s]/g, "");
      if (textarea.value !== clean) textarea.value = clean;
    } else if (mode === "binary") {
      const clean = textarea.value.replace(/[^01\s_]/g, "");
      if (textarea.value !== clean) textarea.value = clean;
    }
  }

  function bytesFromHex(hexText) {
    const clean = String(hexText || "").replace(/[^a-fA-F0-9]/g, "");
    const bytes = [];
    for (let index = 0; index + 1 < clean.length; index += 2) {
      bytes.push(Number.parseInt(clean.slice(index, index + 2), 16));
    }
    return bytes;
  }

  function formatProtocolBytes(hexText, mode = "decoded") {
    const bytes = bytesFromHex(hexText);
    if (!bytes.length) return "";
    if (mode === "json") return JSON.stringify({ hex: bytes.map((value) => value.toString(16).padStart(2, "0")).join(""), bytes }, null, 2);
    if (mode === "binary") return bytes.map((value) => value.toString(2).padStart(8, "0")).join(" ");
    const hex = bytes.map((value) => value.toString(16).padStart(2, "0")).join(" ");
    if (mode === "terminal") {
      return bytes.map((value) => value >= 32 && value <= 126 ? String.fromCharCode(value) : value === 10 ? "\n" : value === 13 ? "\r" : ".").join("");
    }
    if (mode === "hex") return hex;
    const ascii = bytes.map((value) => value >= 32 && value <= 126 ? String.fromCharCode(value) : ".").join("");
    return `HEX ${hex}\nASCII ${ascii}`;
  }

  function positiveInteger(value, fallback) {
    const parsed = Number(value);
    return Number.isFinite(parsed) && parsed >= 0 ? Math.floor(parsed) : fallback;
  }

  function clamp(value, min, max) {
    return Math.min(max, Math.max(min, value));
  }

  function formatNumber(value) {
    if (!Number.isFinite(Number(value))) return String(value);
    return new Intl.NumberFormat("en-US", { maximumFractionDigits: 3 }).format(Number(value));
  }

  function formatFrequency(value) {
    const hz = Number(value);
    if (!Number.isFinite(hz)) return String(value);
    if (hz >= 1000000) return `${formatNumber(hz / 1000000)} MHz`;
    if (hz >= 1000) return `${formatNumber(hz / 1000)} kHz`;
    return `${formatNumber(hz)} Hz`;
  }

  function formatDurationMs(ms) {
    const value = Number(ms);
    if (!Number.isFinite(value)) return "-";
    if (value >= 1000) return `${formatNumber(value / 1000)} s`;
    if (value >= 1) return `${formatNumber(value)} ms`;
    return `${formatNumber(value * 1000)} us`;
  }

  function formatBytes(value) {
    const bytes = Number(value);
    if (!Number.isFinite(bytes)) return String(value);
    if (bytes >= 1024 * 1024) return `${formatNumber(bytes / (1024 * 1024))} MB`;
    if (bytes >= 1024) return `${formatNumber(bytes / 1024)} KB`;
    return `${formatNumber(bytes)} B`;
  }

  function formatMemoryPair(usedBytes, maxBytes) {
    const used = Number(usedBytes);
    const max = Number(maxBytes);
    if (!max) return formatBytes(used);
    if (used >= 1024 || max >= 1024) return `${formatNumber(used / 1024)}/${formatNumber(max / 1024)} KB`;
    return `${formatNumber(used)}/${formatNumber(max)} B`;
  }

  function logicRunDelayMilliseconds(params) {
    const sampleRate = Math.max(1, Number(params.sample_rate ?? 1));
    const samples = Math.max(1, Number(params.samples ?? 1));
    const captureWindowMs = (samples / sampleRate) * 1000;
    return clamp(Math.round(captureWindowMs + 180), 220, 1500);
  }

  function delay(milliseconds) {
    return new Promise((resolve) => setTimeout(resolve, milliseconds));
  }

  function escapeHTML(value) {
    return String(value).replace(/[&<>"']/g, (char) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;", "'": "&#39;" }[char]));
  }

  function wireUI() {
    let resizeFrame = 0;
    window.addEventListener("resize", () => {
      cancelAnimationFrame(resizeFrame);
      resizeFrame = requestAnimationFrame(() => render());
    });
    const languageSelect = $("languageSelect");
    if (languageSelect) {
      languageSelect.value = state.language;
      languageSelect.addEventListener("change", () => {
        state.language = languageSelect.value === "en" ? "en" : "zh";
        localStorage.setItem("embedlabs.rpmon.lang", state.language);
        state.appliedLanguage = "";
        render();
      });
    }
    document.querySelectorAll(".tab-button").forEach((button) => button.addEventListener("click", () => setPanel(button.dataset.panel)));
    ["uart", "i2c", "spi"].forEach((protocol) => {
      $(`${protocol}DeviceSelect`)?.addEventListener("change", (event) => {
        selectProtocolDevice(protocol, event.target.value);
      });
      $(`${protocol}DeviceTable`)?.addEventListener("click", (event) => {
        const release = event.target?.closest?.("[data-protocol-release-row]");
        if (release) {
          event.preventDefault();
          event.stopPropagation();
          selectProtocolDevice(release.dataset.protocolReleaseRow, release.dataset.deviceId);
          releaseSelectedProtocolDevice(release.dataset.protocolReleaseRow);
          return;
        }
        const row = event.target?.closest?.("[data-protocol-device-row]");
        if (row) {
          selectProtocolDevice(row.dataset.protocolDeviceRow, row.dataset.deviceId);
        }
      });
    });
    ["uartTx", "uartRx", "i2cSda", "i2cScl", "spiSck", "spiMosi", "spiMiso", "spiCs"].forEach((id) => {
      $(id)?.addEventListener("change", () => {
        state.activePreset = "";
        if (id.startsWith("uart")) syncSelectedProtocolFromControls("uart");
        if (id.startsWith("i2c")) syncSelectedProtocolFromControls("i2c");
        if (id.startsWith("spi")) syncSelectedProtocolFromControls("spi");
        render();
      });
    });
    document.querySelectorAll("[data-action]").forEach((button) => {
      button.addEventListener("click", () => {
        const action = button.dataset.action;
        if (localActions.has(action)) {
          handleLocalAction(action);
          return;
        }
        try {
          runOperation(operationFromForm(action === "ai.demo" ? "logic.capture" : action)).catch(() => {});
        } catch (error) {
          const message = userFacingErrorMessage(error);
          renderStatus({ error: message });
          setLogicStatus(message, "warning");
          showErrorDialog(message);
        }
      });
    });
    document.querySelectorAll("[data-action-local]").forEach((button) => {
      button.addEventListener("click", () => {
        const action = button.dataset.actionLocal;
        if (localActions.has(action)) {
          handleLocalAction(action);
          return;
        }
        if (action?.endsWith(".clear")) {
          clearProtocolLog(action.split(".")[0]);
          return;
        }
        if (action?.endsWith(".addDevice")) {
          addProtocolDevice(action.split(".")[0]);
          return;
        }
        if (action?.endsWith(".releaseDevice")) {
          releaseSelectedProtocolDevice(action.split(".")[0]);
        }
      });
    });
    $("aiQueue")?.addEventListener("click", (event) => {
      const row = event.target?.closest?.("[data-ai-index]");
      if (!row) return;
      state.selectedQueueIndex = Number(row.dataset.aiIndex || 0);
      renderQueue();
    });
    document.querySelectorAll("[data-preset]").forEach((button) => {
      button.addEventListener("click", () => applyLogicPreset(button.dataset.preset));
    });
    $("logicChannelMatrix").addEventListener("change", (event) => {
      const target = event.target;
      if (target?.dataset?.channelColor !== undefined) {
        state.activePreset = "";
        const channel = channelFor(Number(target.dataset.channelColor));
        if (channel) channel.color = target.value;
        renderLogic(state.snapshot?.logic);
      }
      if (target?.dataset?.channelPull !== undefined) {
        state.activePreset = "";
        const channel = channelFor(Number(target.dataset.channelPull));
        if (channel) channel.pull = target.value || "none";
        render();
      }
      if (target?.dataset?.channelTrigger !== undefined) {
        state.activePreset = "";
        const channel = channelFor(Number(target.dataset.channelTrigger));
        if (channel) channel.trigger = target.value || "ignore";
        render();
      }
      if (target?.dataset?.channelInvert !== undefined) {
        state.activePreset = "";
        const channel = channelFor(Number(target.dataset.channelInvert));
        if (channel) channel.invert = Boolean(target.checked);
        renderLogic(state.snapshot?.logic);
      }
    });
    $("logicChannelMatrix").addEventListener("input", (event) => {
      const target = event.target;
      if (target?.dataset?.channelName !== undefined) {
        state.activePreset = "";
        const channel = channelFor(Number(target.dataset.channelName));
        if (channel) channel.name = target.value || `GP${channel.pin}`;
        renderLogic(state.snapshot?.logic);
      }
    });
    $("logicChannelMatrix").addEventListener("click", (event) => {
      const target = event.target;
      if (target?.dataset?.channelRemove !== undefined) {
        removeChannel(Number(target.dataset.channelRemove));
      }
    });
    $("logicAddChannel").addEventListener("click", () => {
      addSelectedChannel();
    });
    $("logicClearChannels").addEventListener("click", () => {
      clearChannels();
    });
    $("errorModalClose").addEventListener("click", () => $("errorModal").classList.add("hidden"));
    $("errorModal").addEventListener("click", (event) => {
      if (event.target?.id === "errorModal") $("errorModal").classList.add("hidden");
    });
    $("sessionLoadInput")?.addEventListener("change", (event) => {
      const file = event.target?.files?.[0];
      loadAnalyzerSessionFile(file).finally(() => {
        if (event.target) event.target.value = "";
      });
    });
    const cursorAColor = $("cursorAColor");
    const cursorBColor = $("cursorBColor");
    if (cursorAColor) {
      cursorAColor.value = state.cursorColors.a;
      cursorAColor.addEventListener("input", () => {
        state.cursorColors.a = cursorAColor.value || "#f6c85f";
        renderLogic(state.snapshot?.logic);
      });
    }
    if (cursorBColor) {
      cursorBColor.value = state.cursorColors.b;
      cursorBColor.addEventListener("input", () => {
        state.cursorColors.b = cursorBColor.value || "#ff7a7a";
        renderLogic(state.snapshot?.logic);
      });
    }
    const protocolModeHandlers = {
      uartSendMode: () => syncProtocolPayloadMode("uartPayload", "uartSendMode", "uartLineEnding", "uartHex"),
      i2cPayloadMode: () => syncProtocolPayloadMode("i2cPayload", "i2cPayloadMode", "", "i2cWriteHex"),
      spiPayloadMode: () => syncProtocolPayloadMode("spiPayload", "spiPayloadMode", "", "spiHex"),
      uartLineEnding: () => updateProtocolPayloadCache("uartPayload", "uartSendMode", "uartLineEnding", "uartHex"),
      uartBaudPreset: syncUartBaudUI
    };
    Object.entries(protocolModeHandlers).forEach(([id, handler]) => {
      const node = $(id);
      if (!node) return;
      node.addEventListener("change", () => {
        handler();
        const protocol = protocolFromControlId(id);
        if (protocol) syncSelectedProtocolPageState(protocol);
        render();
      });
    });
    [
      ["uartPayload", "uartSendMode", "uartLineEnding", "uartHex"],
      ["i2cPayload", "i2cPayloadMode", "", "i2cWriteHex"],
      ["spiPayload", "spiPayloadMode", "", "spiHex"]
    ].forEach(([textareaId, modeId, lineEndingId, hiddenId]) => {
      const textarea = $(textareaId);
      if (!textarea) return;
      textarea.dataset.payloadMode = read(modeId) || "text";
      updateProtocolPayloadCache(textareaId, modeId, lineEndingId, hiddenId);
      refreshProtocolPayloadDisplay(textareaId, modeId, hiddenId);
      textarea.addEventListener("input", () => {
        updateProtocolPayloadCache(textareaId, modeId, lineEndingId, hiddenId);
        const protocol = protocolFromControlId(textareaId);
        if (protocol) syncSelectedProtocolPageState(protocol);
      });
    });
    syncUartBaudUI();
    ["logicTriggerType", "logicTriggerPin", "logicPatternBase", "logicPattern", "logicPreSamples", "logicPostSamples", "logicSearchSamples", "logicBurstCount", "logicSampleRate", "logicPull", "logicSampleMode", "decoderType", "decoderDataPin", "decoderClockPin", "decoderCsPin", "decoderBaud", "uartDisplayMode", "i2cDisplayMode", "spiDisplayMode", "i2cTransferType", "i2cAddress", "i2cReadLen", "spiReadLen"].forEach((id) => {
      const node = $(id);
      if (node) node.addEventListener("input", () => {
        state.activePreset = "";
        const protocol = protocolFromControlId(id);
        if (protocol) syncSelectedProtocolPageState(protocol);
        render();
      });
      if (node) node.addEventListener("change", () => {
        state.activePreset = "";
        const protocol = protocolFromControlId(id);
        if (protocol) syncSelectedProtocolPageState(protocol);
        render();
      });
    });
    const logicCanvas = $("logicCanvas");
    logicCanvas.addEventListener("pointerdown", (event) => {
      const logic = state.snapshot?.logic;
      if (!logic?.samples || event.button !== 0) return;
      const cursor = cursorHitFromCanvasEvent(event, logic);
      if (cursor) {
        state.cursorDrag = {
          pointerId: event.pointerId,
          cursor,
          moved: false
        };
        logicCanvas.setPointerCapture?.(event.pointerId);
        event.preventDefault();
        return;
      }
      if (state.zoom <= 1) return;
      state.canvasDrag = {
        pointerId: event.pointerId,
        startClientX: event.clientX,
        startViewStart: state.viewStart,
        moved: false
      };
      logicCanvas.setPointerCapture?.(event.pointerId);
      logicCanvas.closest(".waveform-scroll")?.classList.add("dragging");
      event.preventDefault();
    });
    logicCanvas.addEventListener("pointermove", (event) => {
      const cursorDrag = state.cursorDrag;
      const cursorLogic = state.snapshot?.logic;
      if (cursorDrag && cursorDrag.pointerId === event.pointerId && cursorLogic?.samples) {
        const sample = sampleFromCanvasEvent(event, cursorLogic);
        if (cursorDrag.cursor === "a") state.cursorA = sample;
        else state.cursorB = sample;
        cursorDrag.moved = true;
        renderLogic(cursorLogic);
        event.preventDefault();
        return;
      }
      const drag = state.canvasDrag;
      const logic = state.snapshot?.logic;
      if (!drag || drag.pointerId !== event.pointerId || !logic?.samples) return;
      const deltaClientX = event.clientX - drag.startClientX;
      if (Math.abs(deltaClientX) > 3) drag.moved = true;
      const geometry = logicCanvasGeometry(logicCanvas);
      const deltaCanvasX = deltaClientX * geometry.scaleX;
      const view = currentView(logic);
      const maxStart = Math.max(0, Number(logic.samples || 1) - view.span);
      state.viewStart = clamp(drag.startViewStart - (deltaCanvasX / geometry.plotWidth) * view.span, 0, maxStart);
      state.userViewLocked = true;
      renderLogic(logic);
      event.preventDefault();
    });
    const finishCanvasDrag = (event) => {
      const cursorDrag = state.cursorDrag;
      if (cursorDrag && cursorDrag.pointerId === event.pointerId) {
        state.suppressCanvasClick = Boolean(cursorDrag.moved);
        state.cursorDrag = null;
        logicCanvas.releasePointerCapture?.(event.pointerId);
        return;
      }
      const drag = state.canvasDrag;
      if (!drag || drag.pointerId !== event.pointerId) return;
      state.suppressCanvasClick = Boolean(drag.moved);
      state.canvasDrag = null;
      logicCanvas.releasePointerCapture?.(event.pointerId);
      logicCanvas.closest(".waveform-scroll")?.classList.remove("dragging");
    };
    logicCanvas.addEventListener("pointerup", finishCanvasDrag);
    logicCanvas.addEventListener("pointercancel", finishCanvasDrag);
    logicCanvas.addEventListener("wheel", (event) => {
      const logic = state.snapshot?.logic;
      if (!logic?.samples || state.zoom <= 1) return;
      const scrollHost = logicCanvas.closest(".waveform-scroll");
      if (scrollHost && Math.abs(event.deltaY) > Math.abs(event.deltaX) && scrollHost.scrollHeight > scrollHost.clientHeight) {
        return;
      }
      const view = currentView(logic);
      const maxStart = Math.max(0, Number(logic.samples || 1) - view.span);
      const delta = Math.abs(event.deltaX) > Math.abs(event.deltaY) ? event.deltaX : event.deltaY;
      state.viewStart = clamp(state.viewStart + (delta / 900) * view.span, 0, maxStart);
      state.userViewLocked = true;
      renderLogic(logic);
      event.preventDefault();
    }, { passive: false });
    logicCanvas.addEventListener("click", (event) => {
      if (state.suppressCanvasClick) {
        state.suppressCanvasClick = false;
        return;
      }
      const logic = state.snapshot?.logic;
      if (!logic?.samples) return;
      const sample = sampleFromCanvasEvent(event, logic);
      if (state.cursorTarget === "a") {
        state.cursorA = sample;
        state.cursorTarget = "b";
      } else {
        state.cursorB = sample;
        state.cursorTarget = "a";
      }
      renderLogic(logic);
    });
    const logicOverview = $("logicOverview");
    logicOverview.addEventListener("pointerdown", (event) => {
      const logic = state.snapshot?.logic;
      if (!logic?.samples || !logic?.words?.length || event.button !== 0) return;
      state.overviewDrag = {
        pointerId: event.pointerId,
        startClientX: event.clientX,
        moved: false
      };
      logicOverview.setPointerCapture?.(event.pointerId);
      logicOverview.classList.add("dragging");
      setViewFromOverviewEvent(event, logic);
      event.preventDefault();
    });
    logicOverview.addEventListener("pointermove", (event) => {
      const drag = state.overviewDrag;
      const logic = state.snapshot?.logic;
      if (!drag || drag.pointerId !== event.pointerId || !logic?.samples || !logic?.words?.length) return;
      if (Math.abs(event.clientX - drag.startClientX) > 3) drag.moved = true;
      setViewFromOverviewEvent(event, logic);
      event.preventDefault();
    });
    const finishOverviewDrag = (event) => {
      const drag = state.overviewDrag;
      if (!drag || drag.pointerId !== event.pointerId) return;
      state.suppressOverviewClick = Boolean(drag.moved);
      state.overviewDrag = null;
      logicOverview.releasePointerCapture?.(event.pointerId);
      logicOverview.classList.remove("dragging");
    };
    logicOverview.addEventListener("pointerup", finishOverviewDrag);
    logicOverview.addEventListener("pointercancel", finishOverviewDrag);
    logicOverview.addEventListener("click", (event) => {
      if (state.suppressOverviewClick) {
        state.suppressOverviewClick = false;
        return;
      }
      const logic = state.snapshot?.logic;
      if (!logic?.samples || !logic?.words?.length) return;
      setViewFromOverviewEvent(event, logic);
    });
    window.addEventListener("message", (event) => {
      const message = event.data ?? {};
      if (message.type !== "embedlabs.rpmon.operation") return;
      runOperation(message.operation).catch(() => {});
    });
  }

  window.EmbedLabsPicoMonitor = {
    runOperation,
    applySnapshot,
    showPanel: setPanel,
    exportSession: buildAnalyzerSession,
    importSession: applyAnalyzerSession,
    exportEvidence: buildAnalyzerEvidence,
    getState: () => state,
    setClient: (client) => { state.client = client; }
  };

  wireUI();
  setPanel(state.activePanel);
  applySnapshot(state.client.snapshot ? state.client.snapshot() : {});
  if (new URLSearchParams(location.search).get("api") || new URLSearchParams(location.search).get("ws") || window.embedLabsRpmon?.invoke) {
    runOperation(normalizeOperation("probe", {}, { show: true, panel: state.activePanel })).catch(() => {});
  }
})();
