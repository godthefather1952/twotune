#include "SettingsPanel.h"
#include <cmath>
#include <cstring>

namespace theme = evolutionvox::ui::theme;

namespace {
struct RowSpec { const char* name; const char* id; const char* help; };
constexpr RowSpec rowSpecs[] = {
    { "RETUNE SPEED", "retune", "Same control as Zeus's lightning, in milliseconds (0 ms = hard tune)." },
    { "TRACKING", "tracking", "Higher accepts weaker/breathier pitch at the risk of false detections." },
    { "FLEX", "flex", "Lets notes close to the target keep some natural drift." },
    { "HUMANIZE", "humanize", "Slows correction on sustained notes for a more natural sound." },
    { "TRANSPOSE", "transpose", "Shifts the corrected output by semitones." },
};
const juce::StringArray noteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

SettingsPanel::SettingsPanel(EvolutionVoxAudioProcessor& p) : processor(p) {
    setWantsKeyboardFocus(true);
    for (size_t i = 0; i < rows.size(); ++i) {
        auto& row = rows[i];
        row.name.setText(rowSpecs[i].name, juce::dontSendNotification);
        row.name.setFont(theme::font(13.0f, true));
        row.name.setColour(juce::Label::textColourId, theme::taupe);
        addAndMakeVisible(row.name);
        row.slider.setTooltip(rowSpecs[i].help);
        row.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 86, 24);
        addAndMakeVisible(row.slider);
        row.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.parameters, rowSpecs[i].id, row.slider);
    }
    rows[0].slider.textFromValueFunction = [](double v) { return juce::String(v, v < 10.0 ? 2 : 1) + " ms"; };
    for (size_t i = 1; i < 4; ++i)
        rows[i].slider.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + " %"; };
    rows[4].slider.textFromValueFunction = [](double v) {
        const int st = juce::roundToInt(v);
        return (st > 0 ? "+" : "") + juce::String(st) + " st";
    };
    rows[0].slider.valueFromTextFunction = [](const juce::String& t) { return t.retainCharacters("0123456789.").getDoubleValue(); };
    rows[4].slider.valueFromTextFunction = [](const juce::String& t) { return t.retainCharacters("-0123456789").getDoubleValue(); };
    for (auto& row : rows) row.slider.updateText();

    rangeLabel.setText("INPUT RANGE", juce::dontSendNotification);
    rangeLabel.setFont(theme::font(13.0f, true));
    rangeLabel.setColour(juce::Label::textColourId, theme::taupe);
    addAndMakeVisible(rangeLabel);
    range.addItemList({ "Soprano", "Alto / Tenor", "Low Male", "Instrument", "Bass" }, 1);
    range.setTooltip("Expected pitch range of the input; narrower ranges track more reliably.");
    addAndMakeVisible(range);
    rangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, "range", range);

    presetButton.setTooltip("Original factory starting points");
    presetButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible(presetButton);
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeButton);

    for (int i = 0; i < 12; ++i) {
        auto& b = noteButtons[static_cast<size_t>(i)];
        b.setButtonText(noteNames[i]);
        b.onClick = [this, i] { flipNote(i); };
        addAndMakeVisible(b);
    }
    for (auto* l : { &inputNote, &targetNote, &notesStatus }) {
        l->setFont(theme::font(14.0f, true));
        addAndMakeVisible(*l);
    }
    inputNote.setColour(juce::Label::textColourId, theme::charcoal);
    targetNote.setColour(juce::Label::textColourId, theme::goldDeep);
    notesStatus.setColour(juce::Label::textColourId, theme::muted);
    notesStatus.setFont(theme::font(12.5f));
    inputNote.setText("INPUT  --", juce::dontSendNotification);
    targetNote.setText("TARGET  --", juce::dontSendNotification);
    updateNoteButtons(true);
}

SettingsPanel::~SettingsPanel() { stopTimer(); }

juce::Rectangle<int> SettingsPanel::scaled(float x, float y, float w, float h) const {
    const float s = k();
    return juce::Rectangle<float>(x * s, y * s, w * s, h * s).toNearestInt();
}

juce::Rectangle<float> SettingsPanel::card() const { return scaled(70, 52, 860, 552).toFloat(); }
juce::Rectangle<float> SettingsPanel::plotArea() const { return scaled(530, 268, 360, 116).toFloat(); }

void SettingsPanel::resized() {
    const float s = k();
    for (size_t i = 0; i < rows.size(); ++i) {
        const float y = 142.0f + static_cast<float>(i) * 76.0f;
        rows[i].name.setBounds(scaled(108, y, 300, 20));
        rows[i].slider.setBounds(scaled(108, y + 24, 372, 34));
        rows[i].slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, juce::roundToInt(92 * s), juce::roundToInt(26 * s));
    }
    rangeLabel.setBounds(scaled(530, 142, 220, 20));
    range.setBounds(scaled(530, 166, 230, 42));
    presetButton.setBounds(scaled(774, 166, 116, 42));
    inputNote.setBounds(scaled(650, 232, 118, 24));
    targetNote.setBounds(scaled(768, 232, 122, 24));
    for (int i = 0; i < 12; ++i)
        noteButtons[static_cast<size_t>(i)].setBounds(scaled(530.0f + static_cast<float>(i % 6) * 61.0f,
                                                              426.0f + static_cast<float>(i / 6) * 42.0f, 55, 36));
    notesStatus.setBounds(scaled(530, 510, 360, 20));
    closeButton.setBounds(scaled(800, 72, 100, 38));
}

void SettingsPanel::paint(juce::Graphics& g) {
    const float s = k();
    g.fillAll(theme::ivory.withAlpha(0.72f));                 // scrim over the main view
    const auto c = card();
    g.setColour(theme::goldDeep.withAlpha(0.10f));
    g.fillRoundedRectangle(c.translated(0, 6 * s), 22 * s);
    g.setGradientFill(juce::ColourGradient(theme::cloud, c.getX(), c.getY(), theme::ivoryDeep, c.getX(), c.getBottom(), false));
    g.fillRoundedRectangle(c, 22 * s);
    g.setColour(theme::gold.withAlpha(0.8f));
    g.drawRoundedRectangle(c.reduced(0.75f), 22 * s, 1.4f);

    g.setColour(theme::charcoal);
    g.setFont(theme::font(28 * s, true, true));
    g.drawText("SETTINGS", scaled(106, 70, 300, 40), juce::Justification::centredLeft);
    g.setColour(theme::muted);
    g.setFont(theme::font(13 * s));
    g.drawText("Advanced tuning controls. The main screen keeps Retune, Key, Scale and Power.",
               scaled(108, 106, 600, 20), juce::Justification::centredLeft);

    g.setColour(theme::champagne);
    g.drawVerticalLine(juce::roundToInt(505 * s), 142 * s, 520 * s);

    g.setColour(theme::taupe);
    g.setFont(theme::font(13 * s, true));
    g.drawText("LIVE PITCH", scaled(530, 234, 120, 20).translated(0, -1), juce::Justification::centredLeft);
    g.drawText("CUSTOM SCALE NOTES", scaled(530, 400, 300, 20), juce::Justification::centredLeft);

    // live pitch trace
    const auto plot = plotArea();
    g.setColour(theme::cloud);
    g.fillRoundedRectangle(plot, 10 * s);
    g.setColour(theme::champagne);
    g.drawRoundedRectangle(plot, 10 * s, 1.0f);
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine(juce::roundToInt(plot.getY() + plot.getHeight() * static_cast<float>(i) / 4.0f),
                             plot.getX() + 8 * s, plot.getRight() - 8 * s);
    juce::Path trace;
    bool started = false;
    const float n = static_cast<float>(pitchPoints.size() - 1);
    for (size_t i = 0; i < pitchPoints.size(); ++i) {
        const float midi = pitchPoints[i];
        if (midi < 1.0f) { started = false; continue; }
        const float x = plot.getX() + 8 * s + static_cast<float>(i) * (plot.getWidth() - 16 * s) / n;
        const float y = juce::jlimit(plot.getY() + 6 * s, plot.getBottom() - 6 * s,
                                     plot.getBottom() - 6 * s - (midi - 40.0f) / 44.0f * (plot.getHeight() - 12 * s));
        if (!started) { trace.startNewSubPath(x, y); started = true; } else trace.lineTo(x, y);
    }
    g.setColour(theme::gold);
    g.strokePath(trace, juce::PathStrokeType(2.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(theme::muted);
    g.setFont(theme::font(12 * s));
    g.drawText("Prototype DSP: formant correction and throat modelling are not implemented yet.",
               scaled(106, 560, 780, 20), juce::Justification::centredLeft);
}

void SettingsPanel::visibilityChanged() {
    if (isVisible()) {
        updateNoteButtons(true);
        startTimerHz(15);
        grabKeyboardFocus();
    } else {
        stopTimer();
    }
}

bool SettingsPanel::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    return false;
}

juce::String SettingsPanel::noteName(float hz) {
    if (hz <= 0.0f || !std::isfinite(hz)) return "--";
    const int midi = juce::jlimit(0, 127, static_cast<int>(std::round(69 + 12 * std::log2(hz / 440.0f))));
    return noteNames[midi % 12] + juce::String(midi / 12 - 1);
}

void SettingsPanel::timerCallback() {
    // Only touch components whose content actually changed (no blanket repaints).
    const auto input = "INPUT  " + noteName(processor.tuner.getInputHz());
    const auto target = "TARGET  " + noteName(processor.tuner.getTargetHz());
    if (inputNote.getText() != input) inputNote.setText(input, juce::dontSendNotification);
    if (targetNote.getText() != target) targetNote.setText(target, juce::dontSendNotification);
    updateNoteButtons(false);
    const auto history = processor.tuner.historySnapshot();
    if (std::memcmp(history.data(), pitchPoints.data(), sizeof(float) * pitchPoints.size()) != 0) {
        pitchPoints = history;
        repaint(plotArea().getSmallestIntegerContainer());
    }
}

void SettingsPanel::updateNoteButtons(bool force) {
    const bool custom = processor.parameters.getRawParameterValue("scale")->load() > 4.5f;
    const int mask = static_cast<int>(processor.parameters.getRawParameterValue("mask")->load());
    if (!force && mask == lastMask && custom == lastCustom) return;
    lastMask = mask;
    lastCustom = custom;
    for (int i = 0; i < 12; ++i) {
        auto& b = noteButtons[static_cast<size_t>(i)];
        b.setEnabled(custom);
        b.setToggleState(((mask >> i) & 1) != 0, juce::dontSendNotification);
        b.setTooltip(custom ? "Include/exclude this note" : "Choose Scale: Custom on the main screen to edit");
    }
    notesStatus.setText(custom ? "Click notes to include or exclude them from tuning."
                               : "Select Scale \"Custom\" on the main screen to edit these notes.",
                        juce::dontSendNotification);
}

void SettingsPanel::flipNote(int semitone) {
    auto* param = processor.parameters.getParameter("mask");
    if (param == nullptr) return;
    const int mask = static_cast<int>(processor.parameters.getRawParameterValue("mask")->load());
    const int newMask = mask ^ (1 << semitone);
    if (newMask == 0) { updateNoteButtons(true); return; } // at least one note must remain
    param->beginChangeGesture();
    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(newMask)));
    param->endChangeGesture();
    updateNoteButtons(true);
}

void SettingsPanel::showPresetMenu() {
    juce::PopupMenu menu;
    menu.addItem(1, "Hard Tune");
    menu.addItem(2, "Natural Correction");
    menu.addItem(3, "Low Voice");
    menu.addItem(4, "High Voice");
    juce::Component::SafePointer<SettingsPanel> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetButton),
                       [safeThis](int selected) {
                           if (safeThis != nullptr && selected > 0)
                               safeThis->processor.loadFactoryPreset(selected - 1);
                       });
}
