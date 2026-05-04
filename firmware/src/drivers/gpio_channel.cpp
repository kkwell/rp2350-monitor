#include "rpmon/drivers/gpio_channel.h"

#include <cstdio>

#include "hardware/gpio.h"
#include "rpmon/core/event_bus.h"
#include "rpmon/core/pin_manager.h"

namespace rpmon {

bool GpioChannel::configure(const ChannelConfig &config, PinManager &pins, char *err, size_t err_len) {
    if (!pins.is_exposed_gpio(config.pins.gpio)) {
        snprintf(err, err_len, "GPIO pin is not exposed on Pico 2 W header");
        return false;
    }
    if (config.gpio_pull_up && config.gpio_pull_down) {
        snprintf(err, err_len, "GPIO pull_up and pull_down cannot both be true");
        return false;
    }
    if (!pins.claim(config.pins.gpio, config.id, PinRole::Gpio)) {
        snprintf(err, err_len, "GPIO pin already in use or not exposed");
        return false;
    }
    config_ = config;
    configured_ = true;
    active_ = false;
    last_level_ = config_.gpio_initial;
    have_last_level_ = false;
    return true;
}

bool GpioChannel::start(char *err, size_t err_len) {
    if (!configured_) {
        snprintf(err, err_len, "GPIO channel not configured");
        return false;
    }
    gpio_init(config_.pins.gpio);
    gpio_set_function(config_.pins.gpio, GPIO_FUNC_SIO);
    gpio_disable_pulls(config_.pins.gpio);
    if (config_.gpio_output) {
        gpio_put(config_.pins.gpio, config_.gpio_initial ? 1 : 0);
        gpio_set_dir(config_.pins.gpio, GPIO_OUT);
        last_level_ = config_.gpio_initial;
    } else {
        gpio_set_dir(config_.pins.gpio, GPIO_IN);
        if (config_.gpio_pull_up) {
            gpio_pull_up(config_.pins.gpio);
        } else if (config_.gpio_pull_down) {
            gpio_pull_down(config_.pins.gpio);
        }
        last_level_ = sample_level();
    }
    have_last_level_ = true;
    active_ = true;
    return true;
}

void GpioChannel::stop() {
    if (configured_) {
        gpio_disable_pulls(config_.pins.gpio);
        gpio_set_dir(config_.pins.gpio, GPIO_IN);
        gpio_set_function(config_.pins.gpio, GPIO_FUNC_NULL);
    }
    active_ = false;
    have_last_level_ = false;
}

void GpioChannel::poll(EventBus &events) {
    if (!active_ || config_.gpio_output) {
        return;
    }
    bool level = sample_level();
    if (!have_last_level_ || level != last_level_) {
        last_level_ = level;
        have_last_level_ = true;
        publish_level(events, "change", level);
    }
}

bool GpioChannel::write(const uint8_t *data, size_t len, EventBus &events, char *err, size_t err_len) {
    if (!data || len == 0) {
        snprintf(err, err_len, "GPIO write requires one byte");
        return false;
    }
    return set_level(data[0] != 0, events, err, err_len);
}

bool GpioChannel::transfer(uint8_t address, const uint8_t *tx, size_t tx_len, size_t rx_len, EventBus &events, char *err, size_t err_len) {
    (void)address;
    if (tx_len > 0) {
        if (!tx) {
            snprintf(err, err_len, "GPIO transfer had null write payload");
            return false;
        }
        if (!set_level(tx[0] != 0, events, err, err_len)) {
            return false;
        }
    }
    if (rx_len > 0 || tx_len == 0) {
        bool level = false;
        return read_level(level, events, err, err_len);
    }
    return true;
}

bool GpioChannel::set_level(bool level, EventBus &events, char *err, size_t err_len) {
    if (!active_) {
        snprintf(err, err_len, "GPIO channel is not active");
        return false;
    }
    if (!config_.gpio_output) {
        snprintf(err, err_len, "GPIO channel is configured as input");
        return false;
    }
    gpio_put(config_.pins.gpio, level ? 1 : 0);
    last_level_ = level;
    have_last_level_ = true;
    publish_level(events, "write", level);
    return true;
}

bool GpioChannel::read_level(bool &level, EventBus &events, char *err, size_t err_len) {
    if (!active_) {
        snprintf(err, err_len, "GPIO channel is not active");
        return false;
    }
    level = sample_level();
    last_level_ = level;
    have_last_level_ = true;
    publish_level(events, "read", level);
    return true;
}

void GpioChannel::describe_json(char *out, size_t out_len) const {
    bool level = active_ ? sample_level() : last_level_;
    snprintf(out, out_len,
             "{\"id\":%d,\"type\":\"gpio\",\"active\":%s,\"gpio\":%d,\"direction\":\"%s\",\"level\":%s,\"pull\":\"%s\"}",
             config_.id,
             active_ ? "true" : "false",
             config_.pins.gpio,
             config_.gpio_output ? "output" : "input",
             level ? "true" : "false",
             pull_name());
}

const char *GpioChannel::pull_name() const {
    if (config_.gpio_pull_up) {
        return "up";
    }
    if (config_.gpio_pull_down) {
        return "down";
    }
    return "none";
}

bool GpioChannel::sample_level() const {
    return gpio_get(config_.pins.gpio) != 0;
}

void GpioChannel::publish_level(EventBus &events, const char *direction, bool level) {
    uint8_t payload = level ? 1 : 0;
    events.publish_data(config_.id, ProtocolType::Gpio, direction, &payload, 1);
}

} // namespace rpmon
