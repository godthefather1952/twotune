#include "LightningRetuneDial.h"
#include "BinaryData.h"

namespace {
namespace z = evolutionvox::ui::zeus;
using Mapping = evolutionvox::ui::RetuneDialMapping;
namespace theme = evolutionvox::ui::theme;

constexpr float kArcMarginCanvas = 48.0f;      // room for meter glow and the tip spark around the arc
constexpr int kTicks = 54;

juce::Image loadResource(const juce::String& name) {
    int size = 0;
    if (const char* data = BinaryData::getNamedResource(name.toRawUTF8(), size))
        return juce::ImageCache::getFromMemory(data, size); // decoded once per process, shared
    jassertfalse;
    return {};
}

// JUCE arc angles are clockwise from 12 o'clock; RetuneDialMapping uses screen angles from 3 o'clock.
float juceArcAngle(float intensity) {
    return juce::degreesToRadians(Mapping::pointerDegrees(intensity) + 90.0f);
}

class DialValueInterface final : public juce::AccessibilityValueInterface {
public:
    explicit DialValueInterface(LightningRetuneDial& d) : dial(d) {}
    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return dial.getIntensity(); }
    void setValue(double v) override {
        dial.beginGesture();
        dial.setIntensityFromUser(static_cast<float>(v));
        dial.endGesture();
    }
    juce::String getCurrentValueAsString() const override {
        // Accessibility/automation readout may be numeric (ms); the visual knob shows no number.
        return juce::String(Mapping::retuneMsFromIntensity(dial.getIntensity()), 1) + " ms retune speed";
    }
    void setValueAsString(const juce::String& text) override { setValue(text.getDoubleValue()); }
    AccessibleValueRange getRange() const override { return { { 0.0, 100.0 }, 1.0 }; }
private:
    LightningRetuneDial& dial;
};
} // namespace

LightningRetuneDial::LightningRetuneDial(juce::AudioProcessorValueTreeState& state)
    : retuneParameter(state.getParameter("retune")),
      retuneValue(state.getRawParameterValue("retune")),
      powerValue(state.getRawParameterValue("power")) {
    jassert(retuneParameter != nullptr && retuneValue != nullptr && powerValue != nullptr);
    setOpaque(false);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTitle("Retune");
    setDescription("Zeus's lightning sets the retune speed: Natural (left) to Hard Tune (right).");
    setTooltip("Drag the lightning around the gold arc (or drag Zeus up/down). Double-click to reset.");
    syncFromParameters();
    ensureLoaded(poseIndex);  // the visible pose is ready before the first paint
    startTimerHz(30);         // UI-thread poll of two atomics; repaint only on change
}

LightningRetuneDial::~LightningRetuneDial() {
    stopTimer();
    endGesture();
}

z::Bounds LightningRetuneDial::sceneCanvasBounds() { return z::sceneBounds(kArcMarginCanvas); }

juce::AffineTransform LightningRetuneDial::canvasToLocalTransform() const noexcept {
    const auto b = sceneCanvasBounds();
    return juce::AffineTransform::translation(-b.x0, -b.y0).scaled(sceneScale);
}

juce::Point<float> LightningRetuneDial::canvasToLocal(float x, float y) const noexcept {
    const auto b = sceneCanvasBounds();
    return { (x - b.x0) * sceneScale, (y - b.y0) * sceneScale };
}

juce::Point<float> LightningRetuneDial::dialCentreLocal() const noexcept {
    return canvasToLocal(z::dialCentreX, z::dialCentreY);
}

float LightningRetuneDial::dialRadiusLocal() const noexcept { return z::dialRadius * sceneScale; }

void LightningRetuneDial::resized() {
    const auto b = sceneCanvasBounds();
    sceneScale = static_cast<float>(getWidth()) / (b.x1 - b.x0);
    cachedPhysicalScale = 0.0f;   // force rescale of cached layers on next paint
    trackCache = {};
    for (auto& a : art) a.base.scaled = a.arm.scaled = a.bolt.scaled = {};
}

// ---------------------------------------------------------------------------------------------
// Assets

void LightningRetuneDial::ensureLoaded(int pose) {
    auto& a = art[static_cast<size_t>(pose)];
    if (a.loaded) return;
    const auto n = juce::String(pose + 1).paddedLeft('0', 2);
    a.base.source = loadResource("pose_" + n + "_base_png");
    a.arm.source = loadResource("pose_" + n + "_arm_png");
    a.bolt.source = loadResource("bolt_" + n + "_png");
    a.loaded = true;
}

void LightningRetuneDial::ensureScaled(int pose, float physicalScale) {
    ensureLoaded(pose);
    const float k = sceneScale * physicalScale;
    for (auto* layer : { &art[static_cast<size_t>(pose)].base, &art[static_cast<size_t>(pose)].arm,
                         &art[static_cast<size_t>(pose)].bolt }) {
        if (layer->scaled.isValid() || !layer->source.isValid()) continue;
        const int w = std::max(1, juce::roundToInt(static_cast<float>(layer->source.getWidth()) * k));
        const int h = std::max(1, juce::roundToInt(static_cast<float>(layer->source.getHeight()) * k));
        layer->scaled = layer->source.rescaled(w, h, juce::Graphics::highResamplingQuality);
    }
}

void LightningRetuneDial::rebuildTrackCache(float physicalScale) {
    const int w = juce::roundToInt(static_cast<float>(getWidth()) * physicalScale);
    const int h = juce::roundToInt(static_cast<float>(getHeight()) * physicalScale);
    if (w <= 0 || h <= 0) { trackCache = {}; return; }
    trackCache = juce::Image(juce::Image::ARGB, w, h, true);
    juce::Graphics g(trackCache);
    g.addTransform(juce::AffineTransform::scale(physicalScale));

    const auto c = dialCentreLocal();
    const float r = dialRadiusLocal(), t = meterThickness();
    juce::Path arc;
    arc.addCentredArc(c.x, c.y, r, r, 0.0f, juceArcAngle(0.0f), juceArcAngle(100.0f), true);
    const juce::PathStrokeType round(t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    // soft drop shadow, champagne body, bright inner rim
    g.setColour(theme::brass.withAlpha(0.10f));
    g.strokePath(arc, juce::PathStrokeType(t * 1.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                 juce::AffineTransform::translation(0.0f, t * 0.25f));
    g.setColour(theme::track);
    g.strokePath(arc, round);
    g.setColour(juce::Colours::white.withAlpha(0.75f));
    g.strokePath(arc, juce::PathStrokeType(t * 0.28f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                 juce::AffineTransform::translation(0.0f, -t * 0.18f));
    g.setColour(theme::brass.withAlpha(0.35f));
    juce::Path outline;
    outline.addCentredArc(c.x, c.y, r + t * 0.5f, r + t * 0.5f, 0.0f, juceArcAngle(0.0f), juceArcAngle(100.0f), true);
    g.strokePath(outline, juce::PathStrokeType(0.8f));

    // inactive tick marks inside the meter
    for (int i = 0; i <= kTicks; ++i) {
        const float a = juceArcAngle(100.0f * static_cast<float>(i) / kTicks);
        const bool major = i % 6 == 0;
        const float r0 = r - t * 1.25f, r1 = r - t * (major ? 2.5f : 1.9f);
        g.setColour(theme::brass.withAlpha(major ? 0.38f : 0.22f));
        g.drawLine(c.x + r0 * std::sin(a), c.y - r0 * std::cos(a), c.x + r1 * std::sin(a), c.y - r1 * std::cos(a),
                   major ? 1.6f : 1.0f);
    }

    // End labels (words, never numbers). Both sit inside the meter's empty lower-right quadrant:
    // NATURAL just right of the 6 o'clock start, HARD TUNE just under the 3 o'clock end.
    const auto f = theme::font(std::max(9.0f, r * 0.043f), true);
    const float lineH = f.getHeight() * 1.3f, spacing = f.getHeight() * 0.22f;
    g.setColour(theme::taupe);
    const auto start = z::meterPoint(0.0f), end = z::meterPoint(100.0f);
    const auto ls = canvasToLocal(start.x, start.y), le = canvasToLocal(end.x, end.y);
    theme::drawSpacedText(g, "NATURAL", { ls.x + t * 1.2f, ls.y - lineH * 0.5f, r * 0.6f, lineH },
                          f, spacing, juce::Justification::left);
    theme::drawSpacedText(g, "HARD TUNE", { le.x + t * 0.5f - r * 0.6f, le.y + t * 1.3f, r * 0.6f, lineH },
                          f, spacing, juce::Justification::right);
}

// ---------------------------------------------------------------------------------------------
// Painting

void LightningRetuneDial::drawLitMeter(juce::Graphics& g, float value) const {
    const auto c = dialCentreLocal();
    const float r = dialRadiusLocal(), t = meterThickness();
    const auto endPoint = z::meterPoint(value);
    const auto tip = canvasToLocal(endPoint.x, endPoint.y);
    const auto startPoint = z::meterPoint(0.0f);
    const auto start = canvasToLocal(startPoint.x, startPoint.y);

    if (value > 0.05f) {
        juce::Path lit;
        lit.addCentredArc(c.x, c.y, r, r, 0.0f, juceArcAngle(0.0f), juceArcAngle(value), true);
        const auto glowColour = powerOn ? theme::gold : theme::brass;
        g.setColour(glowColour.withAlpha(powerOn ? 0.16f : 0.08f));
        g.strokePath(lit, juce::PathStrokeType(t * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(glowColour.withAlpha(powerOn ? 0.26f : 0.12f));
        g.strokePath(lit, juce::PathStrokeType(t * 1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (powerOn)
            g.setGradientFill(juce::ColourGradient(theme::goldDeep, start.x, start.y, theme::goldBright, tip.x, tip.y, false));
        else
            g.setColour(theme::champagne.darker(0.15f));
        g.strokePath(lit, juce::PathStrokeType(t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colours::white.withAlpha(powerOn ? 0.55f : 0.35f));
        g.strokePath(lit, juce::PathStrokeType(t * 0.26f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                     juce::AffineTransform::translation(0.0f, -t * 0.16f));

        // lit ticks
        g.setColour(powerOn ? theme::goldDeep : theme::brass.withAlpha(0.5f));
        for (int i = 0; i <= kTicks; ++i) {
            const float v = 100.0f * static_cast<float>(i) / kTicks;
            if (v > value) break;
            const float a = juceArcAngle(v);
            const bool major = i % 6 == 0;
            const float r0 = r - t * 1.25f, r1 = r - t * (major ? 2.5f : 1.9f);
            g.drawLine(c.x + r0 * std::sin(a), c.y - r0 * std::cos(a), c.x + r1 * std::sin(a), c.y - r1 * std::cos(a),
                       major ? 1.8f : 1.1f);
        }
    }

    if (powerOn) {
        // spark where the bolt tip touches the meter
        const float s = t * 1.7f;
        juce::ColourGradient spark(juce::Colours::white, tip.x, tip.y, theme::goldBright.withAlpha(0.0f), tip.x + s, tip.y, true);
        spark.addColour(0.35, theme::goldBright.withAlpha(0.85f));
        g.setGradientFill(spark);
        g.fillEllipse(tip.x - s, tip.y - s, s * 2.0f, s * 2.0f);
    }
}

void LightningRetuneDial::paint(juce::Graphics& g) {
    if (getWidth() <= 0 || getHeight() <= 0) return;
    const float physical = std::max(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (!juce::approximatelyEqual(physical, cachedPhysicalScale)) {
        cachedPhysicalScale = physical;
        trackCache = {};
        for (auto& a : art) a.base.scaled = a.arm.scaled = a.bolt.scaled = {};
    }
    if (!trackCache.isValid()) rebuildTrackCache(physical);

    const auto toDevice = juce::AffineTransform::scale(1.0f / physical);
    g.drawImageTransformed(trackCache, toDevice);
    drawLitMeter(g, intensity);

    ensureScaled(poseIndex, physical);
    const auto& pose = z::poses[static_cast<size_t>(poseIndex)];
    const auto& a = art[static_cast<size_t>(poseIndex)];
    const float alpha = powerOn ? 1.0f : 0.55f;

    // Pre-scaled sprites drawn on exact device pixels: a straight blit, no per-frame resampling.
    auto blit = [&](const juce::Image& img, const z::Rect& r, float opacity) {
        if (!img.isValid()) return;
        const auto p = canvasToLocal(static_cast<float>(r.x), static_cast<float>(r.y));
        const float x = std::round(p.x * physical) / physical, y = std::round(p.y * physical) / physical;
        g.setOpacity(opacity);
        g.drawImageTransformed(img, toDevice.translated(x, y));
    };

    blit(a.base.scaled, pose.base, alpha);

    if (a.bolt.scaled.isValid()) {
        // Only the bolt is resampled per frame (small image, medium quality).
        const auto bt = z::boltTransform(poseIndex, intensity);
        const float kb = static_cast<float>(a.bolt.scaled.getWidth()) / static_cast<float>(a.bolt.source.getWidth());
        const auto boltToLocal = juce::AffineTransform::scale(1.0f / kb)
                                     .followedBy(juce::AffineTransform(bt.m00, bt.m01, bt.dx, bt.m10, bt.m11, bt.dy))
                                     .followedBy(canvasToLocalTransform());
        g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
        g.setOpacity(powerOn ? 1.0f : 0.4f);
        g.drawImageTransformed(a.bolt.scaled, boltToLocal);
    }

    blit(a.arm.scaled, pose.arm, alpha);  // fist over the bolt's grip
    g.setOpacity(1.0f);
}

juce::Rectangle<float> LightningRetuneDial::dirtyAreaFor(float value, int pose) const {
    const auto bt = z::boltTransform(pose, value);
    const auto& r = z::poses[static_cast<size_t>(pose)].bolt;
    juce::Rectangle<float> area;
    for (const auto corner : { z::Point{ 0.0f, 0.0f }, z::Point{ static_cast<float>(r.w), 0.0f },
                               z::Point{ 0.0f, static_cast<float>(r.h) },
                               z::Point{ static_cast<float>(r.w), static_cast<float>(r.h) } }) {
        const auto p = bt.apply(corner);
        const auto lp = canvasToLocal(p.x, p.y);
        area = area.isEmpty() ? juce::Rectangle<float>(lp, lp) : area.getUnion(juce::Rectangle<float>(lp, lp));
    }
    return area.expanded(4.0f);
}

void LightningRetuneDial::applyState(float newIntensity, bool newPower, bool force) {
    newIntensity = Mapping::clampIntensity(newIntensity);
    const int newPose = z::poseForIntensity(newIntensity, poseIndex);
    const bool full = force || newPower != powerOn || newPose != poseIndex;
    if (!full && std::abs(newIntensity - intensity) < 1.0e-4f) return;

    juce::Rectangle<float> dirty;
    if (!full) {
        dirty = dirtyAreaFor(intensity, poseIndex).getUnion(dirtyAreaFor(newIntensity, newPose));
        // the meter segment between the old and new value (+ glow, ticks and spark)
        const float lo = std::min(intensity, newIntensity), hi = std::max(intensity, newIntensity);
        for (int i = 0; i <= 12; ++i) {
            const auto p = z::meterPoint(lo + (hi - lo) * static_cast<float>(i) / 12.0f);
            const auto lp = canvasToLocal(p.x, p.y);
            dirty = dirty.getUnion(juce::Rectangle<float>(lp, lp).expanded(meterThickness() * 2.8f));
        }
    }
    intensity = newIntensity;
    poseIndex = newPose;
    powerOn = newPower;
    ++repaintRequests;
    if (full) repaint();
    else repaint(dirty.getSmallestIntegerContainer());
}

void LightningRetuneDial::syncFromParameters() {
    lastRetuneMs = retuneValue->load(std::memory_order_relaxed);
    const bool power = powerValue->load(std::memory_order_relaxed) >= 0.5f;
    applyState(Mapping::intensityFromRetuneMs(lastRetuneMs), power, true);
}

void LightningRetuneDial::timerCallback() {
    const float ms = retuneValue->load(std::memory_order_relaxed);
    const bool power = powerValue->load(std::memory_order_relaxed) >= 0.5f;
    if (std::abs(ms - lastRetuneMs) > 1.0e-4f || power != powerOn) {
        lastRetuneMs = ms;
        applyState(Mapping::intensityFromRetuneMs(ms), power, false);
    }
    // Spread decoding/scaling of the other poses over idle ticks so the first sweep never stalls.
    if (nextToPrefetch < z::numPoses) {
        ensureLoaded(nextToPrefetch);
        if (cachedPhysicalScale > 0.0f) ensureScaled(nextToPrefetch, cachedPhysicalScale);
        ++nextToPrefetch;
    }
}

// ---------------------------------------------------------------------------------------------
// Interaction

float LightningRetuneDial::intensityFromLocalAngle(juce::Point<float> p) const noexcept {
    const auto c = dialCentreLocal();
    return Mapping::intensityFromPointer(p.x - c.x, p.y - c.y);
}

bool LightningRetuneDial::hitTest(int x, int y) {
    const auto c = dialCentreLocal();
    const float r = dialRadiusLocal();
    const auto d = juce::Point<float>(static_cast<float>(x), static_cast<float>(y)).getDistanceFrom(c);
    return d <= r + meterThickness() * 2.5f;
}

void LightningRetuneDial::beginGesture() {
    if (!inGesture && retuneParameter != nullptr) {
        retuneParameter->beginChangeGesture();
        inGesture = true;
    }
}

void LightningRetuneDial::endGesture() {
    if (inGesture && retuneParameter != nullptr) {
        retuneParameter->endChangeGesture();
        inGesture = false;
    }
}

void LightningRetuneDial::setIntensityFromUser(float newIntensity) {
    if (retuneParameter == nullptr) return;
    newIntensity = Mapping::clampIntensity(newIntensity);
    const float ms = Mapping::retuneMsFromIntensity(newIntensity);
    retuneParameter->setValueNotifyingHost(retuneParameter->convertTo0to1(ms));
    lastRetuneMs = retuneValue->load(std::memory_order_relaxed);
    applyState(Mapping::intensityFromRetuneMs(lastRetuneMs), powerOn, false);
}

void LightningRetuneDial::mouseDown(const juce::MouseEvent& e) {
    const auto c = dialCentreLocal();
    const float r = dialRadiusLocal();
    beginGesture();
    if (std::abs(e.position.getDistanceFrom(c) - r) < r * 0.17f) {
        dragMode = DragMode::absolute;            // grabbed the meter: jump to that point
        setIntensityFromUser(intensityFromLocalAngle(e.position));
    } else {
        dragMode = DragMode::relative;            // grabbed Zeus: drag up/down like a knob
        dragStartIntensity = intensity;
        dragStartY = e.position.y;
    }
}

void LightningRetuneDial::mouseDrag(const juce::MouseEvent& e) {
    if (dragMode == DragMode::absolute) {
        setIntensityFromUser(intensityFromLocalAngle(e.position));
    } else if (dragMode == DragMode::relative) {
        const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
        const float delta = (dragStartY - e.position.y) / (dialRadiusLocal() * 1.6f) * 100.0f * fine;
        setIntensityFromUser(dragStartIntensity + delta);
    }
}

void LightningRetuneDial::mouseUp(const juce::MouseEvent&) {
    dragMode = DragMode::none;
    endGesture();
}

void LightningRetuneDial::mouseDoubleClick(const juce::MouseEvent&) {
    if (retuneParameter == nullptr) return;
    beginGesture();
    retuneParameter->setValueNotifyingHost(retuneParameter->getDefaultValue());
    endGesture();
    syncFromParameters();
}

void LightningRetuneDial::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    const float delta = (wheel.isReversed ? -wheel.deltaY : wheel.deltaY) * 12.0f;
    if (std::abs(delta) < 1.0e-4f) return;
    beginGesture();
    setIntensityFromUser(intensity + delta);
    endGesture();
}

bool LightningRetuneDial::keyPressed(const juce::KeyPress& key) {
    const float step = key.getModifiers().isShiftDown() ? 0.25f : 1.0f;
    float target = intensity;
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey) target += step;
    else if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey) target -= step;
    else if (code == juce::KeyPress::pageUpKey) target += 10.0f;
    else if (code == juce::KeyPress::pageDownKey) target -= 10.0f;
    else if (code == juce::KeyPress::homeKey) target = 0.0f;
    else if (code == juce::KeyPress::endKey) target = 100.0f;
    else return false;
    beginGesture();
    setIntensityFromUser(target);
    endGesture();
    return true;
}

std::unique_ptr<juce::AccessibilityHandler> LightningRetuneDial::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::slider, juce::AccessibilityActions(),
        juce::AccessibilityHandler::Interfaces { std::make_unique<DialValueInterface>(*this) });
}
