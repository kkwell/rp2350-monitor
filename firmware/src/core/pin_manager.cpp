#include "rpmon/core/pin_manager.h"

#include <cstdio>

namespace rpmon {

namespace {

constexpr int kUart0Tx[] = {0, 2, 12, 14, 16, 18, 28};
constexpr int kUart0Rx[] = {1, 3, 13, 15, 17, 19};
constexpr int kUart1Tx[] = {4, 6, 8, 20, 22};
constexpr int kUart1Rx[] = {5, 7, 9, 21};

constexpr int kI2c0Sda[] = {0, 4, 8, 12, 16, 20, 28};
constexpr int kI2c0Scl[] = {1, 5, 9, 13, 17, 21};
constexpr int kI2c1Sda[] = {2, 6, 10, 14, 18, 26};
constexpr int kI2c1Scl[] = {3, 7, 11, 15, 19, 27};

constexpr int kSpi0Miso[] = {0, 4, 16, 20};
constexpr int kSpi0Cs[] = {1, 5, 17, 21};
constexpr int kSpi0Sck[] = {2, 6, 18, 22};
constexpr int kSpi0Mosi[] = {3, 7, 19};
constexpr int kSpi1Miso[] = {8, 12, 28};
constexpr int kSpi1Cs[] = {9, 13};
constexpr int kSpi1Sck[] = {10, 14, 26};
constexpr int kSpi1Mosi[] = {11, 15, 27};

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

bool PinManager::validate_uart(int instance, int tx, int rx) const {
    if (instance == 0) {
        return pin_in_list(tx, kUart0Tx, sizeof(kUart0Tx) / sizeof(kUart0Tx[0])) &&
               pin_in_list(rx, kUart0Rx, sizeof(kUart0Rx) / sizeof(kUart0Rx[0]));
    }
    if (instance == 1) {
        return pin_in_list(tx, kUart1Tx, sizeof(kUart1Tx) / sizeof(kUart1Tx[0])) &&
               pin_in_list(rx, kUart1Rx, sizeof(kUart1Rx) / sizeof(kUart1Rx[0]));
    }
    return false;
}

bool PinManager::validate_spi(int instance, int sck, int mosi, int miso, int cs) const {
    if (instance == 0) {
        return pin_in_list(sck, kSpi0Sck, sizeof(kSpi0Sck) / sizeof(kSpi0Sck[0])) &&
               pin_in_list(mosi, kSpi0Mosi, sizeof(kSpi0Mosi) / sizeof(kSpi0Mosi[0])) &&
               pin_in_list(miso, kSpi0Miso, sizeof(kSpi0Miso) / sizeof(kSpi0Miso[0])) &&
               (cs < 0 || pin_in_list(cs, kSpi0Cs, sizeof(kSpi0Cs) / sizeof(kSpi0Cs[0])));
    }
    if (instance == 1) {
        return pin_in_list(sck, kSpi1Sck, sizeof(kSpi1Sck) / sizeof(kSpi1Sck[0])) &&
               pin_in_list(mosi, kSpi1Mosi, sizeof(kSpi1Mosi) / sizeof(kSpi1Mosi[0])) &&
               pin_in_list(miso, kSpi1Miso, sizeof(kSpi1Miso) / sizeof(kSpi1Miso[0])) &&
               (cs < 0 || pin_in_list(cs, kSpi1Cs, sizeof(kSpi1Cs) / sizeof(kSpi1Cs[0])));
    }
    return false;
}

bool PinManager::validate_i2c(int instance, int sda, int scl) const {
    if (instance == 0) {
        return pin_in_list(sda, kI2c0Sda, sizeof(kI2c0Sda) / sizeof(kI2c0Sda[0])) &&
               pin_in_list(scl, kI2c0Scl, sizeof(kI2c0Scl) / sizeof(kI2c0Scl[0]));
    }
    if (instance == 1) {
        return pin_in_list(sda, kI2c1Sda, sizeof(kI2c1Sda) / sizeof(kI2c1Sda[0])) &&
               pin_in_list(scl, kI2c1Scl, sizeof(kI2c1Scl) / sizeof(kI2c1Scl[0]));
    }
    return false;
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

