#pragma once

#include <cstddef>
#include <cstdint>

#include "rpmon/core/event_bus.h"
#include "rpmon/core/pin_manager.h"

namespace rpmon {

class DebugProbe {
public:
    explicit DebugProbe(PinManager &pins);

    bool configure(int swclk, int swdio, int reset, uint32_t freq_khz, char *err, size_t err_len);
    bool release(char *err, size_t err_len);
    bool reset_target(bool hold_reset, bool pulse, uint32_t pulse_ms, char *err, size_t err_len);
    bool process_dap_packet(const uint8_t *request, size_t request_len, uint8_t *response, size_t response_cap, size_t &response_len, char *err, size_t err_len);
    void poll_usb(EventBus &events);

    void caps_json(char *out, size_t out_len) const;
    void status_json(char *out, size_t out_len) const;

    bool port_setup();
    void port_off();
    void set_swclk_freq(uint32_t freq_khz);
    void write_bits(uint32_t bit_count, uint32_t data);
    uint32_t read_bits(uint32_t bit_count);
    void hiz_clocks(uint32_t bit_count);
    void read_mode();
    void write_mode();
    void assert_reset(bool released);
    int reset_level() const;

    static DebugProbe *instance();

private:
    static constexpr int kOwnerId = 900;
    static constexpr int kDefaultSwclk = 2;
    static constexpr int kDefaultSwdio = 3;
    static constexpr int kDefaultReset = 1;
    static constexpr uint32_t kDefaultFreqKhz = 1000;
    static constexpr uint32_t kMaxFreqKhz = 24000;

    bool validate_pins(int swclk, int swdio, int reset, char *err, size_t err_len) const;
    bool claim_pins(char *err, size_t err_len);
    void release_pins();
    bool init_pio(char *err, size_t err_len);
    void deinit_pio();
    uint32_t format_command(uint32_t bit_count, bool out_en, uint32_t command_offset) const;
    void wait_idle();
    void set_last_error(const char *message);

    PinManager &pins_;
    int swclk_ = kDefaultSwclk;
    int swdio_ = kDefaultSwdio;
    int reset_ = kDefaultReset;
    uint32_t freq_khz_ = kDefaultFreqKhz;
    bool configured_ = true;
    bool claimed_ = false;
    bool pio_active_ = false;
    uint32_t dap_packets_ = 0;
    uint32_t dap_errors_ = 0;
    uint32_t port_setups_ = 0;
    uint32_t port_setup_failures_ = 0;
    char last_error_[96] = {};
    class PioState;
    PioState *pio_ = nullptr;
};

} // namespace rpmon
