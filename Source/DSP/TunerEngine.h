#pragma once
#include "PitchDetector.h"
#include "PitchShifter.h"
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <utility>

namespace evolutionvox {

enum class Scale : int { chromatic, major, naturalMinor, harmonicMinor, minorPentatonic, custom };
enum class VoiceRange : int { soprano, altoTenor, lowMale, instrument, bass };

struct Settings {
    int key = 0;                  // C=0, C#=1, ... B=11
    Scale scale = Scale::naturalMinor;
    VoiceRange voiceRange = VoiceRange::altoTenor;
    float retuneMs = 8.0f;
    float tracking = 0.5f;        // 0..1
    float flex = 0.0f;            // 0..1: allows subtle near-note drift
    float humanize = 0.0f;        // 0..1: slows retuning during sustained notes
    int transpose = 0;           // -12 to +12 semitones
    int customMask = 0xFFF;      // 12 absolute pitch classes; bit 0 = C
};

class TunerEngine {
public:
    static constexpr int kMaxChannels = 2;
    void prepare(double sampleRate) noexcept {
        sampleRate_ = std::max(8000.0, sampleRate);
        detector_.prepare(sampleRate_);
        for (auto& shifter : shifters_) shifter.prepare(sampleRate_);
        targetShiftSemitones_ = currentShiftSemitones_ = 0.0f;
        updateCoefficients();
        stableSamples_ = 0; lastTargetMidi_ = -999; lostSamples_ = 0;
        latestInputHz.store(0); latestTargetHz.store(0); latestConfidence.store(0);
        for (auto& a : history_) a.store(0.0f);
        historyIndex_.store(0);
    }
    void setSettings(Settings settings) noexcept {
        settings_ = settings;
        settings_.customMask &= 0xFFF;
        updateCoefficients();
    }
    void processFrame(float* samples, int channels) noexcept {
        channels = std::clamp(channels, 0, kMaxChannels);
        if (channels == 0) return;
        const auto [low, high] = voiceLimits(settings_.voiceRange);
        if (detector_.process(samples[0], low, high, settings_.tracking)) {
            updateTuning(detector_.latest());
        }
        const bool sustained = stableSamples_ > static_cast<int>(sampleRate_ * 0.3);
        const float coeff = sustained ? sustainedCoeff_ : quickCoeff_;
        currentShiftSemitones_ += coeff * (targetShiftSemitones_ - currentShiftSemitones_);
        const float ratio = std::pow(2.0f, (currentShiftSemitones_ + settings_.transpose) / 12.0f);
        for (int ch = 0; ch < channels; ++ch)
            samples[ch] = shifters_[ch].process(samples[ch], ratio, std::max(45.0f, latestInputHz.load(std::memory_order_relaxed)));
    }
    float getInputHz() const noexcept { return latestInputHz.load(std::memory_order_relaxed); }
    float getTargetHz() const noexcept { return latestTargetHz.load(std::memory_order_relaxed); }
    float getConfidence() const noexcept { return latestConfidence.load(std::memory_order_relaxed); }
    static constexpr int kHistory = 192;
    std::array<float, kHistory> historySnapshot() const noexcept {
        std::array<float, kHistory> result{};
        const auto index = historyIndex_.load(std::memory_order_acquire);
        for (int i = 0; i < kHistory; ++i)
            result[i] = history_[(index + i) % kHistory].load(std::memory_order_relaxed);
        return result;
    }

    static bool allowed(int midi, const Settings& s) noexcept {
        const int semitone = (midi % 12 + 12) % 12;
        if (s.scale == Scale::custom) return ((s.customMask >> semitone) & 1) != 0;
        const int rel = (semitone - s.key + 12) % 12;
        constexpr std::array<unsigned, 6> masks = {
            0xFFF, 0xAB5, 0x5AD, 0x9AD, 0x4A9, 0xFFF
        }; // C-root 12-bit maps; see test_scale.cpp
        return (masks[static_cast<int>(s.scale)] & (1u << rel)) != 0;
    }
    static int nearestMidi(float midi, const Settings& s) noexcept {
        int nearest = static_cast<int>(std::round(midi));
        if (allowed(nearest, s)) return nearest;
        for (int offset = 1; offset <= 12; ++offset) {
            const int down = nearest - offset;
            const int up = nearest + offset;
            if (allowed(down, s) && allowed(up, s))
                return std::abs(midi - down) <= std::abs(midi - up) ? down : up;
            if (allowed(down, s)) return down;
            if (allowed(up, s)) return up;
        }
        return nearest;
    }

private:
    double sampleRate_ = 48000.0;
    Settings settings_{};
    PitchDetector detector_{};
    std::array<PitchShifter, kMaxChannels> shifters_{};
    std::atomic<float> latestInputHz{0}, latestTargetHz{0}, latestConfidence{0};
    std::array<std::atomic<float>, kHistory> history_{};
    std::atomic<int> historyIndex_{0};
    float currentShiftSemitones_ = 0.0f, targetShiftSemitones_ = 0.0f;
    int lastTargetMidi_ = -999, stableSamples_ = 0, lostSamples_ = 0;
    float quickCoeff_ = 1.0f, sustainedCoeff_ = 1.0f;
    void updateCoefficients() noexcept {
        const auto coefficient = [this](float timeMs) {
            return timeMs < 0.5f ? 1.0f :
                (1.0f - std::exp(-1000.0f / (timeMs * static_cast<float>(sampleRate_))));
        };
        quickCoeff_ = coefficient(settings_.retuneMs);
        sustainedCoeff_ = coefficient(settings_.retuneMs * (1.0f + settings_.humanize * 5.0f));
    }
    static std::pair<float,float> voiceLimits(VoiceRange range) noexcept {
        switch (range) {
            case VoiceRange::soprano: return {160.0f, 1000.0f};
            case VoiceRange::altoTenor: return {90.0f, 750.0f};
            case VoiceRange::lowMale: return {65.0f, 400.0f};
            case VoiceRange::instrument: return {55.0f, 1200.0f};
            case VoiceRange::bass: return {45.0f, 300.0f};
        }
        return {90.0f, 750.0f};
    }
    void updateTuning(PitchObservation obs) noexcept {
        latestConfidence.store(obs.confidence, std::memory_order_relaxed);
        if (!obs.voiced || settings_.customMask == 0) {
            if (++lostSamples_ > 6) {
                latestInputHz.store(0, std::memory_order_relaxed);
                latestTargetHz.store(0, std::memory_order_relaxed);
                targetShiftSemitones_ = 0.0f;
            }
            appendHistory(0); return;
        }
        lostSamples_ = 0;
        const float midi = 69.0f + 12.0f * std::log2(obs.frequencyHz / 440.0f);
        const int target = nearestMidi(midi, settings_);
        if (target == lastTargetMidi_) stableSamples_ += 256;
        else { stableSamples_ = 0; lastTargetMidi_ = target; }
        const float diff = static_cast<float>(target) - midi;
        float strength = 1.0f;
        if (settings_.flex > 0.0001f) {
            const float deadzone = settings_.flex * 0.15f;
            strength = std::clamp((std::abs(diff) - deadzone) / (settings_.flex * 0.55f), 0.0f, 1.0f);
        }
        targetShiftSemitones_ = diff * strength;
        latestInputHz.store(obs.frequencyHz, std::memory_order_relaxed);
        latestTargetHz.store(440.0f * std::pow(2.0f, (target - 69.0f) / 12.0f), std::memory_order_relaxed);
        appendHistory(midi);
    }
    void appendHistory(float midi) noexcept {
        const int index = historyIndex_.load(std::memory_order_relaxed);
        history_[index].store(midi, std::memory_order_relaxed);
        historyIndex_.store((index + 1) % kHistory, std::memory_order_release);
    }
};

} // namespace evolutionvox
