#include "rpmon/core/config_store.h"

#include <cstring>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

namespace rpmon {

namespace {

constexpr uint32_t kMagic = 0x524d4f4e;
constexpr uint32_t kVersion = 2;
constexpr uint32_t kFlashOffset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

struct LegacyWifiConfig {
    char ssid[33] = {};
    char password[65] = {};
    bool valid = false;
};

struct PersistedConfigV1 {
    uint32_t magic;
    uint32_t version;
    LegacyWifiConfig wifi;
    uint32_t checksum;
};

struct PersistedConfig {
    uint32_t magic;
    uint32_t version;
    WifiSettings wifi;
    uint32_t checksum;
};

constexpr size_t kPersistedProgramSize =
    ((sizeof(PersistedConfig) + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;
static_assert(kPersistedProgramSize <= FLASH_SECTOR_SIZE, "persisted config must fit one flash sector");

} // namespace

bool ConfigStore::load(WifiSettings &settings) {
    const auto *stored = reinterpret_cast<const PersistedConfig *>(XIP_BASE + kFlashOffset);
    if (stored->magic != kMagic) {
        return false;
    }
    if (stored->version == 1) {
        const auto *legacy = reinterpret_cast<const PersistedConfigV1 *>(XIP_BASE + kFlashOffset);
        uint32_t expected = checksum(reinterpret_cast<const uint8_t *>(legacy), sizeof(PersistedConfigV1) - sizeof(uint32_t));
        if (legacy->checksum != expected || !legacy->wifi.valid || legacy->wifi.ssid[0] == '\0') {
            return false;
        }
        settings = {};
        std::strncpy(settings.profiles[0].ssid, legacy->wifi.ssid, sizeof(settings.profiles[0].ssid) - 1);
        std::strncpy(settings.profiles[0].password, legacy->wifi.password, sizeof(settings.profiles[0].password) - 1);
        settings.profiles[0].valid = true;
        settings.active_index = 0;
        return true;
    }
    if (stored->version != kVersion) {
        return false;
    }
    uint32_t expected = checksum(reinterpret_cast<const uint8_t *>(stored), sizeof(PersistedConfig) - sizeof(uint32_t));
    if (stored->checksum != expected) {
        return false;
    }
    settings = stored->wifi;
    if (settings.active_index >= kMaxWifiProfiles) {
        settings.active_index = 0;
    }
    for (WifiProfile &profile : settings.profiles) {
        profile.ssid[sizeof(profile.ssid) - 1] = '\0';
        profile.password[sizeof(profile.password) - 1] = '\0';
        profile.valid = profile.valid && profile.ssid[0] != '\0';
    }
    return true;
}

bool ConfigStore::save(const WifiSettings &settings) {
    PersistedConfig persisted{};
    persisted.magic = kMagic;
    persisted.version = kVersion;
    persisted.wifi = settings;
    if (persisted.wifi.active_index >= kMaxWifiProfiles) {
        persisted.wifi.active_index = 0;
    }
    for (WifiProfile &profile : persisted.wifi.profiles) {
        profile.valid = profile.valid && profile.ssid[0] != '\0';
        profile.ssid[sizeof(profile.ssid) - 1] = '\0';
        profile.password[sizeof(profile.password) - 1] = '\0';
    }
    persisted.checksum = checksum(reinterpret_cast<const uint8_t *>(&persisted), sizeof(PersistedConfig) - sizeof(uint32_t));

    uint8_t page[kPersistedProgramSize] = {};
    std::memcpy(page, &persisted, sizeof(persisted));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(kFlashOffset, FLASH_SECTOR_SIZE);
    flash_range_program(kFlashOffset, page, kPersistedProgramSize);
    restore_interrupts(ints);
    return true;
}

void ConfigStore::clear() {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(kFlashOffset, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
}

uint32_t ConfigStore::checksum(const uint8_t *data, size_t len) const {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

} // namespace rpmon
