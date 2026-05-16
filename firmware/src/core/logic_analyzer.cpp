#include "rpmon/core/logic_analyzer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/bus_ctrl.h"
#include "rpmon/util/hex.h"

namespace rpmon {

namespace {

const char *logic_pull_name(LogicPullMode mode) {
    switch (mode) {
    case LogicPullMode::Up:
        return "up";
    case LogicPullMode::Down:
        return "down";
    case LogicPullMode::None:
    default:
        return "none";
    }
}

} // namespace

LogicAnalyzer::LogicAnalyzer(PinManager &pins) : pins_(pins) {}

bool LogicAnalyzer::configure(uint8_t pin_base,
                              uint8_t pin_count,
                              uint32_t sample_rate_hz,
                              uint32_t sample_count,
                              int trigger_pin,
                              LogicTriggerMode trigger_mode,
                              bool trigger_level,
                              LogicPullMode pull_mode,
                              char *err,
                              size_t err_len) {
    if (running_) {
        snprintf(err, err_len, "logic capture is running");
        return false;
    }
    if (pin_count == 0 || pin_count > 32) {
        snprintf(err, err_len, "logic pin_count must be 1..32");
        return false;
    }
    if (sample_rate_hz == 0 || sample_rate_hz > clk_sys_hz()) {
        snprintf(err, err_len, "logic sample_rate must be 1..%lu Hz", static_cast<unsigned long>(clk_sys_hz()));
        return false;
    }
    if (sample_count == 0) {
        snprintf(err, err_len, "logic sample_count must be positive");
        return false;
    }
    uint32_t record_bits = bits_packed_per_word(pin_count);
    uint64_t total_bits = static_cast<uint64_t>(sample_count) * pin_count;
    uint32_t words = static_cast<uint32_t>((total_bits + record_bits - 1) / record_bits);
    if (words == 0 || words > kLogicCaptureWords) {
        snprintf(err, err_len, "logic capture requires %lu words, max is %u",
                 static_cast<unsigned long>(words),
                 static_cast<unsigned>(kLogicCaptureWords));
        return false;
    }
    if (pin_base + pin_count > 29) {
        snprintf(err, err_len, "logic pin range is outside exposed GPIO range");
        return false;
    }
    for (uint8_t i = 0; i < pin_count; ++i) {
        int gpio = pin_base + i;
        if (!pins_.is_exposed_gpio(gpio)) {
            snprintf(err, err_len, "logic GPIO%d is not exposed", gpio);
            return false;
        }
        int owner = pins_.owner(gpio);
        if (owner != 0 && owner != kOwnerId) {
            snprintf(err, err_len, "logic GPIO%d is already owned by channel %d", gpio, owner);
            return false;
        }
    }
    if (trigger_pin >= 0) {
        if (!pins_.is_exposed_gpio(trigger_pin) || trigger_pin < pin_base || trigger_pin >= pin_base + pin_count) {
            snprintf(err, err_len, "logic trigger_pin must be inside the captured pin range");
            return false;
        }
    }

    release_pins();
    for (uint8_t i = 0; i < pin_count; ++i) {
        if (!pins_.claim(pin_base + i, kOwnerId, PinRole::Logic)) {
            release_pins();
            snprintf(err, err_len, "logic pins already in use or not exposed");
            return false;
        }
    }

    pin_base_ = pin_base;
    pin_count_ = pin_count;
    sample_rate_hz_ = sample_rate_hz;
    sample_count_ = sample_count;
    capture_words_ = words;
    trigger_pin_ = trigger_pin;
    trigger_mode_ = trigger_mode;
    trigger_level_ = trigger_level;
    pull_mode_ = pull_mode;
    configured_ = true;
    complete_ = false;
    completion_reported_ = false;
    return true;
}

bool LogicAnalyzer::start(char *err, size_t err_len) {
    if (!configured_) {
        snprintf(err, err_len, "logic analyzer not configured");
        return false;
    }
    if (running_) {
        snprintf(err, err_len, "logic capture is already running");
        return false;
    }

    release_runtime();
    sm_ = pio_claim_unused_sm(pio_, false);
    if (sm_ < 0) {
        snprintf(err, err_len, "no free PIO2 state machine for logic capture");
        return false;
    }
    dma_chan_ = dma_claim_unused_channel(false);
    if (dma_chan_ < 0) {
        pio_sm_unclaim(pio_, sm_);
        sm_ = -1;
        snprintf(err, err_len, "no free DMA channel for logic capture");
        return false;
    }

    uint16_t capture_instr = pio_encode_in(pio_pins, pin_count_);
    pio_program program{};
    program.instructions = &capture_instr;
    program.length = 1;
    program.origin = -1;
    if (!pio_can_add_program(pio_, &program)) {
        release_runtime();
        snprintf(err, err_len, "no PIO instruction memory for logic capture");
        return false;
    }
    offset_ = pio_add_program(pio_, &program);
    program_loaded_ = true;

    apply_pin_pulls();
    for (uint8_t i = 0; i < pin_count_; ++i) {
        pio_gpio_init(pio_, pin_base_ + i);
    }

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_in_pins(&c, pin_base_);
    sm_config_set_wrap(&c, offset_, offset_);
    sm_config_set_clkdiv(&c, static_cast<float>(clk_sys_hz()) / static_cast<float>(sample_rate_hz_));
    sm_config_set_in_shift(&c, true, true, bits_packed_per_word(pin_count_));
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    pio_sm_init(pio_, static_cast<uint>(sm_), offset_, &c);
    pio_sm_set_enabled(pio_, static_cast<uint>(sm_), false);
    pio_sm_clear_fifos(pio_, static_cast<uint>(sm_));
    pio_sm_restart(pio_, static_cast<uint>(sm_));

    std::memset(buffer_, 0, capture_words_ * sizeof(buffer_[0]));
    dma_channel_config dc = dma_channel_get_default_config(static_cast<uint>(dma_chan_));
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_dreq(&dc, pio_get_dreq(pio_, static_cast<uint>(sm_), false));
    dma_channel_configure(static_cast<uint>(dma_chan_),
                          &dc,
                          buffer_,
                          &pio_->rxf[sm_],
                          capture_words_,
                          true);

    bus_ctrl_hw->priority |= BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
    if (trigger_pin_ >= 0) {
        if (trigger_mode_ == LogicTriggerMode::Rising) {
            pio_sm_exec(pio_, static_cast<uint>(sm_), pio_encode_wait_gpio(false, static_cast<uint>(trigger_pin_)));
            pio_sm_exec(pio_, static_cast<uint>(sm_), pio_encode_wait_gpio(true, static_cast<uint>(trigger_pin_)));
        } else if (trigger_mode_ == LogicTriggerMode::Falling) {
            pio_sm_exec(pio_, static_cast<uint>(sm_), pio_encode_wait_gpio(true, static_cast<uint>(trigger_pin_)));
            pio_sm_exec(pio_, static_cast<uint>(sm_), pio_encode_wait_gpio(false, static_cast<uint>(trigger_pin_)));
        } else {
            pio_sm_exec(pio_, static_cast<uint>(sm_), pio_encode_wait_gpio(trigger_level_, static_cast<uint>(trigger_pin_)));
        }
    }
    ++capture_id_;
    running_ = true;
    complete_ = false;
    completion_reported_ = false;
    pio_sm_set_enabled(pio_, static_cast<uint>(sm_), true);
    return true;
}

bool LogicAnalyzer::stop(char *err, size_t err_len) {
    (void)err;
    (void)err_len;
    if (running_ && dma_chan_ >= 0) {
        dma_channel_abort(static_cast<uint>(dma_chan_));
    }
    release_runtime();
    running_ = false;
    complete_ = false;
    completion_reported_ = false;
    return true;
}

bool LogicAnalyzer::release(char *err, size_t err_len) {
    stop(err, err_len);
    release_pins();
    configured_ = false;
    complete_ = false;
    capture_words_ = 0;
    sample_count_ = 0;
    sample_rate_hz_ = 0;
    pin_base_ = 0;
    pin_count_ = 0;
    trigger_pin_ = -1;
    trigger_mode_ = LogicTriggerMode::Level;
    trigger_level_ = true;
    pull_mode_ = LogicPullMode::None;
    completion_reported_ = false;
    return true;
}

void LogicAnalyzer::poll(EventBus &events) {
    if (!running_ || dma_chan_ < 0 || dma_channel_is_busy(static_cast<uint>(dma_chan_))) {
        return;
    }
    if (sm_ >= 0) {
        pio_sm_set_enabled(pio_, static_cast<uint>(sm_), false);
    }
    running_ = false;
    complete_ = true;
    release_runtime();
    if (!completion_reported_) {
        char extra[180];
        snprintf(extra, sizeof(extra),
                 "\"capture_id\":%lu,\"pin_base\":%u,\"pin_count\":%u,\"sample_rate\":%lu,\"samples\":%lu,\"words\":%lu",
                 static_cast<unsigned long>(capture_id_),
                 static_cast<unsigned>(pin_base_),
                 static_cast<unsigned>(pin_count_),
                 static_cast<unsigned long>(sample_rate_hz_),
                 static_cast<unsigned long>(sample_count_),
                 static_cast<unsigned long>(capture_words_));
        events.publish_status("logic", "capture complete", extra);
        completion_reported_ = true;
    }
}

void LogicAnalyzer::status_json(char *out, size_t out_len) const {
    const char *trigger_mode = "level";
    if (trigger_mode_ == LogicTriggerMode::Rising) {
        trigger_mode = "rising";
    } else if (trigger_mode_ == LogicTriggerMode::Falling) {
        trigger_mode = "falling";
    }
    snprintf(out, out_len,
             "\"logic\":{\"configured\":%s,\"running\":%s,\"complete\":%s,\"capture_id\":%lu,\"pin_base\":%u,\"pin_count\":%u,\"sample_rate\":%lu,\"samples\":%lu,\"words\":%lu,\"record_bits\":%lu,\"trigger_pin\":%d,\"trigger_mode\":\"%s\",\"trigger_level\":%s,\"pull\":\"%s\",\"buffer_words_max\":%u,\"buffer_bytes\":%u,\"chunk_bytes\":%u}",
             configured_ ? "true" : "false",
             running_ ? "true" : "false",
             complete_ ? "true" : "false",
             static_cast<unsigned long>(capture_id_),
             static_cast<unsigned>(pin_base_),
             static_cast<unsigned>(pin_count_),
             static_cast<unsigned long>(sample_rate_hz_),
             static_cast<unsigned long>(sample_count_),
             static_cast<unsigned long>(capture_words_),
             static_cast<unsigned long>(bits_packed_per_word(pin_count_ == 0 ? 1 : pin_count_)),
             trigger_pin_,
             trigger_mode,
             trigger_level_ ? "true" : "false",
             logic_pull_name(pull_mode_),
             static_cast<unsigned>(kLogicCaptureWords),
             static_cast<unsigned>(kLogicCaptureWords * sizeof(uint32_t)),
             static_cast<unsigned>(kLogicUploadChunkBytes));
}

void LogicAnalyzer::caps_json(char *out, size_t out_len) const {
    snprintf(out, out_len,
             "\"logic_caps\":{\"engine\":\"pio2_dma\",\"pin_ranges\":[{\"first\":0,\"last\":22},{\"first\":26,\"last\":28}],\"contiguous_pins\":true,\"pin_count_max\":23,\"sample_rate_max\":%lu,\"buffer_words\":%u,\"buffer_bytes\":%u,\"upload_chunk_bytes\":%u,\"encoding\":\"u32-le-packed\",\"capture_mode\":\"capture_then_upload\",\"triggers\":[\"none\",\"level\",\"rising\",\"falling\"],\"pull_modes\":[\"none\",\"up\",\"down\"],\"host_decoders\":[\"summary\",\"edges\",\"uart\",\"spi\",\"i2c\"],\"host_exports\":[\"csv\",\"vcd\"],\"reserved_features\":[\"pattern_trigger\",\"pretrigger\",\"burst\",\"external_psram\"]}",
             static_cast<unsigned long>(clk_sys_hz()),
             static_cast<unsigned>(kLogicCaptureWords),
             static_cast<unsigned>(kLogicCaptureWords * sizeof(uint32_t)),
             static_cast<unsigned>(kLogicUploadChunkBytes));
}

bool LogicAnalyzer::stream_capture(LineSink &reply, size_t offset_words, size_t max_words, char *err, size_t err_len) const {
    if (!complete_) {
        snprintf(err, err_len, "logic capture is not complete");
        return false;
    }
    if (offset_words >= capture_words_) {
        snprintf(err, err_len, "logic offset_words out of range");
        return false;
    }
    size_t remaining = capture_words_ - offset_words;
    if (max_words == 0 || max_words > remaining) {
        max_words = remaining;
    }

    size_t word_offset = offset_words;
    size_t words_left = max_words;
    while (words_left > 0) {
        size_t chunk_words = std::min(words_left, kLogicUploadChunkBytes / sizeof(uint32_t));
        char hex[kLogicUploadChunkBytes * 2 + 1];
        bytes_to_hex(reinterpret_cast<const uint8_t *>(buffer_ + word_offset), chunk_words * sizeof(uint32_t), hex, sizeof(hex));
        char line[kLogicUploadChunkBytes * 2 + 360];
        snprintf(line, sizeof(line),
                 "{\"type\":\"logic\",\"capture_id\":%lu,\"offset_words\":%u,\"words\":%u,\"pin_base\":%u,\"pin_count\":%u,\"sample_rate\":%lu,\"samples\":%lu,\"record_bits\":%lu,\"pull\":\"%s\",\"encoding\":\"u32-le-packed\",\"hex\":\"%s\"}",
                 static_cast<unsigned long>(capture_id_),
                 static_cast<unsigned>(word_offset),
                 static_cast<unsigned>(chunk_words),
                 static_cast<unsigned>(pin_base_),
                 static_cast<unsigned>(pin_count_),
                 static_cast<unsigned long>(sample_rate_hz_),
                 static_cast<unsigned long>(sample_count_),
                 static_cast<unsigned long>(bits_packed_per_word(pin_count_)),
                 logic_pull_name(pull_mode_),
                 hex);
        if (!reply.send_line(line)) {
            snprintf(err, err_len, "logic upload sink blocked");
            return false;
        }
        word_offset += chunk_words;
        words_left -= chunk_words;
    }
    return true;
}

uint32_t LogicAnalyzer::bits_packed_per_word(uint8_t pin_count) {
    if (pin_count == 0) {
        return 32;
    }
    return 32u - (32u % pin_count);
}

uint32_t LogicAnalyzer::clk_sys_hz() const {
    return clock_get_hz(clk_sys);
}

void LogicAnalyzer::release_runtime() {
    if (sm_ >= 0) {
        pio_sm_set_enabled(pio_, static_cast<uint>(sm_), false);
        pio_sm_clear_fifos(pio_, static_cast<uint>(sm_));
    }
    if (dma_chan_ >= 0) {
        dma_channel_cleanup(static_cast<uint>(dma_chan_));
        dma_channel_unclaim(static_cast<uint>(dma_chan_));
        dma_chan_ = -1;
    }
    if (sm_ >= 0) {
        pio_sm_unclaim(pio_, static_cast<uint>(sm_));
        sm_ = -1;
    }
    if (program_loaded_) {
        uint16_t capture_instr = pio_encode_in(pio_pins, pin_count_);
        pio_program program{};
        program.instructions = &capture_instr;
        program.length = 1;
        program.origin = -1;
        pio_remove_program(pio_, &program, offset_);
        program_loaded_ = false;
    }
}

void LogicAnalyzer::release_pins() {
    for (uint8_t i = 0; i < pin_count_; ++i) {
        uint gpio = pin_base_ + i;
        gpio_disable_pulls(gpio);
        gpio_deinit(gpio);
    }
    pins_.release_channel(kOwnerId);
}

void LogicAnalyzer::apply_pin_pulls() const {
    for (uint8_t i = 0; i < pin_count_; ++i) {
        uint gpio = pin_base_ + i;
        gpio_disable_pulls(gpio);
        if (pull_mode_ == LogicPullMode::Up) {
            gpio_pull_up(gpio);
        } else if (pull_mode_ == LogicPullMode::Down) {
            gpio_pull_down(gpio);
        }
    }
}

} // namespace rpmon
