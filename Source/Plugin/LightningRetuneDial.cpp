#include "LightningRetuneDial.h"
#include "BinaryData.h"

namespace {
namespace z = evolutionvox::ui::zeus;
using Mapping = evolutionvox::ui::RetuneDialMapping;
namespace theme = evolutionvox::ui::theme;

constexpr float kArcMarginCanvas = 48.0f;      // room for meter glow and the tip spark around the arc
constexpr int kTicks = 54;

// Lightning colours (outer amber edge -> gold body -> pale gold -> white-hot core).
const juce::Colour boltEdge  { 0xffa85a0a };
const juce::Colour boltOuter { 0xffc8761a };
const juce::Colour boltBody  { 0xfff0aa28 };
const juce::Colour boltInner { 0xffffdc6c };
const juce::Colour boltCore  { 0xfffff8e0 };

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
    double getCurrentValue() const override { return dial.parameterIntensity(); }
    void setValue(double v) override {
        dial.beginGesture();
        dial.setIntensityFromUser(static_cast<float>(v));
        dial.endGesture();
    }
    juce::String getCurrentValueAsString() const override {
        // Accessibility/automation readout may be numeric (ms); the visual knob shows no number.
        return juce::String(Mapping::retuneMsFromIntensity(dial.parameterIntensity()), 1) + " ms retune speed";
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
    setDescription("Zeus's lightning sets the retune speed: Natural (bottom) to Hard Tune (right).");
    setTooltip("Drag the lightning around the gold arc (or drag Zeus up/down). Double-click to reset.");
    syncFromParameters();
    ensureLoaded(poseIndex);   // the visible pose is ready before the first paint
    setFrameRate(idleFrameRateHz);
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
    for (auto& a : art) a.base.scaled = a.arm.scaled = {};
}

// ---------------------------------------------------------------------------------------------
// Assets

void LightningRetuneDial::ensureLoaded(int pose) {
    auto& a = art[static_cast<size_t>(pose)];
    if (a.loaded) return;
    const auto n = juce::String(pose + 1).paddedLeft('0', 2);
    a.base.source = loadResource("pose_" + n + "_base_png");
    a.arm.source = loadResource("pose_" + n + "_arm_png");
    a.loaded = true;
}

void LightningRetuneDial::ensureScaled(int pose, float physicalScale) {
    ensureLoaded(pose);
    const float k = sceneScale * physicalScale;
    for (auto* layer : { &art[static_cast<size_t>(pose)].base, &art[static_cast<size_t>(pose)].arm }) {
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
// Painting. Every layer depends only on the current state (never on what was drawn before or on
// where a repaint region starts), so a partial repaint is identical to a full one.

void LightningRetuneDial::drawLitMeter(juce::Graphics& g, float value) const {
    const auto c = dialCentreLocal();
    const float r = dialRadiusLocal(), t = meterThickness();
    const auto endPoint = z::meterPoint(value);
    const auto tip = canvasToLocal(endPoint.x, endPoint.y);

    if (value > 0.05f) {
        juce::Path lit;
        lit.addCentredArc(c.x, c.y, r, r, 0.0f, juceArcAngle(0.0f), juceArcAngle(value), true);
        const auto glowColour = powerOn ? theme::gold : theme::brass;
        g.setColour(glowColour.withAlpha(powerOn ? 0.16f : 0.08f));
        g.strokePath(lit, juce::PathStrokeType(t * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(glowColour.withAlpha(powerOn ? 0.26f : 0.12f));
        g.strokePath(lit, juce::PathStrokeType(t * 1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (powerOn) {
            // Fixed gradient across the whole dial (deep gold lower-left -> bright upper-right). It
            // must NOT depend on the tip position, or regions outside a partial repaint go stale.
            g.setGradientFill(juce::ColourGradient(theme::goldDeep, c.x - r, c.y + r, theme::goldBright, c.x + r, c.y - r, false));
        } else {
            g.setColour(theme::champagne.darker(0.15f));
        }
        g.strokePath(lit, juce::PathStrokeType(t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colours::white.withAlpha(powerOn ? 0.55f : 0.35f));
        g.strokePath(lit, juce::PathStrokeType(t * 0.26f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                     juce::AffineTransform::translation(0.0f, -t * 0.16f));

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

void LightningRetuneDial::drawBolt(juce::Graphics& g) const {
    const auto geometry = z::boltGeometry(poseIndex, intensity);
    const auto toLocal = canvasToLocalTransform();
    auto shape = [&](float widthFraction) {
        const auto outline = z::boltOutline(geometry, z::boltWidth * widthFraction);
        juce::Path p;
        p.startNewSubPath(outline[0].x, outline[0].y);
        for (size_t i = 1; i < outline.size(); ++i) p.lineTo(outline[i].x, outline[i].y);
        p.closeSubPath();
        p.applyTransform(toLocal);
        return p;
    };
    const float a = powerOn ? 1.0f : 0.42f;
    const auto outer = shape(1.0f);
    const juce::PathStrokeType glowStroke(z::boltGlowWidth * sceneScale, juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded);
    if (powerOn) {
        g.setColour(theme::goldBright.withAlpha(0.16f));
        g.strokePath(outer, glowStroke);
        g.setColour(theme::goldBright.withAlpha(0.20f));
        g.strokePath(outer, juce::PathStrokeType(z::boltGlowWidth * 0.5f * sceneScale, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }
    g.setColour(boltOuter.withMultipliedAlpha(a));
    g.fillPath(outer);
    g.setColour(boltEdge.withMultipliedAlpha(0.7f * a));
    g.strokePath(outer, juce::PathStrokeType(std::max(0.8f, 1.1f * sceneScale), juce::PathStrokeType::mitered));
    g.setColour(boltBody.withMultipliedAlpha(a));
    g.fillPath(shape(0.74f));
    g.setColour(boltInner.withMultipliedAlpha(a));
    g.fillPath(shape(0.46f));
    g.setColour(boltCore.withMultipliedAlpha(a));
    g.fillPath(shape(0.19f));
}

void LightningRetuneDial::paint(juce::Graphics& g) {
    if (getWidth() <= 0 || getHeight() <= 0) return;
    const float physical = std::max(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (!juce::approximatelyEqual(physical, cachedPhysicalScale)) {
        cachedPhysicalScale = physical;
        trackCache = {};
        for (auto& a : art) a.base.scaled = a.arm.scaled = {};
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
    auto blit = [&](const juce::Image& img, const z::Rect& r) {
        if (!img.isValid()) return;
        const auto p = canvasToLocal(static_cast<float>(r.x), static_cast<float>(r.y));
        const float x = std::round(p.x * physical) / physical, y = std::round(p.y * physical) / physical;
        g.setOpacity(alpha);
        g.drawImageTransformed(img, toDevice.translated(x, y));
        g.setOpacity(1.0f);
    };

    blit(a.base.scaled, pose.base);
    drawBolt(g);                    // vector lightning: crisp at any angle, length and scale
    blit(a.arm.scaled, pose.arm);   // fist over the bolt's grip
}

// ---------------------------------------------------------------------------------------------
// Dirty regions

juce::Rectangle<float> LightningRetuneDial::boltAreaFor(float value, int pose) const {
    const auto outline = z::boltOutline(z::boltGeometry(pose, value), z::boltWidth);
    std::array<juce::Point<float>, z::boltOutlinePoints> local;
    for (size_t i = 0; i < outline.size(); ++i) local[i] = canvasToLocal(outline[i].x, outline[i].y);
    return juce::Rectangle<float>::findAreaContainingPoints(local.data(), static_cast<int>(local.size()))
        .expanded(z::boltGlowWidth * 0.5f * sceneScale + 2.0f);
}

juce::Rectangle<float> LightningRetuneDial::meterAreaBetween(float a, float b) const {
    constexpr int samples = 24;
    std::array<juce::Point<float>, samples + 1> pts;
    for (int i = 0; i <= samples; ++i) {
        const auto p = z::meterPoint(a + (b - a) * static_cast<float>(i) / samples);
        pts[static_cast<size_t>(i)] = canvasToLocal(p.x, p.y);
    }
    // glow (1.3 t), spark (1.7 t), lit ticks (2.5 t inward) around the changed segment
    return juce::Rectangle<float>::findAreaContainingPoints(pts.data(), static_cast<int>(pts.size()))
        .expanded(meterThickness() * 2.8f + 2.0f);
}

void LightningRetuneDial::applyState(float newIntensity, bool newPower, bool force) {
    newIntensity = Mapping::clampIntensity(newIntensity);
    const int newPose = z::poseForIntensity(newIntensity, poseIndex);
    const bool full = force || forceFullRepaint || newPower != powerOn || newPose != poseIndex;
    if (!full && std::abs(newIntensity - intensity) < 1.0e-4f) return;

    juce::Rectangle<float> dirty;
    if (!full)
        dirty = boltAreaFor(intensity, poseIndex)
                    .getUnion(boltAreaFor(newIntensity, newPose))
                    .getUnion(meterAreaBetween(intensity, newIntensity));
    intensity = newIntensity;
    poseIndex = newPose;
    powerOn = newPower;
    ++repaintRequests;
    lastDirtyArea = full ? getLocalBounds() : dirty.getSmallestIntegerContainer().getIntersection(getLocalBounds());
    repaint(lastDirtyArea);
}

// ---------------------------------------------------------------------------------------------
// Frame updates: every input only changes the parameter; this is the one place the picture
// follows it, at most activeFrameRateHz times per second.

float LightningRetuneDial::parameterIntensity() const noexcept {
    return Mapping::intensityFromRetuneMs(retuneValue->load(std::memory_order_relaxed));
}

void LightningRetuneDial::syncFromParameters() {
    lastRetuneMs = retuneValue->load(std::memory_order_relaxed);
    const bool power = powerValue->load(std::memory_order_relaxed) >= 0.5f;
    applyState(Mapping::intensityFromRetuneMs(lastRetuneMs), power, true);
}

bool LightningRetuneDial::pollParametersNow() {
    const float ms = retuneValue->load(std::memory_order_relaxed);
    const bool power = powerValue->load(std::memory_order_relaxed) >= 0.5f;
    if (std::abs(ms - lastRetuneMs) <= 1.0e-5f && power == powerOn)
        return false;
    lastRetuneMs = ms;
    applyState(Mapping::intensityFromRetuneMs(ms), power, false);
    return true;
}

void LightningRetuneDial::setFrameRate(int hz) {
    if (hz == frameRateHz) return;
    frameRateHz = hz;
    startTimerHz(hz);
}

void LightningRetuneDial::markActive() {
    lastActivitySeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    setFrameRate(activeFrameRateHz);
}

void LightningRetuneDial::timerCallback() {
    if (pollParametersNow()) {
        markActive();   // automation or user input is moving: switch to smooth frame rate
        return;
    }
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (frameRateHz == activeFrameRateHz && now - lastActivitySeconds > settleSeconds)
        setFrameRate(idleFrameRateHz);
    // While idle, spread decoding/scaling of the other poses over ticks so a sweep never stalls.
    if (frameRateHz == idleFrameRateHz && nextToPrefetch < z::numPoses) {
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
    const float ms = Mapping::retuneMsFromIntensity(Mapping::clampIntensity(newIntensity));
    retuneParameter->setValueNotifyingHost(retuneParameter->convertTo0to1(ms));
    markActive();   // the next frame draws it, exactly as for automation
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
        dragStartIntensity = parameterIntensity();
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
    markActive();
}

void LightningRetuneDial::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    const float delta = (wheel.isReversed ? -wheel.deltaY : wheel.deltaY) * 12.0f;
    if (std::abs(delta) < 1.0e-4f) return;
    beginGesture();
    // Accumulate from the parameter, not the drawn picture, so fast wheel events are never lost.
    setIntensityFromUser(parameterIntensity() + delta);
    endGesture();
}

bool LightningRetuneDial::keyPressed(const juce::KeyPress& key) {
    const float step = key.getModifiers().isShiftDown() ? 0.25f : 1.0f;
    float target = parameterIntensity();
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
