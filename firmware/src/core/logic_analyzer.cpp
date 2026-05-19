#include "rpmon/core/logic_analyzer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "pico/time.h"
#include "rpmon/util/hex.h"

namespace rpmon {

namespace {

LogicAnalyzer *g_ring_dma_owner = nullptr;

void logic_ring_dma_irq_handler() {
    if (g_ring_dma_owner) {
        g_ring_dma_owner->handle_ring_dma_irq();
    }
}

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

const char *logic_trigger_name(LogicTriggerMode mode) {
    switch (mode) {
    case LogicTriggerMode::Rising:
        return "rising";
    case LogicTriggerMode::Falling:
        return "falling";
    case LogicTriggerMode::Pattern:
        return "pattern";
    case LogicTriggerMode::Level:
    default:
        return "level";
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
                              const LogicPullMode *pin_pull_modes,
                              uint32_t pre_samples,
                              uint32_t post_samples,
                              uint32_t search_samples,
                              uint32_t trigger_mask,
                              uint32_t trigger_value,
                              uint8_t burst_count,
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
    if (pre_samples > sample_count) {
        snprintf(err, err_len, "logic pre_samples cannot exceed samples");
        return false;
    }
    bool has_trigger = trigger_mode == LogicTriggerMode::Pattern || trigger_pin >= 0;
    if (post_samples == 0) {
        post_samples = sample_count > pre_samples ? sample_count - pre_samples : sample_count;
    } else if (has_trigger || pre_samples > 0) {
        uint64_t requested_window = static_cast<uint64_t>(pre_samples) + post_samples;
        if (requested_window > 0xffffffffull) {
            snprintf(err, err_len, "logic pre/post sample window is too large");
            return false;
        }
        sample_count = static_cast<uint32_t>(requested_window);
    }
    if (has_trigger && post_samples < 2) {
        snprintf(err, err_len, "logic PIO trigger post_samples must be at least 2");
        return false;
    }
    if (pre_samples > sample_count) {
        snprintf(err, err_len, "logic pre_samples cannot exceed samples");
        return false;
    }
    if (burst_count == 0) {
        burst_count = 1;
    }
    if (burst_count > 1) {
        snprintf(err, err_len, "logic burst_count > 1 requires hardware burst timestamp support; firmware trigger scanning is disabled");
        return false;
    }
    if (burst_count > kLogicBurstMarksMax) {
        snprintf(err, err_len, "logic burst_count max is %u", static_cast<unsigned>(kLogicBurstMarksMax));
        return false;
    }
    uint32_t words = words_for_samples(pin_count, sample_count);
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
    if (trigger_mode == LogicTriggerMode::Pattern) {
        if (trigger_mask == 0) {
            trigger_mask = pin_count >= 32 ? 0xffffffffu : ((1u << pin_count) - 1u);
        }
        uint32_t allowed_mask = pin_count >= 32 ? 0xffffffffu : ((1u << pin_count) - 1u);
        if ((trigger_mask & ~allowed_mask) != 0 || (trigger_value & ~allowed_mask) != 0) {
            snprintf(err, err_len, "logic trigger_mask/value exceed captured pin_count");
            return false;
        }
        if (trigger_mask != allowed_mask) {
            snprintf(err, err_len, "logic PIO pattern trigger requires trigger_mask to cover the captured pin range; reduce pin_count or use mask 0x%lx",
                     static_cast<unsigned long>(allowed_mask));
            return false;
        }
    } else if (trigger_pin >= 0) {
        if (!pins_.is_exposed_gpio(trigger_pin) || trigger_pin < pin_base || trigger_pin >= pin_base + pin_count) {
            snprintf(err, err_len, "logic trigger_pin must be inside the captured pin range");
            return false;
        }
    }
    if (has_trigger) {
        uint32_t cycles_per_sample = trigger_mode == LogicTriggerMode::Pattern ? 5u : 2u;
        uint32_t max_trigger_rate = clk_sys_hz() / cycles_per_sample;
        if (sample_rate_hz > max_trigger_rate) {
            snprintf(err, err_len, "logic %s PIO trigger sample_rate max is %lu Hz",
                     logic_trigger_name(trigger_mode),
                     static_cast<unsigned long>(max_trigger_rate));
            return false;
        }
    }

    bool ring_mode = has_trigger || pre_samples > 0;
    if (ring_mode && !has_trigger) {
        snprintf(err, err_len, "logic pretrigger/burst requires trigger_pin or pattern trigger");
        return false;
    }
    if (!ring_mode) {
        search_samples = sample_count;
    } else {
        bool sample_word_mode = trigger_mode == LogicTriggerMode::Pattern;
        uint32_t max_ring_samples = sample_word_mode
                                        ? static_cast<uint32_t>(kLogicRingWords)
                                        : max_samples_for_words(pin_count, static_cast<uint32_t>(kLogicRingWords));
        uint32_t storage_words = sample_word_mode ? sample_count : words;
        if (storage_words > kLogicRingWords) {
            snprintf(err, err_len, "logic ring window requires %lu words, max is %u",
                     static_cast<unsigned long>(storage_words),
                     static_cast<unsigned>(kLogicRingWords));
            return false;
        }
        search_samples = max_ring_samples;
    }
    uint32_t dma_words = ring_mode ? static_cast<uint32_t>(kLogicRingWords) : words;
    if (dma_words == 0 || dma_words > kLogicCaptureWords) {
        snprintf(err, err_len, "logic search capture requires %lu words, max is %u",
                 static_cast<unsigned long>(dma_words),
                 static_cast<unsigned>(kLogicCaptureWords));
        return false;
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
    dma_words_ = dma_words;
    search_samples_ = search_samples;
    pre_samples_ = pre_samples;
    post_samples_ = post_samples;
    capture_start_sample_ = 0;
    trigger_sample_ = 0;
    trigger_sample_abs_ = 0;
    trigger_mask_ = trigger_mask;
    trigger_value_ = trigger_value & trigger_mask;
    trigger_pin_ = trigger_pin;
    trigger_mode_ = trigger_mode;
    trigger_level_ = trigger_level;
    trigger_found_ = trigger_pin < 0 && trigger_mode != LogicTriggerMode::Pattern;
    pull_mode_ = pull_mode;
    for (uint8_t i = 0; i < sizeof(pin_pull_modes_) / sizeof(pin_pull_modes_[0]); ++i) {
        pin_pull_modes_[i] = i < pin_count && pin_pull_modes ? pin_pull_modes[i] : pull_mode;
    }
    burst_count_ = burst_count;
    burst_found_ = 0;
    std::memset(burst_samples_, 0, sizeof(burst_samples_));
    ring_mode_ = ring_mode;
    pio_trigger_mode_ = false;
    pio_trigger_irq_seen_ = false;
    sample_word_mode_ = ring_mode && trigger_mode == LogicTriggerMode::Pattern;
    ring_dma_count_ = 0;
    ring_words_base_ = 0;
    ring_buffer_ = nullptr;
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
    if (ring_mode_) {
        dma_chan_b_ = dma_claim_unused_channel(false);
        if (dma_chan_b_ < 0) {
            dma_channel_unclaim(static_cast<uint>(dma_chan_));
            dma_chan_ = -1;
            pio_sm_unclaim(pio_, sm_);
            sm_ = -1;
            snprintf(err, err_len, "no second DMA channel for logic ring capture");
            return false;
        }
    }

    std::memset(program_instrs_, 0, sizeof(program_instrs_));
    program_len_ = 0;
    uint8_t capture_instr_index = 0;
    pio_trigger_mode_ = pio_trigger_supported();

    if (pio_pattern_trigger()) {
        program_instrs_[program_len_++] = pio_encode_pull(false, true);
        program_instrs_[program_len_++] = pio_encode_mov(pio_y, pio_osr);
        uint8_t search_loop = program_len_;
        program_instrs_[program_len_++] = pio_encode_mov(pio_isr, pio_null);
        program_instrs_[program_len_++] = pio_encode_in(pio_pins, pin_count_);
        program_instrs_[program_len_++] = pio_encode_mov(pio_x, pio_isr);
        program_instrs_[program_len_++] = pio_encode_push(false, true);
        program_instrs_[program_len_++] = pio_encode_jmp_x_ne_y(search_loop);
        program_instrs_[program_len_++] = pio_encode_pull(false, true);
        program_instrs_[program_len_++] = pio_encode_mov(pio_x, pio_osr);
        uint8_t post_label = program_len_;
        program_instrs_[program_len_++] = pio_encode_mov(pio_isr, pio_null);
        program_instrs_[program_len_++] = pio_encode_in(pio_pins, pin_count_);
        program_instrs_[program_len_++] = pio_encode_push(false, true);
        program_instrs_[program_len_++] = pio_encode_jmp_x_dec(post_label);
        program_instrs_[program_len_++] = pio_encode_irq_set(false, 0);
        uint8_t lock_label = program_len_;
        program_instrs_[program_len_++] = pio_encode_jmp(lock_label);
        capture_instr_index = 0;
    } else if (pio_trigger_mode_) {
        uint8_t post_label = 0;
        uint8_t lock_label = 0;
        program_instrs_[program_len_++] = pio_encode_pull(false, true);
        program_instrs_[program_len_++] = pio_encode_mov(pio_x, pio_osr);
        if (trigger_mode_ == LogicTriggerMode::Rising) {
            uint8_t seek_low = program_len_;
            program_instrs_[program_len_++] = pio_encode_in(pio_pins, pin_count_);
            program_instrs_[program_len_++] = pio_encode_jmp_pin(seek_low);
            uint8_t low_loop = program_len_;
            program_instrs_[program_len_++] = pio_encode_in(pio_pins, pin_count_);
            post_label = program_len_ + 2;
            program_instrs_[program_len_++] = pio_encode_jmp_pin(post_label);
            program_instrs_[program_len_++] = pio_encode_jmp(low_loop);
        } else if (trigger_mode_ == LogicTriggerMode::Falling) {
            uint8_t seek_high = program_len_;
            program_instrs_[program_len_++] = pio_encode_in(pio_pins, pin_count_);
            uint8_t high_loop = program_len_ + 2;
            program_instrs_[program_len_++] = pio_encode_jmp_pin(high_loop);
            program_instrs_[program_len_++] = pio_encode_jmp(seek_high);
            program_instrs_[program_len_++] = pio_encode_in(pio_pins, pin_count_);
            program_instrs_[program_len_++] = pio_encode_jmp_pin(high_loop);
            post_label = program_len_;
        } else if (trigger_level_) {
            uint8_t level_loop = program_len_;
            program_instrs_[program_len_++] = pio_encode_in(pio_pins, pin_count_);
            post_label = program_len_ + 2;
            program_instrs_[program_len_++] = pio_encode_jmp_pin(post_label);
            program_instrs_[program_len_++] = pio_encode_jmp(level_loop);
        } else {
            uint8_t level_loop = program_len_;
            program_instrs_[program_len_++] = pio_encode_in(pio_pins, pin_count_);
            program_instrs_[program_len_++] = pio_encode_jmp_pin(level_loop);
            post_label = program_len_;
        }
        program_instrs_[program_len_++] = pio_encode_in(pio_pins, pin_count_);
        program_instrs_[program_len_++] = pio_encode_jmp_x_dec(post_label);
        program_instrs_[program_len_++] = pio_encode_irq_set(false, 0);
        lock_label = program_len_;
        program_instrs_[program_len_++] = pio_encode_jmp(lock_label);
        capture_instr_index = 0;
    } else if (!ring_trigger_mode() && trigger_pin_ >= 0) {
        if (trigger_mode_ == LogicTriggerMode::Rising) {
            program_instrs_[program_len_++] = pio_encode_wait_gpio(false, static_cast<uint>(trigger_pin_));
            program_instrs_[program_len_++] = pio_encode_wait_gpio(true, static_cast<uint>(trigger_pin_));
        } else if (trigger_mode_ == LogicTriggerMode::Falling) {
            program_instrs_[program_len_++] = pio_encode_wait_gpio(true, static_cast<uint>(trigger_pin_));
            program_instrs_[program_len_++] = pio_encode_wait_gpio(false, static_cast<uint>(trigger_pin_));
        } else {
            program_instrs_[program_len_++] = pio_encode_wait_gpio(trigger_level_, static_cast<uint>(trigger_pin_));
        }
        capture_instr_index = program_len_;
        program_instrs_[program_len_++] = pio_encode_in(pio_pins, pin_count_);
    } else {
        capture_instr_index = program_len_;
        program_instrs_[program_len_++] = pio_encode_in(pio_pins, pin_count_);
    }
    pio_program program{};
    program.instructions = program_instrs_;
    program.length = program_len_;
    program.origin = pio_trigger_mode_ ? 0 : -1;
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
    if (pio_trigger_mode_) {
        sm_config_set_wrap(&c, offset_, offset_ + program_len_ - 1);
        sm_config_set_jmp_pin(&c, static_cast<uint>(trigger_pin_));
    } else {
        sm_config_set_wrap(&c, offset_ + capture_instr_index, offset_ + capture_instr_index);
    }
    float clock_div = static_cast<float>(clk_sys_hz()) / static_cast<float>(sample_rate_hz_);
    if (sample_word_mode_) {
        clock_div /= 5.0f;
    } else if (pio_trigger_mode_) {
        clock_div /= 2.0f;
    }
    sm_config_set_clkdiv(&c, clock_div);
    if (sample_word_mode_) {
        sm_config_set_in_shift(&c, true, false, 32);
    } else {
        sm_config_set_in_shift(&c, true, true, bits_packed_per_word(pin_count_));
    }
    if (!pio_trigger_mode_) {
        sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    }
    pio_sm_init(pio_, static_cast<uint>(sm_), offset_, &c);
    pio_sm_set_enabled(pio_, static_cast<uint>(sm_), false);
    pio_sm_clear_fifos(pio_, static_cast<uint>(sm_));
    pio_sm_restart(pio_, static_cast<uint>(sm_));

    uint32_t *dma_buffer = buffer_;
    if (ring_mode_) {
        ring_buffer_ = aligned_ring_buffer();
        if (!ring_buffer_) {
            release_runtime();
            snprintf(err, err_len, "logic ring buffer alignment failed");
            return false;
        }
        dma_buffer = ring_buffer_;
    }
    std::memset(dma_buffer, 0, dma_words_ * sizeof(dma_buffer[0]));
    dma_channel_config dc = dma_channel_get_default_config(static_cast<uint>(dma_chan_));
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_dreq(&dc, pio_get_dreq(pio_, static_cast<uint>(sm_), false));
    if (ring_mode_) {
        ring_dma_count_ = dma_words_ / 2u;
        if (ring_dma_count_ == 0 || dma_chan_b_ < 0) {
            release_runtime();
            snprintf(err, err_len, "logic ring DMA configuration failed");
            return false;
        }
        ring_halves_completed_ = 0;
        ring_words_base_ = 0;
        if (pio_trigger_mode_) {
            pio_interrupt_clear(pio_, 0);
            pio_trigger_irq_seen_ = false;
        }

        dma_channel_config dc_b = dma_channel_get_default_config(static_cast<uint>(dma_chan_b_));
        channel_config_set_read_increment(&dc_b, false);
        channel_config_set_write_increment(&dc_b, true);
        channel_config_set_transfer_data_size(&dc_b, DMA_SIZE_32);
        channel_config_set_dreq(&dc_b, pio_get_dreq(pio_, static_cast<uint>(sm_), false));
        channel_config_set_chain_to(&dc, static_cast<uint>(dma_chan_b_));
        channel_config_set_chain_to(&dc_b, static_cast<uint>(dma_chan_));

        dma_channel_configure(static_cast<uint>(dma_chan_b_),
                              &dc_b,
                              dma_buffer + ring_dma_count_,
                              &pio_->rxf[sm_],
                              ring_dma_count_,
                              false);
        dma_channel_configure(static_cast<uint>(dma_chan_),
                              &dc,
                              dma_buffer,
                              &pio_->rxf[sm_],
                              ring_dma_count_,
                              false);
        g_ring_dma_owner = this;
        irq_set_exclusive_handler(DMA_IRQ_0, logic_ring_dma_irq_handler);
        dma_channel_set_irq0_enabled(static_cast<uint>(dma_chan_), true);
        dma_channel_set_irq0_enabled(static_cast<uint>(dma_chan_b_), true);
        irq_set_priority(DMA_IRQ_0, 0);
        irq_set_enabled(DMA_IRQ_0, true);
        dma_channel_start(static_cast<uint>(dma_chan_));
    } else {
        dma_channel_configure(static_cast<uint>(dma_chan_),
                              &dc,
                              dma_buffer,
                              &pio_->rxf[sm_],
                              dma_words_,
                              true);
    }

    if (!ring_mode_) {
        bus_ctrl_hw->priority |= BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
    }
    capture_start_sample_ = 0;
    trigger_sample_ = 0;
    trigger_sample_abs_ = 0;
    trigger_found_ = !trigger_enabled();
    burst_found_ = 0;
    std::memset(burst_samples_, 0, sizeof(burst_samples_));
    if (!ring_trigger_mode() && trigger_pin_ >= 0) {
        trigger_found_ = true;
        trigger_sample_ = 0;
    }
    ++capture_id_;
    running_ = true;
    complete_ = false;
    completion_reported_ = false;
    if (pio_trigger_mode_) {
        uint32_t post_extra = post_samples_ > 1 ? post_samples_ - 2u : 0u;
        pio_trigger_irq_seen_ = false;
        if (pio_pattern_trigger()) {
            pio_sm_put_blocking(pio_, static_cast<uint>(sm_), pattern_compare_word());
        }
        pio_sm_put_blocking(pio_, static_cast<uint>(sm_), post_extra);
    }
    pio_sm_set_enabled(pio_, static_cast<uint>(sm_), true);
    return true;
}

bool LogicAnalyzer::stop(char *err, size_t err_len) {
    (void)err;
    (void)err_len;
    if (running_ && dma_chan_ >= 0) {
        dma_channel_abort(static_cast<uint>(dma_chan_));
    }
    if (running_ && dma_chan_b_ >= 0) {
        dma_channel_abort(static_cast<uint>(dma_chan_b_));
    }
    release_runtime();
    running_ = false;
    complete_ = false;
    pio_trigger_irq_seen_ = false;
    completion_reported_ = false;
    return true;
}

bool LogicAnalyzer::release(char *err, size_t err_len) {
    stop(err, err_len);
    release_pins();
    configured_ = false;
    complete_ = false;
    capture_words_ = 0;
    dma_words_ = 0;
    sample_count_ = 0;
    search_samples_ = 0;
    pre_samples_ = 0;
    post_samples_ = 0;
    capture_start_sample_ = 0;
    trigger_sample_ = 0;
    trigger_mask_ = 0;
    trigger_value_ = 0;
    sample_rate_hz_ = 0;
    pin_base_ = 0;
    pin_count_ = 0;
    trigger_pin_ = -1;
    trigger_mode_ = LogicTriggerMode::Level;
    trigger_level_ = true;
    trigger_found_ = false;
    pull_mode_ = LogicPullMode::None;
    for (LogicPullMode &mode : pin_pull_modes_) {
        mode = LogicPullMode::None;
    }
    burst_count_ = 1;
    burst_found_ = 0;
    std::memset(burst_samples_, 0, sizeof(burst_samples_));
    ring_mode_ = false;
    pio_trigger_mode_ = false;
    pio_trigger_irq_seen_ = false;
    sample_word_mode_ = false;
    ring_dma_count_ = 0;
    ring_halves_completed_ = 0;
    ring_words_base_ = 0;
    ring_buffer_ = nullptr;
    completion_reported_ = false;
    return true;
}

void LogicAnalyzer::poll(EventBus &events) {
    if (!running_ || dma_chan_ < 0) {
        return;
    }

    if (ring_trigger_mode()) {
        if (pio_trigger_mode_) {
            if (pio_interrupt_get(pio_, 0)) {
                pio_trigger_irq_seen_ = true;
            }
            if (pio_trigger_irq_seen_ && available_samples() >= post_samples_) {
                char err[120] = {};
                if (finish_pio_trigger_capture(err, sizeof(err))) {
                    finish_capture(&events);
                } else {
                    events.publish_status("logic", err, "\"level\":\"error\"");
                    pio_interrupt_clear(pio_, 0);
                    pio_trigger_irq_seen_ = false;
                    running_ = false;
                    complete_ = false;
                    release_runtime();
                }
            }
            return;
        }
        return;
    }

    if (dma_channel_is_busy(static_cast<uint>(dma_chan_))) {
        return;
    }
    finish_capture(&events);
}

void LogicAnalyzer::status_json(char *out, size_t out_len) const {
    char pin_pulls[420];
    append_pin_pulls_json(pin_pulls, sizeof(pin_pulls));
    snprintf(out, out_len,
             "\"logic\":{\"configured\":%s,\"running\":%s,\"complete\":%s,\"capture_id\":%lu,\"pin_base\":%u,\"pin_count\":%u,\"sample_rate\":%lu,\"samples\":%lu,\"words\":%lu,\"record_bits\":%lu,\"search_samples\":%lu,\"pre_samples\":%lu,\"post_samples\":%lu,\"capture_start_sample\":%llu,\"trigger_found\":%s,\"trigger_sample\":%lu,\"trigger_pin\":%d,\"trigger_mode\":\"%s\",\"trigger_level\":%s,\"trigger_mask\":%lu,\"trigger_value\":%lu,\"burst_count\":%u,\"burst_found\":%u,\"pull\":\"%s\",\"pin_pulls\":%s,\"ring_mode\":%s,\"pio_trigger_mode\":%s,\"pio_trigger_irq\":%s,\"sample_word_mode\":%s,\"ring_dma_mode\":\"%s\",\"ring_dma_halves_completed\":%lu,\"scan_budget_samples\":0,\"scan_dropped_samples\":0,\"buffer_words_max\":%u,\"buffer_bytes\":%u,\"chunk_bytes\":%u}",
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
             static_cast<unsigned long>(search_samples_),
             static_cast<unsigned long>(pre_samples_),
             static_cast<unsigned long>(post_samples_),
             static_cast<unsigned long long>(capture_start_sample_),
             trigger_found_ ? "true" : "false",
             static_cast<unsigned long>(trigger_sample_),
             trigger_pin_,
             logic_trigger_name(trigger_mode_),
             trigger_level_ ? "true" : "false",
             static_cast<unsigned long>(trigger_mask_),
             static_cast<unsigned long>(trigger_value_),
             static_cast<unsigned>(burst_count_),
             static_cast<unsigned>(burst_found_),
             logic_pull_name(pull_mode_),
             pin_pulls,
             ring_mode_ ? "true" : "false",
             pio_trigger_mode_ ? "true" : "false",
             pio_trigger_irq_pending() ? "true" : "false",
             sample_word_mode_ ? "true" : "false",
             ring_mode_ ? "pingpong" : "linear",
             static_cast<unsigned long>(ring_halves_completed_),
             static_cast<unsigned>(kLogicCaptureWords),
             static_cast<unsigned>(kLogicCaptureWords * sizeof(uint32_t)),
             static_cast<unsigned>(kLogicUploadChunkBytes));
}

void LogicAnalyzer::caps_json(char *out, size_t out_len) const {
    snprintf(out, out_len,
             "\"logic_caps\":{\"engine\":\"pio2_dma\",\"pin_ranges\":[{\"first\":0,\"last\":22},{\"first\":26,\"last\":28}],\"contiguous_pins\":true,\"pin_count_max\":23,\"sample_rate_max\":%lu,\"buffer_words\":%u,\"buffer_bytes\":%u,\"ring_buffer_words\":%u,\"ring_buffer_bytes\":%u,\"ring_dma_mode\":\"pingpong\",\"ring_dma_halves\":2,\"upload_chunk_bytes\":%u,\"encoding\":\"u32-le-packed\",\"capture_modes\":[\"single\",\"pretrigger\"],\"triggers\":[\"none\",\"level\",\"rising\",\"falling\",\"pattern\"],\"pull_modes\":[\"none\",\"up\",\"down\"],\"per_pin_pull\":true,\"pin_pull_field\":\"pin_pulls\",\"pretrigger_single_fix\":true,\"ring_pretrigger\":true,\"open_ended_trigger_wait\":true,\"pio_simple_trigger\":true,\"pio_pattern_trigger\":true,\"firmware_trigger_scan\":false,\"pattern_mask_bits_max\":23,\"pattern_mask_full_width\":true,\"burst_marks_max\":1,\"host_decoders\":[\"summary\",\"bursts\",\"edges\",\"uart\",\"spi\",\"i2c\"],\"host_exports\":[\"csv\",\"vcd\"],\"reserved_features\":[\"hardware_burst_marks\",\"external_psram\",\"sigrok_bridge\"]}",
             static_cast<unsigned long>(clk_sys_hz()),
             static_cast<unsigned>(kLogicCaptureWords),
             static_cast<unsigned>(kLogicCaptureWords * sizeof(uint32_t)),
             static_cast<unsigned>(kLogicRingWords),
             static_cast<unsigned>(kLogicRingBytes),
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
    if (offset_words == 0) {
        char bursts[220];
        append_burst_json(bursts, sizeof(bursts));
        char pin_pulls[420];
        append_pin_pulls_json(pin_pulls, sizeof(pin_pulls));
        char meta[1280];
        snprintf(meta, sizeof(meta),
                 "{\"type\":\"logic_meta\",\"capture_id\":%lu,\"pin_base\":%u,\"pin_count\":%u,\"sample_rate\":%lu,\"samples\":%lu,\"record_bits\":%lu,\"sample_offset\":%llu,\"pull\":\"%s\",\"pin_pulls\":%s,\"pre_samples\":%lu,\"post_samples\":%lu,\"trigger_found\":%s,\"trigger_sample\":%lu,\"trigger_pin\":%d,\"trigger_mode\":\"%s\",\"trigger_mask\":%lu,\"trigger_value\":%lu,\"burst_count\":%u,\"burst_found\":%u,\"burst_samples\":%s}",
                 static_cast<unsigned long>(capture_id_),
                 static_cast<unsigned>(pin_base_),
                 static_cast<unsigned>(pin_count_),
                 static_cast<unsigned long>(sample_rate_hz_),
                 static_cast<unsigned long>(sample_count_),
                 static_cast<unsigned long>(bits_packed_per_word(pin_count_)),
                 static_cast<unsigned long long>(capture_start_sample_),
                 logic_pull_name(pull_mode_),
                 pin_pulls,
                 static_cast<unsigned long>(pre_samples_),
                 static_cast<unsigned long>(post_samples_),
                 trigger_found_ ? "true" : "false",
                 static_cast<unsigned long>(trigger_sample_),
                 trigger_pin_,
                 logic_trigger_name(trigger_mode_),
                 static_cast<unsigned long>(trigger_mask_),
                 static_cast<unsigned long>(trigger_value_),
                 static_cast<unsigned>(burst_count_),
                 static_cast<unsigned>(burst_found_),
                 bursts);
        if (!reply.send_line(meta)) {
            snprintf(err, err_len, "logic upload sink blocked");
            return false;
        }
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
        uint32_t packed[kLogicUploadChunkBytes / sizeof(uint32_t)] = {};
        const uint32_t *chunk_source = buffer_ + word_offset;
        if (ring_mode_ || capture_start_sample_ != 0) {
            uint32_t record_bits = bits_packed_per_word(pin_count_);
            for (size_t local_word = 0; local_word < chunk_words; ++local_word) {
                uint32_t absolute_word = static_cast<uint32_t>(word_offset + local_word);
                uint32_t word = 0;
                for (uint32_t bit = 0; bit < record_bits; ++bit) {
                    uint32_t global_bit = absolute_word * record_bits + bit;
                    uint32_t sample = global_bit / pin_count_;
                    if (sample >= sample_count_) {
                        continue;
                    }
                    uint32_t pin = global_bit % pin_count_;
                    if (raw_sample_bits(capture_start_sample_ + sample) & (1u << pin)) {
                        word |= 1u << (bit + 32u - record_bits);
                    }
                }
                packed[local_word] = word;
            }
            chunk_source = packed;
        }
        bytes_to_hex(reinterpret_cast<const uint8_t *>(chunk_source), chunk_words * sizeof(uint32_t), hex, sizeof(hex));
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

uint32_t LogicAnalyzer::words_for_samples(uint8_t pin_count, uint32_t samples) {
    uint32_t record_bits = bits_packed_per_word(pin_count);
    uint64_t total_bits = static_cast<uint64_t>(samples) * pin_count;
    return static_cast<uint32_t>((total_bits + record_bits - 1) / record_bits);
}

uint32_t LogicAnalyzer::max_samples_for_words(uint8_t pin_count, uint32_t words) {
    if (pin_count == 0) {
        return 0;
    }
    return static_cast<uint32_t>((static_cast<uint64_t>(words) * bits_packed_per_word(pin_count)) / pin_count);
}

uint32_t LogicAnalyzer::samples_per_word() const {
    if (sample_word_mode_) {
        return 1;
    }
    return bits_packed_per_word(pin_count_) / pin_count_;
}

uint32_t LogicAnalyzer::clk_sys_hz() const {
    return clock_get_hz(clk_sys);
}

bool LogicAnalyzer::trigger_enabled() const {
    return trigger_mode_ == LogicTriggerMode::Pattern || trigger_pin_ >= 0;
}

bool LogicAnalyzer::ring_trigger_mode() const {
    return ring_mode_;
}

bool LogicAnalyzer::pio_trigger_supported() const {
    return ring_mode_ &&
           burst_count_ <= 1 &&
           post_samples_ > 0 &&
           (trigger_mode_ == LogicTriggerMode::Pattern || trigger_pin_ >= 0);
}

bool LogicAnalyzer::pio_pattern_trigger() const {
    return pio_trigger_mode_ && trigger_mode_ == LogicTriggerMode::Pattern;
}

bool LogicAnalyzer::pio_trigger_irq_pending() const {
    return pio_trigger_mode_ && (pio_trigger_irq_seen_ || pio_interrupt_get(pio_, 0));
}

void LogicAnalyzer::handle_ring_dma_irq() {
    if (!ring_mode_) {
        return;
    }
    if (dma_chan_ >= 0 && dma_channel_get_irq0_status(static_cast<uint>(dma_chan_))) {
        dma_channel_acknowledge_irq0(static_cast<uint>(dma_chan_));
        ++ring_halves_completed_;
        if (ring_buffer_) {
            dma_channel_set_write_addr(static_cast<uint>(dma_chan_), ring_buffer_, false);
            dma_channel_set_trans_count(static_cast<uint>(dma_chan_), ring_dma_count_, false);
        }
    }
    if (dma_chan_b_ >= 0 && dma_channel_get_irq0_status(static_cast<uint>(dma_chan_b_))) {
        dma_channel_acknowledge_irq0(static_cast<uint>(dma_chan_b_));
        ++ring_halves_completed_;
        if (ring_buffer_) {
            dma_channel_set_write_addr(static_cast<uint>(dma_chan_b_), ring_buffer_ + ring_dma_count_, false);
            dma_channel_set_trans_count(static_cast<uint>(dma_chan_b_), ring_dma_count_, false);
        }
    }
}

uint32_t LogicAnalyzer::dma_words_written() const {
    if (dma_chan_ < 0) {
        return dma_words_;
    }
    uintptr_t base = reinterpret_cast<uintptr_t>(buffer_);
    uintptr_t write = dma_channel_hw_addr(static_cast<uint>(dma_chan_))->write_addr;
    if (write < base) {
        return 0;
    }
    uint32_t words = static_cast<uint32_t>((write - base) / sizeof(uint32_t));
    return std::min(words, dma_words_);
}

uint64_t LogicAnalyzer::ring_words_written() const {
    if (!ring_mode_) {
        return dma_words_written();
    }
    if (dma_chan_ < 0 || dma_chan_b_ < 0 || ring_dma_count_ == 0) {
        return static_cast<uint64_t>(ring_halves_completed_) * ring_dma_count_;
    }

    uint32_t halves = ring_halves_completed_;
    uint32_t progress = 0;
    if (dma_channel_is_busy(static_cast<uint>(dma_chan_))) {
        uint32_t remaining = dma_channel_hw_addr(static_cast<uint>(dma_chan_))->transfer_count;
        progress = ring_dma_count_ > remaining ? ring_dma_count_ - remaining : 0;
    } else if (dma_channel_is_busy(static_cast<uint>(dma_chan_b_))) {
        uint32_t remaining = dma_channel_hw_addr(static_cast<uint>(dma_chan_b_))->transfer_count;
        progress = ring_dma_count_ > remaining ? ring_dma_count_ - remaining : 0;
    }
    return static_cast<uint64_t>(halves) * ring_dma_count_ + progress;
}

uint64_t LogicAnalyzer::available_samples() const {
    if (ring_mode_) {
        return ring_words_written() * samples_per_word();
    }
    return max_samples_for_words(pin_count_, dma_words_written());
}

uint64_t LogicAnalyzer::oldest_available_sample() const {
    if (!ring_mode_) {
        return 0;
    }
    uint64_t capacity = static_cast<uint64_t>(dma_words_) * samples_per_word();
    uint64_t ready = available_samples();
    return ready > capacity ? ready - capacity : 0;
}

uint32_t LogicAnalyzer::sample_bits_from_word(uint32_t word, uint32_t sample_in_word) const {
    uint32_t record_bits = sample_word_mode_ ? pin_count_ : bits_packed_per_word(pin_count_);
    uint32_t out = 0;
    for (uint8_t pin = 0; pin < pin_count_; ++pin) {
        uint32_t bit_index = sample_in_word * pin_count_ + pin;
        uint32_t shift = bit_index + 32u - record_bits;
        if (word & (1u << shift)) {
            out |= 1u << pin;
        }
    }
    return out;
}

uint32_t LogicAnalyzer::raw_sample_bits(uint64_t sample) const {
    uint32_t spw = samples_per_word();
    uint64_t word_abs = sample / spw;
    uint32_t sample_in_word = static_cast<uint32_t>(sample % spw);
    uint32_t word_index = ring_mode_ ? static_cast<uint32_t>(word_abs % dma_words_) : static_cast<uint32_t>(word_abs);
    if (word_index >= kLogicCaptureWords) {
        return 0;
    }
    if (sample_word_mode_) {
        return sample_bits_from_word(sample_storage()[word_index], 0);
    }
    return sample_bits_from_word(sample_storage()[word_index], sample_in_word);
}

uint32_t LogicAnalyzer::pattern_compare_word() const {
    if (pin_count_ >= 32) {
        return trigger_value_;
    }
    return trigger_value_ << (32u - pin_count_);
}

bool LogicAnalyzer::finish_pio_trigger_capture(char *err, size_t err_len) {
    busy_wait_us(50);
    uint64_t ready = available_samples();
    if (ready < post_samples_) {
        snprintf(err, err_len, "logic PIO trigger window is not ready");
        return false;
    }
    uint64_t trigger_abs = ready - post_samples_;
    uint64_t start = trigger_abs > pre_samples_ ? trigger_abs - pre_samples_ : 0;
    if (start < oldest_available_sample()) {
        start = oldest_available_sample();
    }
    uint64_t actual_samples = ready - start;
    uint32_t requested = pre_samples_ + post_samples_;
    if (actual_samples > requested) {
        actual_samples = requested;
        start = ready - actual_samples;
    }
    if (actual_samples == 0 || actual_samples > 0xffffffffull) {
        snprintf(err, err_len, "logic PIO trigger window exceeds SRAM buffer");
        return false;
    }
    uint32_t words = words_for_samples(pin_count_, static_cast<uint32_t>(actual_samples));
    if (words == 0 || words > kLogicCaptureWords) {
        snprintf(err, err_len, "logic PIO trigger window exceeds SRAM buffer");
        return false;
    }
    if (sm_ >= 0) {
        pio_sm_set_enabled(pio_, static_cast<uint>(sm_), false);
    }
    if (dma_chan_ >= 0 && dma_channel_is_busy(static_cast<uint>(dma_chan_))) {
        dma_channel_abort(static_cast<uint>(dma_chan_));
    }
    pio_interrupt_clear(pio_, 0);
    pio_trigger_irq_seen_ = false;
    trigger_found_ = true;
    trigger_sample_abs_ = trigger_abs;
    capture_start_sample_ = start;
    sample_count_ = static_cast<uint32_t>(actual_samples);
    capture_words_ = words;
    trigger_sample_ = static_cast<uint32_t>(trigger_abs - start);
    burst_found_ = 1;
    burst_samples_[0] = trigger_sample_;
    release_runtime();
    return true;
}

void LogicAnalyzer::finish_capture(EventBus *events) {
    if (sm_ >= 0) {
        pio_sm_set_enabled(pio_, static_cast<uint>(sm_), false);
    }
    running_ = false;
    complete_ = true;
    release_runtime();
    if (events && !completion_reported_) {
        char extra[260];
        snprintf(extra, sizeof(extra),
                 "\"capture_id\":%lu,\"pin_base\":%u,\"pin_count\":%u,\"sample_rate\":%lu,\"samples\":%lu,\"words\":%lu,\"trigger_found\":%s,\"trigger_sample\":%lu,\"burst_found\":%u",
                 static_cast<unsigned long>(capture_id_),
                 static_cast<unsigned>(pin_base_),
                 static_cast<unsigned>(pin_count_),
                 static_cast<unsigned long>(sample_rate_hz_),
                 static_cast<unsigned long>(sample_count_),
                 static_cast<unsigned long>(capture_words_),
                 trigger_found_ ? "true" : "false",
                 static_cast<unsigned long>(trigger_sample_),
                 static_cast<unsigned>(burst_found_));
        events->publish_status("logic", "capture complete", extra);
        completion_reported_ = true;
    }
}

void LogicAnalyzer::append_burst_json(char *out, size_t out_len) const {
    if (!out || out_len == 0) {
        return;
    }
    size_t pos = 0;
    int written = snprintf(out, out_len, "[");
    if (written < 0) {
        out[0] = '\0';
        return;
    }
    pos = static_cast<size_t>(written);
    for (uint8_t i = 0; i < burst_found_; ++i) {
        written = snprintf(out + pos, pos < out_len ? out_len - pos : 0,
                           "%s%llu",
                           i == 0 ? "" : ",",
                           static_cast<unsigned long long>(burst_samples_[i]));
        if (written < 0) {
            break;
        }
        pos += static_cast<size_t>(written);
        if (pos >= out_len) {
            break;
        }
    }
    snprintf(out + (pos < out_len ? pos : out_len - 1), pos < out_len ? out_len - pos : 1, "]");
}

void LogicAnalyzer::append_pin_pulls_json(char *out, size_t out_len) const {
    if (!out || out_len == 0) {
        return;
    }
    size_t pos = 0;
    int written = snprintf(out, out_len, "{");
    if (written < 0) {
        out[0] = '\0';
        return;
    }
    pos = static_cast<size_t>(written);
    for (uint8_t i = 0; i < pin_count_; ++i) {
        written = snprintf(out + pos, pos < out_len ? out_len - pos : 0,
                           "%s\"%u\":\"%s\"",
                           i == 0 ? "" : ",",
                           static_cast<unsigned>(pin_base_ + i),
                           logic_pull_name(pin_pull_modes_[i]));
        if (written < 0) {
            break;
        }
        pos += static_cast<size_t>(written);
        if (pos >= out_len) {
            break;
        }
    }
    snprintf(out + (pos < out_len ? pos : out_len - 1), pos < out_len ? out_len - pos : 1, "}");
}

uint32_t *LogicAnalyzer::aligned_ring_buffer() {
    uintptr_t base = reinterpret_cast<uintptr_t>(buffer_);
    uintptr_t end = base + kLogicCaptureWords * sizeof(uint32_t);
    uintptr_t aligned = (base + kLogicRingBytes - 1u) & ~(static_cast<uintptr_t>(kLogicRingBytes) - 1u);
    if (aligned + kLogicRingBytes > end) {
        return nullptr;
    }
    return reinterpret_cast<uint32_t *>(aligned);
}

const uint32_t *LogicAnalyzer::sample_storage() const {
    return ring_mode_ && ring_buffer_ ? ring_buffer_ : buffer_;
}

void LogicAnalyzer::release_runtime() {
    bus_ctrl_hw->priority &= ~(BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS);
    if (g_ring_dma_owner == this) {
        irq_set_enabled(DMA_IRQ_0, false);
        if (dma_chan_ >= 0) {
            dma_channel_set_irq0_enabled(static_cast<uint>(dma_chan_), false);
            dma_channel_acknowledge_irq0(static_cast<uint>(dma_chan_));
        }
        if (dma_chan_b_ >= 0) {
            dma_channel_set_irq0_enabled(static_cast<uint>(dma_chan_b_), false);
            dma_channel_acknowledge_irq0(static_cast<uint>(dma_chan_b_));
        }
        g_ring_dma_owner = nullptr;
    }
    if (sm_ >= 0) {
        pio_sm_set_enabled(pio_, static_cast<uint>(sm_), false);
        pio_sm_clear_fifos(pio_, static_cast<uint>(sm_));
    }
    if (dma_chan_ >= 0) {
        if (dma_channel_is_busy(static_cast<uint>(dma_chan_))) {
            dma_channel_abort(static_cast<uint>(dma_chan_));
        }
        dma_channel_cleanup(static_cast<uint>(dma_chan_));
        dma_channel_unclaim(static_cast<uint>(dma_chan_));
        dma_chan_ = -1;
    }
    if (dma_chan_b_ >= 0) {
        if (dma_channel_is_busy(static_cast<uint>(dma_chan_b_))) {
            dma_channel_abort(static_cast<uint>(dma_chan_b_));
        }
        dma_channel_cleanup(static_cast<uint>(dma_chan_b_));
        dma_channel_unclaim(static_cast<uint>(dma_chan_b_));
        dma_chan_b_ = -1;
    }
    if (sm_ >= 0) {
        pio_sm_unclaim(pio_, static_cast<uint>(sm_));
        sm_ = -1;
    }
    if (program_loaded_) {
        pio_program program{};
        program.instructions = program_instrs_;
        program.length = program_len_ == 0 ? 1 : program_len_;
        program.origin = pio_trigger_mode_ ? 0 : -1;
        pio_remove_program(pio_, &program, offset_);
        program_loaded_ = false;
        program_len_ = 0;
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
        if (pin_pull_modes_[i] == LogicPullMode::Up) {
            gpio_pull_up(gpio);
        } else if (pin_pull_modes_[i] == LogicPullMode::Down) {
            gpio_pull_down(gpio);
        }
    }
}

} // namespace rpmon
