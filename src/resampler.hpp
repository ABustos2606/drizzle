// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace drizzle {

// Polyphase windowed-sinc resampler between any two integer rates, for
// `Channels` channels sharing one clock. The rate ratio is kept as an exact
// fraction, so input and output stay locked however long it runs.
//
// Output m is the input band-limited and read at input position
// m * in_rate / out_rate - delay(), in input samples. It is ready once the
// input sample at floor(m * in_rate / out_rate) has been pushed.
//
// configure() allocates; push(), ready() and next() do not.
template <uint32_t Channels>
class Resampler {
public:
    // Filter phases. Divisible by 2, 3 and 6: the 3:2 and 2:3 ratios of
    // 48 kHz <-> 32 kHz, and 3:1 of 96 kHz, land on exact rows.
    static constexpr uint32_t kPhases = 192;
    // Stopband attenuation of the Kaiser design, dB.
    static constexpr double kAttenuation = 80.0;

    // `min_delay` raises the delay (half the filter length) to at least that
    // many input samples. Returns false for invalid rates.
    bool configure(uint32_t in_rate, uint32_t out_rate, uint32_t min_delay = 0)
    {
        if (in_rate == 0 || out_rate == 0) { return false; }
        const uint32_t g = std::gcd(in_rate, out_rate);
        step_ = in_rate / g;
        denominator_ = out_rate / g;
        identity_ = in_rate == out_rate;
        half_ = 0;
        taps_ = 1;
        if (!identity_) {
            // Transition band: 1/8 of the lower rate, centred on its Nyquist
            // frequency (14 to 18 kHz when the lower rate is 32 kHz).
            const double lower = std::min(in_rate, out_rate);
            const double cutoff = 0.5 * lower / in_rate;    // cycles per input sample
            const double transition = 0.125 * lower / in_rate;
            const double length = (kAttenuation - 8.0) / (2.285 * 2.0 * M_PI * transition);
            half_ = std::max(static_cast<uint32_t>(std::ceil(length / 2.0)), min_delay);
            taps_ = 2 * half_;
            const double beta = 0.1102 * (kAttenuation - 8.7);
            table_.assign(static_cast<size_t>(kPhases + 1) * taps_, 0.0f);
            for (uint32_t phase = 0; phase <= kPhases; ++phase) {
                const double fraction = static_cast<double>(phase) / kPhases;
                std::vector<double> row(taps_);
                double sum = 0;
                for (uint32_t i = 0; i < taps_; ++i) {
                    // Window slot i holds x[n - taps + 1 + i]; its distance from
                    // the read position n + fraction - half is t.
                    const double t = static_cast<double>(taps_ - 1 - i) + fraction - half_;
                    const double x = 2.0 * cutoff * t;
                    const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
                    const double r = t / half_;
                    const double window = r * r < 1.0 ? bessel_i0(beta * std::sqrt(1.0 - r * r)) / bessel_i0(beta) : 0.0;
                    row[i] = 2.0 * cutoff * sinc * window;
                    sum += row[i];
                }
                // Unity gain at DC in every phase.
                for (uint32_t i = 0; i < taps_; ++i) {
                    table_[static_cast<size_t>(phase) * taps_ + i] = static_cast<float>(row[i] / sum);
                }
            }
        }
        size_ = 1;
        while (size_ < taps_ + 16) { size_ <<= 1; }
        for (auto& h : history_) { h.assign(2 * static_cast<size_t>(size_), 0.0f); }
        reset();
        return true;
    }

    void reset()
    {
        for (auto& h : history_) { std::fill(h.begin(), h.end(), 0.0f); }
        pushed_ = 0;
        center_ = 0;
        remainder_ = 0;
    }

    // Delay in input samples (0 when the rates are equal).
    uint32_t delay() const { return half_; }
    bool identity() const { return identity_; }

    void push(const float* x)
    {
        const uint32_t slot = static_cast<uint32_t>(pushed_) & (size_ - 1);
        for (uint32_t c = 0; c < Channels; ++c) {
            history_[c][slot] = x[c];
            history_[c][slot + size_] = x[c];
        }
        ++pushed_;
    }

    bool ready() const { return pushed_ > center_; }

    void next(float* y)
    {
        const uint32_t start = static_cast<uint32_t>(center_ - taps_ + 1) & (size_ - 1);
        if (identity_) {
            for (uint32_t c = 0; c < Channels; ++c) { y[c] = history_[c][start]; }
        } else {
            const uint64_t position = static_cast<uint64_t>(remainder_) * kPhases;
            const uint32_t row = static_cast<uint32_t>(position / denominator_);
            const uint32_t rest = static_cast<uint32_t>(position % denominator_);
            const float* a = &table_[static_cast<size_t>(row) * taps_];
            for (uint32_t c = 0; c < Channels; ++c) {
                const float* w = &history_[c][start];
                float sum = 0;
                for (uint32_t i = 0; i < taps_; ++i) { sum += a[i] * w[i]; }
                if (rest) {
                    // Between two phases: interpolate linearly.
                    const float* b = a + taps_;
                    float other = 0;
                    for (uint32_t i = 0; i < taps_; ++i) { other += b[i] * w[i]; }
                    sum += (other - sum) * (static_cast<float>(rest) / static_cast<float>(denominator_));
                }
                y[c] = sum;
            }
        }
        remainder_ += step_;
        center_ += remainder_ / denominator_;
        remainder_ %= denominator_;
    }

private:
    static double bessel_i0(double x)
    {
        double sum = 1, term = 1;
        for (int k = 1; k < 64; ++k) {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < sum * 1e-17) { break; }
        }
        return sum;
    }

    std::vector<float> table_;
    std::vector<float> history_[Channels];
    uint64_t pushed_ = 0;
    uint64_t center_ = 0;      // input index of the next output's newest tap
    uint32_t remainder_ = 0;   // its fractional position, over denominator_
    uint32_t step_ = 1;
    uint32_t denominator_ = 1;
    uint32_t half_ = 0;
    uint32_t taps_ = 1;
    uint32_t size_ = 1;
    bool identity_ = true;
};

} // namespace drizzle
