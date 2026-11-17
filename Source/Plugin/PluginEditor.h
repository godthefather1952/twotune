#pragma once
// Design V1 "Divine White" editor. Main screen = Power, Zeus Retune dial, Key, Scale, Settings.
// Everything else lives in SettingsPanel. See Docs/DESIGN_V1_IMPLEMENTATION.md.
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "EvolutionLookAndFeel.h"
#include "LightningRetuneDial.h"
#include "SettingsPanel.h"
#include <memory>

class EvolutionVoxAudioProcessorEditor final : public juce::AudioProcessorEditor {
public:
    explicit EvolutionVoxAudioProcessorEditor(EvolutionVoxAudioProcessor&);
    ~EvolutionVoxAudioProcessorEditor() override;
    void paint(juce::Graphics&) override {}
    void resized() override;

    // For tests / screenshot generation.
    LightningRetuneDial& getDial() noexcept { return dial; }
    void showSettings(bool shouldShow);
    bool isSettingsVisible() const noexcept { return settings.isVisible(); }

private:
    // Static ivory chassis: gradient, clouds, logo, labels. Buffered: rendered once per size.
    class Background final : public juce::Component {
    public:
        Background() { setOpaque(true); setBufferedToImage(true); setInterceptsMouseClicks(false, false); }
        void paint(juce::Graphics&) override;
        juce::Point<float> haloCentre;
        float haloRadius = 0.0f;
    };

    class PowerButton final : public juce::Button {
    public:
        PowerButton() : juce::Button("Power") { setClickingTogglesState(true); }
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    };

    class SettingsButton final : public juce::Button {
    public:
        SettingsButton() : juce::Button("Settings") {}
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    };

    evolutionvox::ui::EvolutionLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 700 };
    Background background;
    LightningRetuneDial dial;
    PowerButton power;
    SettingsButton settingsButton;
    juce::ComboBox key, scale;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttachment, scaleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    SettingsPanel settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EvolutionVoxAudioProcessorEditor)
};
