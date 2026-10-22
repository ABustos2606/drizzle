// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
//
// match_beads: when the host runs at the processor's own rate (48 kHz for
// digital and clean tape, 32 kHz for vintage digital, 24 kHz for dirty tape)
// the plugin's output is the vendored Processor's, driven directly the way
// beads_test.cc and beads.cc drive it (Init, Configure, one control read and
// one Process call per 1/4000 s block, the Seed gate 2.5 ms late, one Random
// sequence, the input through the gain control first), bit for bit, late by
// the reported latency; Grain out is the processor's grain gate, as late.
#include "host.hpp"
#include "../src/controls.hpp"
#include "../src/drizzle.hpp"
#include "../src/gain.hpp"

#include "beads/dsp/processor.h"
#include "beads/dsp/synth.h"
#include "stmlib/dsp/delay_line.h"
#include "stmlib/utils/random.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>

using drizzle::Port;
using drizzle::SynthPort;
using test::Instance;
using test::require;
using test::Stereo;

namespace {

constexpr double kSeconds = 6;

struct Settings {
    const char* name;
    beads::Processor::Media media;
    uint32_t rate;
    float size, density, feedback, seed_lock, seed_uses_gate;
    // Milestone 2: Mono, Auto gain, Input gain (dB), CV assign to (-1: the
    // Assign CV unpatched).
    float mono = 0, auto_gain = 1, gain = 0, assign_to = -1;
};

// beads_test.cc: AudioSource, a C4 saw left and a sine right.
Stereo tone(uint64_t frame, uint32_t rate)
{
    const double phase = std::fmod(frame * 261.626 / rate, 1.0);
    return {static_cast<float>(phase - 0.5), static_cast<float>(std::sin(phase * 2.0 * M_PI) * 0.5)};
}
float time_cv(uint64_t frame, uint32_t rate) { return static_cast<float>(0.5 * std::sin(frame * 1.1 / rate)); }
float pitch_cv(uint64_t frame, uint32_t rate) { return static_cast<float>(0.1 * std::sin(frame * 0.4 / rate)); }
float seed_gate(uint64_t frame, uint32_t rate) { return frame % (rate / 3) < rate / 50 ? 1.0f : 0.0f; }
float freeze_gate(uint64_t frame, uint32_t rate) { return frame % (2 * rate) > 3 * rate / 2 ? 1.0f : 0.0f; }
// Control ports change between periods only: the Seed button, by period.
float seed_button(uint64_t frame, uint32_t rate) { return (frame / 128) * 128 % rate < rate / 4 ? 1.0f : 0.0f; }
float assign_cv(uint64_t frame, uint32_t rate) { return static_cast<float>(0.4 * std::sin(frame * 2.3 / rate)); }

void common(const Settings& s, const std::function<float&(Port)>& c)
{
    c(Port::quality) = static_cast<float>(s.media);
    c(Port::time) = 0.3f; c(Port::time_mod) = 0.5f; c(Port::time_uses_cv) = 1;
    c(Port::size) = s.size; c(Port::size_mod) = -0.4f;
    c(Port::shape) = 0.6f; c(Port::shape_mod) = 0.7f;
    c(Port::pitch) = 7; c(Port::pitch_mod) = 0.8f; c(Port::pitch_uses_cv) = 1;
    c(Port::density) = s.density;
    c(Port::feedback) = s.feedback; c(Port::dry_wet) = 0.8f; c(Port::reverb) = 0.4f;
    c(Port::seed_lock) = s.seed_lock; c(Port::seed_uses_gate) = s.seed_uses_gate;
    c(Port::mono) = s.mono; c(Port::auto_gain) = s.auto_gain; c(Port::gain) = s.gain;
    c(Port::assign_to) = std::max(s.assign_to, 0.0f);
    // Grain out as the gate itself, to compare with the processor's.
    c(Port::gate_mode) = 1;
}

// beads.cc: ConfigureAndStartCodec's soft clipping for each quality.
drizzle::GainControl::SoftClippingMode soft_clipping(beads::Processor::Media media)
{
    return media == beads::Processor::MEDIA_DIRTY_TAPE   ? drizzle::GainControl::soft_clipping_on
           : media == beads::Processor::MEDIA_CLEAN_TAPE ? drizzle::GainControl::soft_clipping_dynamic
                                                         : drizzle::GainControl::soft_clipping_off;
}

struct Render {
    std::vector<Stereo> audio;
    std::vector<float> grain;
};

Render reference(const Settings& s, uint32_t latency)
{
    const uint64_t frames = static_cast<uint64_t>(kSeconds * s.rate);
    // The hardware's memory layout (beads.cc, beads_test.cc).
    std::vector<uint8_t> block_1(drizzle::kDelayBuffer1Size), block_2(drizzle::kDelayBuffer2Size);
    std::vector<uint16_t> reverb(drizzle::kReverbSize);
    uint8_t* buffers[3] = {block_1.data(), block_1.data() + drizzle::kDelayBuffer1Size / 2, block_2.data()};
    void* memory = std::calloc(1, sizeof(beads::Processor));
    auto* processor = new (memory) beads::Processor;
    processor->Init(buffers, reverb.data());
    processor->Configure(s.media, s.mono != 0);
    drizzle::GainControl gain;
    gain.init(processor->sample_rate(), soft_clipping(s.media), true);
    require(static_cast<uint32_t>(processor->sample_rate()) == s.rate, std::string(s.name) + ": processor rate");
    stmlib::Random::Seed(drizzle::kRandomSeed);

    std::array<float, drizzle::kPortCount> controls{};
    std::vector<float> time_buffer(frames), pitch_buffer(frames), assign_buffer(frames);
    drizzle::PortArray ports{};
    for (uint32_t p = 0; p < drizzle::kPortCount; ++p) { ports[p] = drizzle::is_signal(p) ? nullptr : &controls[p]; }
    ports[static_cast<uint32_t>(Port::time_cv)] = time_buffer.data();
    ports[static_cast<uint32_t>(Port::pitch_cv)] = pitch_buffer.data();
    ports[static_cast<uint32_t>(Port::assign_cv)] = assign_buffer.data();
    // Every control at its Turtle default, then the test's settings.
    controls[static_cast<uint32_t>(Port::output)] = 0;
    controls[static_cast<uint32_t>(Port::enabled)] = 1;
    common(s, [&](Port p) -> float& { return controls[static_cast<uint32_t>(p)]; });
    for (uint64_t i = 0; i < frames; ++i) {
        time_buffer[i] = time_cv(i, s.rate);
        pitch_buffer[i] = pitch_cv(i, s.rate);
        assign_buffer[i] = s.assign_to >= 0 ? assign_cv(i, s.rate) : 0.0f;
    }

    drizzle::Controls reader;
    reader.reset();
    stmlib::DelayLine<uint8_t, 256> event_delay;
    event_delay.Init();
    stmlib::GateFlags previous_seed = stmlib::GATE_FLAG_LOW;
    const uint32_t size = s.rate / 4000;
    Render out;
    out.audio.assign(latency, Stereo{0, 0});
    out.grain.assign(latency, 0.0f);
    for (uint64_t start = 0; start + size <= frames; start += size) {
        const uint64_t last = start + size - 1;
        controls[static_cast<uint32_t>(Port::seed)] = seed_button(last, s.rate);
        drizzle::BlockControls c;
        reader.read(ports, static_cast<uint32_t>(last), &c);

        beads::FloatFrame in[12], gained[12], frame_out[12];
        bool gate[12];
        bool freeze[12];
        stmlib::GateFlags seed[12];
        for (uint32_t i = 0; i < size; ++i) {
            const Stereo x = tone(start + i, s.rate);
            in[i] = {x.l, x.r};
            freeze[i] = c.freeze_button || freeze_gate(start + i, s.rate) >= 0.4f;
        }
        // beads.cc: Process, with the Seed jack patched.
        const stmlib::GateFlags* seed_input = nullptr;
        if (c.seed_uses_gate) {
            for (uint32_t i = 0; i < size; ++i) {
                event_delay.Write(seed_gate(start + i, s.rate) >= 0.4f);
                const bool has_seed = event_delay.Read(static_cast<size_t>(size) * 10);
                previous_seed = seed[i] = stmlib::ExtractGateFlags(previous_seed, has_seed);
            }
            seed_input = seed;
        } else {
            previous_seed = stmlib::GATE_FLAG_LOW;
        }
        // gain_control.cc: Process, with In R unpatched when Mono is on.
        const float manual = std::clamp(std::pow(10.0f, s.gain / 20.0f), 1.0f, 40.0f);
        gain.process(true, s.auto_gain != 0, manual, s.mono != 0, in, gained, size);
        processor->Process(c.seed_button, seed_input, freeze, c.density, c.parameters, c.mix, gained, frame_out, size,
                           gate);
        for (uint32_t i = 0; i < size; ++i) {
            out.audio.push_back(
                {std::clamp(frame_out[i].l, -1.0f, 0.999999f), std::clamp(frame_out[i].r, -1.0f, 0.999999f)});
            out.grain.push_back(gate[i] ? 1.0f : 0.0f);
        }
    }
    processor->~Processor();
    std::free(memory);
    out.audio.resize(frames);
    out.grain.resize(frames);
    return out;
}

Render plugin(const LV2_Descriptor* d, const Settings& s, uint32_t* latency)
{
    Instance instance(d, s.rate, 128);
    instance.record_grain = true;
    common(s, [&](Port p) -> float& { return instance.control(p); });
    // Activated with the patch's Quality, as a host loading a patch does; a
    // Quality set after activate() is a switch (test_scaffold's qualities).
    instance.activate();
    const uint32_t rate = s.rate;
    const bool assign = s.assign_to >= 0;
    Render out;
    out.audio = instance.render(static_cast<uint64_t>(kSeconds * s.rate), [rate, assign](Instance& in, uint32_t i, uint64_t frame) {
        const Stereo x = tone(frame, rate);
        in.signal(Port::in_l, i) = x.l;
        in.signal(Port::in_r, i) = x.r;
        in.signal(Port::time_cv, i) = time_cv(frame, rate);
        in.signal(Port::pitch_cv, i) = pitch_cv(frame, rate);
        in.signal(Port::seed_gate, i) = seed_gate(frame, rate);
        in.signal(Port::freeze_gate, i) = freeze_gate(frame, rate);
        in.signal(Port::assign_cv, i) = assign ? assign_cv(frame, rate) : 0.0f;
        if (i == 0) { in.control(Port::seed) = seed_button(frame, rate); }
    });
    *latency = static_cast<uint32_t>(instance.control(Port::latency));
    out.grain = instance.grain_out;
    return out;
}

// Drizzle Synth: beads::Synth driven as beads.cc does in synth mode, at
// 48 kHz, with the same controls (Wavetable is Feedback, Osc/grains Dry/wet).
struct SynthSettings {
    const char* name;
    float quality, wavetable, osc_grains, seed_lock, seed_uses_gate, freeze, assign_to;
};

void synth_common(const SynthSettings& s, const std::function<float&(SynthPort)>& c)
{
    c(SynthPort::quality) = s.quality;
    c(SynthPort::time) = 0.3f; c(SynthPort::time_mod) = 0.5f; c(SynthPort::time_uses_cv) = 1;
    c(SynthPort::size) = 0.5f; c(SynthPort::size_mod) = -0.4f;
    c(SynthPort::shape) = 0.6f; c(SynthPort::shape_mod) = 0.7f;
    c(SynthPort::pitch) = -5; c(SynthPort::pitch_mod) = 0.3f; c(SynthPort::pitch_uses_cv) = 1;
    c(SynthPort::density) = 0.8f;
    c(SynthPort::wavetable) = s.wavetable; c(SynthPort::osc_grains) = s.osc_grains; c(SynthPort::reverb) = 0.4f;
    c(SynthPort::seed_lock) = s.seed_lock; c(SynthPort::seed_uses_gate) = s.seed_uses_gate;
    c(SynthPort::freeze) = s.freeze; c(SynthPort::assign_to) = s.assign_to;
    c(SynthPort::gate_mode) = 1;
}

Render synth_reference(const SynthSettings& s, uint32_t latency)
{
    constexpr uint32_t rate = 48000;
    const uint64_t frames = static_cast<uint64_t>(kSeconds * rate);
    std::vector<uint16_t> reverb(drizzle::kReverbSize);
    void* memory = std::calloc(1, sizeof(beads::Synth));
    auto* synth = new (memory) beads::Synth;
    synth->Init(reverb.data());
    stmlib::Random::Seed(drizzle::kRandomSeed);

    // The control code reads Drizzle's port numbers.
    std::array<float, drizzle::kPortCount> controls{};
    std::vector<float> time_buffer(frames), pitch_buffer(frames), assign_buffer(frames);
    drizzle::PortArray ports{};
    for (uint32_t p = 0; p < drizzle::kSynthPortCount; ++p) {
        const auto port = static_cast<uint32_t>(drizzle::kSynthToPort[p]);
        ports[port] = drizzle::is_signal(port) ? nullptr : &controls[port];
    }
    ports[static_cast<uint32_t>(Port::time_cv)] = time_buffer.data();
    ports[static_cast<uint32_t>(Port::pitch_cv)] = pitch_buffer.data();
    ports[static_cast<uint32_t>(Port::assign_cv)] = assign_buffer.data();
    synth_common(s, [&](SynthPort p) -> float& {
        return controls[static_cast<uint32_t>(drizzle::kSynthToPort[static_cast<uint32_t>(p)])];
    });
    for (uint64_t i = 0; i < frames; ++i) {
        time_buffer[i] = time_cv(i, rate);
        pitch_buffer[i] = pitch_cv(i, rate);
        assign_buffer[i] = assign_cv(i, rate);
    }

    drizzle::Controls reader;
    reader.reset();
    stmlib::DelayLine<uint8_t, 256> event_delay;
    event_delay.Init();
    stmlib::GateFlags previous_seed = stmlib::GATE_FLAG_LOW;
    const uint32_t size = rate / 4000;
    Render out;
    out.audio.assign(latency, Stereo{0, 0});
    out.grain.assign(latency, 0.0f);
    for (uint64_t start = 0; start + size <= frames; start += size) {
        const uint64_t last = start + size - 1;
        controls[static_cast<uint32_t>(Port::seed)] = seed_button(last, rate);
        drizzle::BlockControls c;
        reader.read(ports, static_cast<uint32_t>(last), &c);
        beads::FloatFrame frame_out[12];
        bool freeze[12], gate[12];
        stmlib::GateFlags seed[12];
        for (uint32_t i = 0; i < size; ++i) { freeze[i] = c.freeze_button || freeze_gate(start + i, rate) >= 0.4f; }
        const stmlib::GateFlags* seed_input = nullptr;
        if (c.seed_uses_gate) {
            for (uint32_t i = 0; i < size; ++i) {
                event_delay.Write(seed_gate(start + i, rate) >= 0.4f);
                const bool has_seed = event_delay.Read(static_cast<size_t>(size) * 10);
                previous_seed = seed[i] = stmlib::ExtractGateFlags(previous_seed, has_seed);
            }
            seed_input = seed;
        } else {
            previous_seed = stmlib::GATE_FLAG_LOW;
        }
        synth->Render(c.seed_button, seed_input, freeze, beads::Synth::Resolution(static_cast<int>(s.quality)),
                      c.density, c.parameters, c.mix, frame_out, size, gate);
        for (uint32_t i = 0; i < size; ++i) {
            out.audio.push_back(
                {std::clamp(frame_out[i].l, -1.0f, 0.999999f), std::clamp(frame_out[i].r, -1.0f, 0.999999f)});
            out.grain.push_back(gate[i] ? 1.0f : 0.0f);
        }
    }
    synth->~Synth();
    std::free(memory);
    out.audio.resize(frames);
    out.grain.resize(frames);
    return out;
}

Render synth_plugin(const LV2_Descriptor* d, const SynthSettings& s, uint32_t* latency)
{
    constexpr uint32_t rate = 48000;
    Instance instance(d, rate, 128);
    instance.record_grain = true;
    synth_common(s, [&](SynthPort p) -> float& { return instance.control(p); });
    instance.activate();
    Render out;
    out.audio = instance.render(static_cast<uint64_t>(kSeconds * rate), [](Instance& in, uint32_t i, uint64_t frame) {
        in.signal(SynthPort::time_cv, i) = time_cv(frame, rate);
        in.signal(SynthPort::pitch_cv, i) = pitch_cv(frame, rate);
        in.signal(SynthPort::assign_cv, i) = assign_cv(frame, rate);
        in.signal(SynthPort::seed_gate, i) = seed_gate(frame, rate);
        in.signal(SynthPort::freeze_gate, i) = freeze_gate(frame, rate);
        if (i == 0) { in.control(SynthPort::seed) = seed_button(frame, rate); }
    });
    *latency = static_cast<uint32_t>(instance.control(SynthPort::latency));
    out.grain = instance.grain_out;
    return out;
}

void compare(const char* name, const Render& expected, const Render& actual, uint32_t latency)
{
    float peak = 0;
    for (const auto& f : expected.audio) { peak = std::max({peak, std::fabs(f.l), std::fabs(f.r)}); }
    require(peak > 0.01f, std::string(name) + ": reference makes sound");
    size_t grains = 0;
    for (size_t i = 1; i < expected.grain.size(); ++i) { grains += expected.grain[i] > expected.grain[i - 1]; }
    require(grains > 3, std::string(name) + ": reference plays grains");
    require(actual.audio.size() == expected.audio.size() && actual.grain.size() == expected.grain.size(),
            std::string(name) + ": length");
    for (size_t i = 0; i < expected.audio.size(); ++i) {
        require(expected.audio[i] == actual.audio[i], std::string(name) + ": differs at frame " + std::to_string(i));
        require(expected.grain[i] == actual.grain[i], std::string(name) + ": grain out differs at frame " + std::to_string(i));
    }
    std::cout << "  " << name << ": " << expected.audio.size() << " frames equal, " << grains
              << " grain gates, latency " << latency << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::cerr << "usage: test_match drizzle.so\n"; return 1; }
    try {
        test::Library library(argv[1]);
        using P = beads::Processor;
        const Settings settings[] = {
            {"digital, gate-clocked grains", P::MEDIA_DIGITAL, 48000, 0.4f, 0.8f, 0.3f, 1, 1},
            {"clean tape, Seed button", P::MEDIA_CLEAN_TAPE, 48000, 0.6f, 0.7f, 0.2f, 0, 0},
            {"vintage digital, free-running", P::MEDIA_VINTAGE_DIGITAL, 32000, 0.5f, 0.9f, 0.5f, 1, 0},
            {"dirty tape, free-running", P::MEDIA_DIRTY_TAPE, 24000, 0.3f, 0.2f, 0.4f, 1, 0},
            {"digital, delay mode", P::MEDIA_DIGITAL, 48000, 1.0f, 0.3f, 0.6f, 1, 1},
            // Milestone 2: Mono, manual gain (soft clipping in the tapes), Assign.
            {"digital, mono, assign to feedback", P::MEDIA_DIGITAL, 48000, 0.4f, 0.8f, 0.3f, 1, 0, 1, 1, 0, 0},
            {"clean tape, manual gain 12 dB, assign to dry/wet", P::MEDIA_CLEAN_TAPE, 48000, 0.5f, 0.95f, 0.2f, 1, 1,
             0, 0, 12, 1},
            {"dirty tape, mono, manual gain 24 dB, assign to all", P::MEDIA_DIRTY_TAPE, 24000, 0.3f, 0.8f, 0.4f, 1, 0,
             1, 0, 24, 3},
            {"vintage digital, delay mode, assign to reverb", P::MEDIA_VINTAGE_DIGITAL, 32000, 1.0f, 0.3f, 0.5f, 1, 1,
             0, 1, 0, 2},
        };
        for (const auto& s : settings) {
            uint32_t latency = 0;
            const auto actual = plugin(library.descriptor, s, &latency);
            compare(s.name, reference(s, latency), actual, latency);
        }
        std::cout << "PASS match_beads: at each processor rate the output and grain gate equal Processor driven "
                     "directly, bit for bit (4 qualities, delay mode, Seed button and gate, Freeze gate, CVs, Mono, "
                     "auto and manual gain, Assign)\n";

        const SynthSettings synths[] = {
            {"synth, 16 bit, free-running, assign to wavetable", 3, 0.3f, 0.5f, 1, 0, 0, 0},
            {"synth, 4 bit, gate-clocked, assign to all", 0, 0.8f, 0.7f, 1, 1, 0, 3},
            {"synth, 7 bit, Seed button, frozen voice", 2, 0.5f, 0.2f, 0, 0, 1, 1},
        };
        for (const auto& s : synths) {
            uint32_t latency = 0;
            const auto actual = synth_plugin(library.synth, s, &latency);
            compare(s.name, synth_reference(s, latency), actual, latency);
        }
        std::cout << "PASS match_synth: Drizzle Synth's output and grain gate equal Synth driven directly, bit for "
                     "bit (three resolutions, free-running, gate-clocked and Seed button grains, Freeze, Assign)\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL match_beads: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
