#include "DSP/TunerEngine.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

// Manual benchmark. This is NOT a DAW audio-thread CPU or audio-quality measurement.
int main() {
    constexpr int sr = 48000, n = sr * 8;
    std::vector<float> input(n);
    for (int i = 0; i < n; ++i)
        input[i] = 0.25f * (std::sin(6.283185307179586 * 220.0 * i / sr)
                             + std::sin(6.283185307179586 * 447.0 * i / sr));
    evolutionvox::TunerEngine engine;
    engine.prepare(sr);
    evolutionvox::Settings s;
    s.scale = evolutionvox::Scale::major;
    engine.setSettings(s);
    double checksum = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) {
        float frame[2] { input[i], input[i] };
        engine.processFrame(frame, 2);
        checksum += frame[0] + frame[1];
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "48kHz stereo, 8 sec: " << elapsed << " sec CPU-wall processing, checksum " << checksum << "\n";
}
