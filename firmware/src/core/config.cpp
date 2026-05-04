#include "rpmon/config.h"

#include <cstring>

namespace rpmon {

const char *protocol_name(ProtocolType type) {
    switch (type) {
    case ProtocolType::Uart:
        return "uart";
    case ProtocolType::Spi:
        return "spi";
    case ProtocolType::I2c:
        return "i2c";
    case ProtocolType::Can:
        return "can";
    default:
        return "unknown";
    }
}

ProtocolType parse_protocol(const char *name) {
    if (!name) {
        return ProtocolType::Unknown;
    }
    if (std::strcmp(name, "uart") == 0) {
        return ProtocolType::Uart;
    }
    if (std::strcmp(name, "spi") == 0) {
        return ProtocolType::Spi;
    }
    if (std::strcmp(name, "i2c") == 0) {
        return ProtocolType::I2c;
    }
    if (std::strcmp(name, "can") == 0) {
        return ProtocolType::Can;
    }
    return ProtocolType::Unknown;
}

} // namespace rpmon

