#include "DSP/TunerEngine.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <stdexcept>

// Render an elementary comparison of input 447 Hz vs corrected ~440 Hz.
// Outputs are synthetic sine waves, not realistic vocals.
namespace {
void put16(std::FILE* f, std::uint16_t v) { std::fputc(v & 0xff, f); std::fputc((v >> 8) & 0xff, f); }
void put32(std::FILE* f, std::uint32_t v) { put16(f, v & 0xffff); put16(f, v >> 16); }
struct Wav {
    std::FILE* file = nullptr;
    Wav(const char* name, int frames) {
        file = std::fopen(name, "wb"); if (!file) throw std::runtime_error("could not write WAV");
        const int bytes = frames * 2;
        std::fwrite("RIFF", 1, 4, file); put32(file, 36 + bytes);
        std::fwrite("WAVEfmt ", 1, 8, file); put32(file, 16);
        put16(file, 1); put16(file, 1); put32(file, 48000); put32(file, 96000);
        put16(file, 2); put16(file, 16);
        std::fwrite("data", 1, 4, file); put32(file, bytes);
    }
    void write(float x) {
        const auto v = static_cast<std::int16_t>(std::round(std::clamp(x, -1.0f, 1.0f) * 32767.0f));
        put16(file, static_cast<std::uint16_t>(v));
    }
    ~Wav() { if (file) std::fclose(file); }
};
}
int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "usage: evolutionvox_demo before.wav after.wav\n"; return 1; }
    constexpr int sampleRate = 48000;
    constexpr int count = sampleRate * 3;
    Wav before(argv[1], count);
    Wav after(argv[2], count);
    evolutionvox::TunerEngine engine;
    engine.prepare(sampleRate);
    evolutionvox::Settings settings;
    settings.scale = evolutionvox::Scale::chromatic;
    settings.retuneMs = 0.0f;
    engine.setSettings(settings);
    for (int n = 0; n < count; ++n) {
        const float input = 0.6f * std::sin(6.283185307179586 * 447.0 * n / sampleRate);
        float output = input;
        engine.processFrame(&output, 1);
        before.write(input);
        after.write(output);
    }
    std::cout << "Rendered before/after test signals (447 Hz -> ~440 Hz).\n";
}
