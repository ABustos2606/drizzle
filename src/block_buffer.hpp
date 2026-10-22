// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#pragma once

#include "beads/dsp/frame.h"

#include <algorithm>
#include <cstdint>

namespace drizzle {

// beads.cc: kBlockRate. The processor runs one block per 1/4000 s at its own
// rate: 12 frames at 48 kHz, 8 at 32 kHz, 6 at 24 kHz.
inline constexpr uint32_t kBlockRate = 4000;

// Gathers frames into the processor's fixed-size blocks, whatever slices the
// host runs, so the processor and its control reads happen at the same
// frames for any slicing. Outputs lag inputs by one block.
struct BlockBuffer {
    static constexpr uint32_t kMaxSize = 48000 / kBlockRate;

    beads::FloatFrame in[kMaxSize];
    beads::FloatFrame out[kMaxSize];
    // The Freeze and Seed gate inputs at each frame (codec.h's EVENT_FREEZE
    // and EVENT_SEED, read with every sample on the hardware).
    bool freeze[kMaxSize];
    bool seed[kMaxSize];
    // The processor's grain gate at each output frame.
    bool gate[kMaxSize];
    uint32_t size = kMaxSize;
    uint32_t position = 0;

    void reset()
    {
        const beads::FloatFrame zero{0.0f, 0.0f};
        std::fill(in, in + kMaxSize, zero);
        std::fill(out, out + kMaxSize, zero);
        std::fill(freeze, freeze + kMaxSize, false);
        std::fill(seed, seed + kMaxSize, false);
        std::fill(gate, gate + kMaxSize, false);
        position = 0;
    }
};

} // namespace drizzle
