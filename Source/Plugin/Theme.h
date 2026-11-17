#pragma once
// Design V1 "Divine White" palette, fonts and vector icons (ivory + champagne gold).
#include <JuceHeader.h>

namespace evolutionvox::ui::theme {

// Layout is authored in a 1000 x 650 design space and scaled uniformly (resizable editor).
constexpr float designWidth = 1000.0f;
constexpr float designHeight = 650.0f;

const juce::Colour ivory      { 0xfffbf6ec };
const juce::Colour ivoryDeep  { 0xfff2e6d0 };
const juce::Colour cloud      { 0xfffffdf8 };
const juce::Colour champagne  { 0xffe8d4a6 };
const juce::Colour track      { 0xffe6d7b9 };
const juce::Colour gold       { 0xffe3a62a };
const juce::Colour goldBright { 0xffffd76a };
const juce::Colour goldDeep   { 0xffa8741c };
const juce::Colour brass      { 0xff9c7b40 };
const juce::Colour charcoal   { 0xff2a2521 };
const juce::Colour taupe      { 0xff6d5d48 };
const juce::Colour muted      { 0xff9a8a72 };

inline juce::Font font(float height, bool bold = false, bool italic = false) {
    juce::String style = bold ? (italic ? "Bold Italic" : "Bold") : (italic ? "Italic" : "Regular");
    return juce::Font(juce::FontOptions().withHeight(height).withStyle(style));
}

// Draws text with extra letter spacing (the "V O C A L  T U N I N G" look).
inline void drawSpacedText(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                           const juce::Font& f, float spacing, juce::Justification just) {
    // Per-character layout using the font's own advance plus extra spacing.
    // Only used in cached/static layers, never in per-frame painting.
    float totalWidth = 0.0f;
    juce::Array<float> advances;
    for (auto c : text) {
        const float w = juce::GlyphArrangement::getStringWidth(f, juce::String::charToString(c));
        advances.add(w);
        totalWidth += w + spacing;
    }
    totalWidth -= spacing;
    float startX = area.getX();
    if (just.testFlags(juce::Justification::horizontallyCentred)) startX = area.getCentreX() - totalWidth * 0.5f;
    else if (just.testFlags(juce::Justification::right)) startX = area.getRight() - totalWidth;
    g.setFont(f);
    int index = 0;
    for (auto c : text) {
        g.drawText(juce::String::charToString(c),
                   juce::Rectangle<float>(startX, area.getY(), advances[index] + 2.0f, area.getHeight()),
                   juce::Justification::centredLeft, false);
        startX += advances[index++] + spacing;
    }
}

// Power symbol centred in `area`.
inline juce::Path powerIcon(juce::Rectangle<float> area) {
    juce::Path p;
    const auto c = area.getCentre();
    const float r = area.getWidth() * 0.36f;
    p.addCentredArc(c.x, c.y + r * 0.08f, r, r, 0.0f, juce::degreesToRadians(35.0f),
                    juce::degreesToRadians(325.0f), true);
    p.startNewSubPath(c.x, c.y - r * 1.05f);
    p.lineTo(c.x, c.y + r * 0.15f);
    return p;
}

// Eight-tooth gear with a hole, centred in `area`.
inline juce::Path gearIcon(juce::Rectangle<float> area) {
    juce::Path p;
    const auto c = area.getCentre();
    const float outer = area.getWidth() * 0.46f, inner = outer * 0.74f, hole = outer * 0.34f;
    constexpr int teeth = 8;
    for (int i = 0; i < teeth * 4; ++i) {
        const float a = juce::MathConstants<float>::twoPi * (static_cast<float>(i) - 0.5f) / (teeth * 4.0f);
        const float radius = (i % 4 == 0 || i % 4 == 1) ? outer : inner;
        const juce::Point<float> pt(c.x + radius * std::sin(a), c.y - radius * std::cos(a));
        if (i == 0) p.startNewSubPath(pt); else p.lineTo(pt);
    }
    p.closeSubPath();
    p.addEllipse(c.x - hole, c.y - hole, hole * 2.0f, hole * 2.0f);
    p.setUsingNonZeroWinding(false);
    return p;
}

// Small horizontal lightning flourish (title ornaments).
inline juce::Path flourish(juce::Rectangle<float> a, bool pointLeft) {
    juce::Path p;
    const float y = a.getCentreY(), h = a.getHeight();
    p.startNewSubPath(a.getX(), y + h * 0.06f);
    p.lineTo(a.getX() + a.getWidth() * 0.62f, y - h * 0.04f);
    p.lineTo(a.getX() + a.getWidth() * 0.58f, y - h * 0.32f);
    p.lineTo(a.getRight(), y - h * 0.02f);
    p.lineTo(a.getX() + a.getWidth() * 0.40f, y + h * 0.10f);
    p.lineTo(a.getX() + a.getWidth() * 0.44f, y + h * 0.36f);
    p.closeSubPath();
    if (pointLeft)
        p.applyTransform(juce::AffineTransform::scale(-1.0f, 1.0f, a.getCentreX(), y));
    return p;
}

} // namespace evolutionvox::ui::theme
