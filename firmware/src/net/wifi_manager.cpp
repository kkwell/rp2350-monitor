#include "rpmon/net/wifi_manager.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "pico/cyw43_arch.h"
#include "pico/error.h"
#include "pico/time.h"
#include "pico/unique_id.h"
#include "rpmon/config.h"
#include "rpmon/util/json.h"

namespace rpmon {

namespace {

void append_json(char *out, size_t out_len, size_t &pos, const char *fmt, ...) {
    if (pos >= out_len) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(out + pos, out_len - pos, fmt, args);
    va_end(args);
    if (written > 0) {
        pos += static_cast<size_t>(written);
    }
}

const char *connect_error_text(int rc) {
    switch (rc) {
    case PICO_ERROR_TIMEOUT:
        return "timeout or ssid not found";
    case PICO_ERROR_BADAUTH:
        return "bad auth";
    case PICO_ERROR_CONNECT_FAILED:
        return "connect failed";
    default:
        return "connect failed";
    }
}

} // namespace

WifiManager::WifiManager(EventBus &events) : events_(events) {}

bool WifiManager::init() {
    if (initialized_) {
        return true;
    }
    if (cyw43_arch_init() != 0) {
        events_.publish_error("wifi", "cyw43 init failed");
        return false;
    }
    initialized_ = true;
    bool loaded = store_.load(settings_);
    events_.publish_status("wifi", loaded ? "loaded saved wifi profiles" : "no saved wifi profiles");
    return true;
}

bool WifiManager::start_ap() {
    if (!initialized_ && !init()) {
        return false;
    }
    stop_station();
    stop_ap();

    char board_id[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1] = {};
    pico_get_unique_board_id_string(board_id, sizeof(board_id));
    size_t id_len = std::strlen(board_id);
    const char *suffix = id_len > 6 ? board_id + id_len - 6 : board_id;
    snprintf(ap_ssid_, sizeof(ap_ssid_), "%s-%s", kApSsidPrefix, suffix);

    cyw43_arch_enable_ap_mode(ap_ssid_, kApPassword, CYW43_AUTH_WPA2_AES_PSK);
    ap_active_ = true;
    dhcp_.start();
    dns_.start();

    char extra[120];
    snprintf(extra, sizeof(extra), "\"ssid\":\"%s\",\"ip\":\"%s\"", ap_ssid_, kApIp);
    events_.publish_status("wifi", "ap active", extra);
    return true;
}

bool WifiManager::set_credentials(const char *ssid, const char *password, bool save, uint8_t slot) {
    WifiProfile *target = profile(slot);
    if (!target || !ssid || ssid[0] == '\0') {
        return false;
    }
    std::strncpy(target->ssid, ssid, sizeof(target->ssid) - 1);
    target->ssid[sizeof(target->ssid) - 1] = '\0';
    std::strncpy(target->password, password ? password : "", sizeof(target->password) - 1);
    target->password[sizeof(target->password) - 1] = '\0';
    target->valid = true;
    settings_.active_index = slot;
    last_errors_[slot][0] = '\0';
    if (save) {
        return store_.save(settings_);
    }
    return true;
}

bool WifiManager::clear_profile(uint8_t slot, bool save) {
    WifiProfile *target = profile(slot);
    if (!target) {
        return false;
    }
    *target = {};
    last_errors_[slot][0] = '\0';
    if (settings_.active_index == slot) {
        settings_.active_index = 0;
        for (uint8_t i = 0; i < kMaxWifiProfiles; ++i) {
            if (settings_.profiles[i].valid) {
                settings_.active_index = i;
                break;
            }
        }
    }
    if (save) {
        return store_.save(settings_);
    }
    return true;
}

bool WifiManager::select_profile(uint8_t slot, bool save) {
    const WifiProfile *target = profile(slot);
    if (!target || !target->valid) {
        return false;
    }
    settings_.active_index = slot;
    if (save) {
        return store_.save(settings_);
    }
    return true;
}

bool WifiManager::connect_station(char *err, size_t err_len) {
    return connect_station(settings_.active_index, err, err_len);
}

bool WifiManager::connect_station(uint8_t slot, char *err, size_t err_len) {
    if (!initialized_ && !init()) {
        snprintf(err, err_len, "wifi init failed");
        return false;
    }
    const WifiProfile *target = profile(slot);
    if (!target || !target->valid || target->ssid[0] == '\0') {
        snprintf(err, err_len, "wifi profile %u is not configured", static_cast<unsigned>(slot));
        return false;
    }
    settings_.active_index = slot;
    store_.save(settings_);
    stop_ap();
    stop_station();

    cyw43_arch_enable_sta_mode();
    station_enabled_ = true;
    uint32_t auth = target->password[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN;
    int rc = cyw43_arch_wifi_connect_timeout_ms(target->ssid, target->password[0] ? target->password : nullptr, auth, 15000);
    if (rc != 0) {
        snprintf(err, err_len, "wifi connect failed: %s (%d)", connect_error_text(rc), rc);
        std::strncpy(last_errors_[slot], err, sizeof(last_errors_[slot]) - 1);
        last_errors_[slot][sizeof(last_errors_[slot]) - 1] = '\0';
        events_.publish_error("wifi", err);
        station_enabled_ = false;
        start_ap();
        return false;
    }
    last_errors_[slot][0] = '\0';
    char extra[128];
    char escaped_ssid[80];
    json_escape(target->ssid, escaped_ssid, sizeof(escaped_ssid));
    snprintf(extra, sizeof(extra), "\"slot\":%u,\"ssid\":\"%s\",\"ip\":\"%s\"",
             static_cast<unsigned>(slot),
             escaped_ssid,
             station_ip());
    events_.publish_status("wifi", "station connected", extra);
    return true;
}

bool WifiManager::scan_wifi(char *err, size_t err_len) {
    if (!initialized_ && !init()) {
        snprintf(err, err_len, "wifi init failed");
        return false;
    }
    if (cyw43_wifi_scan_active(&cyw43_state)) {
        snprintf(err, err_len, "wifi scan already active");
        return false;
    }
    cyw43_arch_enable_sta_mode();
    std::memset(scan_results_, 0, sizeof(scan_results_));
    cyw43_wifi_scan_options_t options{};
    int rc = cyw43_wifi_scan(&cyw43_state, &options, this, scan_result_cb);
    if (rc != 0) {
        snprintf(err, err_len, "wifi scan failed: %d", rc);
        scan_active_ = false;
        return false;
    }
    scan_active_ = true;
    scan_started_ms_ = to_ms_since_boot(get_absolute_time());
    events_.publish_status("wifi", "wifi scan started");
    return true;
}

void WifiManager::schedule_ap(uint32_t delay_ms) {
    pending_action_ = PendingAction::StartAp;
    pending_action_at_ms_ = to_ms_since_boot(get_absolute_time()) + delay_ms;
}

void WifiManager::schedule_station_connect(uint32_t delay_ms) {
    schedule_station_connect(settings_.active_index, delay_ms);
}

void WifiManager::schedule_station_connect(uint8_t slot, uint32_t delay_ms) {
    pending_action_ = PendingAction::ConnectStation;
    pending_profile_ = slot;
    pending_action_at_ms_ = to_ms_since_boot(get_absolute_time()) + delay_ms;
}

int WifiManager::scan_result_cb(void *env, const cyw43_ev_scan_result_t *result) {
    auto *manager = static_cast<WifiManager *>(env);
    if (manager && result) {
        manager->upsert_scan_result(result);
    }
    return 0;
}

void WifiManager::stop_ap() {
    if (!initialized_) {
        return;
    }
    dhcp_.stop();
    dns_.stop();
    cyw43_arch_disable_ap_mode();
    ap_active_ = false;
    ap_ssid_[0] = '\0';
}

void WifiManager::stop_station() {
    if (!initialized_) {
        return;
    }
    cyw43_arch_disable_sta_mode();
    station_enabled_ = false;
    last_link_status_ = -999;
}

const WifiProfile *WifiManager::active_profile() const {
    return profile(settings_.active_index);
}

WifiProfile *WifiManager::profile(uint8_t slot) {
    if (slot >= kMaxWifiProfiles) {
        return nullptr;
    }
    return &settings_.profiles[slot];
}

const WifiProfile *WifiManager::profile(uint8_t slot) const {
    if (slot >= kMaxWifiProfiles) {
        return nullptr;
    }
    return &settings_.profiles[slot];
}

void WifiManager::upsert_scan_result(const cyw43_ev_scan_result_t *result) {
    if (!result || result->ssid_len == 0) {
        return;
    }
    char ssid[33] = {};
    size_t len = result->ssid_len < sizeof(ssid) - 1 ? result->ssid_len : sizeof(ssid) - 1;
    std::memcpy(ssid, result->ssid, len);
    ssid[len] = '\0';

    int target = -1;
    for (size_t i = 0; i < kMaxWifiScanResults; ++i) {
        if (scan_results_[i].valid && std::strcmp(scan_results_[i].ssid, ssid) == 0) {
            target = static_cast<int>(i);
            break;
        }
        if (!scan_results_[i].valid && target < 0) {
            target = static_cast<int>(i);
        }
    }
    if (target < 0) {
        int weakest = 0;
        for (size_t i = 1; i < kMaxWifiScanResults; ++i) {
            if (scan_results_[i].rssi < scan_results_[weakest].rssi) {
                weakest = static_cast<int>(i);
            }
        }
        if (result->rssi <= scan_results_[weakest].rssi) {
            return;
        }
        target = weakest;
    }

    ScanResult &slot = scan_results_[target];
    slot.valid = true;
    std::strncpy(slot.ssid, ssid, sizeof(slot.ssid) - 1);
    slot.ssid[sizeof(slot.ssid) - 1] = '\0';
    slot.rssi = result->rssi;
    slot.channel = result->channel;
    slot.auth = result->auth_mode;
}

void WifiManager::profiles_json(char *out, size_t out_len) const {
    size_t pos = 0;
    append_json(out, out_len, pos, "\"profiles\":[");
    for (size_t i = 0; i < kMaxWifiProfiles; ++i) {
        const WifiProfile &profile = settings_.profiles[i];
        char ssid[80];
        char error[140];
        json_escape(profile.valid ? profile.ssid : "", ssid, sizeof(ssid));
        json_escape(last_errors_[i], error, sizeof(error));
        append_json(out, out_len, pos,
                    "%s{\"slot\":%u,\"valid\":%s,\"active\":%s,\"ssid\":\"%s\",\"last_error\":\"%s\"}",
                    i == 0 ? "" : ",",
                    static_cast<unsigned>(i),
                    profile.valid ? "true" : "false",
                    settings_.active_index == i ? "true" : "false",
                    ssid,
                    error);
    }
    append_json(out, out_len, pos, "]");
}

void WifiManager::scan_json(char *out, size_t out_len) const {
    size_t pos = 0;
    append_json(out, out_len, pos, "\"scan\":{\"active\":%s,\"results\":[",
                scan_active_ ? "true" : "false");
    bool first = true;
    for (const ScanResult &result : scan_results_) {
        if (!result.valid) {
            continue;
        }
        char ssid[80];
        json_escape(result.ssid, ssid, sizeof(ssid));
        append_json(out, out_len, pos,
                    "%s{\"ssid\":\"%s\",\"rssi\":%d,\"channel\":%u,\"auth\":%u}",
                    first ? "" : ",",
                    ssid,
                    static_cast<int>(result.rssi),
                    static_cast<unsigned>(result.channel),
                    static_cast<unsigned>(result.auth));
        first = false;
    }
    append_json(out, out_len, pos, "]}");
}

const char *WifiManager::last_error_for(uint8_t slot) const {
    return slot < kMaxWifiProfiles ? last_errors_[slot] : "";
}

void WifiManager::status_json(char *out, size_t out_len) const {
    const WifiProfile *active = active_profile();
    char ssid[80];
    json_escape(active && active->valid ? active->ssid : "", ssid, sizeof(ssid));
    char profiles[720];
    char scan[980];
    profiles_json(profiles, sizeof(profiles));
    scan_json(scan, sizeof(scan));
    snprintf(out, out_len,
             "\"wifi\":{\"ap_active\":%s,\"ap_ssid\":\"%s\",\"ap_ip\":\"%s\",\"active_profile\":%u,\"ssid_configured\":%s,\"ssid\":\"%s\",\"station_status\":\"%s\",\"station_ip\":\"%s\",\"pending_action\":\"%s\",%s,%s}",
             ap_active_ ? "true" : "false",
             ap_ssid_,
             kApIp,
             static_cast<unsigned>(settings_.active_index),
             active && active->valid ? "true" : "false",
             ssid,
             station_status(),
             station_ip(),
             pending_action_name(),
             profiles,
             scan);
}

uint8_t WifiManager::active_profile_index() const {
    return settings_.active_index;
}

bool WifiManager::get_profile(uint8_t slot, WifiProfile &out) const {
    const WifiProfile *source = profile(slot);
    if (!source) {
        out = {};
        return false;
    }
    out = *source;
    return source->valid;
}

const char *WifiManager::profile_error(uint8_t slot) const {
    return last_error_for(slot);
}

bool WifiManager::scan_active() const {
    return scan_active_;
}

size_t WifiManager::scan_result_count() const {
    size_t count = 0;
    for (const ScanResult &result : scan_results_) {
        if (result.valid) {
            ++count;
        }
    }
    return count;
}

bool WifiManager::get_scan_result(size_t index, char *ssid, size_t ssid_len, int16_t &rssi, uint16_t &channel, uint8_t &auth) const {
    size_t seen = 0;
    for (const ScanResult &result : scan_results_) {
        if (!result.valid) {
            continue;
        }
        if (seen == index) {
            if (ssid && ssid_len > 0) {
                std::strncpy(ssid, result.ssid, ssid_len - 1);
                ssid[ssid_len - 1] = '\0';
            }
            rssi = result.rssi;
            channel = result.channel;
            auth = result.auth;
            return true;
        }
        ++seen;
    }
    if (ssid && ssid_len > 0) {
        ssid[0] = '\0';
    }
    rssi = 0;
    channel = 0;
    auth = 0;
    return false;
}

void WifiManager::poll() {
    if (!initialized_) {
        return;
    }
    if (scan_active_ && !cyw43_wifi_scan_active(&cyw43_state)) {
        scan_active_ = false;
        if (ap_active_ && !station_enabled_) {
            // AP+STA scans can leave some clients unable to renew DHCP; restart setup AP after scans.
            start_ap();
        }
        char extra[64];
        snprintf(extra, sizeof(extra), "\"elapsed_ms\":%lu",
                 static_cast<unsigned long>(to_ms_since_boot(get_absolute_time()) - scan_started_ms_));
        events_.publish_status("wifi", "wifi scan complete", extra);
    }
    if (pending_action_ != PendingAction::None &&
        static_cast<int32_t>(to_ms_since_boot(get_absolute_time()) - pending_action_at_ms_) >= 0) {
        PendingAction action = pending_action_;
        uint8_t slot = pending_profile_;
        pending_action_ = PendingAction::None;
        if (action == PendingAction::StartAp) {
            start_ap();
        } else if (action == PendingAction::ConnectStation) {
            char err[120] = {};
            connect_station(slot, err, sizeof(err));
        }
    }
    int status = station_enabled_ ? cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) : CYW43_LINK_DOWN;
    if (station_enabled_ && status != last_link_status_) {
        last_link_status_ = status;
        char extra[96];
        snprintf(extra, sizeof(extra), "\"station_status\":\"%s\",\"ip\":\"%s\"", station_status(), station_ip());
        events_.publish_status("wifi", "station status changed", extra);
    }
}

const char *WifiManager::pending_action_name() const {
    switch (pending_action_) {
    case PendingAction::StartAp:
        return "ap";
    case PendingAction::ConnectStation:
        return "station";
    case PendingAction::None:
    default:
        return "none";
    }
}

const char *WifiManager::station_ip() const {
    static char ip[16];
    ip[0] = '\0';
    if (!initialized_ || !station_enabled_) {
        return "0.0.0.0";
    }
    const ip4_addr_t *addr = netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]);
    ip4addr_ntoa_r(addr, ip, sizeof(ip));
    return ip;
}

const char *WifiManager::station_status() const {
    if (!initialized_) {
        return "not_initialized";
    }
    if (!station_enabled_) {
        return "down";
    }
    switch (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA)) {
    case CYW43_LINK_DOWN:
        return "down";
    case CYW43_LINK_JOIN:
        return "joining";
    case CYW43_LINK_NOIP:
        return "no_ip";
    case CYW43_LINK_UP:
        return "up";
    case CYW43_LINK_FAIL:
        return "fail";
    case CYW43_LINK_NONET:
        return "no_network";
    case CYW43_LINK_BADAUTH:
        return "bad_auth";
    default:
        return "unknown";
    }
}

} // namespace rpmon
