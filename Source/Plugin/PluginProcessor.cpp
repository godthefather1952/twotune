#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
using namespace juce;
using APF = AudioParameterFloat;
using API = AudioParameterInt;
using APC = AudioParameterChoice;
using ID = ParameterID;
}

EvolutionVoxAudioProcessor::EvolutionVoxAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "EvolutionVoxSettings", createParameterLayout()) {}

juce::AudioProcessorValueTreeState::ParameterLayout EvolutionVoxAudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<APC>(ID{"key", 1}, "Key",
        juce::StringArray{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 0));
    layout.add(std::make_unique<APC>(ID{"scale", 1}, "Scale",
        juce::StringArray{"Chromatic", "Major", "Natural Minor", "Harmonic Minor", "Minor Pentatonic", "Custom"}, 2));
    layout.add(std::make_unique<APC>(ID{"range", 1}, "Input Range",
        juce::StringArray{"Soprano", "Alto / Tenor", "Low Male", "Instrument", "Bass"}, 1));
    layout.add(std::make_unique<APF>(ID{"retune", 1}, "Retune Speed",
        juce::NormalisableRange<float>(0.0f, 250.0f, 0.1f, 0.4f), 8.0f, "ms"));
    layout.add(std::make_unique<APF>(ID{"tracking", 1}, "Tracking Sensitivity",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f, "%"));
    layout.add(std::make_unique<APF>(ID{"flex", 1}, "Flex",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f, "%"));
    layout.add(std::make_unique<APF>(ID{"humanize", 1}, "Humanize",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f, "%"));
    layout.add(std::make_unique<API>(ID{"transpose", 1}, "Transpose", -12, 12, 0));
    layout.add(std::make_unique<API>(ID{"mask", 1}, "Custom Notes", 1, 4095, 4095));
    return layout;
}

void EvolutionVoxAudioProcessor::prepareToPlay(double sampleRate, int) {
    tuner.prepare(sampleRate);
    setLatencySamples(evolutionvox::PitchShifter::nominalLatencySamples(sampleRate));
}

bool EvolutionVoxAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& output = layouts.getMainOutputChannelSet();
    const auto& input = layouts.getMainInputChannelSet();
    return (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo())
            && input == output;
}

void EvolutionVoxAudioProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numChannels = std::min(audio.getNumChannels(), evolutionvox::TunerEngine::kMaxChannels);
    if (numChannels == 0) return;
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
    auto* left = audio.getWritePointer(0);
    auto* right = numChannels > 1 ? audio.getWritePointer(1) : nullptr;
    const auto count = audio.getNumSamples();
    for (int i = 0; i < count; ++i) {
        float frame[2] { left[i], right == nullptr ? 0.0f : right[i] };
        tuner.processFrame(frame, numChannels);
        left[i] = frame[0];
        if (right != nullptr) right[i] = frame[1];
    }
    for (int ch = numChannels; ch < audio.getNumChannels(); ++ch)
        audio.clear(ch, 0, count);
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
    if (xml != nullptr && xml->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
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
