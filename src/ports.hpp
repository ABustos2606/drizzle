// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#pragma once

#include <cstdint>

namespace drizzle {

inline constexpr char kURI[] = "https://github.com/ABustos2606/drizzle#drizzle";
inline constexpr char kSynthURI[] = "https://github.com/ABustos2606/drizzle#synth";

// Permanent port ABI: never renumber or rename; new ports are appended.
// The kind comments are read by tests/ttl_check.py.
enum class Port : uint32_t {
    in_l = 0,            // audio_in
    in_r = 1,            // audio_in
    out_l = 2,           // audio_out
    out_r = 3,           // audio_out
    time = 4,            // control_in
    size = 5,            // control_in
    shape = 6,           // control_in
    pitch = 7,           // control_in
    density = 8,         // control_in
    time_mod = 9,        // control_in
    size_mod = 10,       // control_in
    shape_mod = 11,      // control_in
    pitch_mod = 12,      // control_in
    time_uses_cv = 13,   // control_in
    size_uses_cv = 14,   // control_in
    shape_uses_cv = 15,  // control_in
    pitch_uses_cv = 16,  // control_in
    feedback = 17,       // control_in
    dry_wet = 18,        // control_in
    reverb = 19,         // control_in
    freeze = 20,         // control_in
    seed = 21,           // control_in
    seed_lock = 22,      // control_in
    quality = 23,        // control_in
    mono = 24,           // control_in
    gain = 25,           // control_in
    assign_to = 26,      // control_in
    gate_mode = 27,      // control_in
    output = 28,         // control_in
    time_cv = 29,        // cv_in
    size_cv = 30,        // cv_in
    shape_cv = 31,       // cv_in
    pitch_cv = 32,       // cv_in
    density_cv = 33,     // cv_in
    assign_cv = 34,      // cv_in
    freeze_gate = 35,    // cv_in
    seed_gate = 36,      // cv_in
    grain_out = 37,      // cv_out
    seed_uses_gate = 38, // control_in
    auto_gain = 39,      // control_in
    enabled = 40,        // control_in
    latency = 41,        // control_out
    count = 42
};

// Drizzle Synth: no audio input, Feedback and Dry/wet become Wavetable and
// Osc/grains (what Beads' synth mode does with those knobs), no buffer
// settings (Mono, gain) and no bypass port.
enum class SynthPort : uint32_t {
    out_l = 0,           // audio_out
    out_r = 1,           // audio_out
    time = 2,            // control_in
    size = 3,            // control_in
    shape = 4,           // control_in
    pitch = 5,           // control_in
    density = 6,         // control_in
    time_mod = 7,        // control_in
    size_mod = 8,        // control_in
    shape_mod = 9,       // control_in
    pitch_mod = 10,      // control_in
    time_uses_cv = 11,   // control_in
    size_uses_cv = 12,   // control_in
    shape_uses_cv = 13,  // control_in
    pitch_uses_cv = 14,  // control_in
    wavetable = 15,      // control_in
    osc_grains = 16,     // control_in
    reverb = 17,         // control_in
    freeze = 18,         // control_in
    seed = 19,           // control_in
    seed_lock = 20,      // control_in
    quality = 21,        // control_in
    assign_to = 22,      // control_in
    gate_mode = 23,      // control_in
    output = 24,         // control_in
    time_cv = 25,        // cv_in
    size_cv = 26,        // cv_in
    shape_cv = 27,       // cv_in
    pitch_cv = 28,       // cv_in
    density_cv = 29,     // cv_in
    assign_cv = 30,      // cv_in
    freeze_gate = 31,    // cv_in
    seed_gate = 32,      // cv_in
    grain_out = 33,      // cv_out
    seed_uses_gate = 34, // control_in
    latency = 35,        // control_out
    count = 36
};

inline constexpr uint32_t kPortCount = static_cast<uint32_t>(Port::count);
inline constexpr uint32_t kSynthPortCount = static_cast<uint32_t>(SynthPort::count);

// Ports that carry one value per frame (audio and CV), not one per run().
inline constexpr bool is_signal(uint32_t index)
{
    return index <= static_cast<uint32_t>(Port::out_r) ||
           (index >= static_cast<uint32_t>(Port::time_cv) && index <= static_cast<uint32_t>(Port::grain_out));
}

inline constexpr bool is_synth_signal(uint32_t index)
{
    return index <= static_cast<uint32_t>(SynthPort::out_r) ||
           (index >= static_cast<uint32_t>(SynthPort::time_cv) && index <= static_cast<uint32_t>(SynthPort::grain_out));
}

} // namespace drizzle
