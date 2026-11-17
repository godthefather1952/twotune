#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
using namespace juce;
using APF = AudioParameterFloat;
using API = AudioParameterInt;
using APC = AudioParameterChoice;
using APB = AudioParameterBool;
using ID = ParameterID;
}

EvolutionVoxAudioProcessor::EvolutionVoxAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "EvolutionVoxSettings", createParameterLayout()) {
    powerParam = parameters.getRawParameterValue("power");
    jassert(powerParam != nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout EvolutionVoxAudioProcessor::createParameterLayout() {
    // Parameter IDs, versions, ranges and order of the Build 0.1 parameters are unchanged so DAW
    // automation and saved projects keep working (VST3 IDs are derived from these string IDs).
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<APC>(ID{"key", 1}, "Key",
        juce::StringArray{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 0));
    layout.add(std::make_unique<APC>(ID{"scale", 1}, "Scale",
        juce::StringArray{"Chromatic", "Major", "Natural Minor", "Harmonic Minor", "Minor Pentatonic", "Custom"}, 2));
    layout.add(std::make_unique<APC>(ID{"range", 1}, "Input Range",
        juce::StringArray{"Soprano", "Alto / Tenor", "Low Male", "Instrument", "Bass"}, 1));
    // Same 0..250 ms range, skew and default as Build 0.1. The 0.1 ms snapping interval was removed
    // (interval 0 = continuous): with it, the top 7% of the Zeus dial had only three reachable
    // values, so the bolt jumped. Normalised automation values and saved ms values are unaffected.
    layout.add(std::make_unique<APF>(ID{"retune", 1}, "Retune Speed",
        juce::NormalisableRange<float>(0.0f, 250.0f, 0.0f, 0.4f), 8.0f, "ms"));
    layout.add(std::make_unique<APF>(ID{"tracking", 1}, "Tracking Sensitivity",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f, "%"));
    layout.add(std::make_unique<APF>(ID{"flex", 1}, "Flex",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f, "%"));
    layout.add(std::make_unique<APF>(ID{"humanize", 1}, "Humanize",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f, "%"));
    layout.add(std::make_unique<API>(ID{"transpose", 1}, "Transpose", -12, 12, 0));
    layout.add(std::make_unique<API>(ID{"mask", 1}, "Custom Notes", 1, 4095, 4095));
    // New in Design V1 (added last). Separate from the host bypass, whose VST3 ID is unchanged.
    // Migration: projects saved before this parameter existed load with Power = On.
    layout.add(std::make_unique<APB>(ID{"power", 1}, "Power", true));
    return layout;
}

void EvolutionVoxAudioProcessor::prepareToPlay(double sampleRate, int) {
    preparedSampleRate = sampleRate;
    tuner.prepare(sampleRate);
    updateTunerSettings();
    latencySamples = evolutionvox::PitchShifter::nominalLatencySamples(sampleRate);
    setLatencySamples(latencySamples);
    for (auto& line : dryDelay)
        line.assign(static_cast<size_t>(std::max(1, latencySamples)), 0.0f);
    dryWrite = 0;
    fadeStep = 1.0f / static_cast<float>(std::max(1.0, 0.02 * sampleRate)); // 20 ms crossfade
    const bool on = powerParam->load() >= 0.5f;
    powerState = on ? PowerState::on : PowerState::off;
    wetGain = on ? 1.0f : 0.0f;
    engagedForUi.store(on);
}

bool EvolutionVoxAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& output = layouts.getMainOutputChannelSet();
    const auto& input = layouts.getMainInputChannelSet();
    return (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo())
            && input == output;
}

void EvolutionVoxAudioProcessor::updateTunerSettings() noexcept {
    evolutionvox::Settings settings;
    settings.key = static_cast<int>(parameters.getRawParameterValue("key")->load());
    settings.scale = static_cast<evolutionvox::Scale>(static_cast<int>(parameters.getRawParameterValue("scale")->load()));
    settings.voiceRange = static_cast<evolutionvox::VoiceRange>(static_cast<int>(parameters.getRawParameterValue("range")->load()));
    settings.retuneMs = parameters.getRawParameterValue("retune")->load();
    settings.tracking = parameters.getRawParameterValue("tracking")->load() / 100.0f;
    settings.flex = parameters.getRawParameterValue("flex")->load() / 100.0f;
    settings.humanize = parameters.getRawParameterValue("humanize")->load() / 100.0f;
    settings.transpose = static_cast<int>(parameters.getRawParameterValue("transpose")->load());
    settings.customMask = static_cast<int>(parameters.getRawParameterValue("mask")->load());
    tuner.setSettings(settings);
}

float EvolutionVoxAudioProcessor::pushDry(int channel, float x) noexcept {
    auto& line = dryDelay[static_cast<size_t>(channel)];
    if (latencySamples <= 0 || line.empty()) return x;
    const float delayed = line[static_cast<size_t>(dryWrite)];
    line[static_cast<size_t>(dryWrite)] = x;
    return delayed;
}

void EvolutionVoxAudioProcessor::advanceDryWrite() noexcept {
    if (latencySamples > 0 && ++dryWrite >= latencySamples) dryWrite = 0;
}

void EvolutionVoxAudioProcessor::runDelayedDry(juce::AudioBuffer<float>& audio, int numChannels) noexcept {
    const auto count = audio.getNumSamples();
    for (int i = 0; i < count; ++i) {
        for (int ch = 0; ch < numChannels; ++ch) {
            auto* data = audio.getWritePointer(ch);
            data[i] = pushDry(ch, data[i]);
        }
        advanceDryWrite();
    }
}

void EvolutionVoxAudioProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numChannels = std::min(audio.getNumChannels(), evolutionvox::TunerEngine::kMaxChannels);
    if (numChannels == 0) return;
    const auto count = audio.getNumSamples();

    // Power transitions happen at block boundaries; the gain ramps sample by sample.
    const bool wantOn = powerParam->load() >= 0.5f;
    if (wantOn) {
        if (powerState == PowerState::off) {
            tuner.prepare(preparedSampleRate); // allocation-free reset: clears stale delay lines
            updateTunerSettings();
            warmupRemaining = latencySamples;
            powerState = warmupRemaining > 0 ? PowerState::warmingUp : PowerState::fadingIn;
        } else if (powerState == PowerState::fadingOut) {
            powerState = PowerState::fadingIn;
        }
    } else {
        if (powerState == PowerState::on || powerState == PowerState::fadingIn)
            powerState = PowerState::fadingOut;
        else if (powerState == PowerState::warmingUp)
            powerState = PowerState::off;
    }

    for (int ch = numChannels; ch < audio.getNumChannels(); ++ch)
        audio.clear(ch, 0, count);

    if (powerState == PowerState::off) {
        // Bypassed: the expensive tuner is not run at all; output is latency-matched dry audio.
        runDelayedDry(audio, numChannels);
        engagedForUi.store(false, std::memory_order_relaxed);
        return;
    }

    updateTunerSettings();
    auto* left = audio.getWritePointer(0);
    auto* right = numChannels > 1 ? audio.getWritePointer(1) : nullptr;
    for (int i = 0; i < count; ++i) {
        const float dryL = pushDry(0, left[i]);
        const float dryR = right != nullptr ? pushDry(1, right[i]) : 0.0f;
        advanceDryWrite();

        float frame[2] { left[i], right == nullptr ? 0.0f : right[i] };
        tuner.processFrame(frame, numChannels);

        switch (powerState) {
            case PowerState::on:
                left[i] = frame[0];
                if (right != nullptr) right[i] = frame[1];
                continue;
            case PowerState::warmingUp:
                left[i] = dryL;
                if (right != nullptr) right[i] = dryR;
                if (--warmupRemaining <= 0) powerState = PowerState::fadingIn;
                continue;
            case PowerState::fadingIn:
                wetGain = std::min(1.0f, wetGain + fadeStep);
                if (wetGain >= 1.0f) powerState = PowerState::on;
                break;
            case PowerState::fadingOut:
                wetGain = std::max(0.0f, wetGain - fadeStep);
                if (wetGain <= 0.0f) powerState = PowerState::off;
                break;
            case PowerState::off:
                break;
        }
        left[i] = dryL + wetGain * (frame[0] - dryL);
        if (right != nullptr) right[i] = dryR + wetGain * (frame[1] - dryR);
    }
    engagedForUi.store(powerState == PowerState::on || powerState == PowerState::fadingIn,
                       std::memory_order_relaxed);
}

void EvolutionVoxAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numChannels = std::min(audio.getNumChannels(), evolutionvox::TunerEngine::kMaxChannels);
    for (int ch = numChannels; ch < audio.getNumChannels(); ++ch)
        audio.clear(ch, 0, audio.getNumSamples());
    if (numChannels == 0) return;
    runDelayedDry(audio, numChannels);
    // When the host un-bypasses, re-engage through warm-up so stale tuner audio is never heard.
    powerState = PowerState::off;
    wetGain = 0.0f;
    engagedForUi.store(false, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* EvolutionVoxAudioProcessor::createEditor() {
    return new EvolutionVoxAudioProcessorEditor(*this);
}

void EvolutionVoxAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}

void EvolutionVoxAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr || !xml->hasTagName(parameters.state.getType()))
        return;
    const bool hasPower = xml->getChildByAttribute("id", "power") != nullptr;
    parameters.replaceState(juce::ValueTree::fromXml(*xml));
    // Build 0.1 projects have no "power" entry: APVTS would otherwise keep whatever value the
    // instance currently holds. Restore the documented default (On) explicitly.
    if (!hasPower)
        if (auto* power = parameters.getParameter("power"))
            power->setValueNotifyingHost(power->getDefaultValue());
}

void EvolutionVoxAudioProcessor::loadFactoryPreset(int index) {
    // Factory settings are original starting points, not artist/Antares presets.
    struct Preset { int key, scale, range; float retune, tracking, flex, humanize; int transpose; };
    constexpr Preset options[] = {
        {0, 2, 1, 0.0f, 50.0f, 0.0f, 0.0f, 0},    // Hard Tune
        {0, 1, 1, 24.0f, 50.0f, 45.0f, 55.0f, 0}, // Natural
        {0, 2, 2, 6.0f, 65.0f, 10.0f, 20.0f, 0},  // Low Voice
        {0, 1, 0, 12.0f, 40.0f, 25.0f, 35.0f, 0} // High Voice
    };
    const auto& p = options[juce::jlimit(0, 3, index)];
    auto assign = [this](const char* key, float value) {
        if (auto* parameter = parameters.getParameter(key)) {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
            parameter->endChangeGesture();
        }
    };
    assign("key", static_cast<float>(p.key));
    assign("scale", static_cast<float>(p.scale));
    assign("range", static_cast<float>(p.range));
    assign("retune", p.retune);
    assign("tracking", p.tracking);
    assign("flex", p.flex);
    assign("humanize", p.humanize);
    assign("transpose", static_cast<float>(p.transpose));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new EvolutionVoxAudioProcessor();
}
