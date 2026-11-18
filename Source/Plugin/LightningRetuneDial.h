#pragma once
// Design V1 master control: Zeus + vector lightning-bolt pointer + 270-degree illuminated meter.
//
// * Driven only by the existing "retune" parameter (0..250 ms) through RetuneDialMapping, so DAW
//   automation, undo and project recall keep working. No new parameter, no percentage text.
// * The bolt IS the pointer: a vector shape from the current pose's fist to exactly the end of the
//   gold fill (ZeusRig.h). It is never a stretched bitmap, so it stays crisp at any angle/scale.
// * One update path for every input: mouse click/drag, wheel, keys, accessibility and DAW
//   automation only set the parameter; the frame timer reads it and updates the picture. While
//   something is moving the timer runs at 60 Hz (frame-limited); otherwise it idles at 15 Hz,
//   polling two atomics and drawing nothing.
// * Repaints cover exactly what changed (old + new bolt, changed meter segment); a pose or power
//   change repaints the dial. Every layer is position-independent, so a partial repaint is
//   pixel-identical to a full one (verified by EvolutionVoxPluginTests).
// * PNGs decode lazily (shared ImageCache, once per process), are pre-scaled once per display
//   scale and blitted 1:1; the static track is cached. Nothing here runs on the audio thread.
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
    // Reads the parameters and repaints the whole dial (editor open, tests, screenshots).
    void syncFromParameters();
    // The frame update: reads the parameters and repaints only what changed. Returns true if the
    // picture changed. Called by the frame timer; public so tests can drive it deterministically.
    bool pollParametersNow();

    // Diagnostics (tests / perf measurements).
    int getRepaintRequestCount() const noexcept { return repaintRequests; }
    juce::Rectangle<int> getLastDirtyArea() const noexcept { return lastDirtyArea; }
    void setForceFullRepaint(bool shouldForce) noexcept { forceFullRepaint = shouldForce; }
    int getCurrentFrameRateHz() const noexcept { return frameRateHz; }

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

    // Sends a new intensity to the host (the picture follows on the next frame).
    // Callers outside a drag should wrap it in begin/endGesture.
    void setIntensityFromUser(float newIntensity);
    void beginGesture();
    void endGesture();
    // Intensity of the current parameter value (may be ahead of the drawn picture by < 1 frame).
    float parameterIntensity() const noexcept;

private:
    struct Layer { juce::Image source, scaled; };
    struct PoseArt { Layer base, arm; bool loaded = false; };

    static constexpr int activeFrameRateHz = 60;
    static constexpr int idleFrameRateHz = 15;
    static constexpr double settleSeconds = 0.4;

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

    int frameRateHz = 0;
    double lastActivitySeconds = 0.0;
    int repaintRequests = 0;
    juce::Rectangle<int> lastDirtyArea;
    bool forceFullRepaint = false;

    void timerCallback() override;
    void setFrameRate(int hz);
    void markActive();
    void ensureLoaded(int pose);
    void ensureScaled(int pose, float physicalScale);
    void rebuildTrackCache(float physicalScale);
    void applyState(float newIntensity, bool newPower, bool force);
    juce::Rectangle<float> boltAreaFor(float value, int pose) const;
    juce::Rectangle<float> meterAreaBetween(float a, float b) const;
    juce::AffineTransform canvasToLocalTransform() const noexcept;
    juce::Point<float> dialCentreLocal() const noexcept;
    float dialRadiusLocal() const noexcept;
    float meterThickness() const noexcept { return dialRadiusLocal() * 0.052f; }
    void drawLitMeter(juce::Graphics&, float value) const;
    void drawBolt(juce::Graphics&) const;
    float intensityFromLocalAngle(juce::Point<float>) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LightningRetuneDial)
};
