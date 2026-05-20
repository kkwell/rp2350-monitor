#include "rpmon/probe/debug_probe.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "swd_probe.pio.h"

extern "C" {
uint32_t DAP_ProcessCommand(const uint8_t *request, uint8_t *response);
void DAP_Setup(void);
}

namespace rpmon {

namespace {

DebugProbe *g_probe = nullptr;
uint8_t g_usb_rx[64];
uint8_t g_usb_tx[64];

uint32_t pio_txstall_mask(PIO pio, uint sm) {
    (void)pio;
    return 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
}

} // namespace

class DebugProbe::PioState {
public:
    PIO pio = pio0;
    uint sm = 0;
    uint offset = 0;
};

DebugProbe::DebugProbe(PinManager &pins) : pins_(pins) {
    g_probe = this;
    DAP_Setup();
}

DebugProbe *DebugProbe::instance() {
    return g_probe;
}

bool DebugProbe::validate_pins(int swclk, int swdio, int reset, char *err, size_t err_len) const {
    if (!pins_.is_exposed_gpio(swclk) || !pins_.is_exposed_gpio(swdio)) {
        snprintf(err, err_len, "probe swclk/swdio pins must be exposed GPIOs");
        return false;
    }
    if (swclk == swdio) {
        snprintf(err, err_len, "probe swclk and swdio must be different GPIOs");
        return false;
    }
    if (reset >= 0 && !pins_.is_exposed_gpio(reset)) {
        snprintf(err, err_len, "probe reset must be an exposed GPIO or -1");
        return false;
    }
    if (reset >= 0 && (reset == swclk || reset == swdio)) {
        snprintf(err, err_len, "probe reset must not share swclk/swdio pins");
        return false;
    }
    return true;
}

bool DebugProbe::configure(int swclk, int swdio, int reset, uint32_t freq_khz, char *err, size_t err_len) {
    if (freq_khz == 0) {
        freq_khz = kDefaultFreqKhz;
    }
    if (freq_khz > kMaxFreqKhz) {
        snprintf(err, err_len, "probe swclk_khz out of range; max %lu", static_cast<unsigned long>(kMaxFreqKhz));
        return false;
    }
    if (!validate_pins(swclk, swdio, reset, err, err_len)) {
        return false;
    }

    deinit_pio();
    release_pins();
    swclk_ = swclk;
    swdio_ = swdio;
    reset_ = reset;
    freq_khz_ = freq_khz;
    configured_ = true;
    last_error_[0] = '\0';

    if (!claim_pins(err, err_len)) {
        set_last_error(err);
        return false;
    }
    if (!init_pio(err, err_len)) {
        release_pins();
        set_last_error(err);
        return false;
    }
    return true;
}

bool DebugProbe::release(char *err, size_t err_len) {
    (void)err;
    (void)err_len;
    deinit_pio();
    release_pins();
    last_error_[0] = '\0';
    return true;
}

bool DebugProbe::claim_pins(char *err, size_t err_len) {
    if (claimed_) {
        return true;
    }
    if (!pins_.claim(swclk_, kOwnerId, PinRole::ProbeSwclk) ||
        !pins_.claim(swdio_, kOwnerId, PinRole::ProbeSwdio) ||
        (reset_ >= 0 && !pins_.claim(reset_, kOwnerId, PinRole::ProbeReset))) {
        pins_.release_channel(kOwnerId);
        snprintf(err, err_len, "probe pins already in use or not exposed");
        return false;
    }
    claimed_ = true;
    return true;
}

void DebugProbe::release_pins() {
    if (claimed_) {
        pins_.release_channel(kOwnerId);
        claimed_ = false;
    }
}

bool DebugProbe::init_pio(char *err, size_t err_len) {
    if (pio_active_) {
        return true;
    }
    if (!configured_) {
        snprintf(err, err_len, "probe pins are not configured");
        return false;
    }
    if (!claim_pins(err, err_len)) {
        return false;
    }

    PIO selected = pio0;
    int sm = pio_claim_unused_sm(selected, false);
    if (sm < 0) {
        selected = pio1;
        sm = pio_claim_unused_sm(selected, false);
    }
    if (sm < 0) {
        snprintf(err, err_len, "no free PIO state machine for probe");
        return false;
    }
    if (!pio_can_add_program(selected, &rpmon_swd_probe_program)) {
        pio_sm_unclaim(selected, static_cast<uint>(sm));
        snprintf(err, err_len, "not enough PIO instruction memory for probe");
        return false;
    }

    if (!pio_) {
        static PioState state;
        pio_ = &state;
    }
    pio_->pio = selected;
    pio_->sm = static_cast<uint>(sm);
    pio_->offset = pio_add_program(selected, &rpmon_swd_probe_program);

    if (reset_ >= 0) {
        gpio_init(reset_);
        gpio_pull_up(reset_);
        gpio_put(reset_, 0);
        gpio_set_dir(reset_, GPIO_IN);
    }
    pio_gpio_init(selected, swclk_);
    pio_gpio_init(selected, swdio_);
    gpio_pull_up(swdio_);

    pio_sm_config c = rpmon_swd_probe_program_get_default_config(pio_->offset);
    sm_config_set_sideset_pins(&c, swclk_);
    sm_config_set_out_pins(&c, swdio_, 1);
    sm_config_set_set_pins(&c, swdio_, 1);
    sm_config_set_in_pins(&c, swdio_);
    sm_config_set_out_shift(&c, true, false, 0);
    sm_config_set_in_shift(&c, true, false, 0);
    pio_sm_set_pindirs_with_mask(selected,
                                  pio_->sm,
                                  (1u << swclk_) | (1u << swdio_),
                                  (1u << swclk_) | (1u << swdio_));
    pio_sm_init(selected, pio_->sm, pio_->offset, &c);
    set_swclk_freq(freq_khz_);
    pio_sm_exec(selected, pio_->sm, pio_->offset + rpmon_swd_probe_offset_get_next_cmd);
    pio_sm_set_enabled(selected, pio_->sm, true);
    pio_active_ = true;
    return true;
}

void DebugProbe::deinit_pio() {
    if (!pio_active_ || !pio_) {
        return;
    }
    read_mode();
    pio_sm_set_enabled(pio_->pio, pio_->sm, false);
    pio_sm_clear_fifos(pio_->pio, pio_->sm);
    pio_remove_program(pio_->pio, &rpmon_swd_probe_program, pio_->offset);
    pio_sm_unclaim(pio_->pio, pio_->sm);

    if (reset_ >= 0) {
        assert_reset(true);
        gpio_deinit(reset_);
        gpio_disable_pulls(reset_);
    }
    gpio_deinit(swclk_);
    gpio_disable_pulls(swclk_);
    gpio_deinit(swdio_);
    gpio_disable_pulls(swdio_);
    pio_active_ = false;
}

bool DebugProbe::port_setup() {
    ++port_setups_;
    char err[96] = {};
    if (!configured_) {
        snprintf(err, sizeof(err), "probe pins are not configured");
        set_last_error(err);
        ++port_setup_failures_;
        return false;
    }
    if (!init_pio(err, sizeof(err))) {
        set_last_error(err);
        ++port_setup_failures_;
        return false;
    }
    last_error_[0] = '\0';
    return true;
}

void DebugProbe::port_off() {
    deinit_pio();
    release_pins();
}

uint32_t DebugProbe::format_command(uint32_t bit_count, bool out_en, uint32_t command_offset) const {
    return ((bit_count - 1u) & 0xffu) | (static_cast<uint32_t>(out_en) << 8u) | (command_offset << 9u);
}

void DebugProbe::set_swclk_freq(uint32_t freq_khz) {
    if (freq_khz == 0) {
        freq_khz = kDefaultFreqKhz;
    }
    freq_khz_ = std::min(freq_khz, kMaxFreqKhz);
    if (!pio_active_ || !pio_) {
        return;
    }
    uint32_t clk_sys_freq_khz = clock_get_hz(clk_sys) / 1000u;
    uint32_t divider = (((clk_sys_freq_khz + freq_khz_ - 1u) / freq_khz_) + 3u) / 4u;
    if (divider == 0) {
        divider = 1;
    }
    if (divider > 65535u) {
        divider = 65535u;
    }
    pio_sm_set_clkdiv_int_frac(pio_->pio, pio_->sm, static_cast<uint16_t>(divider), 0);
}

void DebugProbe::write_bits(uint32_t bit_count, uint32_t data) {
    if (!pio_active_ || !pio_) {
        return;
    }
    pio_sm_put_blocking(pio_->pio, pio_->sm, format_command(bit_count, true, pio_->offset + rpmon_swd_probe_offset_write_cmd));
    pio_sm_put_blocking(pio_->pio, pio_->sm, data);
}

uint32_t DebugProbe::read_bits(uint32_t bit_count) {
    if (!pio_active_ || !pio_) {
        return 0;
    }
    pio_sm_put_blocking(pio_->pio, pio_->sm, format_command(bit_count, false, pio_->offset + rpmon_swd_probe_offset_read_cmd));
    uint32_t data = pio_sm_get_blocking(pio_->pio, pio_->sm);
    if (bit_count < 32) {
        data >>= 32u - bit_count;
    }
    return data;
}

void DebugProbe::hiz_clocks(uint32_t bit_count) {
    if (!pio_active_ || !pio_) {
        return;
    }
    pio_sm_put_blocking(pio_->pio, pio_->sm, format_command(bit_count, false, pio_->offset + rpmon_swd_probe_offset_turnaround_cmd));
    pio_sm_put_blocking(pio_->pio, pio_->sm, 0);
}

void DebugProbe::wait_idle() {
    if (!pio_active_ || !pio_) {
        return;
    }
    uint32_t mask = pio_txstall_mask(pio_->pio, pio_->sm);
    pio_->pio->fdebug = mask;
    while (!(pio_->pio->fdebug & mask)) {
        tight_loop_contents();
    }
}

void DebugProbe::read_mode() {
    if (!pio_active_ || !pio_) {
        return;
    }
    pio_sm_put_blocking(pio_->pio, pio_->sm, format_command(0, false, pio_->offset + rpmon_swd_probe_offset_get_next_cmd));
    wait_idle();
}

void DebugProbe::write_mode() {
    if (!pio_active_ || !pio_) {
        return;
    }
    pio_sm_put_blocking(pio_->pio, pio_->sm, format_command(0, true, pio_->offset + rpmon_swd_probe_offset_get_next_cmd));
    wait_idle();
}

void DebugProbe::assert_reset(bool released) {
    if (reset_ < 0) {
        return;
    }
    gpio_set_dir(reset_, released ? GPIO_IN : GPIO_OUT);
    if (!released) {
        gpio_put(reset_, 0);
    }
}

int DebugProbe::reset_level() const {
    if (reset_ < 0 || !pins_.is_exposed_gpio(reset_)) {
        return -1;
    }
    return gpio_get(reset_);
}

bool DebugProbe::reset_target(bool hold_reset, bool pulse, uint32_t pulse_ms, char *err, size_t err_len) {
    if (reset_ < 0) {
        snprintf(err, err_len, "probe reset pin is disabled");
        return false;
    }
    if (!claim_pins(err, err_len)) {
        return false;
    }
    gpio_init(reset_);
    gpio_pull_up(reset_);
    gpio_put(reset_, 0);
    if (pulse) {
        if (pulse_ms == 0) {
            pulse_ms = 50;
        }
        assert_reset(false);
        sleep_ms(pulse_ms);
        assert_reset(true);
        return true;
    }
    assert_reset(!hold_reset);
    return true;
}

bool DebugProbe::process_dap_packet(const uint8_t *request,
                                    size_t request_len,
                                    uint8_t *response,
                                    size_t response_cap,
                                    size_t &response_len,
                                    char *err,
                                    size_t err_len) {
    response_len = 0;
    if (!request || !response || response_cap < sizeof(g_usb_tx)) {
        snprintf(err, err_len, "invalid DAP packet buffers");
        ++dap_errors_;
        return false;
    }
    if (request_len == 0 || request_len > sizeof(g_usb_rx)) {
        snprintf(err, err_len, "DAP packet length must be 1..64 bytes");
        ++dap_errors_;
        return false;
    }
    uint8_t packet[64] = {};
    std::memcpy(packet, request, request_len);
    uint32_t written = DAP_ProcessCommand(packet, response) & 0xffffu;
    if (written > response_cap) {
        snprintf(err, err_len, "DAP response overflow");
        ++dap_errors_;
        return false;
    }
    response_len = written;
    ++dap_packets_;
    return true;
}

void DebugProbe::poll_usb(EventBus &events) {
    if (!tud_ready()) {
        return;
    }
    while (tud_vendor_available()) {
        std::memset(g_usb_rx, 0, sizeof(g_usb_rx));
        std::memset(g_usb_tx, 0, sizeof(g_usb_tx));
        uint32_t read = tud_vendor_read(g_usb_rx, sizeof(g_usb_rx));
        size_t response_len = 0;
        char err[96] = {};
        if (!process_dap_packet(g_usb_rx, read, g_usb_tx, sizeof(g_usb_tx), response_len, err, sizeof(err))) {
            set_last_error(err);
            events.publish_error("probe", err);
            break;
        }
        if (response_len > 0) {
            tud_vendor_write(g_usb_tx, static_cast<uint32_t>(response_len));
            tud_vendor_flush();
        }
    }
}

void DebugProbe::caps_json(char *out, size_t out_len) const {
    snprintf(out,
             out_len,
             "\"probe_caps\":{\"engine\":\"raspberrypi-debugprobe-pio-swd\",\"source\":\"github.com/raspberrypi/debugprobe\",\"cmsis_dap\":true,\"cmsis_dap_version\":\"v2\",\"usb_bulk\":true,\"json_packet_bridge\":true,\"swd\":true,\"jtag\":false,\"swo\":false,\"runtime_pins\":true,\"default_pins\":{\"swclk\":%d,\"swdio\":%d,\"reset\":%d},\"swclk_khz_default\":%lu,\"swclk_khz_max\":%lu,\"openocd_transport\":\"cmsis-dap\",\"ai_commands\":[\"probe_caps\",\"probe_status\",\"probe_config\",\"probe_release\",\"probe_reset\",\"probe_dap\"]}",
             kDefaultSwclk,
             kDefaultSwdio,
             kDefaultReset,
             static_cast<unsigned long>(kDefaultFreqKhz),
             static_cast<unsigned long>(kMaxFreqKhz));
}

void DebugProbe::status_json(char *out, size_t out_len) const {
    char escaped[128];
    const char *message = last_error_[0] ? last_error_ : "";
    size_t pos = 0;
    escaped[0] = '\0';
    while (*message && pos + 1 < sizeof(escaped)) {
        char c = *message++;
        if (c == '"' || c == '\\') {
            if (pos + 2 >= sizeof(escaped)) {
                break;
            }
            escaped[pos++] = '\\';
        }
        escaped[pos++] = c;
    }
    escaped[pos] = '\0';
    snprintf(out,
             out_len,
             "\"probe\":{\"configured\":%s,\"active\":%s,\"claimed\":%s,\"pins\":{\"swclk\":%d,\"swdio\":%d,\"reset\":%d},\"swclk_khz\":%lu,\"reset_level\":%d,\"dap_packets\":%lu,\"dap_errors\":%lu,\"port_setups\":%lu,\"port_setup_failures\":%lu,\"last_error\":\"%s\"}",
             configured_ ? "true" : "false",
             pio_active_ ? "true" : "false",
             claimed_ ? "true" : "false",
             swclk_,
             swdio_,
             reset_,
             static_cast<unsigned long>(freq_khz_),
             reset_level(),
             static_cast<unsigned long>(dap_packets_),
             static_cast<unsigned long>(dap_errors_),
             static_cast<unsigned long>(port_setups_),
             static_cast<unsigned long>(port_setup_failures_),
             escaped);
}

void DebugProbe::set_last_error(const char *message) {
    if (!message) {
        last_error_[0] = '\0';
        return;
    }
    std::strncpy(last_error_, message, sizeof(last_error_) - 1);
    last_error_[sizeof(last_error_) - 1] = '\0';
}

} // namespace rpmon

extern "C" {

void probe_set_swclk_freq(uint freq_khz) {
    if (auto *probe = rpmon::DebugProbe::instance()) {
        probe->set_swclk_freq(freq_khz);
    }
}

void probe_write_bits(uint bit_count, uint32_t data) {
    if (auto *probe = rpmon::DebugProbe::instance()) {
        probe->write_bits(bit_count, data);
    }
}

uint32_t probe_read_bits(uint bit_count) {
    if (auto *probe = rpmon::DebugProbe::instance()) {
        return probe->read_bits(bit_count);
    }
    return 0;
}

void probe_hiz_clocks(uint bit_count) {
    if (auto *probe = rpmon::DebugProbe::instance()) {
        probe->hiz_clocks(bit_count);
    }
}

void probe_read_mode(void) {
    if (auto *probe = rpmon::DebugProbe::instance()) {
        probe->read_mode();
    }
}

void probe_write_mode(void) {
    if (auto *probe = rpmon::DebugProbe::instance()) {
        probe->write_mode();
    }
}

void probe_init(void) {
    if (auto *probe = rpmon::DebugProbe::instance()) {
        probe->port_setup();
    }
}

void probe_deinit(void) {
    if (auto *probe = rpmon::DebugProbe::instance()) {
        probe->port_off();
    }
}

void probe_assert_reset(bool released) {
    if (auto *probe = rpmon::DebugProbe::instance()) {
        probe->assert_reset(released);
    }
}

int probe_reset_level(void) {
    if (auto *probe = rpmon::DebugProbe::instance()) {
        return probe->reset_level();
    }
    return 0;
}

}
