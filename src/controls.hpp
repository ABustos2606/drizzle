// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#pragma once

#include "ports.hpp"

#include "beads/dsp/processor.h"
#include "stmlib/dsp/hysteresis_filter.h"

#include <array>
#include <cstdint>

namespace drizzle {

using PortArray = std::array<float*, kPortCount>;

// What the processor takes for one block (beads.cc: Process).
struct BlockControls {
    beads::Parameters parameters;
    beads::ModulatedParameter density;
    beads::Processor::MixParameters mix;
    beads::SeedButtonState seed_button;
    bool freeze_button;
    bool seed_uses_gate;
};

// Turns control ports and CV inputs into the processor's parameters once per
// block, as the hardware's CvReader::Read (cv_reader.cc) and the parameter
// code of Process (beads.cc) do once per codec block.
class Controls {
public:
    void reset();
    // `frame` is the host frame whose CV values the block uses.
    void read(const PortArray& ports, uint32_t frame, BlockControls* out);

    // Pots in the hardware's order (drivers/pots_adc.h: PotsAdcChannel).
    enum Pot : uint32_t {
        feedback, dry_wet, reverb, time_amount, size_amount, shape_amount, pitch_amount,
        time, density, pitch, size, shape, pot_count
    };
    // CV inputs in the hardware's order (drivers/cv_adc.h: CvAdcChannel).
    enum Cv : uint32_t { assign_cv, density_cv, pitch_cv, time_cv, size_cv, shape_cv, cv_count };

    float pot(Pot p) const { return pot_hysteresis_[p].value(); }
    float cv(Cv c) const { return cv_value_[c]; }

private:
    std::array<float, pot_count> pot_lp_{};
    std::array<stmlib::HysteresisFilter, pot_count> pot_hysteresis_{};
    std::array<float, cv_count> cv_lp_{};
    std::array<float, cv_count> cv_value_{};
    bool first_ = true;
};

} // namespace drizzle
