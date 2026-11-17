#include "DSP/PitchDetector.h"
#include "DSP/PitchShifter.h"
#include "DSP/TunerEngine.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace evolutionvox;
constexpr double pi = 3.141592653589793;
void require(bool okay, const char* message) { if (!okay) throw std::runtime_error(message); }
float analyzeFrequency(const std::vector<float>& signal, float sampleRate, float minHz, float maxHz) {
    PitchDetector detector;
    detector.prepare(sampleRate);
    for (float sample : signal) detector.process(sample, minHz, maxHz, 0.5f);
    const auto result = detector.latest();
    std::cout << "  detected " << result.frequencyHz << " Hz (confidence " << result.confidence << ")\n";
    require(result.voiced, "not detected as pitched");
    return result.frequencyHz;
}
std::vector<float> sine(float freq, float sampleRate, int count) {
    std::vector<float> data(count);
    for (int i = 0; i < count; ++i) data[i] = 0.65f * std::sin(2.0 * pi * freq * i / sampleRate);
    return data;
}
int main() {
 try {
    constexpr float sr = 48000;
    for (float f : {65.4f, 110.0f, 220.0f, 440.0f, 880.0f}) {
        std::cout << "Input " << f << " Hz:";
        const auto measured = analyzeFrequency(sine(f, sr, 24000), sr, 45.0f, 1100.0f);
        require(std::abs(measured - f) < f * 0.017f, "pitch detection tolerance failed");
    }
    PitchDetector silent;
    silent.prepare(sr);
    for (int i = 0; i < 24000; ++i) silent.process(0, 90, 750, 0.5f);
    require(!silent.latest().voiced, "silence falsely detected");
    Settings major; major.key=0; major.scale=Scale::major;
    require(TunerEngine::allowed(60,major), "C not in C major");
    require(TunerEngine::allowed(62,major), "D not in C major");
    require(TunerEngine::allowed(64,major), "E not in C major");
    require(!TunerEngine::allowed(61,major), "C# unexpectedly in C major");
    require(!TunerEngine::allowed(63,major), "Eb unexpectedly in C major");
    Settings minor; minor.key=0; minor.scale=Scale::naturalMinor;
    require(TunerEngine::allowed(63,minor), "Eb not in C minor");
    require(!TunerEngine::allowed(64,minor), "E unexpectedly in C minor");
    Settings custom; custom.scale=Scale::custom; custom.customMask=1 << 0;
    require(TunerEngine::nearestMidi(61,custom)==60, "custom scale C-only target not working");
    std::cout << "Scale tests passed\n";
    for (auto ratio : {0.5f, 1.0f, 1.5f, 2.0f}) {
        PitchShifter shifter;
        shifter.prepare(sr);
        auto input = sine(220.0f, sr, 96000);
        std::vector<float> output;
        output.reserve(input.size() - 24000);
        for (int n=0; n < (int)input.size(); ++n) {
            const float y = shifter.process(input[n], ratio, 220.0f);
            require(std::isfinite(y), "shifter produced NaN/inf");
            if (n >= 24000) output.push_back(y);
        }
        std::cout << "Ratio " << ratio << ":";
        const float freq = analyzeFrequency(output, sr, 45, 900);
        require(std::abs(freq - 220.0f * ratio) < 220.0f*ratio*0.09f,
                "pitch shifter output out of tolerance");
    }
    for (float rate : {44100.0f, 96000.0f, 192000.0f}) {
        std::cout << "Sample rate " << rate << " Hz:";
        const auto measured = analyzeFrequency(sine(110.0f, rate, static_cast<int>(rate/2)), rate, 45, 1000);
        require(std::abs(measured - 110.0f) < 2.0f, "sample-rate dependent detection failed");
    }
    {
        // End-to-end integration: a deliberately sharp A4 should move toward 440 Hz.
        TunerEngine engine;
        engine.prepare(sr);
        Settings s;
        s.scale = Scale::chromatic;
        s.retuneMs = 0.0f;
        s.flex = 0.0f;
        engine.setSettings(s);
        auto input = sine(447.0f, sr, 96000);
        std::vector<float> output;
        for (int n = 0; n < static_cast<int>(input.size()); ++n) {
            float x = input[n];
            engine.processFrame(&x, 1);
            if (n >= 48000) output.push_back(x);
        }
        std::cout << "Correction A4 input 447 Hz:";
        const float outFreq = analyzeFrequency(output, sr, 400.0f, 470.0f);
        std::cout << "  tuned output: " << outFreq << " Hz\n";
        require(std::abs(outFreq - 440.0f) < 3.0f, "engine correction test failed");
    }
    std::cout << "All DSP tests passed\n";
    return 0;
 } catch(const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n'; return 1;
 }
}
