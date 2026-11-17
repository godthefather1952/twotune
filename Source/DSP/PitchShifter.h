#pragma once
#include <array>
#include <algorithm>
#include <cmath>

namespace evolutionvox {

// Dual-grain, fractional-delay pitch shifter with complementary crossfades.
// Proof-of-concept DSP, not yet production-quality formant-preserving tuning.
class PitchShifter {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);
        centreDelay_ = static_cast<float>(nominalLatencySamples(sampleRate));
        slew_ = 1.0f - std::exp(-1.0f / (0.004f * sampleRate_));
        for (int i = 0; i <= kWindowLut; ++i)
            window_[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(6.283185307179586f * i / kWindowLut);
        reset();
    }
    void reset() noexcept {
        delay_.fill(0.0f); write_ = 0; phase_ = 0.0f;
        smoothRatio_ = 1.0f;
    }
    float process(float x, float ratio, float detectedHz = 220.0f) noexcept {
        // One head sweeps a delay interval of two source periods per cycle.
        // The sweep rate, NOT a fixed grain duration, depends on shift amount:
        // this produces correct small (cents-level) shifts as well as octaves.
        const float period = sampleRate_ / std::clamp(detectedHz, 45.0f, 1200.0f);
        const float sweepSamples = 2.0f * period;
        delay_[write_] = x;
        ratio = std::clamp(ratio, 0.5f, 2.0f);
        // Gentle ratio smoothing prevents discontinuities at analysis updates.
        smoothRatio_ += slew_ * (ratio - smoothRatio_);
        const float otherPhase = phase_ < 0.5f ? phase_ + 0.5f : phase_ - 0.5f;
        const float delta = 1.0f - smoothRatio_;
        const float signedSweep = std::copysign(sweepSamples, delta);
        const float d1 = centreDelay_ + (phase_ - 0.5f) * signedSweep;
        const float d2 = centreDelay_ + (otherPhase - 0.5f) * signedSweep;
        const float windowPosition = phase_ * kWindowLut;
        const int windowIndex = static_cast<int>(windowPosition);
        const float windowFraction = windowPosition - static_cast<float>(windowIndex);
        const float weight = window_[windowIndex] + windowFraction * (window_[windowIndex + 1] - window_[windowIndex]);
        const float y = weight * read(d1) + (1.0f - weight) * read(d2);
        write_ = (write_ + 1) % kSize;
        phase_ += std::abs(1.0f - smoothRatio_) / sweepSamples;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        return y;
    }
    // Buffer alignment can be reported to a host as latency, although the varying
    // grain delays mean perceived onset differs slightly with the shift amount.
    static int nominalLatencySamples(double sampleRate) noexcept {
        return static_cast<int>(std::round(2048.0 * sampleRate / 48000.0));
    }

private:
    static constexpr int kSize = 32768;
    static constexpr int kWindowLut = 1024;
    std::array<float, kWindowLut + 1> window_{};
    std::array<float, kSize> delay_{};
    int write_ = 0;
    float phase_ = 0.0f, smoothRatio_ = 1.0f, sampleRate_ = 48000.0f;
    float slew_ = 0.005f, centreDelay_ = 2048.0f;
    float read(float delaySamples) const noexcept {
        float position = static_cast<float>(write_) - delaySamples;
        if (position < 0.0f) position += kSize;
        const int left = static_cast<int>(position);
        const int right = (left + 1) % kSize;
        const float fraction = position - left;
        return delay_[left] + fraction * (delay_[right] - delay_[left]);
    }
};

} // namespace evolutionvox
