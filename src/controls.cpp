// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#include "controls.hpp"

#include <algorithm>
#include <cmath>

namespace drizzle {
namespace {

// cv_reader.cc: CvReader::Init's PotChannel laws.
enum Law { normal, attenuverter, semitones };

struct PotSource {
    Port port;
    Law law;
    float fallback;   // used when the port is unconnected or not finite
    float hysteresis; // cv_reader.cc: 0.001 for Time, 0 for the others
};

// In PotsAdcChannel order (Controls::Pot).
constexpr std::array<PotSource, Controls::pot_count> kPots{{
    {Port::feedback, normal, 0.0f, 0.0f},
    {Port::dry_wet, normal, 0.5f, 0.0f},
    {Port::reverb, normal, 0.0f, 0.0f},
    {Port::time_mod, attenuverter, 0.0f, 0.0f},
    {Port::size_mod, attenuverter, 0.0f, 0.0f},
    {Port::shape_mod, attenuverter, 0.0f, 0.0f},
    {Port::pitch_mod, attenuverter, 0.0f, 0.0f},
    {Port::time, normal, 0.5f, 0.001f},
    {Port::density, normal, 0.7f, 0.0f},
    {Port::pitch, semitones, 0.0f, 0.0f},
    {Port::size, normal, 0.5f, 0.0f},
    {Port::shape, normal, 0.5f, 0.0f},
}};

struct CvSource {
    Port port;
    float lp_coefficient;
    float scale; // the value at +5 V (1.0 here)
};

// cv_reader.cc: CvReader::Init, with settings.cc's default calibration. The
// hardware's ADC spans -5 V to +5 V (Pitch: 120 semitones at 1 V/oct), and
// Peepo's CV scale is 1.0 = 5 V, so a CV of x reads as x * scale.
constexpr std::array<CvSource, Controls::cv_count> kCvs{{
    {Port::assign_cv, 0.1f, 1.0f},
    {Port::density_cv, 1.0f, 1.0f},
    {Port::pitch_cv, 1.0f, 60.0f},
    {Port::time_cv, 0.5f, 1.6f},
    {Port::size_cv, 0.5f, 1.6f},
    {Port::shape_cv, 0.5f, 1.6f},
}};

float value(const PortArray& ports, Port port, float fallback, float minimum, float maximum)
{
    const float* data = ports[static_cast<uint32_t>(port)];
    return data && std::isfinite(*data) ? std::clamp(*data, minimum, maximum) : fallback;
}

bool toggle(const PortArray& ports, Port port, bool fallback)
{
    return value(ports, port, fallback ? 1.0f : 0.0f, 0.0f, 1.0f) >= 0.5f;
}

float signal(const PortArray& ports, Port port, uint32_t frame)
{
    const float* data = ports[static_cast<uint32_t>(port)];
    // The ADC saturates at +-5 V; NaN or infinity reads as 0 V.
    return data && std::isfinite(data[frame]) ? std::clamp(data[frame], -1.0f, 1.0f) : 0.0f;
}

template <typename T> void constrain(T& x, T lo, T hi) { x = std::min(std::max(x, lo), hi); }

} // namespace

void Controls::reset()
{
    pot_lp_.fill(0);
    for (auto& h : pot_hysteresis_) { h.Init(0.0f); }
    // CvChannel::Init: lp_ at mid-scale (0 V) and value_ at the middle of the
    // range (0 for every channel).
    cv_lp_.fill(0);
    cv_value_.fill(0);
    first_ = true;
}

void Controls::read(const PortArray& ports, uint32_t frame, BlockControls* out)
{
    // CvReader::Read: CV channels (CvChannel::Process).
    for (uint32_t i = 0; i < cv_count; ++i) {
        const auto& cv = kCvs[i];
        cv_lp_[i] += cv.lp_coefficient * (signal(ports, cv.port, frame) - cv_lp_[i]);
        float v = cv_lp_[i] * cv.scale;
        constrain(v, -cv.scale, cv.scale);
        cv_value_[i] = v;
    }
    // CvReader::Read: pots (PotChannel::Process). A pot reads 0 to 1; the
    // Mod knobs here read -1 to 1, the pot's travel either side of noon.
    for (uint32_t i = 0; i < pot_count; ++i) {
        const auto& pot = kPots[i];
        float v;
        switch (pot.law) {
        case attenuverter:
            v = 0.5f * value(ports, pot.port, pot.fallback, -1.0f, 1.0f);
            v = (2.0f / 0.47f) * v * std::max(std::fabs(v) - 0.03f, 0.0f);
            break;
        case semitones:
            // LAW_DETUNE turns the hardware's Pitch pot into semitones, snapped
            // to musical intervals. The Pitch control here is whole semitones
            // already.
            v = value(ports, pot.port, pot.fallback, -24.0f, 24.0f);
            break;
        case normal:
        default:
            v = value(ports, pot.port, pot.fallback, 0.0f, 1.0f);
            break;
        }
        // The hardware starts its pots from 0 at power-on; a plugin starts
        // them where the patch has them.
        if (first_) {
            pot_lp_[i] = v;
            pot_hysteresis_[i].Process(v, 0.0f);
        }
        pot_lp_[i] += 0.05f * (v - pot_lp_[i]);
        pot_hysteresis_[i].Process(pot_lp_[i], pot.hysteresis);
    }
    first_ = false;

    // beads.cc: Process. "Has CV" is the hardware's normalization probe on
    // each CV jack; a plugin can't see a cable, so it is a switch.
    auto& p = out->parameters;
    const auto randomized = [&](beads::RandomizedParameter& r, Pot base, Pot amount, Port uses_cv, Cv input) {
        r.base = pot(base);
        r.attenuverter = pot(amount);
        r.has_cv = toggle(ports, uses_cv, false);
        r.cv = cv(input);
    };
    randomized(p.time, time, time_amount, Port::time_uses_cv, time_cv);
    randomized(p.size, size, size_amount, Port::size_uses_cv, size_cv);
    randomized(p.shape, shape, shape_amount, Port::shape_uses_cv, shape_cv);
    randomized(p.pitch, pitch, pitch_amount, Port::pitch_uses_cv, pitch_cv);
    // ENABLE_ADVANCED_FEATURES is not defined in Beads' makefile.
    p.monophonic_mode = false;

    // The hardware zeroes the Density CV when its jack is unplugged, to hide
    // ADC noise; an unpatched LV2 input already reads 0.
    out->density.base = pot(density);
    out->density.cv = cv(density_cv);

    // Ui::ProcessPot and PotController in its tracking state: the mix pots
    // pass through. The assignable CV (milestone 2) adds nothing yet.
    float mix[3];
    for (uint32_t i = 0; i < 3; ++i) {
        mix[i] = pot(static_cast<Pot>(feedback + i)) * 1.005f;
        constrain(mix[i], 0.0f, 1.0f);
    }
    out->mix.feedback = mix[0];
    out->mix.dry_wet = mix[1];
    out->mix.reverb_amount = mix[2];
    out->mix.output_gate = false;

    // Ui: Seed + Freeze locks the Seed button (and the hardware starts
    // locked); pressing Seed unlocks it. Here the lock is its own switch.
    const bool locked = toggle(ports, Port::seed_lock, true);
    out->seed_button = locked ? beads::SEED_BUTTON_STATE_LOCKED
                              : toggle(ports, Port::seed, false) ? beads::SEED_BUTTON_STATE_ON
                                                                  : beads::SEED_BUTTON_STATE_OFF;
    out->freeze_button = toggle(ports, Port::freeze, false);
    out->seed_uses_gate = toggle(ports, Port::seed_uses_gate, false);
}

} // namespace drizzle
