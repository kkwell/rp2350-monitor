#include "rpmon/drivers/i2c_channel.h"

#include <cstdio>

#include "hardware/gpio.h"
#include "rpmon/core/event_bus.h"
#include "rpmon/core/pin_manager.h"

namespace rpmon {

i2c_inst_t *I2cChannel::i2c() const {
    return config_.instance == 1 ? i2c1 : i2c0;
}

bool I2cChannel::configure(const ChannelConfig &config, PinManager &pins, char *err, size_t err_len) {
    if (!pins.validate_i2c(config.instance, config.pins.sda, config.pins.scl)) {
        snprintf(err, err_len, "invalid I2C%d SDA/SCL pin mapping", config.instance);
        return false;
    }
    if (!pins.claim(config.pins.sda, config.id, PinRole::I2cSda) ||
        !pins.claim(config.pins.scl, config.id, PinRole::I2cScl)) {
        pins.release_channel(config.id);
        snprintf(err, err_len, "I2C pins already in use or not exposed");
        return false;
    }
    config_ = config;
    configured_ = true;
    active_ = false;
    return true;
}

bool I2cChannel::start(char *err, size_t err_len) {
    if (!configured_) {
        snprintf(err, err_len, "I2C channel not configured");
        return false;
    }
    i2c_init(i2c(), config_.baud);
    gpio_set_function(config_.pins.sda, GPIO_FUNC_I2C);
    gpio_set_function(config_.pins.scl, GPIO_FUNC_I2C);
    gpio_pull_up(config_.pins.sda);
    gpio_pull_up(config_.pins.scl);
    active_ = true;
    return true;
}

void I2cChannel::stop() {
    if (active_) {
        i2c_deinit(i2c());
    }
    if (configured_) {
        gpio_set_function(config_.pins.sda, GPIO_FUNC_NULL);
        gpio_set_function(config_.pins.scl, GPIO_FUNC_NULL);
    }
    active_ = false;
}

void I2cChannel::poll(EventBus &events) {
    (void)events;
}

bool I2cChannel::write(const uint8_t *data, size_t len, EventBus &events, char *err, size_t err_len) {
    return transfer(config_.address, data, len, 0, events, err, err_len);
}

bool I2cChannel::transfer(uint8_t address, const uint8_t *tx, size_t tx_len, size_t rx_len, EventBus &events, char *err, size_t err_len) {
    if (!active_) {
        snprintf(err, err_len, "I2C channel is not active");
        return false;
    }
    if (address > 0x7f) {
        snprintf(err, err_len, "I2C address must be 7-bit");
        return false;
    }
    if (tx_len > kMaxPayloadBytes || rx_len > kMaxPayloadBytes) {
        snprintf(err, err_len, "I2C transfer length exceeds %u bytes", static_cast<unsigned>(kMaxPayloadBytes));
        return false;
    }
    if (tx_len > 0) {
        int written = i2c_write_blocking(i2c(), address, tx, tx_len, rx_len > 0);
        if (written < 0) {
            snprintf(err, err_len, "I2C write failed: %d", written);
            return false;
        }
        events.publish_data(config_.id, ProtocolType::I2c, "tx", tx, tx_len);
    }
    if (rx_len > 0) {
        uint8_t rx_buf[kMaxPayloadBytes] = {};
        int read = i2c_read_blocking(i2c(), address, rx_buf, rx_len, false);
        if (read < 0) {
            snprintf(err, err_len, "I2C read failed: %d", read);
            return false;
        }
        events.publish_data(config_.id, ProtocolType::I2c, "rx", rx_buf, rx_len);
    }
    return true;
}

void I2cChannel::describe_json(char *out, size_t out_len) const {
    snprintf(out, out_len,
             "{\"id\":%d,\"type\":\"i2c\",\"instance\":%d,\"active\":%s,\"baud\":%lu,\"sda\":%d,\"scl\":%d,\"address\":%u}",
             config_.id,
             config_.instance,
             active_ ? "true" : "false",
             static_cast<unsigned long>(config_.baud),
             config_.pins.sda,
             config_.pins.scl,
             config_.address);
}

} // namespace rpmon

