#pragma once
// Ivory/champagne-gold LookAndFeel for the Design V1 editor: pill dropdowns, popup menus,
// slim gold sliders and note toggles. Everything is vector-drawn (no bitmaps) and cheap.
#include "Theme.h"

namespace evolutionvox::ui {

class EvolutionLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    EvolutionLookAndFeel() {
        using namespace theme;
        setColour(juce::ComboBox::textColourId, charcoal);
        setColour(juce::ComboBox::arrowColourId, goldDeep);
        setColour(juce::PopupMenu::backgroundColourId, ivory);
        setColour(juce::PopupMenu::textColourId, charcoal);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, champagne);
        setColour(juce::PopupMenu::highlightedTextColourId, charcoal);
        setColour(juce::Label::textColourId, charcoal);
        setColour(juce::Slider::textBoxTextColourId, charcoal);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, champagne);
        setColour(juce::TextEditor::textColourId, charcoal);
        setColour(juce::TextEditor::highlightColourId, champagne);
        setColour(juce::CaretComponent::caretColourId, goldDeep);
        setColour(juce::TooltipWindow::backgroundColourId, ivory);
        setColour(juce::TooltipWindow::textColourId, charcoal);
        setColour(juce::TooltipWindow::outlineColourId, champagne);
        setColour(juce::TextButton::textColourOffId, taupe);
        setColour(juce::TextButton::textColourOnId, charcoal);
    }

    // ---- Dropdowns -------------------------------------------------------------------------
    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int,
                      juce::ComboBox& box) override {
        using namespace theme;
        auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(1.5f);
        const float radius = r.getHeight() * 0.5f;
        g.setColour(goldDeep.withAlpha(0.10f));
        g.fillRoundedRectangle(r.translated(0.0f, 2.0f), radius);
        g.setGradientFill(juce::ColourGradient(cloud, r.getX(), r.getY(), ivoryDeep, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle(r, radius);
        g.setColour(box.isMouseOver(true) ? gold : gold.withAlpha(0.85f));
        g.drawRoundedRectangle(r, radius, 1.6f);
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.drawRoundedRectangle(r.reduced(2.0f), radius - 2.0f, 0.8f);
        // chevron
        const float cx = r.getRight() - radius * 1.15f, cy = r.getCentreY();
        const float s = r.getHeight() * 0.12f;
        juce::Path chevron;
        chevron.startNewSubPath(cx - s, cy - s * 0.45f);
        chevron.lineTo(cx, cy + s * 0.55f);
        chevron.lineTo(cx + s, cy - s * 0.45f);
        g.setColour(goldDeep);
        g.strokePath(chevron, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont(juce::ComboBox& box) override {
        return theme::font(static_cast<float>(box.getHeight()) * 0.40f, true);
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        // A single text line (label only as tall as one line) that squeezes horizontally instead
        // of wrapping, so "Minor Pentatonic" still fits the pill.
        const int h = box.getHeight();
        const auto font = getComboBoxFont(box);
        const int lineH = juce::roundToInt(font.getHeight() * 1.3f);
        label.setBounds(h / 3, (h - lineH) / 2, box.getWidth() - h / 3 - static_cast<int>(h * 0.85f), lineH);
        label.setFont(font);
        label.setMinimumHorizontalScale(0.55f);
        label.setJustificationType(juce::Justification::centred);
    }

    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label) override {
        return LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box, label).withStandardItemHeight(28);
    }

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override {
        g.fillAll(theme::ivory);
        g.setColour(theme::gold.withAlpha(0.7f));
        g.drawRect(0, 0, width, height, 1);
    }

    juce::Font getPopupMenuFont() override { return theme::font(16.0f); }

    // ---- Sliders (Settings panel) ----------------------------------------------------------
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                          float, float, juce::Slider::SliderStyle, juce::Slider& slider) override {
        using namespace theme;
        const float cy = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        const float left = static_cast<float>(x), right = static_cast<float>(x + width);
        const float thickness = 5.0f;
        g.setColour(track);
        g.fillRoundedRectangle(left, cy - thickness * 0.5f, right - left, thickness, thickness * 0.5f);
        // Bipolar parameters (transpose) fill from their centre.
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const float origin = bipolar ? static_cast<float>(slider.getPositionOfValue(0.0)) + left : left;
        const float a = std::min(origin, sliderPos), b = std::max(origin, sliderPos);
        g.setGradientFill(juce::ColourGradient(goldDeep, left, cy, goldBright, right, cy, false));
        g.fillRoundedRectangle(a, cy - thickness * 0.5f, std::max(0.0f, b - a), thickness, thickness * 0.5f);
        const float knob = 15.0f;
        g.setColour(goldDeep.withAlpha(0.25f));
        g.fillEllipse(sliderPos - knob * 0.5f, cy - knob * 0.5f + 1.5f, knob, knob);
        g.setGradientFill(juce::ColourGradient(goldBright, sliderPos, cy - knob * 0.5f, gold, sliderPos, cy + knob * 0.5f, false));
        g.fillEllipse(sliderPos - knob * 0.5f, cy - knob * 0.5f, knob, knob);
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.drawEllipse(sliderPos - knob * 0.5f + 1.0f, cy - knob * 0.5f + 1.0f, knob - 2.0f, knob - 2.0f, 1.0f);
    }

    juce::Label* createSliderTextBox(juce::Slider& slider) override {
        auto* label = LookAndFeel_V4::createSliderTextBox(slider);
        label->setFont(theme::font(15.0f, true));
        label->setJustificationType(juce::Justification::centredRight);
        return label;
    }

    // ---- Buttons (note keyboard, presets, close) ---------------------------------------------
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                              bool highlighted, bool down) override {
        using namespace theme;
        auto r = button.getLocalBounds().toFloat().reduced(1.0f);
        const float radius = std::min(r.getHeight() * 0.5f, 10.0f);
        const bool on = button.getToggleState();
        if (on)
            g.setGradientFill(juce::ColourGradient(goldBright, r.getX(), r.getY(), gold, r.getX(), r.getBottom(), false));
        else
            g.setGradientFill(juce::ColourGradient(cloud, r.getX(), r.getY(), ivoryDeep, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle(r, radius);
        if (down || highlighted) {
            g.setColour(goldDeep.withAlpha(down ? 0.16f : 0.07f));
            g.fillRoundedRectangle(r, radius);
        }
        g.setColour(button.isEnabled() ? gold.withAlpha(on ? 1.0f : 0.75f) : champagne);
        g.drawRoundedRectangle(r, radius, 1.2f);
        if (!button.isEnabled()) {
            g.setColour(ivory.withAlpha(0.55f));
            g.fillRoundedRectangle(r, radius);
        }
    }

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override {
        return theme::font(std::min(15.0f, static_cast<float>(buttonHeight) * 0.46f), true);
    }
};

} // namespace evolutionvox::ui
