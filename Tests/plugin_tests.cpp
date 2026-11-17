// JUCE-level tests for the EvolutionVox processor and Design V1 editor.
//   EvolutionVoxPluginTests                   run all checks (ctest)
//   EvolutionVoxPluginTests --screenshots DIR also write editor PNGs to DIR
//   EvolutionVoxPluginTests --bench           also print offline CPU timing per buffer size
// Offline timings are NOT DAW measurements; they are a repeatable reference on one machine.
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ZeusRig.h"
#include <chrono>
#include <cmath>
#include <iostream>

namespace {
int failures = 0;
void check(bool ok, const juce::String& what) {
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

void setParam(EvolutionVoxAudioProcessor& p, const char* id, float value) {
    auto* param = p.parameters.getParameter(id);
    param->beginChangeGesture();
    param->setValueNotifyingHost(param->convertTo0to1(value));
    param->endChangeGesture();
}
float getParam(EvolutionVoxAudioProcessor& p, const char* id) { return p.parameters.getRawParameterValue(id)->load(); }

void prepare(EvolutionVoxAudioProcessor& p, double sr, int block) {
    p.setRateAndBufferSizeDetails(sr, block);
    p.prepareToPlay(sr, block);
}

juce::AudioBuffer<float> makeSignal(int channels, int samples, double sr, float hz, juce::Random& rng, float noise = 0.0f) {
    juce::AudioBuffer<float> b(channels, samples);
    for (int i = 0; i < samples; ++i) {
        const float s = 0.4f * std::sin(juce::MathConstants<float>::twoPi * hz * static_cast<float>(i) / static_cast<float>(sr));
        for (int ch = 0; ch < channels; ++ch) b.setSample(ch, i, s + noise * (rng.nextFloat() * 2.0f - 1.0f));
    }
    return b;
}

// Runs `input` through the processor in blocks; returns the output.
juce::AudioBuffer<float> run(EvolutionVoxAudioProcessor& p, const juce::AudioBuffer<float>& input, int block, bool hostBypass) {
    juce::AudioBuffer<float> out(input);
    juce::MidiBuffer midi;
    for (int start = 0; start < out.getNumSamples(); start += block) {
        const int n = std::min(block, out.getNumSamples() - start);
        juce::AudioBuffer<float> view(out.getArrayOfWritePointers(), out.getNumChannels(), start, n);
        if (hostBypass) p.processBlockBypassed(view, midi); else p.processBlock(view, midi);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
void testParameters() {
    std::cout << "Parameters / automation identity\n";
    EvolutionVoxAudioProcessor p;
    const juce::StringArray expected { "key", "scale", "range", "retune", "tracking", "flex", "humanize", "transpose", "mask", "power" };
    const auto& params = p.getParameters();
    check(params.size() == expected.size(), "10 parameters (9 from Build 0.1 + power)");
    for (int i = 0; i < std::min(params.size(), expected.size()); ++i) {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(params[i]);
        check(ranged != nullptr && ranged->getParameterID() == expected[i] && ranged->getVersionHint() == 1,
              "parameter " + juce::String(i) + " is '" + expected[i] + "' v1 (order and ID unchanged)");
    }
    // Retune keeps the Build 0.1 normalisation so existing automation lanes map to the same ms.
    const juce::NormalisableRange<float> old(0.0f, 250.0f, 0.1f, 0.4f);
    auto* retune = p.parameters.getParameter("retune");
    bool same = true;
    for (float ms : { 0.0f, 0.1f, 3.9f, 8.0f, 31.2f, 105.4f, 200.0f, 250.0f })
        same &= std::abs(retune->convertTo0to1(ms) - old.convertTo0to1(ms)) < 1.0e-6f;
    check(same, "retune 0..250 ms normalisation identical to Build 0.1");
    check(std::abs(retune->convertFrom0to1(retune->getDefaultValue()) - 8.0f) < 1.0e-4f, "retune default still 8 ms");
    check(p.getBypassParameter() == nullptr, "host bypass still provided by the wrapper (VST3 bypass ID unchanged)");
}

void testState() {
    std::cout << "Project recall\n";
    EvolutionVoxAudioProcessor a;
    setParam(a, "key", 4); setParam(a, "scale", 3); setParam(a, "range", 2); setParam(a, "retune", 17.5f);
    setParam(a, "tracking", 70); setParam(a, "flex", 20); setParam(a, "humanize", 35); setParam(a, "transpose", -3);
    setParam(a, "mask", 0x5AD); setParam(a, "power", 0);
    juce::MemoryBlock blob;
    a.getStateInformation(blob);
    EvolutionVoxAudioProcessor b;
    b.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    bool same = true;
    for (auto* id : { "key", "scale", "range", "retune", "tracking", "flex", "humanize", "transpose", "mask", "power" })
        same &= std::abs(getParam(a, id) - getParam(b, id)) < 1.0e-4f;
    check(same, "all 10 parameters round-trip through get/setStateInformation");

    // A Build 0.1 project: same XML layout, no "power" entry. Must load with Power = On even if
    // the instance was switched off before loading.
    const juce::String oldXml = R"(<EvolutionVoxSettings>
        <PARAM id="key" value="7.0"/><PARAM id="scale" value="1.0"/><PARAM id="range" value="0.0"/>
        <PARAM id="retune" value="24.0"/><PARAM id="tracking" value="50.0"/><PARAM id="flex" value="45.0"/>
        <PARAM id="humanize" value="55.0"/><PARAM id="transpose" value="2.0"/><PARAM id="mask" value="4095.0"/>
        </EvolutionVoxSettings>)";
    juce::MemoryBlock oldBlob;
    juce::AudioProcessor::copyXmlToBinary(*juce::parseXML(oldXml), oldBlob);
    EvolutionVoxAudioProcessor c;
    setParam(c, "power", 0);
    c.setStateInformation(oldBlob.getData(), static_cast<int>(oldBlob.getSize()));
    check(getParam(c, "power") > 0.5f, "Build 0.1 project (no power entry) loads with Power = On");
    check(std::abs(getParam(c, "retune") - 24.0f) < 1.0e-4f && std::abs(getParam(c, "key") - 7.0f) < 1.0e-4f
          && std::abs(getParam(c, "transpose") - 2.0f) < 1.0e-4f, "Build 0.1 values restored unchanged");
}

void testPowerAndBypass() {
    std::cout << "Power / bypass\n";
    constexpr double sr = 48000.0;
    juce::Random rng(1234);
    const auto input = makeSignal(2, 48000, sr, 220.0f, rng, 0.2f);
    {
        EvolutionVoxAudioProcessor p;
        setParam(p, "power", 0);
        prepare(p, sr, 512);
        const int latency = p.getLatencySamples();
        const auto out = run(p, input, 512, false);
        float err = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = latency; i < out.getNumSamples(); ++i)
                err = std::max(err, std::abs(out.getSample(ch, i) - input.getSample(ch, i - latency)));
        check(latency > 0 && err == 0.0f, "Power off = input delayed by exactly the reported latency ("
                                          + juce::String(latency) + " samples)");
        check(!p.isEffectEngaged(), "processor reports effect disengaged");
    }
    {
        EvolutionVoxAudioProcessor p;
        prepare(p, sr, 256);
        const int latency = p.getLatencySamples();
        const auto out = run(p, input, 256, true);
        float err = 0.0f;
        for (int i = latency; i < out.getNumSamples(); ++i)
            err = std::max(err, std::abs(out.getSample(0, i) - input.getSample(0, i - latency)));
        check(err == 0.0f, "host bypass (processBlockBypassed) is latency-matched dry audio");
    }
    {
        // Toggle power mid-stream: crossfade must not click.
        EvolutionVoxAudioProcessor p;
        setParam(p, "scale", 0);
        prepare(p, sr, 512);
        const auto tone = makeSignal(2, 24000, sr, 220.0f, rng);
        juce::AudioBuffer<float> all(2, 24000 * 4);
        int pos = 0;
        for (int phase = 0; phase < 4; ++phase) {
            setParam(p, "power", (phase % 2 == 0) ? 1.0f : 0.0f);
            const auto out = run(p, tone, 512, false);
            for (int ch = 0; ch < 2; ++ch) all.copyFrom(ch, pos, out, ch, 0, out.getNumSamples());
            pos += out.getNumSamples();
        }
        float maxStep = 0.0f;
        for (int i = p.getLatencySamples() + 1; i < all.getNumSamples(); ++i)
            maxStep = std::max(maxStep, std::abs(all.getSample(0, i) - all.getSample(0, i - 1)));
        // A pure 220 Hz, 0.4 amplitude sine changes by at most ~0.012 per sample.
        check(maxStep < 0.06f, "power on/off/on transitions are click-free (max step "
                                 + juce::String(maxStep, 4) + ")");
    }
}

void testKeyScaleReachDsp() {
    std::cout << "Key / Scale reach the DSP\n";
    constexpr double sr = 48000.0;
    juce::Random rng(7);
    EvolutionVoxAudioProcessor p;
    setParam(p, "key", 0); setParam(p, "scale", 0); setParam(p, "retune", 0.0f);
    prepare(p, sr, 512);
    const auto tone = makeSignal(1, 48000, sr, 447.0f, rng);
    juce::AudioBuffer<float> stereo(2, tone.getNumSamples());
    stereo.copyFrom(0, 0, tone, 0, 0, tone.getNumSamples());
    stereo.copyFrom(1, 0, tone, 0, 0, tone.getNumSamples());
    run(p, stereo, 512, false);
    const float chromatic = p.tuner.getTargetHz();
    check(std::abs(chromatic - 440.0f) < 1.0f, "447 Hz, C chromatic -> target A4 (" + juce::String(chromatic, 2) + " Hz)");
    setParam(p, "key", 1); setParam(p, "scale", 1);   // C# major has no A
    run(p, stereo, 512, false);
    const float cSharpMajor = p.tuner.getTargetHz();
    check(std::abs(cSharpMajor - 466.16f) < 1.0f, "447 Hz, C# major -> target A#4 (" + juce::String(cSharpMajor, 2) + " Hz)");
    setParam(p, "scale", 5); setParam(p, "mask", 1 << 7); // Custom: only G
    run(p, stereo, 512, false);
    const float customG = p.tuner.getTargetHz();
    check(std::abs(customG - 392.0f) < 1.0f, "Custom notes {G} -> target G4 (" + juce::String(customG, 2) + " Hz)");
}

void benchmark() {
    std::cout << "Offline processBlock timing (this machine, not a DAW measurement)\n";
    std::cout << "  rate    block  power-on %RT  power-off %RT\n";
    juce::Random rng(99);
    for (double sr : { 44100.0, 48000.0, 96000.0 }) {
        const auto input = makeSignal(2, static_cast<int>(sr * 10), sr, 220.0f, rng, 0.05f);
        for (int block : { 64, 128, 256, 512, 1024 }) {
            double pct[2] {};
            for (int power = 1; power >= 0; --power) {
                EvolutionVoxAudioProcessor p;
                setParam(p, "power", static_cast<float>(power));
                prepare(p, sr, block);
                const auto t0 = std::chrono::steady_clock::now();
                run(p, input, block, false);
                const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                pct[1 - power] = 100.0 * secs / 10.0;
            }
            std::printf("  %6.0f  %5d  %12.2f  %13.3f\n", sr, block, pct[0], pct[1]);
        }
    }
}

void writePng(const juce::Image& img, const juce::File& file) {
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat().writeImageToStream(img, out);
}

void testEditor(const juce::File& shots) {
    std::cout << "Editor\n";
    EvolutionVoxAudioProcessor p;
    prepare(p, 48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto* editor = dynamic_cast<EvolutionVoxAudioProcessorEditor*>(base.get());
    check(editor != nullptr && editor->getWidth() == 1000 && editor->getHeight() == 650, "editor opens at 1000x650");
    if (editor == nullptr) return;
    auto& dial = editor->getDial();

    // No percentage anywhere in the main view's text-bearing components.
    bool percent = false;
    std::function<void(juce::Component&)> scan = [&](juce::Component& c) {
        if (auto* l = dynamic_cast<juce::Label*>(&c)) percent |= l->getText().containsChar('%');
        if (auto* cb = dynamic_cast<juce::ComboBox*>(&c)) percent |= cb->getText().containsChar('%');
        if (auto* b = dynamic_cast<juce::Button*>(&c)) percent |= b->getButtonText().containsChar('%');
        for (auto* child : c.getChildren())
            if (child->isVisible()) scan(*child);
    };
    scan(*editor);
    check(!percent, "main view shows no percentage text");

    using Mapping = evolutionvox::ui::RetuneDialMapping;
    namespace z = evolutionvox::ui::zeus;
    bool posesOk = true;
    for (int i = 0; i < z::numPoses; ++i) {
        const float intensity = z::poses[static_cast<size_t>(i)].intensity;
        setParam(p, "retune", Mapping::retuneMsFromIntensity(intensity));   // as host automation would
        dial.syncFromParameters();
        posesOk &= dial.getPoseIndex() == i && std::abs(dial.getIntensity() - intensity) < 0.05f;
        if (shots != juce::File()) {
            const auto img = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
            writePng(img, shots.getChildFile("main_" + juce::String(static_cast<int>(intensity)).paddedLeft('0', 3) + ".png"));
        }
    }
    check(posesOk, "automating retune drives the matching Zeus pose at all nine stages");

    // 50% screenshot at 2x (Retina) + timing of a full-window software render.
    setParam(p, "retune", Mapping::retuneMsFromIntensity(50.0f));
    dial.syncFromParameters();
    const auto t0 = std::chrono::steady_clock::now();
    juce::Image full;
    for (int i = 0; i < 10; ++i) full = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
    const double fullMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10.0;
    const auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i) dial.createComponentSnapshot(dial.getLocalBounds(), true, 1.0f);
    const double dialMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count() / 10.0;
    std::printf("  software render: full window %.1f ms, dial %.1f ms (first paint incl. caches excluded)\n", fullMs, dialMs);

    if (shots != juce::File()) {
        writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f), shots.getChildFile("main_050_2x.png"));
        setParam(p, "power", 0);
        dial.syncFromParameters();
        writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f), shots.getChildFile("main_power_off.png"));
        setParam(p, "power", 1);
        setParam(p, "scale", 5);
        editor->showSettings(true);
        writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f), shots.getChildFile("settings.png"));
        editor->showSettings(false);
        std::cout << "  screenshots written to " << shots.getFullPathName() << "\n";
    }
    check(!editor->isSettingsVisible(), "settings panel closes");
}
} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File shots;
    bool bench = false;
    for (int i = 1; i < argc; ++i) {
        const juce::String arg(argv[i]);
        if (arg == "--screenshots" && i + 1 < argc) {
            shots = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]);
            shots.createDirectory();
        } else if (arg == "--bench") {
            bench = true;
        }
    }
    testParameters();
    testState();
    testPowerAndBypass();
    testKeyScaleReachDsp();
    testEditor(shots);
    if (bench) benchmark();
    std::cout << (failures == 0 ? "All plugin tests passed\n" : juce::String(failures) + " plugin test(s) FAILED\n");
    return failures == 0 ? 0 : 1;
}
