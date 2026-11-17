#pragma once
#include <JuceHeader.h>
#include "RetuneDialMapping.h"
#include <cmath>

// Design Reference V1: the lightning itself is the tuning pointer.
// This is a reusable, lightweight JUCE visual control. It uses the existing
// "retune" parameter, so DAW automation and preset recall keep working.
// No image decoding, trigonometry, animation or allocations run in the audio callback.
class LightningRetuneDial final : public juce::Component, private juce::Timer {
public:
    explicit LightningRetuneDial(juce::AudioProcessorValueTreeState& state)
        : params(state), parameter(state.getParameter("retune")) {
        jassert(parameter != nullptr);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTooltip("Drag Zeus's lightning around the arc: Natural to Hard Tune");
        lastRetuneMs = readRetuneMs();
        startTimerHz(15); // UI-only automation polling, repaint on value change.
    }

    ~LightningRetuneDial() override { stopTimer(); }

    // Future transparent Zeus layer, NOT the flattened Design Reference V1 screenshot.
    // Asset should exclude its fixed bolt and ring; the independent bolt stays the pointer.
    void setZeusArtwork(juce::Image transparentZeus) {
        artwork = std::move(transparentZeus);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        using Mapping = evolutionvox::ui::RetuneDialMapping;
        const auto width = static_cast<float>(getWidth());
        const auto height = static_cast<float>(getHeight());
        const juce::Point<float> center(width * 0.5f, height * 0.53f);
        const float radius = std::min(width, height) * 0.43f;
        const float intensity = Mapping::intensityFromRetuneMs(readRetuneMs());
        const float degrees = Mapping::pointerDegrees(intensity);
        const auto angle = juce::degreesToRadians(degrees);

        // Ivory/transparent surface: parent paints the Divine White cloud chassis.
        const juce::Colour metallic(0xffb98730), golden(0xffedbd46),
                           lightGold(0xffffdc7c), subdued(0xffdbc7a7);
        g.setColour(juce::Colour(0xfffcf6ef));
        g.fillEllipse(center.x - radius * 0.82f, center.y - radius * 0.82f,
                      radius * 1.64f, radius * 1.64f);

        // Neutral full arc and lit amount. Exactly the same parameter drives the pointer.
        drawArc(g, center, radius, 0.0f, 100.0f, subdued.withAlpha(0.72f), 15.0f);
        drawArc(g, center, radius, 0.0f, intensity, metallic, 15.0f);
        drawArc(g, center, radius, 0.0f, intensity, lightGold, 8.0f);

        for (int i = 0; i <= 36; ++i) {
            const auto a = juce::degreesToRadians(Mapping::startDegrees
                                                 + Mapping::sweepDegrees * (i / 36.0f));
            const float outer = radius * 0.92f;
            const float inner = radius * (i % 6 == 0 ? 0.83f : 0.87f);
            const juce::Colour tick = (i / 36.0f) <= intensity / 100.0f
                                       ? metallic : subdued;
            g.setColour(tick);
            g.drawLine(center.x + std::cos(a) * inner,
                       center.y + std::sin(a) * inner,
                       center.x + std::cos(a) * outer,
                       center.y + std::sin(a) * outer,
                       i % 6 == 0 ? 2.8f : 1.3f);
        }

        // Zeus is a stationary character layer. Only lightning rotates toward the note arc.
        if (artwork.isValid()) {
            const int size = juce::roundToInt(radius * 1.36f);
            g.drawImageWithin(artwork, juce::roundToInt(center.x - size * 0.5f),
                             juce::roundToInt(center.y - size * 0.5f),
                             size, size, juce::RectanglePlacement::centred);
        } else {
            juce::ColourGradient medallion(juce::Colour(0xfffff5e5), center.x, center.y - radius,
                                          juce::Colour(0xffead8ba), center.x, center.y + radius,
                                          false);
            g.setGradientFill(medallion);
            g.fillEllipse(center.x - radius * 0.66f, center.y - radius * 0.66f,
                          radius * 1.32f, radius * 1.32f);
            g.setColour(metallic.withAlpha(0.55f));
            g.drawEllipse(center.x - radius * 0.66f, center.y - radius * 0.66f,
                          radius * 1.32f, radius * 1.32f, 2.0f);
            g.setColour(juce::Colour(0xff7c562e));
            g.setFont(juce::Font(juce::FontOptions().withHeight(22.0f)));
            g.drawText("ZEUS", juce::Rectangle<int>(juce::roundToInt(center.x - 60),
                        juce::roundToInt(center.y - 8), 120, 22),
                        juce::Justification::centred);
        }

        // Stylized lightning is the pointer, NOT a separate circular knob.
        // Its tip is aimed at the current percentage on the gold arc.
        juce::Path bolt;
        const float r = radius;
        bolt.startNewSubPath(center.x - 9.0f, center.y + r * 0.06f);
        bolt.lineTo(center.x + 11.0f, center.y + r * 0.06f);
        bolt.lineTo(center.x + r * 0.05f, center.y - r * 0.36f);
        bolt.lineTo(center.x + r * 0.17f, center.y - r * 0.36f);
        bolt.lineTo(center.x, center.y - r); // tip meets the dial arc
        bolt.lineTo(center.x - r * 0.12f, center.y - r * 0.48f);
        bolt.lineTo(center.x - r * 0.02f, center.y - r * 0.48f);
        bolt.closeSubPath();
        bolt.applyTransform(juce::AffineTransform::rotation(
            juce::degreesToRadians(degrees + 90.0f), center.x, center.y));

        g.setColour(juce::Colour(0x55e3a51a));
        g.strokePath(bolt, juce::PathStrokeType(6.0f));
        g.setColour(lightGold);
        g.fillPath(bolt);
        g.setColour(metallic);
        g.strokePath(bolt, juce::PathStrokeType(2.0f));

        // Tip glint follows bolt rather than misleadingly floating somewhere else.
        const float tipX = center.x + radius * std::cos(angle);
        const float tipY = center.y + radius * std::sin(angle);
        g.setColour(juce::Colour(0xffffe3a0));
        g.fillEllipse(tipX - 5.0f, tipY - 5.0f, 10.0f, 10.0f);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (parameter == nullptr) return;
        parameter->beginChangeGesture();
        dragging = true;
        updateFromMouse(e.position);
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragging) updateFromMouse(e.position);
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (dragging && parameter != nullptr) parameter->endChangeGesture();
        dragging = false;
    }

    void mouseDoubleClick(const juce::MouseEvent&) override {
        if (parameter == nullptr) return;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->getDefaultValue());
        parameter->endChangeGesture();
        repaint();
    }

    void mouseWheelMove(const juce::MouseEvent&,
                        const juce::MouseWheelDetails& wheel) override {
        if (parameter == nullptr || wheel.deltaY == 0.0f) return;
        using Mapping = evolutionvox::ui::RetuneDialMapping;
        const float current = Mapping::intensityFromRetuneMs(readRetuneMs());
        setIntensity(current + wheel.deltaY * 7.0f);
    }

private:
    juce::AudioProcessorValueTreeState& params;
    juce::RangedAudioParameter* parameter = nullptr;
    juce::Image artwork;
    bool dragging = false;
    float lastRetuneMs = -1.0f;

    float readRetuneMs() const noexcept {
        if (auto* value = params.getRawParameterValue("retune"))
            return value->load(std::memory_order_relaxed);
        return 8.0f;
    }

    void setIntensity(float intensity) {
        if (parameter == nullptr) return;
        const float milliseconds =
            evolutionvox::ui::RetuneDialMapping::retuneMsFromIntensity(intensity);
        if (!dragging) parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(milliseconds));
        if (!dragging) parameter->endChangeGesture();
        repaint();
    }

    void updateFromMouse(juce::Point<float> point) {
        const auto centre = juce::Point<float>(getWidth() * 0.5f, getHeight() * 0.53f);
        const auto intensity = evolutionvox::ui::RetuneDialMapping::intensityFromPointer(
            point.x - centre.x, point.y - centre.y);
        setIntensity(intensity);
    }

    static void drawArc(juce::Graphics& g, juce::Point<float> c, float radius,
                        float startPercent, float endPercent, juce::Colour colour,
                        float thickness) {
        using Mapping = evolutionvox::ui::RetuneDialMapping;
        juce::Path path;
        for (int i = 0; i <= 96; ++i) {
            const float value = startPercent + (endPercent - startPercent) * (i / 96.0f);
            const float angle = juce::degreesToRadians(Mapping::pointerDegrees(value));
            const juce::Point<float> p(c.x + std::cos(angle) * radius,
                                       c.y + std::sin(angle) * radius);
            if (i == 0) path.startNewSubPath(p);
            else path.lineTo(p);
        }
        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(thickness));
    }

    void timerCallback() override {
        const float current = readRetuneMs();
        if (std::abs(current - lastRetuneMs) > 0.025f) {
            lastRetuneMs = current;
            repaint();
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LightningRetuneDial)
};
