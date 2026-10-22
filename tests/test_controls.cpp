// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
//
// controls: each knob, amount, switch and CV moves the matching processor
// parameter and nothing else, with cv_reader.cc's laws, filters and scales.
#include "../src/controls.hpp"

#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using drizzle::BlockControls;
using drizzle::Port;

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) { throw std::runtime_error(message); }
}

struct Rig {
    std::array<float, drizzle::kPortCount> controls{};
    std::array<std::vector<float>, drizzle::kPortCount> signals;
    drizzle::PortArray ports{};
    drizzle::Controls reader;

    Rig()
    {
        for (uint32_t p = 0; p < drizzle::kPortCount; ++p) {
            if (drizzle::is_signal(p)) {
                signals[p].assign(16, 0.0f);
                ports[p] = signals[p].data();
            } else {
                ports[p] = &controls[p];
            }
        }
        // The Turtle defaults.
        c(Port::time) = c(Port::size) = c(Port::shape) = 0.5f;
        c(Port::density) = 0.7f;
        c(Port::dry_wet) = 0.5f;
        c(Port::seed_lock) = 1;
        c(Port::quality) = 3;
        c(Port::auto_gain) = 1;
        c(Port::enabled) = 1;
        reader.reset();
    }
    float& c(Port p) { return controls[static_cast<uint32_t>(p)]; }
    void cv(Port p, float v) { std::fill(signals[static_cast<uint32_t>(p)].begin(), signals[static_cast<uint32_t>(p)].end(), v); }
    BlockControls read()
    {
        BlockControls out;
        reader.read(ports, 15, &out);
        return out;
    }
};

std::vector<std::pair<const char*, float>> fields(const BlockControls& b)
{
    const auto& p = b.parameters;
    return {
        {"time.base", p.time.base}, {"time.attenuverter", p.time.attenuverter}, {"time.cv", p.time.cv},
        {"time.has_cv", p.time.has_cv}, {"size.base", p.size.base}, {"size.attenuverter", p.size.attenuverter},
        {"size.cv", p.size.cv}, {"size.has_cv", p.size.has_cv}, {"shape.base", p.shape.base},
        {"shape.attenuverter", p.shape.attenuverter}, {"shape.cv", p.shape.cv}, {"shape.has_cv", p.shape.has_cv},
        {"pitch.base", p.pitch.base}, {"pitch.attenuverter", p.pitch.attenuverter}, {"pitch.cv", p.pitch.cv},
        {"pitch.has_cv", p.pitch.has_cv}, {"monophonic_mode", p.monophonic_mode},
        {"density.base", b.density.base}, {"density.cv", b.density.cv}, {"mix.feedback", b.mix.feedback},
        {"mix.dry_wet", b.mix.dry_wet}, {"mix.reverb_amount", b.mix.reverb_amount},
        {"mix.output_gate", b.mix.output_gate}, {"seed_button", static_cast<float>(b.seed_button)},
        {"freeze_button", b.freeze_button}, {"seed_uses_gate", b.seed_uses_gate},
    };
}

int cases = 0;

// Applies `change` to a fresh rig and checks that exactly `field` moved, to
// `expected`.
void only(const char* what, const std::function<void(Rig&)>& change, const char* field, float expected)
{
    Rig base, changed;
    change(changed);
    const auto a = fields(base.read()), b = fields(changed.read());
    for (size_t i = 0; i < a.size(); ++i) {
        const bool target = std::string(a[i].first) == field;
        if (target) {
            require(std::fabs(b[i].second - expected) < 1e-5f,
                    std::string(what) + ": " + field + " is " + std::to_string(b[i].second) + ", expected " +
                        std::to_string(expected));
        } else {
            require(a[i].second == b[i].second, std::string(what) + " also moved " + a[i].first);
        }
    }
    ++cases;
}

} // namespace

int main()
{
    try {
        // Defaults: Seed locked (the hardware starts locked), nothing else on.
        Rig rig;
        const auto d = rig.read();
        require(d.seed_button == beads::SEED_BUTTON_STATE_LOCKED && !d.freeze_button && !d.seed_uses_gate, "defaults");
        require(d.parameters.time.base == 0.5f && d.density.base == 0.7f && d.mix.dry_wet == 0.5f * 1.005f, "default values");

        only("Time", [](Rig& r) { r.c(Port::time) = 0.8f; }, "time.base", 0.8f);
        only("Size", [](Rig& r) { r.c(Port::size) = 0.9f; }, "size.base", 0.9f);
        only("Shape", [](Rig& r) { r.c(Port::shape) = 0.1f; }, "shape.base", 0.1f);
        only("Pitch", [](Rig& r) { r.c(Port::pitch) = -12; }, "pitch.base", -12.0f);
        only("Density", [](Rig& r) { r.c(Port::density) = 0.25f; }, "density.base", 0.25f);
        // PotChannel::LAW_ATTENUVERTER: full travel is +-1, with a dead zone at noon.
        only("Time mod", [](Rig& r) { r.c(Port::time_mod) = 1; }, "time.attenuverter", 1.0f);
        only("Size mod", [](Rig& r) { r.c(Port::size_mod) = -1; }, "size.attenuverter", -1.0f);
        only("Shape mod", [](Rig& r) { r.c(Port::shape_mod) = 0.5f; }, "shape.attenuverter",
             (2.0f / 0.47f) * 0.25f * (0.25f - 0.03f));
        only("Pitch mod", [](Rig& r) { r.c(Port::pitch_mod) = -0.5f; }, "pitch.attenuverter",
             (2.0f / 0.47f) * -0.25f * (0.25f - 0.03f));
        for (Port p : {Port::time_mod, Port::size_mod, Port::shape_mod, Port::pitch_mod}) {
            Rig a, b;
            b.c(p) = 0.05f; // inside the dead zone
            require(fields(a.read()) == fields(b.read()), "Mod knob dead zone at noon");
        }
        only("Time uses CV", [](Rig& r) { r.c(Port::time_uses_cv) = 1; }, "time.has_cv", 1);
        only("Size uses CV", [](Rig& r) { r.c(Port::size_uses_cv) = 1; }, "size.has_cv", 1);
        only("Shape uses CV", [](Rig& r) { r.c(Port::shape_uses_cv) = 1; }, "shape.has_cv", 1);
        only("Pitch uses CV", [](Rig& r) { r.c(Port::pitch_uses_cv) = 1; }, "pitch.has_cv", 1);
        // CV: 1.0 = 5 V. Time, Size and Shape reach +-1.6 at +-5 V through a
        // 0.5 one-pole per block; Pitch is 12 semitones per volt, unfiltered.
        only("Time CV", [](Rig& r) { r.cv(Port::time_cv, 0.5f); }, "time.cv", 0.5f * 0.5f * 1.6f);
        only("Size CV", [](Rig& r) { r.cv(Port::size_cv, -1.0f); }, "size.cv", -0.5f * 1.6f);
        only("Shape CV", [](Rig& r) { r.cv(Port::shape_cv, 3.0f); }, "shape.cv", 0.5f * 1.6f);
        only("Pitch CV", [](Rig& r) { r.cv(Port::pitch_cv, 0.2f); }, "pitch.cv", 12.0f);
        only("Density CV", [](Rig& r) { r.cv(Port::density_cv, -0.3f); }, "density.cv", -0.3f);
        // Mix pots: Process scales by 1.005 and clamps.
        only("Feedback", [](Rig& r) { r.c(Port::feedback) = 0.5f; }, "mix.feedback", 0.5f * 1.005f);
        only("Dry/wet", [](Rig& r) { r.c(Port::dry_wet) = 1; }, "mix.dry_wet", 1.0f);
        only("Reverb", [](Rig& r) { r.c(Port::reverb) = 0.2f; }, "mix.reverb_amount", 0.2f * 1.005f);
        only("Seed lock off", [](Rig& r) { r.c(Port::seed_lock) = 0; }, "seed_button", beads::SEED_BUTTON_STATE_OFF);
        only("Seed, unlocked", [](Rig& r) { r.c(Port::seed_lock) = 0; r.c(Port::seed) = 1; }, "seed_button",
             beads::SEED_BUTTON_STATE_ON);
        {
            Rig a, b;
            b.c(Port::seed) = 1;
            require(fields(a.read()) == fields(b.read()), "Seed while locked changes nothing");
        }
        only("Freeze", [](Rig& r) { r.c(Port::freeze) = 1; }, "freeze_button", 1);
        only("Seed uses gate", [](Rig& r) { r.c(Port::seed_uses_gate) = 1; }, "seed_uses_gate", 1);
        // Ports that do nothing until milestone 2.
        for (Port p : {Port::mono, Port::gain, Port::auto_gain, Port::assign_to, Port::gate_mode}) {
            Rig a, b;
            b.c(p) = 1;
            require(fields(a.read()) == fields(b.read()), "a milestone 2 port changes nothing yet");
        }
        {
            Rig a, b;
            b.cv(Port::assign_cv, 1);
            require(fields(a.read()) == fields(b.read()), "the Assign CV changes nothing yet");
        }

        // Pots glide: one-pole 0.05 per block after the first read, which
        // starts where the patch is.
        Rig glide;
        glide.read();
        glide.c(Port::size) = 1;
        const float first = glide.read().parameters.size.base;
        require(std::fabs(first - (0.5f + 0.05f * 0.5f)) < 1e-6f, "pot glide: 5% per block");
        for (int i = 0; i < 400; ++i) { glide.read(); }
        require(std::fabs(glide.read().parameters.size.base - 1.0f) < 1e-5f, "pot glide settles");
        // Time's hysteresis (0.001): a change smaller than that is ignored.
        Rig hysteresis;
        hysteresis.read();
        hysteresis.c(Port::time) = 0.5005f;
        for (int i = 0; i < 400; ++i) { hysteresis.read(); }
        require(hysteresis.read().parameters.time.base == 0.5f, "Time hysteresis");
        // CV filters settle to the full value.
        Rig settle;
        settle.cv(Port::time_cv, 0.5f);
        for (int i = 0; i < 100; ++i) { settle.read(); }
        require(std::fabs(settle.read().parameters.time.cv - 0.8f) < 1e-6f, "Time CV settles to 0.8 at 2.5 V");
        // Unconnected and non-finite controls fall back to the defaults.
        Rig broken;
        for (uint32_t p = 0; p < drizzle::kPortCount; ++p) {
            if (!drizzle::is_signal(p)) { broken.ports[p] = nullptr; }
        }
        broken.c(Port::time) = std::nanf("");
        require(fields(Rig().read()) == fields(broken.read()), "unconnected controls read their defaults");
        std::printf("PASS controls: %d cases; dead zone, glide, hysteresis, CV filters, fallbacks\n", cases);
    } catch (const std::exception& e) {
        std::cerr << "FAIL controls: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
