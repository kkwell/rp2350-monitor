#pragma once

#include "rpmon/drivers/channel.h"

#include "hardware/spi.h"

namespace rpmon {

class SpiChannel final : public Channel {
public:
    bool configure(const ChannelConfig &config, PinManager &pins, char *err, size_t err_len) override;
    bool start(char *err, size_t err_len) override;
    void stop() override;
    void poll(EventBus &events) override;
    bool write(const uint8_t *data, size_t len, EventBus &events, char *err, size_t err_len) override;
    bool transfer(uint8_t address, const uint8_t *tx, size_t tx_len, size_t rx_len, EventBus &events, char *err, size_t err_len) override;
    void describe_json(char *out, size_t out_len) const override;
    int id() const override { return config_.id; }
    int instance() const override { return config_.instance; }
    ProtocolType type() const override { return ProtocolType::Spi; }
    bool active() const override { return active_; }

private:
    spi_inst_t *spi() const;
    ChannelConfig config_{};
    bool configured_ = false;
    bool active_ = false;
};

} // namespace rpmon
