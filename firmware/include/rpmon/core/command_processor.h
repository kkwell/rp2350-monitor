#pragma once

#include "rpmon/core/channel_manager.h"
#include "rpmon/core/event_bus.h"
#include "rpmon/core/logic_analyzer.h"
#include "rpmon/net/wifi_manager.h"
#include "rpmon/probe/debug_probe.h"

namespace rpmon {

class CommandProcessor {
public:
    CommandProcessor(WifiManager &wifi, ChannelManager &channels, LogicAnalyzer &logic, DebugProbe &probe, PinManager &pins, EventBus &events);
    void handle_line(const char *line, LineSink &reply);

private:
    void handle_channel_config(const char *line, LineSink &reply);
    void handle_channel_io(const char *line, LineSink &reply, const char *cmd);
    void handle_gpio_io(const char *line, LineSink &reply, const char *cmd);
    void handle_logic_io(const char *line, LineSink &reply, const char *cmd);
    void handle_probe_io(const char *line, LineSink &reply, const char *cmd);

    WifiManager &wifi_;
    ChannelManager &channels_;
    LogicAnalyzer &logic_;
    DebugProbe &probe_;
    PinManager &pins_;
    EventBus &events_;
};

} // namespace rpmon
