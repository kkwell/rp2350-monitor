#pragma once

#include <cstdint>

#include "rpmon/config.h"

namespace rpmon {

enum class PinRole : uint8_t {
    UartTx,
    UartRx,
    SpiSck,
    SpiMosi,
    SpiMiso,
    SpiCs,
    I2cSda,
    I2cScl,
    CanTx,
    CanRx,
    Gpio,
    Logic
};

class PinManager {
public:
    bool is_exposed_gpio(int gpio) const;
    bool claim(int gpio, int channel_id, PinRole role);
    void release_channel(int channel_id);
    int owner(int gpio) const;
    bool validate_uart(int instance, int tx, int rx) const;
    bool validate_spi(int instance, int sck, int mosi, int miso, int cs) const;
    bool validate_i2c(int instance, int sda, int scl) const;
    void pins_json(char *out, size_t out_len) const;

private:
    bool pin_in_list(int pin, const int *pins, size_t count) const;
    int owners_[29] = {};
};

} // namespace rpmon
