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
    void handle_ring_dma_irq();

private:
    static constexpr int kOwnerId = 1000;
    static constexpr size_t kLogicRingWords = 16384;
    static constexpr size_t kLogicRingBytes = kLogicRingWords * sizeof(uint32_t);

    static uint32_t bits_packed_per_word(uint8_t pin_count);
    static uint32_t words_for_samples(uint8_t pin_count, uint32_t samples);
    static uint32_t max_samples_for_words(uint8_t pin_count, uint32_t words);
    uint32_t samples_per_word() const;
    uint32_t clk_sys_hz() const;
    bool trigger_enabled() const;
    bool ring_trigger_mode() const;
    bool pio_trigger_supported() const;
    bool pio_pattern_trigger() const;
    bool pio_trigger_irq_pending() const;
    uint32_t dma_words_written() const;
    uint64_t ring_words_written() const;
    uint64_t available_samples() const;
    uint64_t oldest_available_sample() const;
    uint32_t sample_bits_from_word(uint32_t word, uint32_t sample_in_word) const;
    uint32_t raw_sample_bits(uint64_t sample) const;
    uint32_t pattern_compare_word() const;
    bool finish_pio_trigger_capture(char *err, size_t err_len);
    void finish_capture(EventBus *events);
    void append_burst_json(char *out, size_t out_len) const;
    void append_pin_pulls_json(char *out, size_t out_len) const;
    uint32_t *aligned_ring_buffer();
    const uint32_t *sample_storage() const;
    void release_runtime();
    void release_pins();
    void apply_pin_pulls() const;

    PinManager &pins_;
    PIO pio_ = pio2;
    int sm_ = -1;
    int dma_chan_ = -1;
    int dma_chan_b_ = -1;
    uint offset_ = 0;
    bool program_loaded_ = false;
    uint16_t program_instrs_[16] = {};
    uint8_t program_len_ = 0;
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
    uint64_t capture_start_sample_ = 0;
    uint32_t trigger_sample_ = 0;
    uint64_t trigger_sample_abs_ = 0;
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
    uint64_t burst_samples_[kLogicBurstMarksMax] = {};
    bool ring_mode_ = false;
    bool pio_trigger_mode_ = false;
    bool pio_trigger_irq_seen_ = false;
    bool sample_word_mode_ = false;
    uint32_t ring_dma_count_ = 0;
    volatile uint32_t ring_halves_completed_ = 0;
    uint64_t ring_words_base_ = 0;
    uint32_t *ring_buffer_ = nullptr;
    uint32_t buffer_[kLogicCaptureWords] = {};
};

} // namespace rpmon
