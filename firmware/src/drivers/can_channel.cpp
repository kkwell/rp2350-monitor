#include "rpmon/drivers/can_channel.h"

#include <cstdio>

namespace rpmon {

bool CanChannel::configure(const ChannelConfig &config, PinManager &pins, char *err, size_t err_len) {
    (void)pins;
    config_ = config;
    snprintf(err, err_len, "CAN is reserved; select MCP2515-over-SPI or PIO-CAN before enabling");
    return false;
}

bool CanChannel::start(char *err, size_t err_len) {
    snprintf(err, err_len, "CAN is reserved in this firmware version");
    return false;
}

void CanChannel::poll(EventBus &events) {
    (void)events;
}

bool CanChannel::write(const uint8_t *data, size_t len, EventBus &events, char *err, size_t err_len) {
    (void)data;
    (void)len;
    (void)events;
    snprintf(err, err_len, "CAN is reserved in this firmware version");
    return false;
}

bool CanChannel::transfer(uint8_t address, const uint8_t *tx, size_t tx_len, size_t rx_len, EventBus &events, char *err, size_t err_len) {
    (void)address;
    (void)tx;
    (void)tx_len;
    (void)rx_len;
    (void)events;
    snprintf(err, err_len, "CAN is reserved in this firmware version");
    return false;
}

void CanChannel::describe_json(char *out, size_t out_len) const {
    snprintf(out, out_len, "{\"id\":%d,\"type\":\"can\",\"active\":false,\"reserved\":true}", config_.id);
}

} // namespace rpmon

