#pragma once

#include "rpmon/core/event_bus.h"
#include "rpmon/core/pin_manager.h"
#include "rpmon/drivers/can_channel.h"
#include "rpmon/drivers/gpio_channel.h"
#include "rpmon/drivers/i2c_channel.h"
#include "rpmon/drivers/spi_channel.h"
#include "rpmon/drivers/uart_channel.h"

namespace rpmon {

class ChannelManager {
public:
    ChannelManager(PinManager &pins, EventBus &events);
    bool configure(const ChannelConfig &config, char *err, size_t err_len);
    bool start(int id, char *err, size_t err_len);
    bool stop(int id, char *err, size_t err_len);
    bool release(int id, char *err, size_t err_len);
    bool write(int id, const uint8_t *data, size_t len, char *err, size_t err_len);
    bool transfer(int id, uint8_t address, const uint8_t *tx, size_t tx_len, size_t rx_len, char *err, size_t err_len);
    bool gpio_write(int id, bool level, char *err, size_t err_len);
    bool gpio_read(int id, bool &level, char *err, size_t err_len);
    void poll();
    void list_json(char *out, size_t out_len) const;

private:
    struct Slot {
        bool used = false;
        ProtocolType type = ProtocolType::Unknown;
        UartChannel uart;
        SpiChannel spi;
        I2cChannel i2c;
        CanChannel can;
        GpioChannel gpio;
    };

    Slot *find_slot(int id);
    const Slot *find_slot(int id) const;
    Channel *driver_for(Slot &slot);
    const Channel *driver_for(const Slot &slot) const;

    PinManager &pins_;
    EventBus &events_;
    Slot slots_[kMaxChannels];
};

} // namespace rpmon
