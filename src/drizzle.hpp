// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#pragma once

#include "controls.hpp"
#include "ports.hpp"
#include "rate_bridge.hpp"

#include "beads/dsp/processor.h"
#include "stmlib/dsp/delay_line.h"
#include "stmlib/utils/gate_flags.h"

#include <lv2/core/lv2.h>

#include <cstdint>
#include <vector>

namespace drizzle {

extern const LV2_Descriptor descriptor;
extern const LV2_Descriptor synth_descriptor;

// beads.cc: delay_buffer_1, delay_buffer_2 and reverb_buffer.
inline constexpr uint32_t kDelayBuffer1Size = 512 * 1024;
inline constexpr uint32_t kDelayBuffer2Size = 256 * 1024;
inline constexpr uint32_t kReverbSize = 32768;
// stmlib::Random's initial state upstream (stmlib/utils/random.cc).
inline constexpr uint32_t kRandomSeed = 0x21;
// The processor rates (Processor::sample_rate() with ENABLE_HARDWARE_24K):
// dirty tape at 24 kHz, vintage digital at 32 kHz, the others at 48 kHz.
inline constexpr uint32_t kProcessorRates[3] = {24000, 32000, 48000};

class Drizzle {
public:
    static Drizzle* create(double rate) noexcept;
    ~Drizzle();
    Drizzle(const Drizzle&) = delete;
    Drizzle& operator=(const Drizzle&) = delete;

    void connect(uint32_t index, void* data) noexcept;
    void activate() noexcept;
    void run(uint32_t frames) noexcept;

private:
    // Changing Quality: fade out, clear the delay buffer a slice at a time,
    // Configure, fade in.
    enum class Switch { running, fading_out, clearing, fading_in };

    explicit Drizzle(double rate) noexcept;
    float control(Port port, float fallback, float minimum, float maximum) const noexcept;
    float input(Port port, uint32_t frame) const noexcept;
    bool gate(Port port, uint32_t frame) const noexcept;
    beads::Processor::Media requested_media() const noexcept;
    void configure(beads::Processor::Media media, bool clear) noexcept;
    void clear_slice() noexcept;
    void process_block(BlockBuffer& block, uint32_t frame) noexcept;

    double rate_;
    float smoothing_;
    float ramp_step_;
    PortArray ports_{};
    beads::Processor* processor_ = nullptr;
    uint8_t* delay_buffer_1_ = nullptr;
    uint8_t* delay_buffer_2_ = nullptr;
    uint16_t* reverb_ = nullptr;
    RateBridge bridges_[3];
    RateBridge* bridge_ = nullptr;
    // Every quality has the latency of the slowest path: the others are
    // delayed to match, so changing Quality doesn't shift the timing.
    uint32_t latency_ = 0;
    std::vector<beads::FloatFrame> pad_;
    uint32_t pad_length_ = 0;
    uint32_t pad_position_ = 0;
    Controls controls_;
    BlockControls block_controls_{};
    uint32_t random_state_ = kRandomSeed;

    beads::Processor::Media media_ = beads::Processor::MEDIA_DIGITAL;
    beads::Processor::Media next_media_ = beads::Processor::MEDIA_DIGITAL;
    Switch switch_ = Switch::running;
    float switch_gain_ = 1;
    uint32_t clear_offset_ = 0;
    uint32_t priming_ = 0;

    // beads.cc: event_delay and previous_seed (the Seed gate input is read
    // 2.5 ms late).
    stmlib::DelayLine<uint8_t, 256> seed_delay_;
    stmlib::GateFlags previous_seed_ = stmlib::GATE_FLAG_LOW;

    float output_gain_ = 1;
    // 1 when on, 0 when bypassed: the input goes round the processor, which
    // keeps playing its tails.
    float enabled_gain_ = 1;
    bool first_ = true;
};

} // namespace drizzle
