#pragma once

#include "rpmon/core/channel_manager.h"
#include "rpmon/core/event_bus.h"
#include "rpmon/net/wifi_manager.h"

namespace rpmon {

class CommandProcessor {
public:
    CommandProcessor(WifiManager &wifi, ChannelManager &channels, PinManager &pins, EventBus &events);
    void handle_line(const char *line, LineSink &reply);

private:
    void handle_channel_config(const char *line, LineSink &reply);
    void handle_channel_io(const char *line, LineSink &reply, const char *cmd);

    WifiManager &wifi_;
    ChannelManager &channels_;
    PinManager &pins_;
    EventBus &events_;
};

} // namespace rpmon

