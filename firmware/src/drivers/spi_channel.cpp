#include "rpmon/drivers/spi_channel.h"

#include <algorithm>
#include <cstdio>

#include "hardware/gpio.h"
#include "rpmon/core/event_bus.h"
#include "rpmon/core/pin_manager.h"

namespace rpmon {

spi_inst_t *SpiChannel::spi() const {
    return config_.instance == 1 ? spi1 : spi0;
}

bool SpiChannel::configure(const ChannelConfig &config, PinManager &pins, char *err, size_t err_len) {
    if (!pins.validate_spi(config.instance, config.pins.sck, config.pins.mosi, config.pins.miso, config.pins.cs, err, err_len)) {
        return false;
    }
    if (!pins.claim(config.pins.sck, config.id, PinRole::SpiSck) ||
        !pins.claim(config.pins.mosi, config.id, PinRole::SpiMosi) ||
        !pins.claim(config.pins.miso, config.id, PinRole::SpiMiso) ||
        (config.pins.cs >= 0 && !pins.claim(config.pins.cs, config.id, PinRole::SpiCs))) {
        pins.release_channel(config.id);
        snprintf(err, err_len, "SPI pins already in use or not exposed");
        return false;
    }
    config_ = config;
    configured_ = true;
    active_ = false;
    return true;
}

bool SpiChannel::start(char *err, size_t err_len) {
    if (!configured_) {
        snprintf(err, err_len, "SPI channel not configured");
        return false;
    }
    spi_init(spi(), config_.baud);
    gpio_set_function(config_.pins.sck, GPIO_FUNC_SPI);
    gpio_set_function(config_.pins.mosi, GPIO_FUNC_SPI);
    gpio_set_function(config_.pins.miso, GPIO_FUNC_SPI);
    if (config_.pins.cs >= 0) {
        gpio_init(config_.pins.cs);
        gpio_set_dir(config_.pins.cs, GPIO_OUT);
        gpio_put(config_.pins.cs, 1);
    }
    active_ = true;
    return true;
}

void SpiChannel::stop() {
    if (active_) {
        spi_deinit(spi());
    }
    if (configured_) {
        gpio_set_function(config_.pins.sck, GPIO_FUNC_NULL);
        gpio_set_function(config_.pins.mosi, GPIO_FUNC_NULL);
        gpio_set_function(config_.pins.miso, GPIO_FUNC_NULL);
        if (config_.pins.cs >= 0) {
            gpio_set_function(config_.pins.cs, GPIO_FUNC_NULL);
        }
    }
    active_ = false;
}

void SpiChannel::poll(EventBus &events) {
    (void)events;
}

bool SpiChannel::write(const uint8_t *data, size_t len, EventBus &events, char *err, size_t err_len) {
    return transfer(0, data, len, 0, events, err, err_len);
}

bool SpiChannel::transfer(uint8_t address, const uint8_t *tx, size_t tx_len, size_t rx_len, EventBus &events, char *err, size_t err_len) {
    (void)address;
    if (!active_) {
        snprintf(err, err_len, "SPI channel is not active");
        return false;
    }
    size_t count = std::max(tx_len, rx_len);
    if (count == 0 || count > kMaxPayloadBytes) {
        snprintf(err, err_len, "SPI transfer length must be 1..%u bytes", static_cast<unsigned>(kMaxPayloadBytes));
        return false;
    }
    uint8_t tx_buf[kMaxPayloadBytes] = {};
    uint8_t rx_buf[kMaxPayloadBytes] = {};
    for (size_t i = 0; i < tx_len && i < sizeof(tx_buf); ++i) {
        tx_buf[i] = tx[i];
    }
    if (config_.pins.cs >= 0) {
        gpio_put(config_.pins.cs, 0);
    }
    int rc = spi_write_read_blocking(spi(), tx_buf, rx_buf, count);
    if (config_.pins.cs >= 0) {
        gpio_put(config_.pins.cs, 1);
    }
    if (rc < 0) {
        snprintf(err, err_len, "SPI transfer failed: %d", rc);
        return false;
    }
    if (tx_len > 0) {
        events.publish_data(config_.id, ProtocolType::Spi, "tx", tx_buf, tx_len);
    }
    events.publish_data(config_.id, ProtocolType::Spi, "rx", rx_buf, count);
    return true;
}

void SpiChannel::describe_json(char *out, size_t out_len) const {
    snprintf(out, out_len,
             "{\"id\":%d,\"type\":\"spi\",\"instance\":%d,\"active\":%s,\"baud\":%lu,\"sck\":%d,\"mosi\":%d,\"miso\":%d,\"cs\":%d}",
             config_.id,
             config_.instance,
             active_ ? "true" : "false",
             static_cast<unsigned long>(config_.baud),
             config_.pins.sck,
             config_.pins.mosi,
             config_.pins.miso,
             config_.pins.cs);
}

} // namespace rpmon
