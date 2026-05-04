#include "rpmon/core/channel_manager.h"

#include <cstdio>

namespace rpmon {

namespace {

bool has_singleton_instance(ProtocolType type) {
    return type == ProtocolType::Uart || type == ProtocolType::Spi || type == ProtocolType::I2c;
}

} // namespace

ChannelManager::ChannelManager(PinManager &pins, EventBus &events) : pins_(pins), events_(events) {}

bool ChannelManager::configure(const ChannelConfig &config, char *err, size_t err_len) {
    if (config.id <= 0) {
        snprintf(err, err_len, "channel id must be positive");
        return false;
    }
    if (config.type == ProtocolType::Unknown) {
        snprintf(err, err_len, "unknown protocol type");
        return false;
    }
    for (Slot &existing : slots_) {
        const Channel *drv = driver_for(existing);
        if (existing.used && drv && has_singleton_instance(config.type) && drv->id() != config.id && existing.type == config.type &&
            drv->instance() == config.instance) {
            snprintf(err, err_len, "%s%d is already assigned to another channel",
                     protocol_name(config.type), config.instance);
            return false;
        }
    }

    Slot *slot = find_slot(config.id);
    if (!slot) {
        for (Slot &candidate : slots_) {
            if (!candidate.used) {
                slot = &candidate;
                break;
            }
        }
    }
    if (!slot) {
        snprintf(err, err_len, "channel table full");
        return false;
    }

    if (slot->used) {
        Channel *old = driver_for(*slot);
        if (old) {
            old->stop();
        }
        pins_.release_channel(config.id);
    }

    slot->type = config.type;
    Channel *driver = driver_for(*slot);
    if (!driver || !driver->configure(config, pins_, err, err_len)) {
        pins_.release_channel(config.id);
        slot->used = false;
        slot->type = ProtocolType::Unknown;
        return false;
    }
    slot->used = true;
    return true;
}

bool ChannelManager::start(int id, char *err, size_t err_len) {
    Slot *slot = find_slot(id);
    if (!slot) {
        snprintf(err, err_len, "channel not found");
        return false;
    }
    return driver_for(*slot)->start(err, err_len);
}

bool ChannelManager::stop(int id, char *err, size_t err_len) {
    Slot *slot = find_slot(id);
    if (!slot) {
        snprintf(err, err_len, "channel not found");
        return false;
    }
    driver_for(*slot)->stop();
    return true;
}

bool ChannelManager::release(int id, char *err, size_t err_len) {
    Slot *slot = find_slot(id);
    if (!slot) {
        snprintf(err, err_len, "channel not found");
        return false;
    }
    Channel *driver = driver_for(*slot);
    if (driver) {
        driver->stop();
    }
    pins_.release_channel(id);
    events_.release_channel(id);
    *slot = Slot{};
    return true;
}

bool ChannelManager::write(int id, const uint8_t *data, size_t len, char *err, size_t err_len) {
    Slot *slot = find_slot(id);
    if (!slot) {
        snprintf(err, err_len, "channel not found");
        return false;
    }
    return driver_for(*slot)->write(data, len, events_, err, err_len);
}

bool ChannelManager::transfer(int id, uint8_t address, const uint8_t *tx, size_t tx_len, size_t rx_len, char *err, size_t err_len) {
    Slot *slot = find_slot(id);
    if (!slot) {
        snprintf(err, err_len, "channel not found");
        return false;
    }
    return driver_for(*slot)->transfer(address, tx, tx_len, rx_len, events_, err, err_len);
}

bool ChannelManager::gpio_write(int id, bool level, char *err, size_t err_len) {
    Slot *slot = find_slot(id);
    if (!slot) {
        snprintf(err, err_len, "channel not found");
        return false;
    }
    if (slot->type != ProtocolType::Gpio) {
        snprintf(err, err_len, "channel is not GPIO");
        return false;
    }
    return slot->gpio.set_level(level, events_, err, err_len);
}

bool ChannelManager::gpio_read(int id, bool &level, char *err, size_t err_len) {
    Slot *slot = find_slot(id);
    if (!slot) {
        snprintf(err, err_len, "channel not found");
        return false;
    }
    if (slot->type != ProtocolType::Gpio) {
        snprintf(err, err_len, "channel is not GPIO");
        return false;
    }
    return slot->gpio.read_level(level, events_, err, err_len);
}

void ChannelManager::poll() {
    for (Slot &slot : slots_) {
        if (slot.used) {
            Channel *driver = driver_for(slot);
            if (driver) {
                driver->poll(events_);
            }
        }
    }
}

void ChannelManager::list_json(char *out, size_t out_len) const {
    size_t pos = 0;
    int written = snprintf(out, out_len, "\"channels\":[");
    if (written < 0) {
        return;
    }
    pos = static_cast<size_t>(written);
    bool first = true;
    for (const Slot &slot : slots_) {
        if (!slot.used) {
            continue;
        }
        char desc[256];
        const Channel *driver = driver_for(slot);
        if (!driver) {
            continue;
        }
        driver->describe_json(desc, sizeof(desc));
        written = snprintf(out + pos, pos < out_len ? out_len - pos : 0, "%s%s", first ? "" : ",", desc);
        if (written < 0) {
            break;
        }
        pos += static_cast<size_t>(written);
        first = false;
        if (pos >= out_len) {
            break;
        }
    }
    snprintf(out + (pos < out_len ? pos : out_len - 1), pos < out_len ? out_len - pos : 1, "]");
}

ChannelManager::Slot *ChannelManager::find_slot(int id) {
    for (Slot &slot : slots_) {
        Channel *driver = driver_for(slot);
        if (slot.used && driver && driver->id() == id) {
            return &slot;
        }
    }
    return nullptr;
}

const ChannelManager::Slot *ChannelManager::find_slot(int id) const {
    for (const Slot &slot : slots_) {
        const Channel *driver = driver_for(slot);
        if (slot.used && driver && driver->id() == id) {
            return &slot;
        }
    }
    return nullptr;
}

Channel *ChannelManager::driver_for(Slot &slot) {
    switch (slot.type) {
    case ProtocolType::Uart:
        return &slot.uart;
    case ProtocolType::Spi:
        return &slot.spi;
    case ProtocolType::I2c:
        return &slot.i2c;
    case ProtocolType::Can:
        return &slot.can;
    case ProtocolType::Gpio:
        return &slot.gpio;
    default:
        return nullptr;
    }
}

const Channel *ChannelManager::driver_for(const Slot &slot) const {
    switch (slot.type) {
    case ProtocolType::Uart:
        return &slot.uart;
    case ProtocolType::Spi:
        return &slot.spi;
    case ProtocolType::I2c:
        return &slot.i2c;
    case ProtocolType::Can:
        return &slot.can;
    case ProtocolType::Gpio:
        return &slot.gpio;
    default:
        return nullptr;
    }
}

} // namespace rpmon
