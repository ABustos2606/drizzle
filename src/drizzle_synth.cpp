// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
//
// Drizzle Synth. Milestone 2 runs Beads' synth here; until then the plugin
// loads, keeps its full port list and plays silence.
#include "drizzle.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <new>

namespace drizzle {
namespace {

class Synth {
public:
    void connect(uint32_t index, void* data) noexcept
    {
        if (index < kSynthPortCount) { ports_[index] = static_cast<float*>(data); }
    }

    void run(uint32_t frames) noexcept
    {
        for (SynthPort p : {SynthPort::out_l, SynthPort::out_r, SynthPort::grain_out}) {
            if (float* data = ports_[static_cast<uint32_t>(p)]) { std::fill(data, data + frames, 0.0f); }
        }
        if (float* latency = ports_[static_cast<uint32_t>(SynthPort::latency)]) { *latency = 0.0f; }
    }

private:
    std::array<float*, kSynthPortCount> ports_{};
};

LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*, const LV2_Feature* const*)
{
    if (!std::isfinite(rate) || rate < 8000 || rate > 768000) { return nullptr; }
    return new (std::nothrow) Synth;
}
void connect_port(LV2_Handle h, uint32_t index, void* data) { static_cast<Synth*>(h)->connect(index, data); }
void run(LV2_Handle h, uint32_t frames) { static_cast<Synth*>(h)->run(frames); }
void cleanup(LV2_Handle h) { delete static_cast<Synth*>(h); }
const void* extension_data(const char*) { return nullptr; }

} // namespace

const LV2_Descriptor synth_descriptor{
    kSynthURI, instantiate, connect_port, nullptr, run, nullptr, cleanup, extension_data,
};

} // namespace drizzle
