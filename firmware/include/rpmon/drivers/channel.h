#pragma once

#include <cstddef>
#include <cstdint>

#include "rpmon/config.h"

namespace rpmon {

class EventBus;
class PinManager;

class Channel {
public:
    virtual ~Channel() = default;
    virtual bool configure(const ChannelConfig &config, PinManager &pins, char *err, size_t err_len) = 0;
    virtual bool start(char *err, size_t err_len) = 0;
    virtual void stop() = 0;
    virtual void poll(EventBus &events) = 0;
    virtual bool write(const uint8_t *data, size_t len, EventBus &events, char *err, size_t err_len) = 0;
    virtual bool transfer(uint8_t address, const uint8_t *tx, size_t tx_len, size_t rx_len, EventBus &events, char *err, size_t err_len) = 0;
    virtual void describe_json(char *out, size_t out_len) const = 0;
    virtual int id() const = 0;
    virtual int instance() const = 0;
    virtual ProtocolType type() const = 0;
    virtual bool active() const = 0;
};

} // namespace rpmon
