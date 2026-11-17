#pragma once
// Settings / Advanced panel (overlay). Holds every Build 0.1 control that is not on the main
// screen: Retune Speed in ms, Tracking, Flex, Humanize, Transpose, Input Range, factory presets,
// the custom-scale note keyboard and the live pitch display. Its timer only runs while visible.
#include "PluginProcessor.h"
#include "Theme.h"
#include <array>
#include <memory>

class SettingsPanel final : public juce::Component, private juce::Timer {
public:
    explicit SettingsPanel(EvolutionVoxAudioProcessor&);
    ~SettingsPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    bool keyPressed(const juce::KeyPress&) override;
    std::function<void()> onClose;

private:
    struct Row {
        juce::Label name;
        juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    EvolutionVoxAudioProcessor& processor;
    std::array<Row, 5> rows;
    juce::Label rangeLabel;
    juce::ComboBox range;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rangeAttachment;
    juce::TextButton presetButton { "PRESETS" }, closeButton { "DONE" };
    std::array<juce::TextButton, 12> noteButtons;
    juce::Label inputNote, targetNote, notesStatus;

    std::array<float, evolutionvox::TunerEngine::kHistory> pitchPoints {};
    int lastMask = -1;
    bool lastCustom = false;

    juce::Rectangle<float> card() const;
    juce::Rectangle<float> plotArea() const;
    float k() const noexcept { return static_cast<float>(getWidth()) / evolutionvox::ui::theme::designWidth; }
    juce::Rectangle<int> scaled(float x, float y, float w, float h) const;
    void timerCallback() override;
    void updateNoteButtons(bool force);
    void flipNote(int semitone);
    void showPresetMenu();
    static juce::String noteName(float hz);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPanel)
};
