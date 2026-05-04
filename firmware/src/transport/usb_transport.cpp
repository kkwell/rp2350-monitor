#include "rpmon/transport/usb_transport.h"

#include <cstdio>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

namespace rpmon {

UsbTransport::UsbTransport(CommandProcessor &processor) : processor_(processor) {}

void UsbTransport::poll() {
    while (true) {
        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) {
            break;
        }
        push_char(static_cast<char>(ch));
    }
}

bool UsbTransport::send_line(const char *line) {
    if (!stdio_usb_connected()) {
        return false;
    }
    std::printf("%s\n", line ? line : "");
    return true;
}

void UsbTransport::push_char(char c) {
    if (c == '\r') {
        return;
    }
    if (c == '\n') {
        line_[pos_] = '\0';
        if (pos_ > 0) {
            processor_.handle_line(line_, *this);
        }
        pos_ = 0;
        return;
    }
    if (pos_ + 1 < sizeof(line_)) {
        line_[pos_++] = c;
    } else {
        pos_ = 0;
    }
}

} // namespace rpmon
