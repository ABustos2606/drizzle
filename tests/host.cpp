// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#include "host.hpp"

#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <stdexcept>

namespace test {

void require(bool condition, const std::string& message)
{
    if (!condition) { throw std::runtime_error(message); }
}

Library::Library(const char* path) : library_(dlopen(path, RTLD_NOW | RTLD_LOCAL))
{
    if (!library_) { throw std::runtime_error(dlerror()); }
    get = reinterpret_cast<const LV2_Descriptor* (*)(uint32_t)>(dlsym(library_, "lv2_descriptor"));
    require(get, "lv2_descriptor export");
    descriptor = get(0);
    synth = get(1);
    require(descriptor && synth, "descriptors 0 and 1");
}

Library::~Library() { dlclose(library_); }

Instance::Instance(const LV2_Descriptor* d, double sample_rate, uint32_t size)
    : descriptor(d), rate(sample_rate), block(size),
      synth(std::strcmp(d->URI, drizzle::kSynthURI) == 0),
      port_count(synth ? drizzle::kSynthPortCount : drizzle::kPortCount)
{
    const LV2_Feature* features[] = {nullptr};
    handle = d->instantiate(d, rate, ".", features);
    require(handle, "instantiate at " + std::to_string(rate) + " Hz");
    for (auto& buffer : buffers_) { buffer.assign(size + 1, 0.0f); }
    // The Turtle defaults that are not 0, one assignment per line: ttl_check
    // reads them.
    if (synth) {
        using drizzle::SynthPort;
        // synth-defaults-begin
        control(SynthPort::time) = 0.5f;
        control(SynthPort::size) = 0.5f;
        control(SynthPort::shape) = 0.5f;
        control(SynthPort::density) = 0.7f;
        control(SynthPort::osc_grains) = 0.5f;
        control(SynthPort::seed_lock) = 1.0f;
        control(SynthPort::quality) = 3.0f;
        // synth-defaults-end
    } else {
        using drizzle::Port;
        // drizzle-defaults-begin
        control(Port::time) = 0.5f;
        control(Port::size) = 0.5f;
        control(Port::shape) = 0.5f;
        control(Port::density) = 0.7f;
        control(Port::dry_wet) = 0.5f;
        control(Port::seed_lock) = 1.0f;
        control(Port::quality) = 3.0f;
        control(Port::auto_gain) = 1.0f;
        control(Port::enabled) = 1.0f;
        // drizzle-defaults-end
    }
    // Hosts connect every port before activating (Ingen does).
    connect(0);
    activate();
}

Instance::~Instance()
{
    if (descriptor->deactivate) { descriptor->deactivate(handle); }
    descriptor->cleanup(handle);
}

void Instance::activate()
{
    if (descriptor->activate) { descriptor->activate(handle); }
}

void Instance::connect(uint32_t offset)
{
    for (uint32_t p = 0; p < port_count; ++p) {
        descriptor->connect_port(handle, p, is_signal(p) ? buffers_[p].data() + offset : &controls_[p]);
    }
}

void Instance::period()
{
    for (uint32_t p = 0; p < port_count; ++p) { buffers_[p][block] = kGuard; }
    uint32_t offset = 0;
    double duration = 0;
    while (offset < block) {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        const uint32_t count = random_slices ? std::min(1 + rng_ % 97, block - offset) : block;
        connect(offset);
        const auto start = std::chrono::steady_clock::now();
        descriptor->run(handle, count);
        duration += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
        offset += count;
    }
    if (measure) { run_times.push_back(duration); }
    for (uint32_t p = 0; p < port_count; ++p) {
        if (buffers_[p][block] != kGuard) {
            throw std::runtime_error("write past the end of a period, port " + std::to_string(p));
        }
    }
}

std::vector<Stereo> Instance::render(uint64_t frames, const Input& input)
{
    const uint32_t out_l = synth ? static_cast<uint32_t>(drizzle::SynthPort::out_l) : static_cast<uint32_t>(drizzle::Port::out_l);
    const uint32_t out_r = synth ? static_cast<uint32_t>(drizzle::SynthPort::out_r) : static_cast<uint32_t>(drizzle::Port::out_r);
    const uint32_t grain = synth ? static_cast<uint32_t>(drizzle::SynthPort::grain_out) : static_cast<uint32_t>(drizzle::Port::grain_out);
    std::vector<Stereo> output;
    output.reserve(frames);
    if (record_grain) { grain_out.reserve(grain_out.size() + frames); }
    while (output.size() < frames) {
        if (input) {
            for (uint32_t i = 0; i < block; ++i) { input(*this, i, frame + i); }
        }
        period();
        for (uint32_t i = 0; i < block && output.size() < frames; ++i) {
            output.push_back({buffers_[out_l][i], buffers_[out_r][i]});
            if (record_grain) { grain_out.push_back(buffers_[grain][i]); }
        }
        frame += block;
    }
    return output;
}

} // namespace test
