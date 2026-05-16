#include "rpmon/core/command_processor.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
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

bool parse_logic_pull_text(const char *text, LogicPullMode &out) {
    if (std::strcmp(text, "none") == 0 || std::strcmp(text, "off") == 0) {
        out = LogicPullMode::None;
        return true;
    }
    if (std::strcmp(text, "up") == 0 || std::strcmp(text, "pullup") == 0) {
        out = LogicPullMode::Up;
        return true;
    }
    if (std::strcmp(text, "down") == 0 || std::strcmp(text, "pulldown") == 0) {
        out = LogicPullMode::Down;
        return true;
    }
    return false;
}

bool read_logic_pull_mode(const char *line, LogicPullMode &out) {
    char text[12];
    if (json_get_string(line, "pull", text, sizeof(text))) {
        return parse_logic_pull_text(text, out);
    }

    bool enabled = false;
    if (read_boolish(line, "pull_up", enabled) && enabled) {
        out = LogicPullMode::Up;
        return true;
    }
    if (read_boolish(line, "pull_down", enabled) && enabled) {
        out = LogicPullMode::Down;
        return true;
    }
    return true;
}

const char *json_value_for_key(const char *json, const char *key) {
    if (!json || !key) {
        return nullptr;
    }
    char pattern[40];
    int n = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(pattern)) {
        return nullptr;
    }
    const char *p = json;
    while ((p = std::strstr(p, pattern)) != nullptr) {
        const char *after = p + n;
        while (*after && std::isspace(static_cast<unsigned char>(*after))) {
            ++after;
        }
        if (*after == ':') {
            ++after;
            while (*after && std::isspace(static_cast<unsigned char>(*after))) {
                ++after;
            }
            return after;
        }
        p += n;
    }
    return nullptr;
}

const char *skip_json_ws(const char *p) {
    while (*p && std::isspace(static_cast<unsigned char>(*p))) {
        ++p;
    }
    return p;
}

bool read_json_token_string(const char *&p, char *out, size_t out_len) {
    p = skip_json_ws(p);
    if (*p != '"' || !out || out_len == 0) {
        return false;
    }
    ++p;
    size_t pos = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p) {
            c = *p++;
        }
        if (pos + 1 >= out_len) {
            return false;
        }
        out[pos++] = c;
    }
    if (*p != '"') {
        return false;
    }
    ++p;
    out[pos] = '\0';
    return true;
}

bool read_logic_pin_pulls(const char *line,
                          int pin_base,
                          int pin_count,
                          LogicPullMode default_pull,
                          LogicPullMode *pin_pull_modes,
                          char *err,
                          size_t err_len) {
    for (int i = 0; i < 32; ++i) {
        pin_pull_modes[i] = i < pin_count ? default_pull : LogicPullMode::None;
    }
    const char *p = json_value_for_key(line, "pin_pulls");
    if (!p) {
        return true;
    }
    p = skip_json_ws(p);
    if (*p != '{') {
        snprintf(err, err_len, "logic pin_pulls must be an object");
        return false;
    }
    ++p;
    while (true) {
        p = skip_json_ws(p);
        if (*p == '}') {
            return true;
        }
        char key[12];
        if (!read_json_token_string(p, key, sizeof(key))) {
            snprintf(err, err_len, "invalid logic pin_pulls key");
            return false;
        }
        p = skip_json_ws(p);
        if (*p != ':') {
            snprintf(err, err_len, "invalid logic pin_pulls entry");
            return false;
        }
        ++p;
        char mode_text[12];
        if (!read_json_token_string(p, mode_text, sizeof(mode_text))) {
            snprintf(err, err_len, "invalid logic pin_pulls value");
            return false;
        }
        char *end = nullptr;
        long key_value = std::strtol(key, &end, 0);
        if (end == key || *end != '\0') {
            snprintf(err, err_len, "logic pin_pulls key must be a GPIO number");
            return false;
        }
        int index = -1;
        if (key_value >= pin_base && key_value < pin_base + pin_count) {
            index = static_cast<int>(key_value) - pin_base;
        } else if (key_value >= 0 && key_value < pin_count) {
            index = static_cast<int>(key_value);
        }
        if (index < 0 || index >= pin_count) {
            snprintf(err, err_len, "logic pin_pulls key outside captured pin range");
            return false;
        }
        LogicPullMode mode = LogicPullMode::None;
        if (!parse_logic_pull_text(mode_text, mode)) {
            snprintf(err, err_len, "invalid logic pin_pulls value");
            return false;
        }
        pin_pull_modes[index] = mode;
        p = skip_json_ws(p);
        if (*p == ',') {
            ++p;
            continue;
        }
        if (*p == '}') {
            return true;
        }
        snprintf(err, err_len, "invalid logic pin_pulls object");
        return false;
    }
}

} // namespace

CommandProcessor::CommandProcessor(WifiManager &wifi, ChannelManager &channels, LogicAnalyzer &logic, PinManager &pins, EventBus &events)
    : wifi_(wifi), channels_(channels), logic_(logic), pins_(pins), events_(events) {}

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
        char logic[1600];
        static char extra[7400];
        wifi_.status_json(wifi, sizeof(wifi));
        channels_.list_json(channels, sizeof(channels));
        logic_.status_json(logic, sizeof(logic));
        events_.stats_json(buffers, sizeof(buffers));
        snprintf(extra, sizeof(extra), "%s,%s,%s,%s", wifi, channels, logic, buffers);
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
    if (std::strcmp(cmd, "logic_config") == 0 || std::strcmp(cmd, "logic_start") == 0 ||
        std::strcmp(cmd, "logic_stop") == 0 || std::strcmp(cmd, "logic_release") == 0 ||
        std::strcmp(cmd, "logic_status") == 0 || std::strcmp(cmd, "logic_caps") == 0 ||
        std::strcmp(cmd, "logic_read") == 0) {
        handle_logic_io(line, reply, cmd);
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

void CommandProcessor::handle_logic_io(const char *line, LineSink &reply, const char *cmd) {
    char err[160] = {};
    bool ok = false;
    if (std::strcmp(cmd, "logic_config") == 0) {
        int pin_base = -1;
        int pin_count = 0;
        int trigger_pin = -1;
        uint32_t sample_rate = 1000000;
        uint32_t samples = 1024;
        uint32_t pre_samples = 0;
        uint32_t post_samples = 0;
        uint32_t search_samples = 0;
        uint32_t trigger_mask = 0;
        uint32_t trigger_value = 0;
        uint32_t burst_count = 1;
        LogicTriggerMode trigger_mode = LogicTriggerMode::Level;
        LogicPullMode pull_mode = LogicPullMode::None;
        LogicPullMode pin_pull_modes[32] = {};
        bool trigger_level = true;
        if (!json_get_int(line, "pin_base", pin_base)) {
            json_get_int(line, "base", pin_base);
        }
        if (!json_get_int(line, "pin_count", pin_count)) {
            json_get_int(line, "count", pin_count);
        }
        json_get_uint32(line, "sample_rate", sample_rate);
        json_get_uint32(line, "rate", sample_rate);
        json_get_uint32(line, "samples", samples);
        json_get_int(line, "trigger_pin", trigger_pin);
        char trigger_mode_text[12];
        if (json_get_string(line, "trigger_mode", trigger_mode_text, sizeof(trigger_mode_text))) {
            if (std::strcmp(trigger_mode_text, "level") == 0) {
                trigger_mode = LogicTriggerMode::Level;
            } else if (std::strcmp(trigger_mode_text, "rising") == 0 || std::strcmp(trigger_mode_text, "rise") == 0) {
                trigger_mode = LogicTriggerMode::Rising;
            } else if (std::strcmp(trigger_mode_text, "falling") == 0 || std::strcmp(trigger_mode_text, "fall") == 0) {
                trigger_mode = LogicTriggerMode::Falling;
            } else if (std::strcmp(trigger_mode_text, "pattern") == 0 ||
                       std::strcmp(trigger_mode_text, "complex") == 0 ||
                       std::strcmp(trigger_mode_text, "fast") == 0) {
                trigger_mode = LogicTriggerMode::Pattern;
            } else {
                events_.publish_response(reply, false, cmd, "invalid logic trigger_mode");
                return;
            }
        }
        read_boolish(line, "trigger_level", trigger_level);
        json_get_uint32(line, "pre_samples", pre_samples);
        json_get_uint32(line, "post_samples", post_samples);
        json_get_uint32(line, "search_samples", search_samples);
        json_get_uint32(line, "trigger_mask", trigger_mask);
        json_get_uint32(line, "trigger_value", trigger_value);
        json_get_uint32(line, "burst_count", burst_count);
        if (!read_logic_pull_mode(line, pull_mode)) {
            events_.publish_response(reply, false, cmd, "invalid logic pull mode");
            return;
        }
        if (pin_base < 0 || pin_count <= 0) {
            events_.publish_response(reply, false, cmd, "missing pin_base or pin_count");
            return;
        }
        if (pin_count > 32) {
            events_.publish_response(reply, false, cmd, "pin_count out of range");
            return;
        }
        if (!read_logic_pin_pulls(line, pin_base, pin_count, pull_mode, pin_pull_modes, err, sizeof(err))) {
            events_.publish_response(reply, false, cmd, err);
            return;
        }
        ok = logic_.configure(static_cast<uint8_t>(pin_base),
                              static_cast<uint8_t>(pin_count),
                              sample_rate,
                              samples,
                              trigger_pin,
                              trigger_mode,
                              trigger_level,
                              pull_mode,
                              pin_pull_modes,
                              pre_samples,
                              post_samples,
                              search_samples,
                              trigger_mask,
                              trigger_value,
                              static_cast<uint8_t>(burst_count),
                              err,
                              sizeof(err));
        char extra[1600];
        logic_.status_json(extra, sizeof(extra));
        events_.publish_response(reply, ok, cmd, ok ? "logic configured" : err, ok ? extra : nullptr);
        return;
    }
    if (std::strcmp(cmd, "logic_start") == 0) {
        ok = logic_.start(err, sizeof(err));
        events_.publish_response(reply, ok, cmd, ok ? "logic capture armed" : err);
        return;
    }
    if (std::strcmp(cmd, "logic_stop") == 0) {
        ok = logic_.stop(err, sizeof(err));
        events_.publish_response(reply, ok, cmd, ok ? "logic capture stopped" : err);
        return;
    }
    if (std::strcmp(cmd, "logic_release") == 0) {
        ok = logic_.release(err, sizeof(err));
        events_.publish_response(reply, ok, cmd, ok ? "logic analyzer released" : err);
        return;
    }
    if (std::strcmp(cmd, "logic_status") == 0) {
        char extra[1600];
        logic_.status_json(extra, sizeof(extra));
        events_.publish_response(reply, true, cmd, "ok", extra);
        return;
    }
    if (std::strcmp(cmd, "logic_caps") == 0) {
        char extra[1400];
        logic_.caps_json(extra, sizeof(extra));
        events_.publish_response(reply, true, cmd, "ok", extra);
        return;
    }

    int offset_words = 0;
    int count_words = 0;
    json_get_int(line, "offset_words", offset_words);
    json_get_int(line, "count_words", count_words);
    if (offset_words < 0 || count_words < 0) {
        events_.publish_response(reply, false, cmd, "logic read offsets must be non-negative");
        return;
    }
    ok = logic_.stream_capture(reply,
                               static_cast<size_t>(offset_words),
                               static_cast<size_t>(count_words),
                               err,
                               sizeof(err));
    events_.publish_response(reply, ok, cmd, ok ? "logic capture uploaded" : err);
}

} // namespace rpmon
