// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#pragma once

#include "beads/dsp/frame.h"

#include <cstddef>

namespace drizzle {

// The hardware's input stage (gain_control.cc: GainControl), run on each
// processor block before Process: automatic or manual gain, and in the tape
// qualities soft clipping when the gain is manual. It also copies the left
// input to the right when Mono is on, as the hardware does when one input is
// unplugged.
class GainControl {
public:
    // gain_control.h: SoftClippingMode. beads.cc's ConfigureAndStartCodec
    // picks it from the quality: on in dirty tape, dynamic in clean tape.
    enum SoftClippingMode { soft_clipping_off, soft_clipping_on, soft_clipping_dynamic };

    // gain_control.cc: Init. Not real-time only in that it is called rarely.
    void init(float sample_rate, SoftClippingMode mode, bool reset);
    // gain_control.h: Restart, what the hardware does when its gain is set
    // back to auto: the gain is learnt again.
    void restart() { time_ = ignore_time_; }

    // gain_control.cc: Process. `manual_gain` is the target gain (1 to 40)
    // used when `auto_gain` is off; the hardware's gain setting of 1 to 255
    // maps to the same range. The hardware's "both inputs unplugged" case is
    // not here: a plugin can't see cables.
    void process(bool active, bool auto_gain, float manual_gain, bool mono, const beads::FloatFrame* in,
                 beads::FloatFrame* out, size_t size);

    float gain() const { return gain_; }

private:
    float peak_ = 0;
    float agc_peak_ = 0;
    float stable_agc_peak_ = 0;
    float gain_ = 0;
    float soft_clipping_amount_ = 0;
    SoftClippingMode soft_clipping_mode_ = soft_clipping_off;
    float manual_gain_ = 0;
    bool first_ = true;
    float one_hertz_ = 0;
    size_t time_ = 0;
    size_t ignore_time_ = 0;
    size_t active_time_ = 0;
};

} // namespace drizzle
