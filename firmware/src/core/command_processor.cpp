#include "rpmon/core/command_processor.h"

#include <cstdio>
#include <cstring>

#include "rpmon/util/hex.h"
#include "rpmon/util/json.h"

namespace rpmon {

namespace {

bool read_boolish(const char *line, const char *key, bool &out) {
    if (json_get_bool(line, key, out)) {
        return true;
    }
    int value = 0;
    if (json_get_int(line, key, value)) {
        out = value != 0;
        return true;
    }
    char text[12];
    if (!json_get_string(line, key, text, sizeof(text))) {
        return false;
    }
    if (std::strcmp(text, "1") == 0 || std::strcmp(text, "true") == 0 ||
        std::strcmp(text, "high") == 0 || std::strcmp(text, "on") == 0) {
        out = true;
        return true;
    }
    if (std::strcmp(text, "0") == 0 || std::strcmp(text, "false") == 0 ||
        std::strcmp(text, "low") == 0 || std::strcmp(text, "off") == 0) {
        out = false;
        return true;
    }
    return false;
}

} // namespace

CommandProcessor::CommandProcessor(WifiManager &wifi, ChannelManager &channels, PinManager &pins, EventBus &events)
    : wifi_(wifi), channels_(channels), pins_(pins), events_(events) {}

void CommandProcessor::handle_line(const char *line, LineSink &reply) {
    char cmd[40];
    if (!line || !json_get_string(line, "cmd", cmd, sizeof(cmd))) {
        events_.publish_response(reply, false, "unknown", "expected newline-delimited JSON with a cmd field");
        return;
    }

    if (std::strcmp(cmd, "hello") == 0) {
        char extra[128];
        snprintf(extra, sizeof(extra), "\"version\":\"%s\",\"board\":\"pico2_w\",\"links\":[\"wifi\",\"usb\"]", kFirmwareVersion);
        events_.publish_response(reply, true, cmd, "ready", extra);
        return;
    }
    if (std::strcmp(cmd, "status") == 0) {
        static char wifi[1800];
        static char channels[1600];
        char buffers[1800];
        static char extra[5600];
        wifi_.status_json(wifi, sizeof(wifi));
        channels_.list_json(channels, sizeof(channels));
        events_.stats_json(buffers, sizeof(buffers));
        snprintf(extra, sizeof(extra), "%s,%s,%s", wifi, channels, buffers);
        events_.publish_response(reply, true, cmd, "ok", extra);
        return;
    }
    if (std::strcmp(cmd, "buffer_status") == 0) {
        char buffers[1800];
        events_.stats_json(buffers, sizeof(buffers));
        events_.publish_response(reply, true, cmd, "ok", buffers);
        return;
    }
    if (std::strcmp(cmd, "events_read") == 0) {
        int count = 16;
        int channel = -1;
        uint32_t since_seq = 0;
        json_get_int(line, "count", count);
        json_get_int(line, "channel", channel);
        json_get_uint32(line, "since_seq", since_seq);
        if (count <= 0) {
            count = 16;
        }
        if (count > static_cast<int>(kEventReplayMax)) {
            count = static_cast<int>(kEventReplayMax);
        }
        size_t sent = events_.replay_buffered(reply, static_cast<size_t>(count), since_seq, channel);
        char extra[128];
        snprintf(extra, sizeof(extra), "\"sent\":%u,\"max\":%u,\"since_seq\":%lu,\"channel\":%d",
                 static_cast<unsigned>(sent),
                 static_cast<unsigned>(count),
                 static_cast<unsigned long>(since_seq),
                 channel);
        events_.publish_response(reply, true, cmd, "events replayed", extra);
        return;
    }
    if (std::strcmp(cmd, "pins") == 0) {
        char pins[780];
        pins_.pins_json(pins, sizeof(pins));
        events_.publish_response(reply, true, cmd, "ok", pins);
        return;
    }
    if (std::strcmp(cmd, "wifi_set") == 0) {
        char ssid[33];
        char password[65];
        bool save = true;
        int slot = 0;
        json_get_bool(line, "save", save);
        json_get_int(line, "slot", slot);
        if (slot < 0 || slot >= static_cast<int>(kMaxWifiProfiles)) {
            events_.publish_response(reply, false, cmd, "wifi slot out of range");
            return;
        }
        if (!json_get_string(line, "ssid", ssid, sizeof(ssid))) {
            events_.publish_response(reply, false, cmd, "missing ssid");
            return;
        }
        if (!json_get_string(line, "password", password, sizeof(password))) {
            password[0] = '\0';
        }
        bool ok = wifi_.set_credentials(ssid, password, save, static_cast<uint8_t>(slot));
        events_.publish_response(reply, ok, cmd, ok ? "wifi credentials updated" : "failed to update wifi credentials");
        return;
    }
    if (std::strcmp(cmd, "wifi_clear") == 0) {
        int slot = -1;
        if (!json_get_int(line, "slot", slot) || slot < 0 || slot >= static_cast<int>(kMaxWifiProfiles)) {
            events_.publish_response(reply, false, cmd, "wifi slot out of range");
            return;
        }
        bool ok = wifi_.clear_profile(static_cast<uint8_t>(slot), true);
        events_.publish_response(reply, ok, cmd, ok ? "wifi profile cleared" : "failed to clear wifi profile");
        return;
    }
    if (std::strcmp(cmd, "wifi_connect") == 0) {
        char err[160] = {};
        int slot = -1;
        bool ok = false;
        if (json_get_int(line, "slot", slot)) {
            if (slot < 0 || slot >= static_cast<int>(kMaxWifiProfiles)) {
                events_.publish_response(reply, false, cmd, "wifi slot out of range");
                return;
            }
            ok = wifi_.connect_station(static_cast<uint8_t>(slot), err, sizeof(err));
        } else {
            ok = wifi_.connect_station(err, sizeof(err));
        }
        events_.publish_response(reply, ok, cmd, ok ? "station connected" : err);
        return;
    }
    if (std::strcmp(cmd, "wifi_scan") == 0) {
        char err[160] = {};
        bool ok = wifi_.scan_wifi(err, sizeof(err));
        events_.publish_response(reply, ok, cmd, ok ? "wifi scan started" : err);
        return;
    }
    if (std::strcmp(cmd, "wifi_ap") == 0) {
        bool ok = wifi_.start_ap();
        events_.publish_response(reply, ok, cmd, ok ? "ap active" : "failed to start ap");
        return;
    }
    if (std::strcmp(cmd, "channel_config") == 0) {
        handle_channel_config(line, reply);
        return;
    }
    if (std::strcmp(cmd, "channel_start") == 0 || std::strcmp(cmd, "channel_stop") == 0 ||
        std::strcmp(cmd, "channel_release") == 0 ||
        std::strcmp(cmd, "channel_write") == 0 || std::strcmp(cmd, "channel_transfer") == 0 ||
        std::strcmp(cmd, "spi_xfer") == 0 || std::strcmp(cmd, "i2c_xfer") == 0) {
        handle_channel_io(line, reply, cmd);
        return;
    }
    if (std::strcmp(cmd, "gpio_read") == 0 || std::strcmp(cmd, "gpio_write") == 0) {
        handle_gpio_io(line, reply, cmd);
        return;
    }
    if (std::strcmp(cmd, "channels") == 0) {
        char channels[1600];
        channels_.list_json(channels, sizeof(channels));
        events_.publish_response(reply, true, cmd, "ok", channels);
        return;
    }

    events_.publish_response(reply, false, cmd, "unknown command");
}

void CommandProcessor::handle_channel_config(const char *line, LineSink &reply) {
    char type_name[16];
    ChannelConfig config{};
    uint32_t baud = 0;
    if (!json_get_int(line, "id", config.id) || !json_get_string(line, "type", type_name, sizeof(type_name))) {
        events_.publish_response(reply, false, "channel_config", "missing id or type");
        return;
    }
    config.type = parse_protocol(type_name);
    json_get_int(line, "instance", config.instance);
    if (json_get_uint32(line, "baud", baud)) {
        config.baud = baud;
    } else if (config.type == ProtocolType::Spi) {
        config.baud = 1000000;
    } else if (config.type == ProtocolType::I2c) {
        config.baud = 100000;
    }
    int address = 0;
    if (json_get_int(line, "address", address)) {
        config.address = static_cast<uint8_t>(address);
    }
    json_get_bool(line, "loopback", config.loopback);

    json_get_int(line, "tx", config.pins.tx);
    json_get_int(line, "rx", config.pins.rx);
    json_get_int(line, "sck", config.pins.sck);
    json_get_int(line, "mosi", config.pins.mosi);
    json_get_int(line, "miso", config.pins.miso);
    json_get_int(line, "cs", config.pins.cs);
    json_get_int(line, "sda", config.pins.sda);
    json_get_int(line, "scl", config.pins.scl);
    json_get_int(line, "gpio", config.pins.gpio);

    char direction[12];
    if (json_get_string(line, "direction", direction, sizeof(direction))) {
        if (std::strcmp(direction, "output") == 0 || std::strcmp(direction, "out") == 0) {
            config.gpio_output = true;
        } else if (std::strcmp(direction, "input") == 0 || std::strcmp(direction, "in") == 0) {
            config.gpio_output = false;
        } else {
            events_.publish_response(reply, false, "channel_config", "invalid GPIO direction");
            return;
        }
    }
    char pull[12];
    if (json_get_string(line, "pull", pull, sizeof(pull))) {
        if (std::strcmp(pull, "up") == 0) {
            config.gpio_pull_up = true;
            config.gpio_pull_down = false;
        } else if (std::strcmp(pull, "down") == 0) {
            config.gpio_pull_up = false;
            config.gpio_pull_down = true;
        } else if (std::strcmp(pull, "none") == 0 || std::strcmp(pull, "off") == 0) {
            config.gpio_pull_up = false;
            config.gpio_pull_down = false;
        } else {
            events_.publish_response(reply, false, "channel_config", "invalid GPIO pull mode");
            return;
        }
    }
    read_boolish(line, "pull_up", config.gpio_pull_up);
    read_boolish(line, "pull_down", config.gpio_pull_down);
    read_boolish(line, "initial", config.gpio_initial);
    read_boolish(line, "level", config.gpio_initial);

    char err[160] = {};
    bool ok = channels_.configure(config, err, sizeof(err));
    events_.publish_response(reply, ok, "channel_config", ok ? "channel configured" : err);
}

void CommandProcessor::handle_channel_io(const char *line, LineSink &reply, const char *cmd) {
    int id = -1;
    if (!json_get_int(line, "id", id)) {
        events_.publish_response(reply, false, cmd, "missing channel id");
        return;
    }

    char err[160] = {};
    bool ok = false;
    if (std::strcmp(cmd, "channel_start") == 0) {
        ok = channels_.start(id, err, sizeof(err));
        events_.publish_response(reply, ok, cmd, ok ? "channel started" : err);
        return;
    }
    if (std::strcmp(cmd, "channel_stop") == 0) {
        ok = channels_.stop(id, err, sizeof(err));
        events_.publish_response(reply, ok, cmd, ok ? "channel stopped" : err);
        return;
    }
    if (std::strcmp(cmd, "channel_release") == 0) {
        ok = channels_.release(id, err, sizeof(err));
        events_.publish_response(reply, ok, cmd, ok ? "channel released" : err);
        return;
    }

    char hex[2 * kMaxPayloadBytes + 1] = {};
    if (!json_get_string(line, "hex", hex, sizeof(hex))) {
        json_get_string(line, "write", hex, sizeof(hex));
    }
    uint8_t data[kMaxPayloadBytes] = {};
    size_t data_len = 0;
    if (!hex_to_bytes(hex, data, sizeof(data), data_len)) {
        events_.publish_response(reply, false, cmd, "invalid hex payload");
        return;
    }

    if (std::strcmp(cmd, "channel_write") == 0) {
        ok = channels_.write(id, data, data_len, err, sizeof(err));
        events_.publish_response(reply, ok, cmd, ok ? "write queued" : err);
        return;
    }

    int addr = 0;
    int read_len = 0;
    json_get_int(line, "addr", addr);
    json_get_int(line, "read_len", read_len);
    if (read_len < 0 || read_len > static_cast<int>(kMaxPayloadBytes)) {
        events_.publish_response(reply, false, cmd, "read_len out of range");
        return;
    }
    if (std::strcmp(cmd, "spi_xfer") == 0 && read_len == 0) {
        read_len = static_cast<int>(data_len);
    }
    ok = channels_.transfer(id, static_cast<uint8_t>(addr), data, data_len, static_cast<size_t>(read_len), err, sizeof(err));
    events_.publish_response(reply, ok, cmd, ok ? "transfer complete" : err);
}

void CommandProcessor::handle_gpio_io(const char *line, LineSink &reply, const char *cmd) {
    int id = -1;
    if (!json_get_int(line, "id", id)) {
        events_.publish_response(reply, false, cmd, "missing channel id");
        return;
    }

    char err[160] = {};
    if (std::strcmp(cmd, "gpio_write") == 0) {
        bool level = false;
        if (!read_boolish(line, "level", level)) {
            events_.publish_response(reply, false, cmd, "missing or invalid GPIO level");
            return;
        }
        bool ok = channels_.gpio_write(id, level, err, sizeof(err));
        char extra[24];
        snprintf(extra, sizeof(extra), "\"level\":%s", level ? "true" : "false");
        events_.publish_response(reply, ok, cmd, ok ? "gpio level set" : err, ok ? extra : nullptr);
        return;
    }

    bool level = false;
    bool ok = channels_.gpio_read(id, level, err, sizeof(err));
    char extra[24];
    snprintf(extra, sizeof(extra), "\"level\":%s", level ? "true" : "false");
    events_.publish_response(reply, ok, cmd, ok ? "gpio level read" : err, ok ? extra : nullptr);
}

} // namespace rpmon
