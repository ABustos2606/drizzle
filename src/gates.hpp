// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#pragma once

#include "stmlib/dsp/delay_line.h"
#include "stmlib/utils/gate_flags.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>

namespace drizzle {

// beads.cc: Process's Seed input. The gate is read 2.5 ms (10 blocks) late
// through event_delay, and given to the processor only when the Seed jack is
// patched; here, when Seed uses gate is on.
class SeedGate {
public:
    // ConfigureAndStartCodec: event_delay.Init().
    void reset()
    {
        delay_.Init();
        previous_ = stmlib::GATE_FLAG_LOW;
    }

    // `gate` is the Seed gate at each of the block's `size` frames. Returns
    // the flags for Process, or nullptr for an unpatched jack.
    const stmlib::GateFlags* process(bool uses_gate, const bool* gate, size_t size, stmlib::GateFlags* flags)
    {
        if (!uses_gate) {
            previous_ = stmlib::GATE_FLAG_LOW;
            return nullptr;
        }
        for (size_t i = 0; i < size; ++i) {
            delay_.Write(gate[i] ? 1 : 0);
            const bool has_seed = delay_.Read(size * 10) != 0;
            previous_ = flags[i] = stmlib::ExtractGateFlags(previous_, has_seed);
        }
        return flags;
    }

private:
    stmlib::DelayLine<uint8_t, 256> delay_;
    stmlib::GateFlags previous_ = stmlib::GATE_FLAG_LOW;
};

// The Grain out CV: the processor's grain gate (what the hardware's gate
// output mode puts on the right output), delayed to line up with the audio,
// either as it is or as a fixed pulse on each rising edge.
class GrainOutput {
public:
    // Allocates; not real-time. `max_delay` is the longest delay in host
    // frames; `trigger_frames` the length of a trigger.
    bool configure(uint32_t max_delay, uint32_t trigger_frames) noexcept
    {
        try {
            delay_.assign(max_delay + 1, 0);
        } catch (const std::bad_alloc&) {
            return false;
        }
        trigger_frames_ = trigger_frames;
        return true;
    }

    // The delay that lines the gate up with the audio, in host frames.
    void reset(uint32_t delay) noexcept
    {
        length_ = std::min<uint32_t>(delay, static_cast<uint32_t>(delay_.size()) - 1);
        std::fill(delay_.begin(), delay_.end(), 0);
        position_ = 0;
        previous_ = false;
        remaining_ = 0;
    }

    // One host frame: the gate as the processor left it; the CV out (0 or 1).
    float frame(bool gate, bool trigger_mode) noexcept
    {
        if (length_) {
            const bool delayed = delay_[position_] != 0;
            delay_[position_] = gate ? 1 : 0;
            if (++position_ == length_) { position_ = 0; }
            gate = delayed;
        }
        if (gate && !previous_) { remaining_ = trigger_frames_; }
        previous_ = gate;
        if (!trigger_mode) { return gate ? 1.0f : 0.0f; }
        if (remaining_) {
            --remaining_;
            return 1.0f;
        }
        return 0.0f;
    }

private:
    std::vector<uint8_t> delay_;
    uint32_t length_ = 0;
    uint32_t position_ = 0;
    uint32_t trigger_frames_ = 0;
    uint32_t remaining_ = 0;
    bool previous_ = false;
};

} // namespace drizzle
