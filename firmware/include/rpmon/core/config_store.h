#pragma once

#include <cstddef>
#include <cstdint>

#include "rpmon/config.h"

namespace rpmon {

struct WifiProfile {
    char ssid[33] = {};
    char password[65] = {};
    bool valid = false;
};

struct WifiSettings {
    WifiProfile profiles[kMaxWifiProfiles];
    uint8_t active_index = 0;
};

class ConfigStore {
public:
    bool load(WifiSettings &settings);
    bool save(const WifiSettings &settings);
    void clear();

private:
    uint32_t checksum(const uint8_t *data, size_t len) const;
};

} // namespace rpmon
