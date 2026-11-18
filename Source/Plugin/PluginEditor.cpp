#include "PluginEditor.h"

namespace theme = evolutionvox::ui::theme;

namespace layout {
// Design-space (1000 x 650) vertical layout of the main screen.
constexpr float dialTop = 92.0f;
constexpr float dialBottom = 548.0f;
constexpr float captionY = 548.0f;     // "RETUNE" caption
constexpr float slotTop = 576.0f;      // reserved secondary-control area starts here
constexpr float slotWidth = 380.0f;
} // namespace layout

namespace {
juce::Rectangle<int> design(float k, float x, float y, float w, float h) {
    return juce::Rectangle<float>(x * k, y * k, w * k, h * k).toNearestInt();
}

// Soft cumulus made of overlapping radial puffs (painted once into the buffered background).
void drawCloud(juce::Graphics& g, float cx, float cy, float width, float alpha) {
    struct Puff { float dx, dy, r; };
    constexpr Puff puffs[] = { { -0.36f, 0.10f, 0.20f }, { -0.16f, -0.06f, 0.26f }, { 0.08f, -0.14f, 0.30f },
                               { 0.32f, 0.02f, 0.22f }, { 0.0f, 0.14f, 0.30f }, { 0.44f, 0.16f, 0.15f },
                               { -0.48f, 0.20f, 0.12f } };
    for (const auto& p : puffs) {
        const float r = p.r * width, x = cx + p.dx * width, y = cy + p.dy * width;
        juce::ColourGradient shade(theme::cloud.withAlpha(alpha), x - r * 0.25f, y - r * 0.35f,
                                   theme::ivoryDeep.withAlpha(0.0f), x + r, y + r, true);
        shade.addColour(0.6, theme::ivoryDeep.withAlpha(alpha * 0.55f));
        g.setGradientFill(shade);
        g.fillEllipse(x - r, y - r, r * 2.0f, r * 2.0f);
    }
}
} // namespace

// ---------------------------------------------------------------------------------------------
void EvolutionVoxAudioProcessorEditor::Background::paint(juce::Graphics& g) {
    const float k = static_cast<float>(getWidth()) / theme::designWidth;
    const auto bounds = getLocalBounds().toFloat();
    g.setGradientFill(juce::ColourGradient(theme::ivory, 0, 0, theme::ivoryDeep, 0, bounds.getBottom(), false));
    g.fillAll();

    // warm light behind Zeus
    if (haloRadius > 0.0f) {
        juce::ColourGradient halo(theme::goldBright.withAlpha(0.30f), haloCentre.x, haloCentre.y,
                                  theme::ivory.withAlpha(0.0f), haloCentre.x + haloRadius, haloCentre.y, true);
        halo.addColour(0.45, theme::champagne.withAlpha(0.22f));
        g.setGradientFill(halo);
        g.fillEllipse(haloCentre.x - haloRadius, haloCentre.y - haloRadius, haloRadius * 2.0f, haloRadius * 2.0f);
    }

    // clouds (kept to the edges so the controls stay high-contrast)
    struct C { float x, y, w, a; };
    constexpr C clouds[] = { { 80, 640, 300, 0.95f }, { 250, 690, 260, 0.85f }, { 930, 630, 320, 0.95f },
                             { 760, 700, 280, 0.85f }, { 40, 470, 200, 0.70f }, { 975, 470, 220, 0.70f },
                             { 150, 190, 230, 0.55f }, { 870, 200, 240, 0.55f }, { 500, 690, 260, 0.6f } };
    for (const auto& c : clouds) drawCloud(g, c.x * k, c.y * k, c.w * k, c.a);

    // logo: EVOLUTION (charcoal) VOX (gold), bold italic, centred
    const auto titleFont = theme::font(52.0f * k, true, true);
    const float evoW = juce::GlyphArrangement::getStringWidth(titleFont, "EVOLUTION");
    const float voxW = juce::GlyphArrangement::getStringWidth(titleFont, "VOX");
    const float titleX = (theme::designWidth * k - (evoW + voxW)) * 0.5f;
    const juce::Rectangle<float> titleRow(titleX, 16.0f * k, evoW + voxW, 60.0f * k);
    g.setFont(titleFont);
    g.setColour(theme::goldDeep.withAlpha(0.18f));
    g.drawText("EVOLUTION", titleRow.withWidth(evoW + 4).translated(0, 2.0f * k), juce::Justification::centredLeft, false);
    g.setColour(theme::charcoal);
    g.drawText("EVOLUTION", titleRow.withWidth(evoW + 4), juce::Justification::centredLeft, false);
    const juce::Rectangle<float> voxRow(titleX + evoW, titleRow.getY(), voxW + 6, titleRow.getHeight());
    g.setGradientFill(juce::ColourGradient(theme::goldBright, 0, voxRow.getY() + 10 * k, theme::goldDeep, 0,
                                           voxRow.getBottom() - 8 * k, false));
    g.drawText("VOX", voxRow, juce::Justification::centredLeft, false);

    // lightning flourishes either side of the logo
    g.setGradientFill(juce::ColourGradient(theme::goldBright, 0, 34 * k, theme::goldDeep, 0, 62 * k, false));
    g.fillPath(theme::flourish({ titleX - 128 * k, 34 * k, 104 * k, 26 * k }, false));
    g.fillPath(theme::flourish({ titleRow.getRight() + 24 * k, 34 * k, 104 * k, 26 * k }, true));

    g.setColour(theme::taupe);
    theme::drawSpacedText(g, "VOCAL TUNING", { 0, 78 * k, theme::designWidth * k, 20 * k },
                          theme::font(13.0f * k, true), 7.0f * k, juce::Justification::centred);

    // selector labels and the master knob caption (a word, never a percentage)
    const auto labelFont = theme::font(15.0f * k, true);
    theme::drawSpacedText(g, "KEY", design(k, 36, 300, 166, 24).toFloat(), labelFont, 4.0f * k, juce::Justification::centred);
    theme::drawSpacedText(g, "SCALE", design(k, 798, 300, 166, 24).toFloat(), labelFont, 4.0f * k, juce::Justification::centred);
    g.setColour(theme::goldDeep);
    theme::drawSpacedText(g, "RETUNE", design(k, 350, layout::captionY, 300, 26).toFloat(), theme::font(18.0f * k, true),
                          6.0f * k, juce::Justification::centred);
    g.setColour(theme::gold.withAlpha(0.7f));
    g.fillRect(design(k, 392, layout::captionY + 13, 26, 1).toFloat().withHeight(std::max(1.0f, k)));
    g.fillRect(design(k, 582, layout::captionY + 13, 26, 1).toFloat().withHeight(std::max(1.0f, k)));
}

// ---------------------------------------------------------------------------------------------
namespace {
void paintSquareButton(juce::Graphics& g, juce::Rectangle<float> r, bool highlighted, bool down, bool lit) {
    const float radius = r.getWidth() * 0.26f;
    g.setColour(theme::goldDeep.withAlpha(0.14f));
    g.fillRoundedRectangle(r.translated(0, r.getHeight() * 0.05f), radius);
    g.setGradientFill(juce::ColourGradient(theme::cloud, r.getX(), r.getY(), theme::ivoryDeep, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, radius);
    if (lit) {
        juce::ColourGradient glow(theme::goldBright.withAlpha(0.35f), r.getCentreX(), r.getCentreY(),
                                  theme::goldBright.withAlpha(0.0f), r.getRight(), r.getCentreY(), true);
        g.setGradientFill(glow);
        g.fillRoundedRectangle(r, radius);
    }
    if (highlighted || down) {
        g.setColour(theme::goldDeep.withAlpha(down ? 0.12f : 0.05f));
        g.fillRoundedRectangle(r, radius);
    }
    g.setColour(theme::gold);
    g.drawRoundedRectangle(r.reduced(0.8f), radius, 1.8f);
    g.setColour(juce::Colours::white.withAlpha(0.8f));
    g.drawRoundedRectangle(r.reduced(3.0f), radius - 2.5f, 0.8f);
}
} // namespace

void EvolutionVoxAudioProcessorEditor::PowerButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto r = getLocalBounds().toFloat().reduced(3.0f);
    const bool on = getToggleState();
    paintSquareButton(g, r, highlighted, down, on);
    const auto icon = theme::powerIcon(r.reduced(r.getWidth() * 0.24f));
    const float stroke = r.getWidth() * 0.075f;
    g.setColour(on ? theme::goldDeep : theme::muted);
    g.strokePath(icon, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void EvolutionVoxAudioProcessorEditor::SettingsButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto r = getLocalBounds().toFloat().reduced(3.0f);
    paintSquareButton(g, r, highlighted, down, false);
    g.setColour(theme::goldDeep);
    g.fillPath(theme::gearIcon(r.reduced(r.getWidth() * 0.23f)));
}

// ---------------------------------------------------------------------------------------------
EvolutionVoxAudioProcessorEditor::EvolutionVoxAudioProcessorEditor(EvolutionVoxAudioProcessor& p)
    : AudioProcessorEditor(&p), dial(p.parameters), settings(p) {
    setLookAndFeel(&lookAndFeel);
    setOpaque(true);

    addAndMakeVisible(background);
    addAndMakeVisible(dial);

    power.setTooltip("Power: turn tuning on/off (latency-compensated bypass)");
    power.setTitle("Power");
    addAndMakeVisible(power);
    powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters, "power", power);

    settingsButton.setTooltip("Settings: tracking, range, flex, humanize, transpose, presets, custom notes");
    settingsButton.setTitle("Settings");
    settingsButton.onClick = [this] { showSettings(!settings.isVisible()); };
    addAndMakeVisible(settingsButton);

    key.addItemList({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    scale.addItemList({ "Chromatic", "Major", "Natural Minor", "Harmonic Minor", "Minor Pentatonic", "Custom" }, 1);
    key.setTitle("Key");
    scale.setTitle("Scale");
    key.setTooltip("Key (root note) the voice is tuned to");
    scale.setTooltip("Scale the voice is tuned to (Custom: choose notes in Settings)");
    addAndMakeVisible(key);
    addAndMakeVisible(scale);
    keyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters, "key", key);
    scaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters, "scale", scale);

    settings.onClose = [this] { showSettings(false); };
    addChildComponent(settings);

    setResizable(true, true);
    setResizeLimits(700, 455, 1500, 975);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio(static_cast<double>(theme::designWidth / theme::designHeight));
    setSize(static_cast<int>(theme::designWidth), static_cast<int>(theme::designHeight));
}

EvolutionVoxAudioProcessorEditor::~EvolutionVoxAudioProcessorEditor() {
    setLookAndFeel(nullptr);
}

void EvolutionVoxAudioProcessorEditor::showSettings(bool shouldShow) {
    settings.setVisible(shouldShow);
    if (shouldShow) settings.toFront(true);
    else if (dial.isShowing()) dial.grabKeyboardFocus();
}

void EvolutionVoxAudioProcessorEditor::resized() {
    const float k = static_cast<float>(getWidth()) / theme::designWidth;
    background.setBounds(getLocalBounds());
    settings.setBounds(getLocalBounds());

    power.setBounds(design(k, 32, 24, 68, 68));
    settingsButton.setBounds(design(k, 900, 24, 68, 68));
    key.setBounds(design(k, 36, 330, 166, 52));
    scale.setBounds(design(k, 798, 330, 166, 52));

    // Zeus scene: fills the space between header and caption, with the dial centre on the
    // window's centre line. Its aspect comes from the rig bounds so nothing is clipped.
    const auto b = LightningRetuneDial::sceneCanvasBounds();
    const float sceneW = b.x1 - b.x0, sceneH = b.y1 - b.y0;
    const float height = (layout::dialBottom - layout::dialTop) * k;
    const float sceneScale = height / sceneH;
    const float width = sceneW * sceneScale;
    const float x = 500.0f * k - (evolutionvox::ui::zeus::dialCentreX - b.x0) * sceneScale;
    dial.setBounds(juce::Rectangle<float>(x, layout::dialTop * k, width, height).toNearestInt());
    // Reserved for a future secondary control directly below the dial. Intentionally empty:
    // nothing is drawn or placed here yet.
    secondarySlot = design(k, 500 - layout::slotWidth * 0.5f, layout::slotTop, layout::slotWidth,
                           theme::designHeight - layout::slotTop - 8);

    const auto centre = dial.getPosition().toFloat() + dial.canvasToLocal(evolutionvox::ui::zeus::dialCentreX,
                                                                          evolutionvox::ui::zeus::dialCentreY);
    background.haloCentre = centre.translated(0.0f, 10.0f * k);
    background.haloRadius = evolutionvox::ui::zeus::dialRadius * sceneScale * 1.05f;
    background.repaint();
}
