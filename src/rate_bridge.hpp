// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#pragma once

#include "block_buffer.hpp"
#include "resampler.hpp"

#include <cmath>
#include <cstdint>
#include <new>

namespace drizzle {

// Host rate -> processor rate -> host rate around a processor that runs in
// blocks of rate / 4000 frames. When the two rates are equal both resamplers
// are bypassed and the latency is one block.
class RateBridge {
public:
    // Allocates; not real-time.
    bool configure(uint32_t host_rate, uint32_t processor_rate) noexcept
    {
        try {
            rate_ = processor_rate;
            block_.size = processor_rate / kBlockRate;
            if (block_.size == 0 || block_.size > BlockBuffer::kMaxSize || processor_rate % kBlockRate) { return false; }
            if (!down_.configure(host_rate, processor_rate) || !up_.configure(processor_rate, host_rate)) { return false; }
            // Round the upsampler's delay up so the total latency is a whole
            // number of host frames when the rates allow.
            uint32_t up_delay = up_.delay();
            for (uint32_t extra = 0; extra < 8 && !up_.identity(); ++extra) {
                if ((static_cast<uint64_t>(up_delay + extra + block_.size) * host_rate) % processor_rate == 0) {
                    up_delay += extra;
                    break;
                }
            }
            if (!up_.configure(processor_rate, host_rate, up_delay)) { return false; }
            // The downsampler's delay in host frames, then the upsampler's and
            // the staging's in processor frames.
            exact_latency_ = down_.delay() +
                static_cast<double>(up_.delay() + block_.size) * host_rate / processor_rate;
            latency_ = static_cast<uint32_t>(std::lround(exact_latency_));
        } catch (const std::bad_alloc&) {
            return false;
        }
        reset();
        return true;
    }

    void reset() noexcept
    {
        down_.reset();
        up_.reset();
        block_.reset();
    }

    uint32_t rate() const { return rate_; }
    uint32_t block_size() const { return block_.size; }
    uint32_t latency() const { return latency_; }
    double exact_latency() const { return exact_latency_; }

    // One host frame: `in` and `out` are {left, right}; `freeze` and `seed`
    // are the gate inputs at this frame, staged with every processor frame
    // it produces. process(BlockBuffer&) runs the processor on a full block.
    template <typename Process>
    void frame(const float* in, bool freeze, bool seed, float* out, Process&& process) noexcept
    {
        down_.push(in);
        while (down_.ready()) {
            float processor_in[2];
            down_.next(processor_in);
            const uint32_t p = block_.position;
            block_.in[p] = {processor_in[0], processor_in[1]};
            block_.freeze[p] = freeze;
            block_.seed[p] = seed;
            const float processor_out[2] = {block_.out[p].l, block_.out[p].r};
            up_.push(processor_out);
            if (++block_.position == block_.size) {
                process(block_);
                block_.position = 0;
            }
        }
        // Always ready: a host frame's output needs processor frames up to
        // its own time, which the frames so far have produced.
        out[0] = out[1] = 0.0f;
        if (up_.ready()) { up_.next(out); }
    }

private:
    Resampler<2> down_;
    Resampler<2> up_;
    BlockBuffer block_{};
    uint32_t rate_ = 0;
    uint32_t latency_ = 0;
    double exact_latency_ = 0;
};

} // namespace drizzle
