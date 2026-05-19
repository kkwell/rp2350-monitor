#include "rpmon/core/pin_manager.h"

#include <cstdio>

namespace rpmon {

namespace {

constexpr int kUart0Tx[] = {0, 2, 12, 14, 16, 18, 28};
constexpr int kUart0Rx[] = {1, 3, 13, 15, 17, 19};
constexpr int kUart1Tx[] = {4, 6, 8, 10, 20, 22, 26};
constexpr int kUart1Rx[] = {5, 7, 9, 11, 21, 27};

constexpr int kI2c0Sda[] = {0, 4, 8, 12, 16, 20, 28};
constexpr int kI2c0Scl[] = {1, 5, 9, 13, 17, 21};
constexpr int kI2c1Sda[] = {2, 6, 10, 14, 18, 22, 26};
constexpr int kI2c1Scl[] = {3, 7, 11, 15, 19, 27};

constexpr int kSpi0Miso[] = {0, 4, 16, 20};
constexpr int kSpi0Cs[] = {1, 5, 17, 21};
constexpr int kSpi0Sck[] = {2, 6, 18, 22};
constexpr int kSpi0Mosi[] = {3, 7, 19};
constexpr int kSpi1Miso[] = {8, 12, 28};
constexpr int kSpi1Cs[] = {9, 13};
constexpr int kSpi1Sck[] = {10, 14, 26};
constexpr int kSpi1Mosi[] = {11, 15, 27};

template <size_t N>
constexpr size_t array_count(const int (&)[N]) {
    return N;
}

struct PinList {
    const int *pins;
    size_t count;
};

PinList uart_tx_pins(int instance) {
    if (instance == 0) {
        return {kUart0Tx, array_count(kUart0Tx)};
    }
    if (instance == 1) {
        return {kUart1Tx, array_count(kUart1Tx)};
    }
    return {nullptr, 0};
}

PinList uart_rx_pins(int instance) {
    if (instance == 0) {
        return {kUart0Rx, array_count(kUart0Rx)};
    }
    if (instance == 1) {
        return {kUart1Rx, array_count(kUart1Rx)};
    }
    return {nullptr, 0};
}

PinList spi_sck_pins(int instance) {
    if (instance == 0) {
        return {kSpi0Sck, array_count(kSpi0Sck)};
    }
    if (instance == 1) {
        return {kSpi1Sck, array_count(kSpi1Sck)};
    }
    return {nullptr, 0};
}

PinList spi_mosi_pins(int instance) {
    if (instance == 0) {
        return {kSpi0Mosi, array_count(kSpi0Mosi)};
    }
    if (instance == 1) {
        return {kSpi1Mosi, array_count(kSpi1Mosi)};
    }
    return {nullptr, 0};
}

PinList spi_miso_pins(int instance) {
    if (instance == 0) {
        return {kSpi0Miso, array_count(kSpi0Miso)};
    }
    if (instance == 1) {
        return {kSpi1Miso, array_count(kSpi1Miso)};
    }
    return {nullptr, 0};
}

PinList i2c_sda_pins(int instance) {
    if (instance == 0) {
        return {kI2c0Sda, array_count(kI2c0Sda)};
    }
    if (instance == 1) {
        return {kI2c1Sda, array_count(kI2c1Sda)};
    }
    return {nullptr, 0};
}

PinList i2c_scl_pins(int instance) {
    if (instance == 0) {
        return {kI2c0Scl, array_count(kI2c0Scl)};
    }
    if (instance == 1) {
        return {kI2c1Scl, array_count(kI2c1Scl)};
    }
    return {nullptr, 0};
}

void write_pin_list(char *out, size_t out_len, PinList list) {
    if (!out || out_len == 0) {
        return;
    }
    size_t pos = 0;
    out[0] = '\0';
    for (size_t i = 0; i < list.count; ++i) {
        int written = snprintf(out + pos, pos < out_len ? out_len - pos : 0, "%s%d", i == 0 ? "" : ",", list.pins[i]);
        if (written < 0) {
            return;
        }
        pos += static_cast<size_t>(written);
        if (pos >= out_len) {
            out[out_len - 1] = '\0';
            return;
        }
    }
}

void invalid_instance(char *err, size_t err_len, const char *proto, int instance) {
    if (err && err_len > 0) {
        snprintf(err, err_len, "%s instance must be 0 or 1, got %d", proto, instance);
    }
}

void invalid_signal_pin(char *err, size_t err_len, const char *proto, int instance, const char *signal, int pin, PinList valid) {
    if (!err || err_len == 0) {
        return;
    }
    char pins[64];
    write_pin_list(pins, sizeof(pins), valid);
    snprintf(err, err_len, "invalid %s%d %s GPIO%d; valid %s GPIOs: %s", proto, instance, signal, pin, signal, pins);
}

} // namespace

bool PinManager::is_exposed_gpio(int gpio) const {
    return (gpio >= 0 && gpio <= 22) || (gpio >= 26 && gpio <= 28);
}

bool PinManager::claim(int gpio, int channel_id, PinRole role) {
    (void)role;
    if (!is_exposed_gpio(gpio)) {
        return false;
    }
    if (owners_[gpio] != 0 && owners_[gpio] != channel_id) {
        return false;
    }
    owners_[gpio] = channel_id;
    return true;
}

void PinManager::release_channel(int channel_id) {
    for (int &owner : owners_) {
        if (owner == channel_id) {
            owner = 0;
        }
    }
}

int PinManager::owner(int gpio) const {
    if (gpio < 0 || gpio >= static_cast<int>(sizeof(owners_) / sizeof(owners_[0]))) {
        return -1;
    }
    return owners_[gpio];
}

bool PinManager::validate_uart(int instance, int tx, int rx, char *err, size_t err_len) const {
    PinList tx_pins = uart_tx_pins(instance);
    PinList rx_pins = uart_rx_pins(instance);
    if (!tx_pins.pins || !rx_pins.pins) {
        invalid_instance(err, err_len, "UART", instance);
        return false;
    }
    if (!pin_in_list(tx, tx_pins.pins, tx_pins.count)) {
        invalid_signal_pin(err, err_len, "UART", instance, "tx", tx, tx_pins);
        return false;
    }
    if (!pin_in_list(rx, rx_pins.pins, rx_pins.count)) {
        invalid_signal_pin(err, err_len, "UART", instance, "rx", rx, rx_pins);
        return false;
    }
    return true;
}

bool PinManager::validate_spi(int instance, int sck, int mosi, int miso, int cs, char *err, size_t err_len) const {
    PinList sck_pins = spi_sck_pins(instance);
    PinList mosi_pins = spi_mosi_pins(instance);
    PinList miso_pins = spi_miso_pins(instance);
    if (!sck_pins.pins || !mosi_pins.pins || !miso_pins.pins) {
        invalid_instance(err, err_len, "SPI", instance);
        return false;
    }
    if (!pin_in_list(sck, sck_pins.pins, sck_pins.count)) {
        invalid_signal_pin(err, err_len, "SPI", instance, "sck", sck, sck_pins);
        return false;
    }
    if (!pin_in_list(mosi, mosi_pins.pins, mosi_pins.count)) {
        invalid_signal_pin(err, err_len, "SPI", instance, "mosi", mosi, mosi_pins);
        return false;
    }
    if (!pin_in_list(miso, miso_pins.pins, miso_pins.count)) {
        invalid_signal_pin(err, err_len, "SPI", instance, "miso", miso, miso_pins);
        return false;
    }
    if (cs >= 0 && !is_exposed_gpio(cs)) {
        if (err && err_len > 0) {
            snprintf(err, err_len, "invalid SPI%d cs GPIO%d; cs is manual GPIO and must be exposed or omitted", instance, cs);
        }
        return false;
    }
    if (cs >= 0 && (cs == sck || cs == mosi || cs == miso)) {
        if (err && err_len > 0) {
            snprintf(err, err_len, "invalid SPI%d cs GPIO%d; cs must not share sck/mosi/miso pins", instance, cs);
        }
        return false;
    }
    return true;
}

bool PinManager::validate_i2c(int instance, int sda, int scl, char *err, size_t err_len) const {
    PinList sda_pins = i2c_sda_pins(instance);
    PinList scl_pins = i2c_scl_pins(instance);
    if (!sda_pins.pins || !scl_pins.pins) {
        invalid_instance(err, err_len, "I2C", instance);
        return false;
    }
    if (!pin_in_list(sda, sda_pins.pins, sda_pins.count)) {
        invalid_signal_pin(err, err_len, "I2C", instance, "sda", sda, sda_pins);
        return false;
    }
    if (!pin_in_list(scl, scl_pins.pins, scl_pins.count)) {
        invalid_signal_pin(err, err_len, "I2C", instance, "scl", scl, scl_pins);
        return false;
    }
    return true;
}

void PinManager::pins_json(char *out, size_t out_len) const {
    size_t pos = 0;
    int written = snprintf(out, out_len, "\"pins\":[");
    if (written < 0) {
        return;
    }
    pos = static_cast<size_t>(written);
    bool first = true;
    for (int gpio = 0; gpio <= 28; ++gpio) {
        if (!is_exposed_gpio(gpio)) {
            continue;
        }
        written = snprintf(out + pos, pos < out_len ? out_len - pos : 0,
                           "%s{\"gpio\":%d,\"owner\":%d}", first ? "" : ",", gpio, owners_[gpio]);
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

bool PinManager::pin_in_list(int pin, const int *pins, size_t count) const {
    if (!is_exposed_gpio(pin)) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (pins[i] == pin) {
            return true;
        }
    }
    return false;
}

} // namespace rpmon
