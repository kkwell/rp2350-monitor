#pragma once

#include <cstddef>
#include <cstdint>

#include "rpmon/config.h"
#include "rpmon/core/event_bus.h"
#include "rpmon/core/pin_manager.h"

#include "hardware/dma.h"
#include "hardware/pio.h"

namespace rpmon {

enum class LogicTriggerMode : uint8_t {
    Level,
    Rising,
    Falling,
    Pattern
};

enum class LogicPullMode : uint8_t {
    None,
    Up,
    Down
};

class LogicAnalyzer {
public:
    explicit LogicAnalyzer(PinManager &pins);

    bool configure(uint8_t pin_base,
                   uint8_t pin_count,
                   uint32_t sample_rate_hz,
                   uint32_t sample_count,
                   int trigger_pin,
                   LogicTriggerMode trigger_mode,
                   bool trigger_level,
                   LogicPullMode pull_mode,
                   const LogicPullMode *pin_pull_modes,
                   uint32_t pre_samples,
                   uint32_t post_samples,
                   uint32_t search_samples,
                   uint32_t trigger_mask,
                   uint32_t trigger_value,
                   uint8_t burst_count,
                   char *err,
                   size_t err_len);
    bool start(char *err, size_t err_len);
    bool stop(char *err, size_t err_len);
    bool release(char *err, size_t err_len);
    void poll(EventBus &events);
    void status_json(char *out, size_t out_len) const;
    void caps_json(char *out, size_t out_len) const;
    bool stream_capture(LineSink &reply, size_t offset_words, size_t max_words, char *err, size_t err_len) const;

private:
    static constexpr int kOwnerId = 1000;

    static uint32_t bits_packed_per_word(uint8_t pin_count);
    static uint32_t words_for_samples(uint8_t pin_count, uint32_t samples);
    static uint32_t max_samples_for_words(uint8_t pin_count, uint32_t words);
    uint32_t clk_sys_hz() const;
    bool scan_trigger_mode() const;
    uint32_t dma_words_written() const;
    uint32_t available_samples() const;
    uint32_t raw_sample_bits(uint32_t sample) const;
    bool trigger_matches(uint32_t sample, bool &matched) const;
    void scan_available_samples();
    bool finish_scanned_capture(char *err, size_t err_len);
    void finish_capture(EventBus *events);
    void append_burst_json(char *out, size_t out_len) const;
    void append_pin_pulls_json(char *out, size_t out_len) const;
    void release_runtime();
    void release_pins();
    void apply_pin_pulls() const;

    PinManager &pins_;
    PIO pio_ = pio2;
    int sm_ = -1;
    int dma_chan_ = -1;
    uint offset_ = 0;
    bool program_loaded_ = false;
    bool configured_ = false;
    bool running_ = false;
    bool complete_ = false;
    bool completion_reported_ = false;
    uint32_t capture_id_ = 0;
    uint8_t pin_base_ = 0;
    uint8_t pin_count_ = 0;
    uint32_t sample_rate_hz_ = 0;
    uint32_t sample_count_ = 0;
    uint32_t capture_words_ = 0;
    uint32_t dma_words_ = 0;
    uint32_t search_samples_ = 0;
    uint32_t pre_samples_ = 0;
    uint32_t post_samples_ = 0;
    uint32_t capture_start_sample_ = 0;
    uint32_t scan_next_sample_ = 0;
    uint32_t trigger_sample_ = 0;
    uint32_t trigger_mask_ = 0;
    uint32_t trigger_value_ = 0;
    int trigger_pin_ = -1;
    LogicTriggerMode trigger_mode_ = LogicTriggerMode::Level;
    bool trigger_level_ = true;
    bool trigger_found_ = false;
    LogicPullMode pull_mode_ = LogicPullMode::None;
    LogicPullMode pin_pull_modes_[32] = {};
    uint8_t burst_count_ = 1;
    uint8_t burst_found_ = 0;
    uint32_t burst_samples_[kLogicBurstMarksMax] = {};
    uint32_t buffer_[kLogicCaptureWords] = {};
};

} // namespace rpmon
