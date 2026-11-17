#pragma once
#include <algorithm>
#include <cmath>

namespace evolutionvox::ui {

// One source of truth for the pointer, arc, readout and existing "retune" parameter.
// 0% points to the lower-left (natural); 100% to the lower-right (hard tune).
// The path sweeps 270 degrees OVER the top of the dial, not across its bottom gap.
struct RetuneDialMapping {
    static constexpr float maxRetuneMs = 250.0f;
    static constexpr float startDegrees = 135.0f;
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
        float a = std::atan2(y, x) * (180.0f / pi);
        // The lower, inactive 90-degree gap snaps to the nearest endpoint.
        if (a >= 45.0f && a < 135.0f)
            return a < 90.0f ? 100.0f : 0.0f;
        if (a < startDegrees) a += 360.0f;
        return clampIntensity((a - startDegrees) * 100.0f / sweepDegrees);
    }
};
} // namespace evolutionvox::ui
