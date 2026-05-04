#pragma once

#include "rpmon/core/command_processor.h"
#include "rpmon/core/event_bus.h"

namespace rpmon {

class UsbTransport final : public LineSink {
public:
    explicit UsbTransport(CommandProcessor &processor);
    void poll();
    bool send_line(const char *line) override;

private:
    void push_char(char c);

    CommandProcessor &processor_;
    char line_[kLineBufferSize] = {};
    size_t pos_ = 0;
};

} // namespace rpmon
