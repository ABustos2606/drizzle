// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
//
// Ported from gain_control.cc (Beads, Emilie Gillet, MIT): the same rules and
// constants, on float input instead of the codec's 32-bit integers.
#include "gain.hpp"

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include <algorithm>
#include <cmath>

namespace drizzle {
namespace {

constexpr float kIgnoreTime = 1.0f; // second
constexpr float kActiveTime = 5.0f; // second
constexpr float kMaxGain = 40.0f;

} // namespace

void GainControl::init(float sample_rate, SoftClippingMode mode, bool reset)
{
    if (reset) {
        stable_agc_peak_ = agc_peak_ = peak_ = 0.5f;
        gain_ = 1.0f;
        soft_clipping_amount_ = 0.0f;
        first_ = true;
    }
    soft_clipping_mode_ = mode;

    ignore_time_ = static_cast<size_t>(sample_rate * kIgnoreTime);

    const bool no_readjust = !reset && time_ && time_ >= active_time_;
    active_time_ = static_cast<size_t>(sample_rate * (kIgnoreTime + kActiveTime));
    time_ = no_readjust ? active_time_ : 0;
    one_hertz_ = 1.0f / sample_rate;
}

void GainControl::process(bool active, bool auto_gain, float manual_gain, bool mono, const beads::FloatFrame* in,
                          beads::FloatFrame* out, size_t size)
{
    const float hz = static_cast<float>(size) * one_hertz_;
    if (time_ < ignore_time_) {
        agc_peak_ = 0.5f;
    } else if (time_ < active_time_) {
        SLOPE(agc_peak_, peak_,
              10.0f * hz, // Reduce gain in 0.1s if a peak is detected
              2.0f * hz); // Adjust gain in 0.5s if the level is too low
        stable_agc_peak_ = agc_peak_;
    } else {
        SLOPE(agc_peak_, peak_,
              5.0f * hz, // Reduce gain in 0.2s if a peak is detected
              // Recover from a peak reduction in 2.0s as long as the observed
              // level isn't lower than what was measured while learning.
              // Otherwise, pump the gain in 100s.
              (peak_ > stable_agc_peak_ ? 0.5f : 0.01f) * hz);
    }

    if (active) {
        if (time_ > ignore_time_ || peak_ > 0.001f) { time_ += size; }
        if (time_ >= active_time_) { time_ = active_time_; }
    }

    // The hardware glides its manual gain from where it was (0 at power-on);
    // here it starts where the patch has it, as the pots do (controls.cpp).
    if (first_) {
        manual_gain_ = manual_gain;
        first_ = false;
    }
    ONE_POLE(manual_gain_, manual_gain, 0.01f);

    const float optimal_gain = std::min(0.9f / std::min(agc_peak_, 0.9f), kMaxGain);
    const float gain = auto_gain ? optimal_gain : manual_gain_;

    float soft_clipping_amount;
    if (soft_clipping_mode_ == soft_clipping_off) {
        soft_clipping_amount = 0.0f;
    } else if (soft_clipping_mode_ == soft_clipping_on) {
        soft_clipping_amount = 1.0f;
    } else {
        soft_clipping_amount = 2.0f * (manual_gain_ / optimal_gain - 0.75f);
        CONSTRAIN(soft_clipping_amount, 0.0f, 1.0f)
    }

    stmlib::ParameterInterpolator gain_modulation(&gain_, gain, size);
    stmlib::ParameterInterpolator soft_clipping_amount_modulation(&soft_clipping_amount_, soft_clipping_amount, size);
    while (size--) {
        float l = in->l;
        float r = mono ? in->l : in->r;

        if (active) {
            const float peak = std::max(std::fabs(l), std::fabs(r));
            SLOPE(peak_, peak, 0.1f, 2.0f * one_hertz_);
        }

        const float g = gain_modulation.Next();
        l *= g;
        r *= g;

        if (!auto_gain) {
            if (soft_clipping_mode_ == soft_clipping_off) {
                CONSTRAIN(l, -1.0f, +1.0f)
                CONSTRAIN(r, -1.0f, +1.0f)
            } else {
                // With manual gain, crossfade between the clean but possibly
                // hard-clipped signal and the soft-clipped one.
                const float amount = soft_clipping_amount_modulation.Next();
                const float bias = 0.05f;
                const float soft_clipping_l = (1.0f - bias) * stmlib::SoftClip(l + bias) - bias;
                const float soft_clipping_r = (1.0f - bias) * stmlib::SoftClip(r + bias) - bias;

                CONSTRAIN(l, -1.0f, +1.0f)
                CONSTRAIN(r, -1.0f, +1.0f)
                l = stmlib::Crossfade(l, soft_clipping_l, amount);
                r = stmlib::Crossfade(r, soft_clipping_r, amount);
            }
        }

        out->l = l;
        out->r = r;
        ++out;
        ++in;
    }
}

} // namespace drizzle
