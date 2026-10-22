// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
//
// gain: the input stage ported from gain_control.cc. Auto gain learns the
// level (0.9 / peak, at most 40 times), manual gain glides to its setting and
// clips, soft clipping follows the quality, Mono copies In L across.
#include "../src/gain.hpp"

#include "stmlib/dsp/dsp.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

using drizzle::GainControl;

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) { throw std::runtime_error(message); }
}

constexpr float kRate = 48000;
constexpr size_t kBlock = 12;

struct Run {
    float peak_l = 0, peak_r = 0, max_l = -1;
    beads::FloatFrame last{0, 0};
};

// `seconds` of a 220 Hz sine of `amplitude` on the left and `right` times it
// on the right; returns the output peaks over the second half, and the last
// frame.
Run run(GainControl& gain, double seconds, float amplitude, bool auto_gain, float manual, bool mono = false,
        float right = 1.0f)
{
    Run result;
    const auto blocks = static_cast<size_t>(seconds * kRate / kBlock);
    static uint64_t frame = 0;
    for (size_t b = 0; b < blocks; ++b) {
        beads::FloatFrame in[kBlock], out[kBlock];
        for (size_t i = 0; i < kBlock; ++i, ++frame) {
            const float x = amplitude * static_cast<float>(std::sin(frame * 2.0 * M_PI * 220.0 / kRate));
            in[i] = {x, x * right};
        }
        gain.process(true, auto_gain, manual, mono, in, out, kBlock);
        if (2 * b >= blocks) {
            for (size_t i = 0; i < kBlock; ++i) {
                result.peak_l = std::max(result.peak_l, std::fabs(out[i].l));
                result.peak_r = std::max(result.peak_r, std::fabs(out[i].r));
                result.max_l = std::max(result.max_l, out[i].l);
            }
        }
        result.last = out[kBlock - 1];
    }
    return result;
}

} // namespace

int main()
{
    try {
        // Auto gain: 0.9 / 0.5 = 1.8 while ignoring the first second, then the
        // gain settles where the peak reaches 0.9.
        GainControl gain;
        gain.init(kRate, GainControl::soft_clipping_off, true);
        run(gain, 0.5, 0.2f, true, 1);
        require(std::fabs(gain.gain() - 1.8f) < 1e-4f, "auto gain starts at 1.8: " + std::to_string(gain.gain()));
        run(gain, 8, 0.2f, true, 1);
        require(std::fabs(gain.gain() * 0.2f - 0.9f) < 0.05f, "auto gain learns 0.9 / peak: " + std::to_string(gain.gain()));
        const float learnt = gain.gain();
        // Learnt: a quality change (init without reset) keeps it.
        gain.init(32000, GainControl::soft_clipping_off, false);
        run(gain, 0.1, 0.2f, true, 1);
        require(std::fabs(gain.gain() - learnt) < 0.05f * learnt, "a quality change keeps the learnt gain");
        // A very quiet input is held at 40 times.
        GainControl quiet;
        quiet.init(kRate, GainControl::soft_clipping_off, true);
        run(quiet, 8, 0.001f, true, 1);
        require(std::fabs(quiet.gain() - 40.0f) < 1e-3f, "auto gain is at most 40: " + std::to_string(quiet.gain()));

        // Manual gain: the setting, linear, after its glide (0.01 per block).
        GainControl manual;
        manual.init(kRate, GainControl::soft_clipping_off, true);
        const float twelve_db = std::pow(10.0f, 12.0f / 20.0f);
        run(manual, 0.5, 0.1f, false, twelve_db);
        require(std::fabs(manual.gain() - twelve_db) < 1e-3f, "manual gain: " + std::to_string(manual.gain()));
        // Digital qualities hard-clip at full scale.
        const auto clipped = run(manual, 0.5, 0.5f, false, twelve_db);
        require(clipped.peak_l == 1.0f, "manual gain clips at 1 with soft clipping off");

        // Dirty tape: soft clipping on, the clean signal crossfaded fully to
        // (1 - 0.05) SoftClip(x + 0.05) - 0.05.
        GainControl tape;
        tape.init(24000, GainControl::soft_clipping_on, true);
        const auto soft = run(tape, 0.5, 0.5f, false, twelve_db);
        const float expected = 0.95f * stmlib::SoftClip(0.5f * twelve_db + 0.05f) - 0.05f;
        require(std::fabs(soft.max_l - expected) < 2e-3f, "soft clipping: " + std::to_string(soft.max_l));
        // Auto gain never clips here: the processor's own limiter does.
        GainControl loud;
        loud.init(kRate, GainControl::soft_clipping_on, true);
        const auto unclipped = run(loud, 0.5, 0.9f, true, 1);
        require(unclipped.peak_l > 1.2f, "auto gain does not clip: " + std::to_string(unclipped.peak_l));

        // Clean tape: dynamic soft clipping, off while the manual gain is well
        // below the gain auto would pick.
        GainControl clean;
        clean.init(kRate, GainControl::soft_clipping_dynamic, true);
        run(clean, 8, 0.2f, false, 1.0f);
        const auto dynamic_low = run(clean, 0.1, 0.2f, false, 1.0f);
        require(std::fabs(dynamic_low.peak_l - 0.2f) < 0.002f, "clean tape, low gain: no soft clipping");

        // Mono: In L on both sides, In R ignored.
        GainControl mono;
        mono.init(kRate, GainControl::soft_clipping_off, true);
        const auto m = run(mono, 0.1, 0.3f, false, 1, true, -0.5f);
        require(m.last.l == m.last.r, "Mono copies In L to the right");
        const auto stereo = run(mono, 0.1, 0.3f, false, 1, false, -0.5f);
        require(stereo.last.l != stereo.last.r, "stereo keeps In R");

        // Restart learns again from the active phase: the gain moves.
        loud.restart();
        run(loud, 1, 0.05f, true, 1);
        require(loud.gain() > 2.0f, "restart learns the gain again: " + std::to_string(loud.gain()));
        std::printf("PASS gain: auto gain learns and holds, manual gain glides and clips, soft clipping per quality, "
                    "Mono, restart\n");
    } catch (const std::exception& e) {
        std::cerr << "FAIL gain: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
