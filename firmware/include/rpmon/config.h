#pragma once

#include <cstddef>
#include <cstdint>

namespace rpmon {

constexpr const char *kFirmwareVersion = RPMON_FW_VERSION;
constexpr uint16_t kTcpControlPort = 4242;
constexpr uint16_t kHttpPort = 80;
constexpr const char *kApSsidPrefix = "RP2350-Monitor";
constexpr const char *kApPassword = "rpmon2350";
constexpr const char *kApIp = "192.168.4.1";
constexpr size_t kLineBufferSize = 512;
constexpr size_t kMaxChannels = 8;
constexpr size_t kMaxPayloadBytes = 128;
constexpr size_t kMaxWifiProfiles = 3;
constexpr size_t kMaxWifiScanResults = 8;
constexpr size_t kEventQueueCapacity = 128;
constexpr size_t kEventLineMax = 512;
constexpr size_t kEventReplayMax = 64;

enum class ProtocolType : uint8_t {
    Uart,
    Spi,
    I2c,
    Can,
    Gpio,
    Unknown
};

struct PinSet {
    int tx = -1;
    int rx = -1;
    int sck = -1;
    int mosi = -1;
    int miso = -1;
    int cs = -1;
    int sda = -1;
    int scl = -1;
    int gpio = -1;
};

struct ChannelConfig {
    int id = -1;
    ProtocolType type = ProtocolType::Unknown;
    int instance = 0;
    uint32_t baud = 115200;
    uint8_t address = 0;
    bool loopback = false;
    bool gpio_output = false;
    bool gpio_pull_up = false;
    bool gpio_pull_down = false;
    bool gpio_initial = false;
    PinSet pins;
};

const char *protocol_name(ProtocolType type);
ProtocolType parse_protocol(const char *name);

} // namespace rpmon
