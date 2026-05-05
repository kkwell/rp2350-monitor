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
    Falling
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
                   char *err,
                   size_t err_len);
    bool start(char *err, size_t err_len);
    bool stop(char *err, size_t err_len);
    bool release(char *err, size_t err_len);
    void poll(EventBus &events);
    void status_json(char *out, size_t out_len) const;
    bool stream_capture(LineSink &reply, size_t offset_words, size_t max_words, char *err, size_t err_len) const;

private:
    static constexpr int kOwnerId = 1000;

    static uint32_t bits_packed_per_word(uint8_t pin_count);
    uint32_t clk_sys_hz() const;
    void release_runtime();
    void release_pins();

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
    int trigger_pin_ = -1;
    LogicTriggerMode trigger_mode_ = LogicTriggerMode::Level;
    bool trigger_level_ = true;
    uint32_t buffer_[kLogicCaptureWords] = {};
};

} // namespace rpmon
