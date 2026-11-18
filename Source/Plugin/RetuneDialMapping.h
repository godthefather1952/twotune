#pragma once
#include <algorithm>
#include <cmath>

namespace evolutionvox::ui {

// One source of truth for the pointer, arc, readout and existing "retune" parameter.
// Angles are screen angles in degrees: 0 = 3 o'clock, increasing clockwise (screen Y points down).
// The usable travel is exactly 270 degrees:
//   0%   -> 90 deg  (6 o'clock, straight down)     Natural
//   50%  -> 225 deg (10:30, upper-left diagonal)
//   100% -> 360 deg (3 o'clock, horizontal right)   Hard Tune: Zeus strikes straight forward
// The inactive 90-degree gap is the lower-right quadrant (3 o'clock -> 6 o'clock).
struct RetuneDialMapping {
    static constexpr float maxRetuneMs = 250.0f;
    static constexpr float startDegrees = 90.0f;
    static constexpr float sweepDegrees = 270.0f;
    static constexpr float pi = 3.14159265358979323846f;

    static float clampIntensity(float percent) noexcept {
        return std::clamp(percent, 0.0f, 100.0f);
    }

    // Perceptually useful cubic taper: 0%=250ms, 50%=31.25ms,
    // 75%=3.90625ms, 100%=0ms. Does not change the audio engine parameter.
    static float retuneMsFromIntensity(float percent) noexcept {
        const float remaining = 1.0f - clampIntensity(percent) * 0.01f;
        return maxRetuneMs * remaining * remaining * remaining;
    }

    static float intensityFromRetuneMs(float milliseconds) noexcept {
        const float normal = std::clamp(milliseconds, 0.0f, maxRetuneMs) / maxRetuneMs;
        return clampIntensity(100.0f * (1.0f - std::cbrt(normal)));
    }

    static float pointerDegrees(float percent) noexcept {
        return startDegrees + sweepDegrees * clampIntensity(percent) * 0.01f;
    }

    // x/y are mouse coordinates relative to dial center (screen Y points down).
    static float intensityFromPointer(float x, float y) noexcept {
        const float a = std::atan2(y, x) * (180.0f / pi);              // -180..180
        const float rel = std::fmod(a - startDegrees + 720.0f, 360.0f); // 0..360 from the 0% end
        if (rel <= sweepDegrees)
            return clampIntensity(rel * 100.0f / sweepDegrees);
        // Inside the inactive gap: snap to the nearer end.
        return rel < sweepDegrees + (360.0f - sweepDegrees) * 0.5f ? 100.0f : 0.0f;
    }
};
} // namespace evolutionvox::ui
