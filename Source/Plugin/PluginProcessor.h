#pragma once
#include <JuceHeader.h>
#include "DSP/TunerEngine.h"
#include <array>
#include <atomic>
#include <vector>

class EvolutionVoxAudioProcessor final : public juce::AudioProcessor {
public:
    EvolutionVoxAudioProcessor();
    ~EvolutionVoxAudioProcessor() override = default;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // Host bypass (the VST3 wrapper's own bypass parameter, unchanged since Build 0.1).
    // Passes audio through the same latency as the effect so timing does not jump.
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    evolutionvox::TunerEngine tuner;
    void loadFactoryPreset(int index);
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // True while the Power switch has fully engaged the effect (for UI/tests; relaxed read).
    bool isEffectEngaged() const noexcept { return engagedForUi.load(std::memory_order_relaxed); }

private:
    // Power state machine. Off -> WarmingUp (tuner refills its delay line while the delayed dry
    // signal keeps playing) -> FadingIn -> On -> FadingOut -> Off.
    enum class PowerState { off, warmingUp, fadingIn, on, fadingOut };
    PowerState powerState = PowerState::on;
    int warmupRemaining = 0;
    float wetGain = 1.0f;
    float fadeStep = 1.0f / 960.0f;
    int latencySamples = 0;
    double preparedSampleRate = 48000.0;

    // Dry path delayed by the reported latency (allocated in prepareToPlay, never on audio thread).
    std::array<std::vector<float>, evolutionvox::TunerEngine::kMaxChannels> dryDelay;
    int dryWrite = 0;

    std::atomic<float>* powerParam = nullptr;
    std::atomic<bool> engagedForUi { true };

    void updateTunerSettings() noexcept;
    float pushDry(int channel, float x) noexcept;
    void advanceDryWrite() noexcept;
    void runDelayedDry(juce::AudioBuffer<float>&, int numChannels) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EvolutionVoxAudioProcessor)
};
