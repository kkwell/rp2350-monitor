#pragma once

#include "rpmon/core/config_store.h"
#include "rpmon/core/event_bus.h"
#include "rpmon/net/dhcp_server.h"
#include "rpmon/net/dns_server.h"

extern "C" {
#include "cyw43.h"
}

namespace rpmon {

class WifiManager {
public:
    explicit WifiManager(EventBus &events);
    bool init();
    bool start_ap();
    bool set_credentials(const char *ssid, const char *password, bool save, uint8_t slot = 0);
    bool clear_profile(uint8_t slot, bool save);
    bool select_profile(uint8_t slot, bool save);
    bool connect_station(char *err, size_t err_len);
    bool connect_station(uint8_t slot, char *err, size_t err_len);
    bool scan_wifi(char *err, size_t err_len);
    void schedule_ap(uint32_t delay_ms = 750);
    void schedule_station_connect(uint32_t delay_ms = 750);
    void schedule_station_connect(uint8_t slot, uint32_t delay_ms = 750);
    void status_json(char *out, size_t out_len) const;
    uint8_t active_profile_index() const;
    bool get_profile(uint8_t slot, WifiProfile &out) const;
    const char *profile_error(uint8_t slot) const;
    bool scan_active() const;
    size_t scan_result_count() const;
    bool get_scan_result(size_t index, char *ssid, size_t ssid_len, int16_t &rssi, uint16_t &channel, uint8_t &auth) const;
    void poll();

private:
    enum class PendingAction : uint8_t {
        None,
        StartAp,
        ConnectStation,
    };

    struct ScanResult {
        bool valid = false;
        char ssid[33] = {};
        int16_t rssi = 0;
        uint16_t channel = 0;
        uint8_t auth = 0;
    };

    static int scan_result_cb(void *env, const cyw43_ev_scan_result_t *result);
    void stop_ap();
    void stop_station();
    const WifiProfile *active_profile() const;
    WifiProfile *profile(uint8_t slot);
    const WifiProfile *profile(uint8_t slot) const;
    void upsert_scan_result(const cyw43_ev_scan_result_t *result);
    void profiles_json(char *out, size_t out_len) const;
    void scan_json(char *out, size_t out_len) const;
    const char *last_error_for(uint8_t slot) const;
    const char *pending_action_name() const;
    const char *station_ip() const;
    const char *station_status() const;

    EventBus &events_;
    ConfigStore store_;
    WifiSettings settings_;
    DhcpServer dhcp_;
    DnsServer dns_;
    bool initialized_ = false;
    bool ap_active_ = false;
    bool station_enabled_ = false;
    char ap_ssid_[33] = {};
    PendingAction pending_action_ = PendingAction::None;
    uint8_t pending_profile_ = 0;
    uint32_t pending_action_at_ms_ = 0;
    char last_errors_[kMaxWifiProfiles][96] = {};
    ScanResult scan_results_[kMaxWifiScanResults];
    bool scan_active_ = false;
    uint32_t scan_started_ms_ = 0;
    int last_link_status_ = -999;
};

} // namespace rpmon
