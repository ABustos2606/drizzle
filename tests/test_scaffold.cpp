// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#include "host.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define DRIZZLE_COUNT_ALLOCATIONS 0
#else
#define DRIZZLE_COUNT_ALLOCATIONS 1
// Counts allocations made while `counting` is set on the calling thread
// (sanitizers replace malloc themselves, so not under SAN=1 or SAN=thread).
namespace {
thread_local bool counting = false;
std::atomic<int> allocations{0};
} // namespace
extern "C" void* __libc_malloc(size_t);
extern "C" void* __libc_calloc(size_t, size_t);
extern "C" void* malloc(size_t size)
{
    if (counting) { allocations.fetch_add(1); }
    return __libc_malloc(size);
}
extern "C" void* calloc(size_t count, size_t size)
{
    if (counting) { allocations.fetch_add(1); }
    return __libc_calloc(count, size);
}
#endif

using drizzle::Port;
using drizzle::SynthPort;
using test::Instance;
using test::require;
using test::Stereo;

namespace {

float peak(const std::vector<Stereo>& audio, size_t from = 0, size_t to = SIZE_MAX)
{
    float result = 0;
    for (size_t i = from; i < std::min(to, audio.size()); ++i) {
        require(std::isfinite(audio[i].l) && std::isfinite(audio[i].r), "finite output");
        result = std::max({result, std::fabs(audio[i].l), std::fabs(audio[i].r)});
    }
    return result;
}

size_t at(const Instance& instance, double seconds) { return static_cast<size_t>(seconds * instance.rate); }

float noise(uint64_t frame, uint32_t salt = 0)
{
    uint32_t x = static_cast<uint32_t>(frame) * 2654435761u + salt * 40503u;
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
    return static_cast<float>(x % 2001) / 2000.0f - 0.5f;
}

// beads_test.cc: AudioSource without a file, a C4 saw left and sine right.
void tone(Instance& instance, uint32_t i, uint64_t frame)
{
    const double phase = std::fmod(frame * 261.626 / instance.rate, 1.0);
    instance.signal(Port::in_l, i) = static_cast<float>(phase - 0.5);
    instance.signal(Port::in_r, i) = static_cast<float>(std::sin(phase * 2.0 * M_PI) * 0.5);
}

// Noise in, Time CV moving, a Seed gate every 0.25 s, Freeze gated for 0.3 s
// every 2 s: exercises audio, CV, both gates and the Random state.
void performance(Instance& instance, uint32_t i, uint64_t frame)
{
    const double t = static_cast<double>(frame) / instance.rate;
    instance.signal(Port::in_l, i) = noise(frame);
    instance.signal(Port::in_r, i) = noise(frame, 1);
    instance.signal(Port::time_cv, i) = static_cast<float>(0.4 * std::sin(t * 1.3));
    instance.signal(Port::pitch_cv, i) = static_cast<float>(0.1 * std::sin(t * 0.7));
    instance.signal(Port::seed_gate, i) = std::fmod(t, 0.25) < 0.05 ? 1.0f : 0.0f;
    instance.signal(Port::freeze_gate, i) = std::fmod(t, 2.0) > 1.7 ? 1.0f : 0.0f;
    instance.signal(Port::assign_cv, i) = static_cast<float>(0.3 * std::sin(t * 0.9));
}

// The same for Drizzle Synth, which has no audio input.
void synth_performance(Instance& instance, uint32_t i, uint64_t frame)
{
    const double t = static_cast<double>(frame) / instance.rate;
    instance.signal(SynthPort::time_cv, i) = static_cast<float>(0.4 * std::sin(t * 1.3));
    instance.signal(SynthPort::pitch_cv, i) = static_cast<float>(0.1 * std::sin(t * 0.7));
    instance.signal(SynthPort::seed_gate, i) = std::fmod(t, 0.25) < 0.05 ? 1.0f : 0.0f;
    instance.signal(SynthPort::freeze_gate, i) = std::fmod(t, 2.0) > 1.7 ? 1.0f : 0.0f;
    instance.signal(SynthPort::assign_cv, i) = static_cast<float>(0.3 * std::sin(t * 0.9));
}

void busy(Instance& instance)
{
    instance.control(Port::time_uses_cv) = 1;
    instance.control(Port::time_mod) = 0.6f;
    instance.control(Port::pitch_uses_cv) = 1;
    instance.control(Port::pitch_mod) = 1.0f;
    instance.control(Port::size_mod) = -0.5f;
    instance.control(Port::dry_wet) = 0.8f;
    instance.control(Port::feedback) = 0.4f;
    instance.control(Port::reverb) = 0.5f;
    instance.control(Port::assign_to) = 3;
}

void test_descriptor(const test::Library& library)
{
    require(std::strcmp(library.descriptor->URI, drizzle::kURI) == 0, "Drizzle URI");
    require(std::strcmp(library.synth->URI, drizzle::kSynthURI) == 0, "Drizzle Synth URI");
    require(!library.get(2) && !library.get(UINT32_MAX), "two descriptors");
    for (const auto* d : {library.descriptor, library.synth}) {
        for (double rate : {0.0, -1.0, 100.0, std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::infinity()}) {
            require(!d->instantiate(d, rate, ".", nullptr), "reject rate " + std::to_string(rate));
        }
        for (double rate : {24000.0, 32000.0, 44100.0, 48000.0, 96000.0}) {
            auto h = d->instantiate(d, rate, ".", nullptr);
            require(h, "instantiate at " + std::to_string(rate));
            // Unconnected ports and unknown indices must not crash.
            d->connect_port(h, 999, nullptr);
            d->run(h, 0);
            d->run(h, 128);
            if (d->activate) { d->activate(h); }
            d->run(h, 37);
            d->cleanup(h);
        }
    }
    std::cout << "PASS descriptor: two plugins, rates, unconnected ports, zero frames\n";
}

void test_audio(const LV2_Descriptor* d)
{
    // Default settings: the processor plays grains of the input.
    Instance instance(d, 48000);
    const auto out = instance.render(at(instance, 3), tone);
    require(peak(out, at(instance, 1)) > 0.05f, "default settings make sound: peak " + std::to_string(peak(out)));
    const float latency = instance.control(Port::latency);
    require(latency > 12 && latency < 200, "latency port: " + std::to_string(latency));

    // Dry only, with the gain at 1: the input times 0.75 (Processor::Process,
    // post_gain_dry), late by the reported latency.
    Instance dry(d, 48000);
    dry.control(Port::dry_wet) = 0;
    dry.control(Port::auto_gain) = 0;
    const auto dry_out = dry.render(at(dry, 1), tone);
    const auto lag = static_cast<uint64_t>(latency);
    float error = 0;
    for (uint64_t i = at(dry, 0.1); i < dry_out.size(); ++i) {
        const double phase = std::fmod((i - lag) * 261.626 / 48000.0, 1.0);
        error = std::max(error, std::fabs(dry_out[i].l - 0.75f * static_cast<float>(phase - 0.5)));
        error = std::max(error, std::fabs(dry_out[i].r - 0.75f * static_cast<float>(std::sin(phase * 2.0 * M_PI) * 0.5)));
    }
    require(error < 1e-4f, "dry path is the input times 0.75, late by the latency: error " + std::to_string(error));
    std::cout << "PASS audio: default settings play grains; dry path exact; latency " << latency << " frames\n";
}

float max_step(const std::vector<Stereo>& audio, size_t from, size_t to)
{
    float result = 0;
    for (size_t i = std::max<size_t>(from, 1); i < std::min(to, audio.size()); ++i) {
        result = std::max({result, std::fabs(audio[i].l - audio[i - 1].l), std::fabs(audio[i].r - audio[i - 1].r)});
    }
    return result;
}

// Counts rising edges of Grain out and checks every high stretch is
// `length` frames long (0: any length).
size_t pulses(const std::vector<float>& cv, size_t length)
{
    size_t count = 0, high = 0;
    for (size_t i = 0; i < cv.size(); ++i) {
        require(cv[i] == 0.0f || cv[i] == 1.0f, "Grain out is 0 or 1");
        if (cv[i] == 1.0f) {
            ++high;
        } else {
            if (high) { require(!length || high == length, "trigger length " + std::to_string(high)); }
            count += high != 0;
            high = 0;
        }
    }
    return count;
}

void test_synth(const LV2_Descriptor* d)
{
    // Default settings: free-running grains of the wavetables, with no input.
    Instance synth(d, 48000);
    synth.record_grain = true;
    const auto out = synth.render(at(synth, 3));
    require(peak(out, at(synth, 0.5)) > 0.05f, "Drizzle Synth plays with default settings: " + std::to_string(peak(out)));
    require(synth.control(SynthPort::latency) == 12, "synth latency: one block");
    require(pulses(synth.grain_out, 480) > 10, "synth Grain out: 10 ms triggers");

    // Each control it shares with Drizzle changes the sound.
    const auto with = [&](SynthPort port, float value) {
        Instance changed(d, 48000);
        changed.control(port) = value;
        return changed.render(at(changed, 3));
    };
    for (auto [port, value] : {std::pair{SynthPort::wavetable, 0.9f}, {SynthPort::osc_grains, 1.0f},
                               {SynthPort::quality, 0.0f}, {SynthPort::pitch, 7.0f}, {SynthPort::reverb, 0.8f}}) {
        require(!(with(port, value) == out), "synth control " + std::to_string(static_cast<uint32_t>(port)) + " changes the sound");
    }
    // Seed unlocked and not pressed, Osc/grains all grains: silence.
    Instance quiet(d, 48000);
    quiet.control(SynthPort::seed_lock) = 0;
    quiet.control(SynthPort::osc_grains) = 1;
    require(peak(quiet.render(at(quiet, 2)), at(quiet, 0.5)) < 1e-4f, "synth: no grains while Seed is off");

    // Slicing, other rates, activate.
    for (double rate : {48000.0, 44100.0}) {
        Instance whole(d, rate, 128), sliced(d, rate, 128), small(d, rate, 37);
        whole.random_slices = false;
        for (auto* instance : {&whole, &sliced, &small}) {
            instance->control(SynthPort::seed_uses_gate) = 1;
            instance->control(SynthPort::time_uses_cv) = 1;
            instance->control(SynthPort::assign_to) = 3;
        }
        const auto frames = static_cast<uint64_t>(2 * rate);
        const auto a = whole.render(frames, synth_performance);
        require(peak(a) > 0.01f, "synth slicing test makes sound");
        require(a == sliced.render(frames, synth_performance) && a == small.render(frames, synth_performance),
                "synth: same output for any slicing at " + std::to_string(rate) + " Hz");
        whole.activate();
        whole.frame = 0;
        require(whole.render(frames, synth_performance) == a, "synth: activate restarts it");
    }
    std::cout << "PASS synth: plays with no input, controls change it, Seed off is silent, 10 ms grain triggers, "
                 "slicing, 44.1 kHz, activate\n";
}

void test_mono(const LV2_Descriptor* d)
{
    // Mono on: In R is not heard; In L goes to both sides of the buffer.
    const auto render = [&](bool mono, bool right) {
        Instance instance(d, 48000);
        busy(instance);
        instance.control(Port::mono) = mono ? 1.0f : 0.0f;
        return instance.render(at(instance, 3), [right](Instance& in, uint32_t i, uint64_t frame) {
            performance(in, i, frame);
            if (!right) { in.signal(Port::in_r, i) = 0; }
        });
    };
    const auto mono = render(true, true);
    require(peak(mono, at(Instance(d, 48000), 1)) > 0.01f, "Mono sounds");
    require(mono == render(true, false), "Mono: In R is ignored");
    require(!(render(false, true) == render(false, false)), "stereo: In R is heard");

    // Switching Mono while running: faded like a quality change.
    Instance instance(d, 48000);
    instance.control(Port::dry_wet) = 0;
    instance.control(Port::auto_gain) = 0;
    const auto sine = [](Instance& in, uint32_t i, uint64_t frame) {
        const float x = static_cast<float>(0.5 * std::sin(frame * 261.626 / 48000.0 * 2.0 * M_PI));
        in.signal(Port::in_l, i) = in.signal(Port::in_r, i) = x;
    };
    const size_t segment = 187 * 128;
    std::vector<Stereo> out;
    for (int k = 0; k < 4; ++k) {
        instance.control(Port::mono) = static_cast<float>(k % 2);
        for (auto& f : instance.render(segment, sine)) { out.push_back(f); }
    }
    const float steady = max_step(out, at(instance, 0.1), segment);
    float worst = 0;
    for (size_t k = 1; k < 4; ++k) {
        worst = std::max(worst, max_step(out, segment * k, segment * k + at(instance, 0.1)));
        require(peak(out, segment * k + at(instance, 0.05), segment * (k + 1)) > 0.2f, "sound after a Mono change");
    }
    require(worst < 2 * steady, "Mono switch click: step " + std::to_string(worst));
    std::cout << "PASS mono: In R ignored, In L recorded; switching steps at most " << worst << " (steady " << steady
              << ")\n";
}

// The dry path's level after `seconds` of a 0.1 sine, whose output is
// 0.75 * gain * 0.1.
float dry_level(const LV2_Descriptor* d, float auto_gain, float gain_db, float seconds)
{
    Instance instance(d, 48000);
    instance.control(Port::dry_wet) = 0;
    instance.control(Port::auto_gain) = auto_gain;
    instance.control(Port::gain) = gain_db;
    const auto out = instance.render(at(instance, seconds), [](Instance& in, uint32_t i, uint64_t frame) {
        in.signal(Port::in_l, i) = in.signal(Port::in_r, i) =
            static_cast<float>(0.1 * std::sin(frame * 220.0 / 48000.0 * 2.0 * M_PI));
    });
    return peak(out, out.size() - at(instance, 0.1)) / 0.075f;
}

void test_gain_port(const LV2_Descriptor* d)
{
    const float unity = dry_level(d, 0, 0, 1);
    const float twelve = dry_level(d, 0, 12, 1);
    const float top = dry_level(d, 0, 32, 1);
    const float learnt = dry_level(d, 1, 0, 8);
    require(std::fabs(unity - 1.0f) < 1e-3f, "Input gain 0 dB: " + std::to_string(unity));
    require(std::fabs(twelve - std::pow(10.0f, 0.6f)) < 0.01f, "Input gain 12 dB: " + std::to_string(twelve));
    require(top > 9.9f && top < 10.1f, "Input gain 32 dB clips at full scale: " + std::to_string(top));
    require(std::fabs(learnt - 9.0f) < 0.5f, "Auto gain learns 0.9 / peak: " + std::to_string(learnt));
    std::cout << "PASS gain: Input gain 0, 12 and 32 dB (clipped); Auto gain brings a 0.1 sine to 0.9 (" << learnt
              << " times)\n";
}

void test_grain_out(const LV2_Descriptor* d)
{
    for (double rate : {48000.0, 44100.0}) {
        Instance trigger(d, rate), gate(d, rate);
        for (auto* instance : {&trigger, &gate}) {
            instance->record_grain = true;
            busy(*instance);
        }
        gate.control(Port::gate_mode) = 1;
        trigger.render(at(trigger, 3), performance);
        gate.render(at(gate, 3), performance);
        // drizzle.hpp: kTriggerSeconds, 10 ms.
        const auto length = static_cast<size_t>(0.01 * rate);
        const size_t triggers = pulses(trigger.grain_out, length);
        require(triggers > 10, "Grain out triggers: " + std::to_string(triggers));
        // The same grains in either mode: each trigger starts where a gate
        // rises.
        for (size_t i = 1; i < trigger.grain_out.size(); ++i) {
            const bool rise_t = trigger.grain_out[i] > trigger.grain_out[i - 1];
            const bool rise_g = gate.grain_out[i] > gate.grain_out[i - 1];
            if (rise_t) { require(rise_g, "a trigger without a gate"); }
        }
        require(!(trigger.grain_out == gate.grain_out), "Gate mode differs from Trigger mode");
    }
    // Bypassed or not, the processor keeps playing, so Grain out goes on.
    Instance off(d, 48000);
    off.record_grain = true;
    off.control(Port::enabled) = 0;
    off.render(48000, performance);
    require(pulses(off.grain_out, 480) > 1, "Grain out while bypassed");
    std::cout << "PASS grain_out: 10 ms triggers on each grain, gates in Gate mode, at 48 and 44.1 kHz\n";
}

void test_slicing(const LV2_Descriptor* d)
{
    for (double rate : {48000.0, 44100.0}) {
        for (float quality : {3.0f, 2.0f, 0.0f}) {
            Instance whole(d, rate, 128), sliced(d, rate, 128), small(d, rate, 37);
            whole.random_slices = false;
            for (auto* instance : {&whole, &sliced, &small}) {
                busy(*instance);
                instance->control(Port::quality) = quality;
                instance->control(Port::seed_uses_gate) = 1;
                instance->control(Port::mono) = quality == 2.0f;
            }
            const auto frames = static_cast<uint64_t>(3 * rate);
            const auto a = whole.render(frames, performance);
            const auto b = sliced.render(frames, performance);
            const auto c = small.render(frames, performance);
            require(peak(a) > 0.01f, "slicing test makes sound");
            require(a == b && a == c, "same output for any slicing at " + std::to_string(rate) + " Hz, quality " +
                                          std::to_string(quality));
        }
    }
    std::cout << "PASS slicing: identical output for whole periods, random slices, odd period length "
                 "(48 and 44.1 kHz; digital, vintage digital, dirty tape)\n";
}

void test_instances(const LV2_Descriptor* d)
{
    // Grains draw from stmlib::Random: every instance keeps its own sequence
    // whether alone, interleaved on one thread or on two.
    const uint64_t frames = 3 * 48000;
    Instance alone(d, 48000);
    busy(alone);
    const auto reference = alone.render(frames, performance);

    Instance a(d, 48000), b(d, 48000);
    busy(a); busy(b);
    std::vector<Stereo> out_a, out_b;
    for (uint64_t i = 0; i < frames / 1024 + 1; ++i) {
        for (auto& f : a.render(1024, performance)) { out_a.push_back(f); }
        for (auto& f : b.render(1024, performance)) { out_b.push_back(f); }
    }
    out_a.resize(reference.size()); out_b.resize(reference.size());
    require(out_a == reference && out_b == reference, "interleaved instances on one thread");

    Instance t1(d, 48000), t2(d, 48000);
    busy(t1); busy(t2);
    std::vector<Stereo> r1, r2;
    std::thread first([&] { r1 = t1.render(frames, performance); });
    std::thread second([&] { r2 = t2.render(frames, performance); });
    first.join(); second.join();
    require(r1 == reference && r2 == reference, "instances on two threads");

    alone.activate();
    alone.frame = 0;
    require(alone.render(frames, performance) == reference, "activate restarts the processor");
    std::cout << "PASS instances: own Random sequence alone, interleaved and threaded; activate resets\n";
}

void test_qualities(const LV2_Descriptor* d)
{
    std::vector<Stereo> renders[4];
    float latency[4];
    for (int q = 0; q < 4; ++q) {
        Instance instance(d, 48000);
        busy(instance);
        instance.control(Port::quality) = static_cast<float>(q);
        renders[q] = instance.render(at(instance, 3), performance);
        latency[q] = instance.control(Port::latency);
        require(peak(renders[q], at(instance, 1)) > 0.01f, "quality " + std::to_string(q) + " sounds");
    }
    for (int q = 1; q < 4; ++q) {
        require(!(renders[q] == renders[0]), "qualities differ");
        require(latency[q] == latency[0], "every quality reports the same latency");
    }

    // Switching while running, on the dry path (a 261 Hz sine at 0.5, so
    // 0.375 out): the fade keeps every step small; an unfaded switch would
    // jump by up to 0.375.
    Instance instance(d, 48000);
    instance.control(Port::dry_wet) = 0;
    const auto sine = [](Instance& in, uint32_t i, uint64_t frame) {
        const float x = static_cast<float>(0.5 * std::sin(frame * 261.626 / 48000.0 * 2.0 * M_PI));
        in.signal(Port::in_l, i) = in.signal(Port::in_r, i) = x;
    };
    const int order[] = {3, 2, 1, 0, 3, 0, 2};
    const size_t segment = 187 * 128; // whole periods, about 0.5 s
    std::vector<Stereo> out;
    for (int q : order) {
        instance.control(Port::quality) = static_cast<float>(q);
        for (auto& f : instance.render(segment, sine)) { out.push_back(f); }
    }
    const float steady = max_step(out, at(instance, 0.1), segment);
    float worst = 0;
    for (size_t k = 1; k < std::size(order); ++k) {
        const size_t s = segment * k;
        worst = std::max(worst, max_step(out, s, s + at(instance, 0.1)));
        // Back in sound 50 ms after the change.
        require(peak(out, s + at(instance, 0.05), s + segment) > 0.2f, "sound after a quality change");
    }
    require(worst < 2 * steady, "quality switch click: step " + std::to_string(worst));
    std::cout << "PASS qualities: four qualities sound and differ, same latency; switching steps at most " << worst
              << " (steady " << steady << ")\n";
}

void test_freeze_seed(const LV2_Descriptor* d)
{
    const auto wet_only = [](Instance& instance) {
        instance.control(Port::dry_wet) = 1;
        instance.control(Port::size) = 0.3f;
    };
    // Freeze stops recording: frozen on silence, later noise is never heard.
    for (bool frozen : {true, false}) {
        Instance instance(d, 48000);
        wet_only(instance);
        instance.control(Port::freeze) = frozen ? 1.0f : 0.0f;
        const auto out = instance.render(at(instance, 4), [](Instance& in, uint32_t i, uint64_t frame) {
            const bool loud = frame > 48000;
            in.signal(Port::in_l, i) = loud ? noise(frame) : 0.0f;
            in.signal(Port::in_r, i) = loud ? noise(frame, 1) : 0.0f;
        });
        if (frozen) {
            require(peak(out) == 0.0f, "Freeze: nothing recorded, silence: " + std::to_string(peak(out)));
        } else {
            require(peak(out, at(instance, 2)) > 0.01f, "not frozen: the noise is heard");
        }
    }
    // The Freeze gate does the same.
    Instance gated(d, 48000);
    wet_only(gated);
    const auto gated_out = gated.render(at(gated, 4), [](Instance& in, uint32_t i, uint64_t frame) {
        const bool loud = frame > 48000;
        in.signal(Port::in_l, i) = loud ? noise(frame) : 0.0f;
        in.signal(Port::freeze_gate, i) = 1.0f;
    });
    require(peak(gated_out) == 0.0f, "Freeze gate: nothing recorded");

    // Seed unlocked: grains only while Seed is held.
    Instance seed(d, 48000);
    wet_only(seed);
    seed.control(Port::seed_lock) = 0;
    const auto held = seed.render(at(seed, 6), [](Instance& in, uint32_t i, uint64_t frame) {
        in.signal(Port::in_l, i) = noise(frame);
        in.signal(Port::in_r, i) = noise(frame, 1);
        if (i == 0) { in.control(Port::seed) = frame >= 2 * 48000 && frame < 4 * 48000 ? 1.0f : 0.0f; }
    });
    require(peak(held, 0, at(seed, 2)) == 0.0f, "unlocked, Seed off: no grains");
    require(peak(held, at(seed, 2), at(seed, 4)) > 0.01f, "Seed held: grains");
    require(peak(held, at(seed, 5.5)) < 1e-4f, "Seed released: grains stop");

    // Manual taps (Density at noon): one grain per rising edge, none while held.
    Instance taps(d, 48000);
    wet_only(taps);
    taps.control(Port::seed_lock) = 0;
    taps.control(Port::density) = 0.5f;
    taps.control(Port::size) = 0.3f;
    // Time near the write head: at 0.5 a grain started 2 s in reads the
    // buffer from before the input began, which is silent.
    taps.control(Port::time) = 0.1f;
    const auto tapped = taps.render(at(taps, 4), [](Instance& in, uint32_t i, uint64_t frame) {
        in.signal(Port::in_l, i) = noise(frame);
        in.signal(Port::in_r, i) = noise(frame, 1);
        if (i == 0) { in.control(Port::seed) = frame >= 2 * 48000 ? 1.0f : 0.0f; }
    });
    require(peak(tapped, at(taps, 2), at(taps, 2.3)) > 0.01f, "a tap plays a grain");
    require(peak(tapped, at(taps, 3), at(taps, 4)) < 1e-4f, "holding Seed plays no more grains");

    // Locked with Seed uses gate: the gate clocks the grains (Density at
    // the top: every pulse). With no pulses, silence.
    for (bool pulses : {true, false}) {
        Instance clocked(d, 48000);
        wet_only(clocked);
        clocked.control(Port::seed_uses_gate) = 1;
        clocked.control(Port::density) = 1;
        const auto out = clocked.render(at(clocked, 3), [pulses](Instance& in, uint32_t i, uint64_t frame) {
            in.signal(Port::in_l, i) = noise(frame);
            in.signal(Port::in_r, i) = noise(frame, 1);
            in.signal(Port::seed_gate, i) = pulses && frame % 12000 < 600 ? 1.0f : 0.0f;
        });
        if (pulses) {
            require(peak(out, at(clocked, 1)) > 0.01f, "clocked by the Seed gate: grains");
        } else {
            require(peak(out) < 1e-4f, "Seed uses gate, no pulses: no grains");
        }
    }
    std::cout << "PASS freeze_seed: Freeze and its gate stop recording; Seed held, taps on rising edges, "
                 "gate-clocked grains\n";
}

void test_bypass(const LV2_Descriptor* d)
{
    // Bypassed from the start: the input, exactly (nothing recorded, so the
    // processor plays silence).
    Instance off(d, 48000);
    off.control(Port::enabled) = 0;
    const auto out = off.render(48000, performance);
    for (size_t i = 0; i < out.size(); ++i) {
        require(out[i].l == noise(i) && out[i].r == noise(i, 1), "bypassed: the input exactly");
    }
    // Bypassed while playing: the tails ring on.
    Instance tails(d, 48000);
    tails.control(Port::dry_wet) = 1;
    tails.control(Port::reverb) = 0.8f;
    const auto played = tails.render(at(tails, 4), [](Instance& in, uint32_t i, uint64_t frame) {
        const bool playing = frame < 2 * 48000;
        in.signal(Port::in_l, i) = playing ? noise(frame) : 0.0f;
        in.signal(Port::in_r, i) = playing ? noise(frame, 1) : 0.0f;
        if (i == 0) { in.control(Port::enabled) = playing ? 1.0f : 0.0f; }
    });
    require(peak(played, at(tails, 2.2), at(tails, 2.6)) > 0.001f, "bypass lets the tails ring out");
    std::cout << "PASS bypass: the input exactly; tails ring out\n";
}

void test_nonfinite(const LV2_Descriptor* d)
{
    Instance instance(d, 48000);
    busy(instance);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (Port p : {Port::time, Port::pitch, Port::quality, Port::output, Port::dry_wet, Port::gain, Port::assign_to,
                   Port::mono, Port::auto_gain, Port::gate_mode}) {
        instance.control(p) = nan;
    }
    const auto out = instance.render(48000, [&](Instance& in, uint32_t i, uint64_t frame) {
        performance(in, i, frame);
        in.signal(Port::in_l, i) = frame % 3 ? nan : inf;
        in.signal(Port::in_r, i) = 1e30f;
        in.signal(Port::time_cv, i) = frame % 2 ? -inf : nan;
        in.signal(Port::pitch_cv, i) = 1e30f;
        in.signal(Port::seed_gate, i) = nan;
        in.signal(Port::assign_cv, i) = frame % 2 ? inf : nan;
    });
    peak(out);
    for (Port p : {Port::time, Port::pitch, Port::quality, Port::output, Port::dry_wet}) { instance.control(p) = 0.5f; }
    instance.control(Port::gain) = instance.control(Port::assign_to) = instance.control(Port::mono) = 0;
    instance.control(Port::auto_gain) = 1;
    instance.control(Port::gate_mode) = 0;
    instance.control(Port::quality) = 3;
    instance.control(Port::output) = 0;
    const auto after = instance.render(at(instance, 3), performance);
    require(peak(after, at(instance, 1)) > 0.01f, "recovers after non-finite input");
    std::cout << "PASS nonfinite: NaN, infinite and huge inputs and controls leave the output finite; recovers\n";
}

void test_allocation(const LV2_Descriptor* d, const LV2_Descriptor* synth_d)
{
#if DRIZZLE_COUNT_ALLOCATIONS
    // run() on a thread that has never run Drizzle: its first touch of the
    // thread_local Random state must not allocate (initial-exec TLS). A
    // quality change in the middle.
    Instance instance(d, 48000, 128);
    busy(instance);
    std::thread worker([&] {
        counting = true;
        instance.render(48000, performance);
        instance.control(Port::quality) = 2;
        instance.render(48000, performance);
        instance.control(Port::mono) = 1;
        instance.control(Port::auto_gain) = 0;
        instance.render(48000, performance);
        counting = false;
    });
    worker.join();
    // render() itself allocates once for each result vector; nothing else may.
    require(allocations.load() <= 3, "run() allocates: " + std::to_string(allocations.load()));
    Instance synth(synth_d, 48000, 128);
    std::thread synth_worker([&] {
        counting = true;
        synth.render(48000, synth_performance);
        counting = false;
    });
    synth_worker.join();
    require(allocations.load() <= 4, "Drizzle Synth's run() allocates: " + std::to_string(allocations.load()));
    std::cout << "PASS allocation: no allocation in run(), on a new thread, across quality and Mono changes; "
                 "Drizzle Synth too\n";
#else
    (void)d;
    (void)synth_d;
    std::cout << "SKIP allocation: sanitizer build\n";
#endif
}

// rt_timing: 10 simulated minutes of random controls, every quality, Density
// often at its maximum.
void test_timing(const LV2_Descriptor* d)
{
    Instance instance(d, 48000, 128);
    instance.measure = true;
    const double seconds = 600;
    uint32_t rng = 0x9E3779B9;
    const auto random = [&rng] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng % 10000) / 10000.0f; };
    instance.render(static_cast<uint64_t>(seconds * 48000), [&](Instance& in, uint32_t i, uint64_t frame) {
        performance(in, i, frame);
        if (i == 0 && frame % 24000 == 0) {
            for (Port p : {Port::time, Port::size, Port::shape, Port::feedback, Port::dry_wet, Port::reverb}) {
                in.control(p) = random();
            }
            for (Port p : {Port::time_mod, Port::size_mod, Port::shape_mod, Port::pitch_mod}) {
                in.control(p) = 2 * random() - 1;
            }
            in.control(Port::density) = random() < 0.5f ? 1.0f : random();
            in.control(Port::pitch) = std::round(48 * random() - 24);
            in.control(Port::seed_uses_gate) = random() < 0.3f;
            in.control(Port::assign_to) = std::floor(4 * random());
            in.control(Port::auto_gain) = random() < 0.5f;
            in.control(Port::gain) = 32 * random();
            if (frame % (48000 * 30) == 0) {
                in.control(Port::quality) = std::floor(4 * random());
                in.control(Port::mono) = random() < 0.3f;
            }
        }
    });
    auto times = instance.run_times;
    std::sort(times.begin(), times.end());
    const double period_us = 128 / 48000.0 * 1e6;
    double total = 0;
    for (double t : times) { total += t; }
    std::cout << "TIMING rt_timing, 48 kHz, 128 frames (" << period_us << " us), 10 simulated minutes: mean "
              << total / times.size() << " us, 99.9% " << times[times.size() * 999 / 1000] << " us, worst "
              << times.back() << " us, load " << 100 * total / (seconds * 1e6) << "%\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::cerr << "usage: test_scaffold drizzle.so [--timing]\n"; return 1; }
    try {
        test::Library library(argv[1]);
        if (argc > 2 && std::strcmp(argv[2], "--timing") == 0) {
            test_timing(library.descriptor);
            return 0;
        }
        test_descriptor(library);
        test_audio(library.descriptor);
        test_synth(library.synth);
        test_mono(library.descriptor);
        test_gain_port(library.descriptor);
        test_grain_out(library.descriptor);
        test_slicing(library.descriptor);
        test_instances(library.descriptor);
        test_qualities(library.descriptor);
        test_freeze_seed(library.descriptor);
        test_bypass(library.descriptor);
        test_nonfinite(library.descriptor);
        test_allocation(library.descriptor, library.synth);
    } catch (const std::exception& e) {
        std::cerr << "FAIL scaffold: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
