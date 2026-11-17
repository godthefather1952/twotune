#include "PluginEditor.h"
#include <cmath>

namespace {
const juce::Colour bg(0xff17191d), panel(0xff25272d), gold(0xffefcb64),
                   muted(0xffa5a8b1), white(0xfff3f3f5), stroke(0xff363941);
void styledLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, muted);
    label.setJustificationType(juce::Justification::centredLeft);
}
}

EvolutionVoxAudioProcessorEditor::EvolutionVoxAudioProcessorEditor(EvolutionVoxAudioProcessor& p)
  : AudioProcessorEditor(&p), processor(p) {
    setOpaque(true);
    setSize(850, 585);
    key.addItemList(juce::StringArray{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 1);
    scale.addItemList(juce::StringArray{"Chromatic", "Major", "Natural Minor", "Harmonic Minor", "Minor Pentatonic", "Custom"}, 1);
    range.addItemList(juce::StringArray{"Soprano", "Alto / Tenor", "Low Male", "Instrument", "Bass"}, 1);
    for (auto* c : {&key, &scale, &range}) {
        addAndMakeVisible(*c);
        c->setColour(juce::ComboBox::backgroundColourId, panel);
        c->setColour(juce::ComboBox::textColourId, white);
        c->setColour(juce::ComboBox::outlineColourId, stroke);
    }
    styledLabel(keyLabel, "KEY"); styledLabel(scaleLabel, "SCALE"); styledLabel(rangeLabel, "INPUT RANGE");
    for (auto* l : {&keyLabel, &scaleLabel, &rangeLabel}) addAndMakeVisible(*l);
    keyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters, "key", key);
    scaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters, "scale", scale);
    rangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters, "range", range);
    addKnob("RETUNE SPEED", "retune");
    addKnob("TRACKING", "tracking");
    addKnob("FLEX", "flex");
    addKnob("HUMANIZE", "humanize");
    addKnob("TRANSPOSE", "transpose");
    const juce::StringArray names {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    for (int i = 0; i < 12; ++i) {
        noteButtons[static_cast<size_t>(i)].setButtonText(names[i]);
        addAndMakeVisible(noteButtons[static_cast<size_t>(i)]);
        noteButtons[static_cast<size_t>(i)].onClick = [this, i] { flipNote(i); };
    }
    addAndMakeVisible(presetButton);
    presetButton.setColour(juce::TextButton::buttonColourId, panel);
    presetButton.setColour(juce::TextButton::textColourOffId, gold);
    presetButton.onClick = [this] { setPresetMenu(); };
    styledLabel(detected, "INPUT  --"); styledLabel(target, "TARGET  --");
    styledLabel(status, "Choose Custom to enable the note keyboard.");
    for (auto* l : {&detected, &target, &status}) addAndMakeVisible(*l);
    detected.setColour(juce::Label::textColourId, white);
    target.setColour(juce::Label::textColourId, gold);
    updateNoteButtons();
    startTimerHz(20);
}

EvolutionVoxAudioProcessorEditor::~EvolutionVoxAudioProcessorEditor() { stopTimer(); }

void EvolutionVoxAudioProcessorEditor::addKnob(const juce::String& name, const char* parameterID) {
    auto entry = std::make_unique<Knob>();
    entry->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    entry->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 95, 25);
    entry->slider.setColour(juce::Slider::rotarySliderFillColourId, gold);
    entry->slider.setColour(juce::Slider::rotarySliderOutlineColourId, stroke);
    entry->slider.setColour(juce::Slider::thumbColourId, gold);
    entry->slider.setColour(juce::Slider::textBoxTextColourId, white);
    entry->slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(entry->slider);
    entry->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, parameterID, entry->slider);
    styledLabel(entry->label, name);
    entry->label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(entry->label);
    knobs.push_back(std::move(entry));
}

void EvolutionVoxAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(bg);
    auto plot = juce::Rectangle<float>(26, 160, 798, 138);
    g.setColour(panel); g.fillRoundedRectangle(plot, 11.0f);
    g.setColour(stroke);
    for (int i = 1; i < 5; ++i)
        g.drawHorizontalLine(160 + i * 27, 40.0f, 810.0f);
    const auto first = std::find_if(pitchPoints.begin(), pitchPoints.end(), [](float f) { return f > 0.0f; });
    if (first != pitchPoints.end()) {
        juce::Path path; bool started = false;
        for (int i = 0; i < static_cast<int>(pitchPoints.size()); ++i) {
            const float v = pitchPoints[static_cast<size_t>(i)];
            if (v < 1) { started = false; continue; }
            const float x = 40.0f + i * (770.0f / (pitchPoints.size() - 1));
            const float y = juce::jlimit(176.0f, 285.0f, 282.0f - (v - 48.0f) * 2.6f);
            if (!started) { path.startNewSubPath(x, y); started = true; }
            else path.lineTo(x, y);
        }
        g.setColour(gold); g.strokePath(path, juce::PathStrokeType(2.0f));
    }
    g.setColour(white);
    g.setFont(juce::Font(juce::FontOptions().withHeight(28.0f).withStyle("Bold")));
    g.drawText("EVOLUTION", 26, 15, 190, 42, juce::Justification::centredLeft, false);
    g.setColour(gold);
    g.drawText("VOX", 216, 15, 100, 42, juce::Justification::centredLeft, false);
    g.setColour(muted);
    g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
    g.drawText("REAL-TIME VOCAL TUNING  /  DEVELOPMENT BUILD 0.1", 27, 54, 460, 16, juce::Justification::centredLeft);
    g.drawText("LIVE INPUT PITCH (MIDI NOTES)", 40, 168, 270, 18, juce::Justification::centredLeft);
    g.drawText("CUSTOM PITCH CLASSES", 26, 493, 300, 20, juce::Justification::centredLeft);
    g.drawText("PROTOTYPE DSP · NOT FORMANT-PRESERVING", 500, 549, 322, 18, juce::Justification::centredRight);
}

void EvolutionVoxAudioProcessorEditor::resized() {
    presetButton.setBounds(700, 24, 124, 36);
    keyLabel.setBounds(26, 80, 80, 20); key.setBounds(26, 102, 105, 40);
    scaleLabel.setBounds(150, 80, 190, 20); scale.setBounds(150, 102, 245, 40);
    rangeLabel.setBounds(415, 80, 170, 20); range.setBounds(415, 102, 210, 40);
    detected.setBounds(631, 103, 108, 18); target.setBounds(631, 123, 180, 18);
    constexpr int width = 142;
    for (int i = 0; i < static_cast<int>(knobs.size()); ++i) {
        const int x = 35 + i * 160;
        knobs[static_cast<size_t>(i)]->slider.setBounds(x, 310, width, 135);
        knobs[static_cast<size_t>(i)]->label.setBounds(x, 447, width, 25);
    }
    const int gap = 5, x0 = 26, noteWidth = 62;
    for (int i = 0; i < 12; ++i)
        noteButtons[static_cast<size_t>(i)].setBounds(x0 + i * (noteWidth + gap), 515, noteWidth, 34);
    status.setBounds(26, 555, 470, 20);
}

juce::String EvolutionVoxAudioProcessorEditor::noteName(float hz) {
    if (hz <= 0.0f || !std::isfinite(hz)) return "--";
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int midi = juce::jlimit(0, 127, static_cast<int>(std::round(69 + 12 * std::log2(hz / 440.0f))));
    return juce::String(names[midi % 12]) + juce::String(midi / 12 - 1);
}

void EvolutionVoxAudioProcessorEditor::timerCallback() {
    const float hz = processor.tuner.getInputHz();
    const float to = processor.tuner.getTargetHz();
    detected.setText("INPUT  " + noteName(hz), juce::dontSendNotification);
    target.setText("TARGET  " + noteName(to), juce::dontSendNotification);
    pitchPoints = processor.tuner.historySnapshot();
    updateNoteButtons();
    repaint(juce::Rectangle<int>(25, 160, 800, 140));
}

void EvolutionVoxAudioProcessorEditor::updateNoteButtons() {
    const bool custom = processor.parameters.getRawParameterValue("scale")->load() == 5.0f;
    const int mask = static_cast<int>(processor.parameters.getRawParameterValue("mask")->load());
    for (int i = 0; i < 12; ++i) {
        auto& button = noteButtons[static_cast<size_t>(i)];
        button.setEnabled(custom);
        const bool active = ((mask >> i) & 1) != 0;
        button.setColour(juce::TextButton::buttonColourId, active ? gold : panel);
        button.setColour(juce::TextButton::textColourOffId, active ? bg : muted);
    }
    status.setText(custom ? "Click notes to include/exclude them from tuning."
                          : "Choose Custom from Scale to edit the note keyboard.",
                   juce::dontSendNotification);
}

void EvolutionVoxAudioProcessorEditor::flipNote(int semitone) {
    auto* param = processor.parameters.getParameter("mask");
    if (param == nullptr) return;
    int mask = static_cast<int>(processor.parameters.getRawParameterValue("mask")->load());
    const int newMask = mask ^ (1 << semitone);
    if (newMask == 0) return; // At least one note must remain active.
    param->beginChangeGesture();
    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(newMask)));
    param->endChangeGesture();
    updateNoteButtons();
}

void EvolutionVoxAudioProcessorEditor::setPresetMenu() {
    juce::PopupMenu menu;
    menu.addItem(1, "Hard Tune");
    menu.addItem(2, "Natural Correction");
    menu.addItem(3, "Low Voice");
    menu.addItem(4, "High Voice");
    juce::Component::SafePointer<EvolutionVoxAudioProcessorEditor> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetButton),
        [safeThis](int selected) {
            if (safeThis != nullptr && selected > 0)
                safeThis->processor.loadFactoryPreset(selected - 1);
        });
}
