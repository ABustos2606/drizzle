// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
//
// Offline renderer for listening: loads drizzle.so in the test host, plays a
// WAV (or, without one, beads_test.cc's C4 saw and sine) into Drizzle, and
// writes a stereo 32-bit float WAV.
//
//   render drizzle.so out.wav [--rate 48000] [--seconds 8] [--in guitar.wav]
//          [--seed-every SECONDS] [symbol=value ...]
//
// Controls start at the plugin's defaults and are set by port symbol; a CV
// port given a value holds it, e.g. time_cv=0.2. --seed-every 0.5 pulses the
// Seed gate input every half second (set seed_uses_gate=1 to hear it clock
// the grains).
#include "../tests/host.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using drizzle::Port;

namespace {

// Port symbols in index order; tests/ttl_check.py holds the enum and the
// Turtle together, and the count here is checked against the enum.
const char* const kSymbols[] = {
    "in_l", "in_r", "out_l", "out_r", "time", "size", "shape", "pitch", "density", "time_mod", "size_mod",
    "shape_mod", "pitch_mod", "time_uses_cv", "size_uses_cv", "shape_uses_cv", "pitch_uses_cv", "feedback",
    "dry_wet", "reverb", "freeze", "seed", "seed_lock", "quality", "mono", "gain", "assign_to", "gate_mode",
    "output", "time_cv", "size_cv", "shape_cv", "pitch_cv", "density_cv", "assign_cv", "freeze_gate",
    "seed_gate", "grain_out", "seed_uses_gate", "auto_gain", "enabled", "latency",
};
static_assert(sizeof(kSymbols) / sizeof(kSymbols[0]) == drizzle::kPortCount, "one symbol per port");

uint32_t port_index(const std::string& symbol)
{
    for (uint32_t i = 0; i < drizzle::kPortCount; ++i) {
        if (symbol == kSymbols[i]) { return i; }
    }
    throw std::runtime_error("unknown port symbol: " + symbol);
}

template <typename T> T read_le(std::istream& in)
{
    T value{};
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return value;
}

// PCM 16/24/32-bit or float 32-bit WAV, mono or stereo (more channels: the
// first two). Its rate must match.
std::vector<test::Stereo> read_wav(const std::string& path, uint32_t rate)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) { throw std::runtime_error("cannot open " + path); }
    char id[4];
    in.read(id, 4); read_le<uint32_t>(in); in.read(id + 0, 4);
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t file_rate = 0;
    while (in.read(id, 4)) {
        const auto size = read_le<uint32_t>(in);
        const std::string chunk(id, 4);
        if (chunk == "fmt ") {
            format = read_le<uint16_t>(in); channels = read_le<uint16_t>(in); file_rate = read_le<uint32_t>(in);
            read_le<uint32_t>(in); read_le<uint16_t>(in); bits = read_le<uint16_t>(in);
            in.seekg(size - 16 + (size & 1), std::ios::cur);
            if (format == 0xFFFE) { format = bits == 32 ? 3 : 1; } // extensible: assume PCM or float by width
        } else if (chunk == "data") {
            if (!channels || !bits) { throw std::runtime_error(path + ": data before fmt"); }
            if (file_rate != rate) { throw std::runtime_error(path + ": rate " + std::to_string(file_rate) + ", not " + std::to_string(rate)); }
            std::vector<char> data(size);
            in.read(data.data(), size);
            const uint32_t width = bits / 8, frames = size / (width * channels);
            std::vector<test::Stereo> audio(frames);
            for (uint32_t f = 0; f < frames; ++f) {
                float v[2] = {0, 0};
                for (uint32_t c = 0; c < std::min<uint32_t>(channels, 2); ++c) {
                    const char* p = &data[(static_cast<size_t>(f) * channels + c) * width];
                    if (format == 3 && bits == 32) { std::memcpy(&v[c], p, 4); }
                    else if (bits == 16) { int16_t x; std::memcpy(&x, p, 2); v[c] = x / 32768.0f; }
                    else if (bits == 24) {
                        int32_t x = (static_cast<uint8_t>(p[0]) | static_cast<uint8_t>(p[1]) << 8 | static_cast<int8_t>(p[2]) * 65536);
                        v[c] = x / 8388608.0f;
                    } else if (bits == 32) { int32_t x; std::memcpy(&x, p, 4); v[c] = static_cast<float>(x / 2147483648.0); }
                    else { throw std::runtime_error(path + ": unsupported sample format"); }
                }
                audio[f] = {v[0], channels == 1 ? v[0] : v[1]};
            }
            return audio;
        } else {
            in.seekg(size + (size & 1), std::ios::cur);
        }
    }
    throw std::runtime_error(path + ": no data chunk");
}

void write_wav(const std::string& path, const std::vector<test::Stereo>& audio, uint32_t rate)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) { throw std::runtime_error("cannot write " + path); }
    const uint32_t bytes = static_cast<uint32_t>(audio.size() * 8);
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4); u32(36 + bytes); out.write("WAVE", 4);
    out.write("fmt ", 4); u32(16); u16(3); u16(2); u32(rate); u32(rate * 8); u16(8); u16(32);
    out.write("data", 4); u32(bytes);
    for (const auto& f : audio) { out.write(reinterpret_cast<const char*>(&f.l), 4); out.write(reinterpret_cast<const char*>(&f.r), 4); }
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc < 3) {
            std::cerr << "usage: render drizzle.so out.wav [--rate R] [--seconds S] [--in file.wav]"
                         " [--seed-every S] [symbol=value ...]\n";
            return 1;
        }
        uint32_t rate = 48000;
        double seconds = 8, seed_every = 0;
        std::string input;
        std::vector<std::pair<uint32_t, float>> settings;
        for (int i = 3; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() -> std::string {
                if (i + 1 >= argc) { throw std::runtime_error(arg + " needs a value"); }
                return argv[++i];
            };
            if (arg == "--rate") { rate = static_cast<uint32_t>(std::stoul(value())); }
            else if (arg == "--seconds") { seconds = std::stod(value()); }
            else if (arg == "--in") { input = value(); }
            else if (arg == "--seed-every") { seed_every = std::stod(value()); }
            else {
                const auto eq = arg.find('=');
                if (eq == std::string::npos) { throw std::runtime_error("unknown argument " + arg); }
                settings.emplace_back(port_index(arg.substr(0, eq)), std::stof(arg.substr(eq + 1)));
            }
        }
        const std::vector<test::Stereo> audio = input.empty() ? std::vector<test::Stereo>{} : read_wav(input, rate);

        test::Library library(argv[1]);
        test::Instance instance(library.descriptor, rate, 128);
        std::vector<std::pair<uint32_t, float>> held_cvs;
        for (const auto& [index, value] : settings) {
            if (drizzle::is_signal(index)) { held_cvs.emplace_back(index, value); }
            else { instance.control(static_cast<Port>(index)) = value; }
        }
        const auto out = instance.render(static_cast<uint64_t>(seconds * rate), [&](test::Instance& in, uint32_t i, uint64_t frame) {
            if (input.empty()) {
                const double phase = std::fmod(frame * 261.626 / rate, 1.0);
                in.signal(Port::in_l, i) = static_cast<float>(phase - 0.5);
                in.signal(Port::in_r, i) = static_cast<float>(std::sin(phase * 2.0 * M_PI) * 0.5);
            } else {
                const test::Stereo x = frame < audio.size() ? audio[frame] : test::Stereo{0, 0};
                in.signal(Port::in_l, i) = x.l;
                in.signal(Port::in_r, i) = x.r;
            }
            const double t = static_cast<double>(frame) / rate;
            in.signal(Port::seed_gate, i) = seed_every > 0 && std::fmod(t, seed_every) < 0.01 ? 1.0f : 0.0f;
            for (const auto& [index, value] : held_cvs) { in.signal(static_cast<Port>(index), i) = value; }
        });
        write_wav(argv[2], out, rate);
        float peak = 0;
        for (const auto& f : out) { peak = std::max({peak, std::fabs(f.l), std::fabs(f.r)}); }
        std::printf("wrote %s: %.2f s at %u Hz, peak %.3f, latency %.0f frames\n", argv[2], seconds, rate,
                    peak, instance.control(Port::latency));
    } catch (const std::exception& e) {
        std::cerr << "render: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
