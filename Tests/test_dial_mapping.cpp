#include "Plugin/RetuneDialMapping.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using evolutionvox::ui::RetuneDialMapping;
void check(bool cond, const char* msg) { if (!cond) throw std::runtime_error(msg); }
bool near(float a, float b, float epsilon = 0.02f) { return std::abs(a-b) < epsilon; }

int main() {
    try {
        check(near(RetuneDialMapping::retuneMsFromIntensity(0), 250), "0% must equal 250ms");
        check(near(RetuneDialMapping::retuneMsFromIntensity(25), 105.46875f), "25%");
        check(near(RetuneDialMapping::retuneMsFromIntensity(50), 31.25f), "50%");
        check(near(RetuneDialMapping::retuneMsFromIntensity(75), 3.90625f), "75%");
        check(near(RetuneDialMapping::retuneMsFromIntensity(100), 0), "100%");
        for (float intensity : {0.0f, 10.0f, 25.0f, 50.0f, 75.0f, 90.0f, 100.0f}) {
            const float ms = RetuneDialMapping::retuneMsFromIntensity(intensity);
            check(near(RetuneDialMapping::intensityFromRetuneMs(ms), intensity), "roundtrip");
            const float theta = RetuneDialMapping::pointerDegrees(intensity)
                                * RetuneDialMapping::pi / 180.0f;
            const float fromPoint = RetuneDialMapping::intensityFromPointer(
                std::cos(theta), std::sin(theta));
            check(near(fromPoint, intensity, 0.05f), "bolt direction differs from displayed intensity");
        }
        check(near(RetuneDialMapping::pointerDegrees(0), 90), "0% points straight down (6 o'clock)");
        check(near(RetuneDialMapping::pointerDegrees(50), 225), "50% points to the upper-left diagonal");
        check(near(RetuneDialMapping::pointerDegrees(100), 360), "100% points horizontally right (3 o'clock)");
        check(near(RetuneDialMapping::pointerDegrees(100) - RetuneDialMapping::pointerDegrees(0), 270),
              "exactly 270 degrees of travel");
        // Clicks in the inactive lower-right gap snap to the nearer end.
        check(near(RetuneDialMapping::intensityFromPointer(1.0f, 0.2f), 100), "gap near 3 o'clock -> 100%");
        check(near(RetuneDialMapping::intensityFromPointer(0.2f, 1.0f), 0), "gap near 6 o'clock -> 0%");
        std::cout << "Design Reference V1 lightning-dial mapping passed\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
