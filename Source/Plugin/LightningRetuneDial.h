#pragma once
// Design V1 master control: Zeus + lightning-bolt pointer + 270-degree illuminated gold meter.
//
// * Driven only by the existing "retune" parameter (0..250 ms) through RetuneDialMapping, so DAW
//   automation, undo and project recall keep working. No new parameter, no percentage text.
// * The bolt IS the pointer: ZeusRig computes a transform that pins its grip to the current pose's
//   fist and its tip exactly onto the end of the gold fill.
// * Performance: PNGs are decoded lazily (shared ImageCache, one decode per process), pre-scaled
//   once per display scale, and drawn as 1:1 blits. The static track is cached. The component only
//   repaints when the parameter value/power state changes, and then only the dirty region.
//   Nothing here runs on the audio thread.
#include "Theme.h"
#include "ZeusRig.h"
#include <array>

class LightningRetuneDial final : public juce::Component,
                                  public juce::SettableTooltipClient,
                                  private juce::Timer {
public:
    explicit LightningRetuneDial(juce::AudioProcessorValueTreeState& state);
    ~LightningRetuneDial() override;

    // Canvas-space area this component shows; the editor sizes the component to this aspect.
    static evolutionvox::ui::zeus::Bounds sceneCanvasBounds();
    // Component-space point of a canvas coordinate, for labels placed by the editor.
    juce::Point<float> canvasToLocal(float x, float y) const noexcept;

    float getIntensity() const noexcept { return intensity; }
    int getPoseIndex() const noexcept { return poseIndex; }
    // For tests/screenshots: read parameters now instead of waiting for the poll timer.
    void syncFromParameters();
    // Repaint instrumentation (perf measurements).
    int getRepaintRequestCount() const noexcept { return repaintRequests; }

    void paint(juce::Graphics&) override;
    void resized() override;
    bool hitTest(int x, int y) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    // Sends a new intensity to the host. Callers outside a drag should wrap it in a gesture.
    void setIntensityFromUser(float newIntensity);
    void beginGesture();
    void endGesture();

private:
    struct Layer { juce::Image source, scaled; };
    struct PoseArt { Layer base, arm, bolt; bool loaded = false; };

    juce::RangedAudioParameter* retuneParameter = nullptr;
    std::atomic<float>* retuneValue = nullptr;
    std::atomic<float>* powerValue = nullptr;

    float intensity = 0.0f;
    float lastRetuneMs = -1.0f;
    int poseIndex = 0;
    bool powerOn = true;
    std::array<PoseArt, evolutionvox::ui::zeus::numPoses> art;
    int nextToPrefetch = 0;

    float sceneScale = 1.0f;          // canvas px -> component px
    float cachedPhysicalScale = 0.0f; // component px -> device px used by the scaled caches
    juce::Image trackCache;           // inactive meter, ticks and end labels (device resolution)

    enum class DragMode { none, absolute, relative };
    DragMode dragMode = DragMode::none;
    float dragStartIntensity = 0.0f;
    float dragStartY = 0.0f;
    bool inGesture = false;
    int repaintRequests = 0;

    void timerCallback() override;
    void ensureLoaded(int pose);
    void ensureScaled(int pose, float physicalScale);
    void rebuildTrackCache(float physicalScale);
    void applyState(float newIntensity, bool newPower, bool force);
    juce::Rectangle<float> dirtyAreaFor(float value, int pose) const;
    juce::AffineTransform canvasToLocalTransform() const noexcept;
    juce::Point<float> dialCentreLocal() const noexcept;
    float dialRadiusLocal() const noexcept;
    float meterThickness() const noexcept { return dialRadiusLocal() * 0.052f; }
    void drawLitMeter(juce::Graphics&, float value) const;
    float intensityFromLocalAngle(juce::Point<float>) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LightningRetuneDial)
};
