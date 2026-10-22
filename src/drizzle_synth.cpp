// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
//
// Drizzle Synth: Beads' synth mode (dsp/synth.cc), which the hardware falls
// into when nothing is patched in its audio inputs, as a sound source of its
// own. beads.cc runs it at 48 kHz in 12-frame blocks with the same controls
// as the processor: Feedback picks the wavetable bank, Dry/wet balances the
// oscillator against the grains and Quality sets the bit depth.
#include "drizzle.hpp"

#include "beads/dsp/synth.h"
#include "stmlib/utils/random.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace drizzle {
namespace {

// beads.cc: ConfigureAndStartCodec runs the synth at 48 kHz in any quality.
constexpr uint32_t kSynthRate = 48000;

class Synth {
public:
    static Synth* create(double rate) noexcept
    {
        auto* synth = new (std::nothrow) Synth(rate);
        if (!synth) { return nullptr; }
        // As the processor (drizzle.cpp): built by activate() in zeroed memory.
        void* memory = std::calloc(1, sizeof(beads::Synth));
        synth->reverb_ = new (std::nothrow) uint16_t[kReverbSize];
        bool ok = memory && synth->reverb_ &&
                  synth->bridge_.configure(static_cast<uint32_t>(std::llround(rate)), kSynthRate);
        ok = ok && synth->grain_output_.configure(synth->bridge_.latency(), static_cast<uint32_t>(kTriggerSeconds * rate));
        if (!ok) {
            std::free(memory);
            delete synth;
            return nullptr;
        }
        std::memset(synth->reverb_, 0, kReverbSize * sizeof(uint16_t));
        synth->synth_ = static_cast<beads::Synth*>(memory);
        synth->activate();
        return synth;
    }

    ~Synth()
    {
        if (synth_) {
            std::destroy_at(synth_);
            std::free(synth_);
        }
        delete[] reverb_;
    }

    Synth(const Synth&) = delete;
    Synth& operator=(const Synth&) = delete;

    void connect(uint32_t index, void* data) noexcept
    {
        // The control code reads Drizzle's port numbers (ports.hpp).
        if (index < kSynthPortCount) { ports_[static_cast<uint32_t>(kSynthToPort[index])] = static_cast<float*>(data); }
    }

    // Not real-time: Init clears the reverb buffer.
    void activate() noexcept
    {
        std::memset(static_cast<void*>(synth_), 0, sizeof(beads::Synth));
        synth_ = new (synth_) beads::Synth;
        synth_->Init(reverb_);
        bridge_.reset();
        controls_.reset();
        seed_gate_.reset();
        // The gate is one block late, as the audio is.
        grain_output_.reset(bridge_.latency() - std::min(bridge_.latency(), bridge_.gate_latency()));
        random_state_ = kRandomSeed;
        first_ = true;
    }

    // Real-time section: no allocation, locks or I/O.
    void run(uint32_t frames) noexcept
    {
        const float output_target = std::pow(10.0f, control(Port::output, 0, -24, 12) / 20.0f);
        if (first_) {
            output_gain_ = output_target;
            first_ = false;
        }
        float* out_l = ports_[static_cast<uint32_t>(Port::out_l)];
        float* out_r = ports_[static_cast<uint32_t>(Port::out_r)];
        float* grain_out = ports_[static_cast<uint32_t>(Port::grain_out)];
        for (uint32_t i = 0; i < frames; ++i) {
            output_gain_ += smoothing_ * (output_target - output_gain_);
            const float silence[2] = {0.0f, 0.0f};
            float out[2];
            bool grain;
            bridge_.frame(silence, gate(Port::freeze_gate, i), gate(Port::seed_gate, i), out, &grain,
                          [this, i](BlockBuffer& block) { render_block(block, i); });
            if (out_l) { out_l[i] = out[0] * output_gain_; }
            if (out_r) { out_r[i] = out[1] * output_gain_; }
            const float grain_cv = grain_output_.frame(grain, block_controls_.grain_trigger);
            if (grain_out) { grain_out[i] = grain_cv; }
        }
        if (float* latency = ports_[static_cast<uint32_t>(Port::latency)]) {
            *latency = static_cast<float>(bridge_.latency());
        }
    }

private:
    explicit Synth(double rate) noexcept : smoothing_(static_cast<float>(1.0 - std::exp(-1.0 / (0.01 * rate)))) {}

    float control(Port port, float fallback, float minimum, float maximum) const noexcept
    {
        const float* data = ports_[static_cast<uint32_t>(port)];
        return data && std::isfinite(*data) ? std::clamp(*data, minimum, maximum) : fallback;
    }

    bool gate(Port port, uint32_t frame) const noexcept
    {
        const float* data = ports_[static_cast<uint32_t>(port)];
        return data && data[frame] >= kGateThreshold;
    }

    // beads.cc: Process, in synth mode.
    void render_block(BlockBuffer& block, uint32_t frame) noexcept
    {
        controls_.read(ports_, frame, &block_controls_);
        const BlockControls& c = block_controls_;
        const uint32_t size = block.size;

        bool freeze[BlockBuffer::kMaxSize];
        for (uint32_t i = 0; i < size; ++i) { freeze[i] = c.freeze_button || block.freeze[i]; }
        stmlib::GateFlags seed[BlockBuffer::kMaxSize];
        const stmlib::GateFlags* seed_input = seed_gate_.process(c.seed_uses_gate, block.seed, size, seed);

        // Synth::Resolution follows the quality setting, in the same order
        // as Processor::Media (settings.cc's default 3: 16 bit).
        const auto resolution =
            static_cast<beads::Synth::Resolution>(std::lround(control(Port::quality, 3, 0, 3)));

        stmlib::Random::Seed(random_state_);
        synth_->Render(c.seed_button, seed_input, freeze, resolution, c.density, c.parameters, c.mix, block.out, size,
                       block.gate);
        random_state_ = stmlib::Random::state();

        // beads.cc: the output is clamped before it goes to the codec.
        for (uint32_t i = 0; i < size; ++i) {
            float& l = block.out[i].l;
            float& r = block.out[i].r;
            l = std::isfinite(l) ? std::clamp(l, -1.0f, 0.999999f) : 0.0f;
            r = std::isfinite(r) ? std::clamp(r, -1.0f, 0.999999f) : 0.0f;
        }
    }

    float smoothing_;
    PortArray ports_{};
    beads::Synth* synth_ = nullptr;
    uint16_t* reverb_ = nullptr;
    RateBridge bridge_;
    Controls controls_;
    BlockControls block_controls_{};
    SeedGate seed_gate_;
    GrainOutput grain_output_;
    uint32_t random_state_ = kRandomSeed;
    float output_gain_ = 1;
    bool first_ = true;
};

LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*, const LV2_Feature* const*)
{
    if (!std::isfinite(rate) || rate < 8000 || rate > 768000) { return nullptr; }
    return Synth::create(rate);
}
void connect_port(LV2_Handle h, uint32_t index, void* data) { static_cast<Synth*>(h)->connect(index, data); }
void activate(LV2_Handle h) { static_cast<Synth*>(h)->activate(); }
void run(LV2_Handle h, uint32_t frames) { static_cast<Synth*>(h)->run(frames); }
void cleanup(LV2_Handle h) { delete static_cast<Synth*>(h); }
const void* extension_data(const char*) { return nullptr; }

} // namespace

const LV2_Descriptor synth_descriptor{
    kSynthURI, instantiate, connect_port, activate, run, nullptr, cleanup, extension_data,
};

} // namespace drizzle
