#pragma once
#include <array>
#include <algorithm>
#include <cmath>

namespace evolutionvox {

struct PitchObservation {
    float frequencyHz = 0.0f;
    float confidence = 0.0f;
    float rms = 0.0f;
    bool voiced = false;
};

// Decimated YIN/CMNDF fundamental-frequency estimator. No allocation in the audio callback.
// This is a monophonic detector, not a polyphonic or formant-tracking detector.
class PitchDetector {
public:
    void prepare(double sampleRate) noexcept {
        decimationFactor_ = std::max(1, static_cast<int>(std::round(sampleRate / 12000.0)));
        downsampledRate_ = static_cast<float>(sampleRate / decimationFactor_);
        hop_ = std::max(64, static_cast<int>(std::round(sampleRate / 187.5)));
        reset();
    }
    void reset() noexcept {
        history_.fill(0.0f); decimationSum_ = 0.0f;
        decimationCount_ = 0; write_ = 0; filled_ = 0; hopSamples_ = 0;
        latest_ = {};
    }
    // Returns true only when a new analysis result has been produced.
    bool process(float sample, float minHz, float maxHz, float sensitivity) noexcept {
        decimationSum_ += sample;
        if (++decimationCount_ == decimationFactor_) {
            history_[write_] = decimationSum_ / decimationFactor_;
            write_ = (write_ + 1) % kWindow;
            filled_ = std::min(filled_ + 1, kWindow);
            decimationSum_ = 0.0f; decimationCount_ = 0;
        }
        if (++hopSamples_ < hop_ || filled_ != kWindow)
            return false;
        hopSamples_ = 0;
        latest_ = estimate(minHz, maxHz, sensitivity);
        return true;
    }
    PitchObservation latest() const noexcept { return latest_; }
    int hopSamples() const noexcept { return hop_; }

private:
    static constexpr int kWindow = 768;
    static constexpr int kCompare = 384;
    static constexpr int kMaxLag = 350;
    std::array<float, kWindow> history_{};
    std::array<float, kMaxLag + 2> differences_{};
    int write_ = 0, filled_ = 0, decimationCount_ = 0, hopSamples_ = 0;
    int decimationFactor_ = 4, hop_ = 256;
    float decimationSum_ = 0.0f;
    float downsampledRate_ = 12000.0f;
    PitchObservation latest_{};

    PitchObservation estimate(float minHz, float maxHz, float sensitivity) noexcept {
        std::array<float, kWindow> frame{};
        float energy = 0.0f;
        for (int i = 0; i < kWindow; ++i) {
            const float v = history_[(write_ + i) % kWindow];
            frame[i] = v;
            energy += v * v;
        }
        PitchObservation result;
        result.rms = std::sqrt(energy / kWindow);
        if (result.rms < 0.002f) return result; // roughly -54 dBFS input gate

        const int minLag = std::clamp(static_cast<int>(downsampledRate_ / maxHz), 3, kMaxLag - 2);
        const int maxLag = std::clamp(static_cast<int>(downsampledRate_ / minHz), minLag + 1, kMaxLag);
        float cumulative = 0.0f;
        differences_[0] = 1.0f;
        for (int tau = 1; tau <= maxLag; ++tau) {
            float diff = 0.0f;
            for (int j = 0; j < kCompare; ++j) {
                const float delta = frame[j] - frame[j + tau];
                diff += delta * delta;
            }
            cumulative += diff;
            differences_[tau] = cumulative > 1e-12f ? (diff * tau / cumulative) : 1.0f;
        }
        // More permissive tracking accepts weaker periodicity at the cost of false detections.
        const float threshold = 0.10f + std::clamp(sensitivity, 0.0f, 1.0f) * 0.13f;
        int best = -1;
        for (int tau = minLag; tau <= maxLag; ++tau) {
            if (differences_[tau] < threshold) {
                while (tau + 1 <= maxLag && differences_[tau + 1] < differences_[tau]) ++tau;
                best = tau; break;
            }
        }
        if (best == -1) {
            best = minLag;
            for (int tau = minLag + 1; tau <= maxLag; ++tau)
                if (differences_[tau] < differences_[best]) best = tau;
            if (differences_[best] > threshold) return result;
        }
        float correctedLag = static_cast<float>(best);
        if (best > 1 && best < maxLag) {
            const float a = differences_[best - 1], b = differences_[best], c = differences_[best + 1];
            const float denom = a - 2.0f * b + c;
            if (std::abs(denom) > 1e-7f)
                correctedLag += std::clamp(0.5f * (a - c) / denom, -1.0f, 1.0f);
        }
        result.frequencyHz = downsampledRate_ / correctedLag;
        result.confidence = std::clamp(1.0f - differences_[best], 0.0f, 1.0f);
        result.voiced = std::isfinite(result.frequencyHz)
                        && result.frequencyHz >= minHz && result.frequencyHz <= maxHz;
        return result;
    }
};

} // namespace evolutionvox
