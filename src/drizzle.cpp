// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#include "drizzle.hpp"

#include "beads/dsp/mu_law.h"
#include "stmlib/utils/random.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

namespace drizzle {
namespace {

// A Quality change fades out over 5 ms, clears the 768 KB delay buffer 1 KB
// per host frame (16 ms at 48 kHz, a few microseconds per period), waits for
// the new path to fill, then fades in over 5 ms: about 40 ms in all. The
// hardware stops its codec and mutes for 0.25 s.
constexpr double kSwitchFadeSeconds = 0.005;
constexpr uint32_t kClearBytesPerFrame = 1024;
constexpr double kPrimeSeconds = 0.01;
constexpr uint32_t kDelayBufferSize = kDelayBuffer1Size + kDelayBuffer2Size;
// delay_line.h: the freeze tail, 256 FloatFrames at the end of the last bank.
constexpr uint32_t kFreezeTailBytes = 256 * sizeof(beads::FloatFrame);
// The bypass crossfade.
constexpr double kBypassFadeSeconds = 0.01;
// Peepo's gates are 0 or 1; Chamber and Poly's ports use the same threshold.
constexpr float kGateThreshold = 0.4f;

LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*, const LV2_Feature* const*)
{
    if (!std::isfinite(rate) || rate < 8000 || rate > 768000) { return nullptr; }
    return Drizzle::create(rate);
}
void connect_port(LV2_Handle h, uint32_t index, void* data) { static_cast<Drizzle*>(h)->connect(index, data); }
void activate(LV2_Handle h) { static_cast<Drizzle*>(h)->activate(); }
void run(LV2_Handle h, uint32_t frames) { static_cast<Drizzle*>(h)->run(frames); }
void cleanup(LV2_Handle h) { delete static_cast<Drizzle*>(h); }
const void* extension_data(const char*) { return nullptr; }

float db_to_gain(float db) { return std::pow(10.0f, db / 20.0f); }

} // namespace

const LV2_Descriptor descriptor{
    kURI, instantiate, connect_port, activate, run, nullptr, cleanup, extension_data,
};

Drizzle* Drizzle::create(double rate) noexcept
{
    auto* drizzle = new (std::nothrow) Drizzle(rate);
    if (!drizzle) { return nullptr; }
    // Processor's constructor leaves its members as they are; on the hardware
    // it is a zero-initialised global. activate() builds it in zeroed memory.
    void* memory = std::calloc(1, sizeof(beads::Processor));
    drizzle->delay_buffer_1_ = new (std::nothrow) uint8_t[kDelayBuffer1Size];
    drizzle->delay_buffer_2_ = new (std::nothrow) uint8_t[kDelayBuffer2Size];
    drizzle->reverb_ = new (std::nothrow) uint16_t[kReverbSize];
    bool ok = memory && drizzle->delay_buffer_1_ && drizzle->delay_buffer_2_ && drizzle->reverb_;
    for (uint32_t i = 0; ok && i < 3; ++i) {
        ok = drizzle->bridges_[i].configure(static_cast<uint32_t>(std::llround(rate)), kProcessorRates[i]);
        if (ok) { drizzle->latency_ = std::max(drizzle->latency_, drizzle->bridges_[i].latency()); }
    }
    if (ok) {
        try {
            drizzle->pad_.assign(drizzle->latency_ + 1, beads::FloatFrame{0.0f, 0.0f});
        } catch (const std::bad_alloc&) {
            ok = false;
        }
    }
    if (!ok) {
        std::free(memory);
        delete drizzle;
        return nullptr;
    }
    // Touch every page now, so the first run() does not fault them in.
    std::memset(drizzle->delay_buffer_1_, 0, kDelayBuffer1Size);
    std::memset(drizzle->delay_buffer_2_, 0, kDelayBuffer2Size);
    std::memset(drizzle->reverb_, 0, kReverbSize * sizeof(uint16_t));
    drizzle->processor_ = static_cast<beads::Processor*>(memory);
    drizzle->activate();
    return drizzle;
}

Drizzle::Drizzle(double rate) noexcept
    : rate_(rate), smoothing_(static_cast<float>(1.0 - std::exp(-1.0 / (0.01 * rate)))),
      ramp_step_(static_cast<float>(1.0 / (kSwitchFadeSeconds * rate)))
{
}

Drizzle::~Drizzle()
{
    if (processor_) {
        processor_->~Processor();
        std::free(processor_);
    }
    delete[] delay_buffer_1_;
    delete[] delay_buffer_2_;
    delete[] reverb_;
}

void Drizzle::connect(uint32_t index, void* data) noexcept
{
    if (index < kPortCount) { ports_[index] = static_cast<float*>(data); }
}

// Not real-time: Init and Configure clear the whole delay buffer.
void Drizzle::activate() noexcept
{
    std::memset(static_cast<void*>(processor_), 0, sizeof(beads::Processor));
    processor_ = new (processor_) beads::Processor;
    uint8_t* buffers[3] = {delay_buffer_1_, delay_buffer_1_ + kDelayBuffer1Size / 2, delay_buffer_2_};
    processor_->Init(buffers, reverb_);
    configure(requested_media(), true);
    controls_.reset();
    random_state_ = kRandomSeed;
    switch_ = Switch::running;
    switch_gain_ = 1;
    first_ = true;
}

float Drizzle::control(Port port, float fallback, float minimum, float maximum) const noexcept
{
    const float* data = ports_[static_cast<uint32_t>(port)];
    return data && std::isfinite(*data) ? std::clamp(*data, minimum, maximum) : fallback;
}

float Drizzle::input(Port port, uint32_t frame) const noexcept
{
    const float* data = ports_[static_cast<uint32_t>(port)];
    // NaN or infinity reads as silence; the codec clips at full scale.
    return data && std::isfinite(data[frame]) ? std::clamp(data[frame], -1.0f, 1.0f) : 0.0f;
}

bool Drizzle::gate(Port port, uint32_t frame) const noexcept
{
    const float* data = ports_[static_cast<uint32_t>(port)];
    return data && data[frame] >= kGateThreshold;
}

beads::Processor::Media Drizzle::requested_media() const noexcept
{
    // settings.cc: the hardware's default media is 3, digital.
    return static_cast<beads::Processor::Media>(std::lround(control(Port::quality, 3, 0, 3)));
}

void Drizzle::configure(beads::Processor::Media media, bool clear) noexcept
{
    // The Mono switch is milestone 2.
    processor_->Configure(media, false, clear);
    media_ = media;
    const auto rate = static_cast<uint32_t>(std::lround(processor_->sample_rate()));
    for (auto& bridge : bridges_) {
        if (bridge.rate() == rate) { bridge_ = &bridge; }
    }
    bridge_->reset();
    pad_length_ = latency_ - bridge_->latency();
    std::fill(pad_.begin(), pad_.end(), beads::FloatFrame{0.0f, 0.0f});
    pad_position_ = 0;
    // ConfigureAndStartCodec: event_delay.Init().
    seed_delay_.Init();
    previous_seed_ = stmlib::GATE_FLAG_LOW;
}

// What DelayLine::Reset(true) writes, a slice at a time: every byte of the
// three banks is the empty item of the new quality (mu-law's zero in dirty
// tape, 0 in the others), and the freeze tail is float zeros.
void Drizzle::clear_slice() noexcept
{
    const uint8_t zero = next_media_ == beads::Processor::MEDIA_DIRTY_TAPE ? beads::Lin2MuLaw(0) : 0;
    const uint32_t end = std::min(clear_offset_ + kClearBytesPerFrame, kDelayBufferSize);
    for (uint32_t offset = clear_offset_; offset < end;) {
        uint8_t* bytes;
        uint32_t count;
        if (offset < kDelayBuffer1Size) {
            bytes = delay_buffer_1_ + offset;
            count = std::min(end, kDelayBuffer1Size) - offset;
        } else {
            bytes = delay_buffer_2_ + (offset - kDelayBuffer1Size);
            count = end - offset;
        }
        std::memset(bytes, zero, count);
        offset += count;
    }
    clear_offset_ = end;
    if (clear_offset_ == kDelayBufferSize) {
        std::memset(delay_buffer_2_ + kDelayBuffer2Size - kFreezeTailBytes, 0, kFreezeTailBytes);
    }
}

// Real-time section: no allocation, locks or I/O.
void Drizzle::run(uint32_t frames) noexcept
{
    const float output_target = db_to_gain(control(Port::output, 0, -24, 12));
    const float enabled_target = control(Port::enabled, 1, 0, 1) >= 0.5f ? 1.0f : 0.0f;
    const float enabled_step = static_cast<float>(1.0 / (kBypassFadeSeconds * rate_));
    const auto requested = requested_media();
    if (switch_ == Switch::running && requested != media_) {
        next_media_ = requested;
        clear_offset_ = 0;
        // Before the first run nothing has been heard: no fade out.
        switch_ = first_ ? Switch::clearing : Switch::fading_out;
        if (first_) { switch_gain_ = 0; }
    }
    if (first_) {
        output_gain_ = output_target;
        enabled_gain_ = enabled_target;
        first_ = false;
    }
    float* out_l = ports_[static_cast<uint32_t>(Port::out_l)];
    float* out_r = ports_[static_cast<uint32_t>(Port::out_r)];

    for (uint32_t i = 0; i < frames; ++i) {
        output_gain_ += smoothing_ * (output_target - output_gain_);
        enabled_gain_ = enabled_target > enabled_gain_ ? std::min(enabled_target, enabled_gain_ + enabled_step)
                                                       : std::max(enabled_target, enabled_gain_ - enabled_step);
        const float in[2] = {input(Port::in_l, i), input(Port::in_r, i)};

        switch (switch_) {
        case Switch::fading_out:
            switch_gain_ -= ramp_step_;
            if (switch_gain_ <= 0.0f) {
                switch_gain_ = 0;
                switch_ = Switch::clearing;
            }
            break;
        case Switch::clearing:
            clear_slice();
            if (clear_offset_ == kDelayBufferSize) {
                configure(next_media_, false);
                switch_ = Switch::fading_in;
                // The new path's first output arrives one latency later, and
                // dirty tape's wow and flutter delay (up to 128 frames at
                // 24 kHz) fills after that.
                priming_ = latency_ + static_cast<uint32_t>(kPrimeSeconds * rate_);
            }
            break;
        case Switch::fading_in:
            if (priming_) {
                --priming_;
                break;
            }
            switch_gain_ += ramp_step_;
            if (switch_gain_ >= 1.0f) {
                switch_gain_ = 1;
                switch_ = Switch::running;
            }
            break;
        case Switch::running:
            break;
        }

        beads::FloatFrame wet{0.0f, 0.0f};
        if (switch_ != Switch::clearing) {
            const float processor_in[2] = {in[0] * enabled_gain_, in[1] * enabled_gain_};
            float processor_out[2];
            // A block completed in host frame i uses that frame's CVs.
            bridge_->frame(processor_in, gate(Port::freeze_gate, i), gate(Port::seed_gate, i), processor_out,
                           [this, i](BlockBuffer& block) { process_block(block, i); });
            wet = {processor_out[0], processor_out[1]};
            if (pad_length_) {
                std::swap(wet, pad_[pad_position_]);
                if (++pad_position_ == pad_length_) { pad_position_ = 0; }
            }
        }
        const float dry = 1.0f - enabled_gain_;
        if (out_l) { out_l[i] = (wet.l * switch_gain_ + in[0] * dry) * output_gain_; }
        if (out_r) { out_r[i] = (wet.r * switch_gain_ + in[1] * dry) * output_gain_; }
    }
    // The grain trigger or gate output is milestone 2.
    if (float* grain_out = ports_[static_cast<uint32_t>(Port::grain_out)]) {
        std::fill(grain_out, grain_out + frames, 0.0f);
    }
    if (float* latency = ports_[static_cast<uint32_t>(Port::latency)]) {
        *latency = static_cast<float>(latency_);
    }
}

void Drizzle::process_block(BlockBuffer& block, uint32_t frame) noexcept
{
    controls_.read(ports_, frame, &block_controls_);
    const BlockControls& c = block_controls_;
    const uint32_t size = block.size;

    // beads.cc: Process. Freeze is the button or the gate, per frame.
    bool freeze[BlockBuffer::kMaxSize];
    for (uint32_t i = 0; i < size; ++i) { freeze[i] = c.freeze_button || block.freeze[i]; }

    // The Seed gate, delayed by 10 blocks (2.5 ms), is only given to the
    // processor when its jack is patched; here, when Seed uses gate is on.
    stmlib::GateFlags seed[BlockBuffer::kMaxSize];
    const stmlib::GateFlags* seed_input = nullptr;
    if (c.seed_uses_gate) {
        for (uint32_t i = 0; i < size; ++i) {
            seed_delay_.Write(block.seed[i] ? 1 : 0);
            const bool has_seed = seed_delay_.Read(static_cast<size_t>(size) * 10) != 0;
            previous_seed_ = seed[i] = stmlib::ExtractGateFlags(previous_seed_, has_seed);
        }
        seed_input = seed;
    } else {
        previous_seed_ = stmlib::GATE_FLAG_LOW;
    }

    // stmlib::Random is one state per thread (third_party/eurorack/README.md);
    // each instance keeps its own sequence.
    stmlib::Random::Seed(random_state_);
    processor_->Process(c.seed_button, seed_input, freeze, c.density, c.parameters, c.mix, block.in, block.out, size);
    random_state_ = stmlib::Random::state();

    // beads.cc: the output is clamped before it goes to the codec.
    for (uint32_t i = 0; i < size; ++i) {
        float& l = block.out[i].l;
        float& r = block.out[i].r;
        l = std::isfinite(l) ? std::clamp(l, -1.0f, 0.999999f) : 0.0f;
        r = std::isfinite(r) ? std::clamp(r, -1.0f, 0.999999f) : 0.0f;
    }
}

} // namespace drizzle
