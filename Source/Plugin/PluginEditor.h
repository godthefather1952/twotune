#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <array>
#include <memory>
#include <vector>

class EvolutionVoxAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer {
public:
    explicit EvolutionVoxAudioProcessorEditor(EvolutionVoxAudioProcessor&);
    ~EvolutionVoxAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Knob {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    EvolutionVoxAudioProcessor& processor;
    juce::ComboBox key, scale, range;
    juce::Label keyLabel, scaleLabel, rangeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttachment, scaleAttachment, rangeAttachment;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::array<juce::TextButton, 12> noteButtons;
    juce::TextButton presetButton{"PRESETS"};
    juce::Label detected, target, status;
    std::array<float, evolutionvox::TunerEngine::kHistory> pitchPoints{};
    void timerCallback() override;
    void updateNoteButtons();
    void flipNote(int semitone);
    void addKnob(const juce::String& name, const char* parameterID);
    void setPresetMenu();
    static juce::String noteName(float hertz);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EvolutionVoxAudioProcessorEditor)
};
