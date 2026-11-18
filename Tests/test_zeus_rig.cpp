// Zeus lightning-dial geometry tests (no JUCE). Verifies the Design V1 interaction contract:
// the bolt tip lands exactly on the gold meter at every value, 50% is straight up at the top of
// the meter, the nine poses progress in order, and nothing produces NaN or a degenerate bolt.
#include "Plugin/ZeusRig.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace z = evolutionvox::ui::zeus;
using evolutionvox::ui::RetuneDialMapping;

static void check(bool cond, const std::string& msg) { if (!cond) throw std::runtime_error(msg); }
static bool near(float a, float b, float eps) { return std::abs(a - b) <= eps; }

int main() {
    try {
        // 1. Exact tip/meter contact and fist attachment for every value and every pose that can be
        //    selected around it (including the hysteresis neighbour).
        float minStretch = 1e9f, maxStretch = 0.0f;
        for (int step = 0; step <= 1000; ++step) {
            const float intensity = static_cast<float>(step) * 0.1f;
            const auto expected = z::meterPoint(intensity);
            check(near(std::hypot(expected.x - z::dialCentreX, expected.y - z::dialCentreY), z::dialRadius, 0.01f),
                  "meter point not on the arc");
            const int pose = z::poseForIntensity(intensity);
            for (int candidate : { pose - 1, pose, pose + 1 }) {
                if (candidate < 0 || candidate >= z::numPoses) continue;
                const auto t = z::boltTransform(candidate, intensity);
                const auto& p = z::poses[static_cast<size_t>(candidate)];
                const auto grip = t.apply(p.boltAnchor), tip = t.apply(p.boltTip);
                check(std::isfinite(grip.x) && std::isfinite(tip.y), "NaN in bolt transform");
                check(near(grip.x, p.fist.x, 0.02f) && near(grip.y, p.fist.y, 0.02f),
                      "bolt detached from fist at " + std::to_string(intensity));
                check(near(tip.x, expected.x, 0.02f) && near(tip.y, expected.y, 0.02f),
                      "bolt tip misses meter at " + std::to_string(intensity));
                // Thickness (determinant / length) stays within the uniform-scale clamp.
                const float det = std::abs(t.m00 * t.m11 - t.m01 * t.m10);
                const float across = det / t.stretch;
                check(across >= z::boltUniformScaleMin - 1e-3f && across <= z::boltUniformScaleMax + 1e-3f,
                      "bolt thickness outside limits");
                if (candidate == pose) {
                    minStretch = std::min(minStretch, t.stretch);
                    maxStretch = std::max(maxStretch, t.stretch);
                }
            }
        }
        check(minStretch > 0.5f && maxStretch < 2.4f, "bolt stretch out of documented range");

        // 2. 100%: pose 9, bolt horizontal straight forward to the right, tip on the meter's end at
        //    3 o'clock. 50%: pose 5 (raised).
        const int p100 = z::poseForIntensity(100.0f);
        check(p100 == 8, "100% must show pose 9 (max tune)");
        const auto t100 = z::boltTransform(p100, 100.0f);
        check(near(t100.tip.x, z::dialCentreX + z::dialRadius, 0.01f) && near(t100.tip.y, z::dialCentreY, 0.01f),
              "100% tip is not at 3 o'clock on the meter");
        check(near(z::poses[8].fist.y, z::dialCentreY, 0.01f) && t100.tip.x > z::poses[8].fist.x,
              "100% bolt is not horizontal / pointing right");
        check(z::poseForIntensity(50.0f) == 4, "50% must show pose 5 (raised)");
        check(near(RetuneDialMapping::retuneMsFromIntensity(50.0f), 31.25f, 0.01f), "50% != 31.25 ms");

        // 3. 0% points straight down (6 o'clock), below every pose, so the tip is never hidden.
        const auto p0 = z::meterPoint(0.0f);
        check(near(p0.x, z::dialCentreX, 0.01f) && near(p0.y, z::dialCentreY + z::dialRadius, 0.01f),
              "0% must be at 6 o'clock");
        for (const auto& p : z::poses)
            check(static_cast<float>(p.base.y + p.base.h) < p0.y, "a pose overlaps the 0% end of the meter");

        // 4. Pose progression: monotonic, all nine reached, each nominal value shows its own pose.
        int last = 0, seen = 0;
        for (int step = 0; step <= 1000; ++step) {
            const int pose = z::poseForIntensity(step * 0.1f);
            check(pose >= last, "pose sequence went backwards");
            if (pose != last || step == 0) ++seen;
            last = pose;
        }
        check(seen == z::numPoses, "not all nine poses are reachable");
        for (int i = 0; i < z::numPoses; ++i)
            check(z::poseForIntensity(z::poses[static_cast<size_t>(i)].intensity) == i, "nominal pose mismatch");

        // 5. Hysteresis: hovering on a boundary keeps the current pose (no flicker).
        const float boundary = 0.5f * (z::poses[3].intensity + z::poses[4].intensity);
        check(z::poseForIntensity(boundary + 0.4f, 3) == 3, "hysteresis should hold pose 4");
        check(z::poseForIntensity(boundary - 0.4f, 4) == 4, "hysteresis should hold pose 5");
        check(z::poseForIntensity(boundary + 2.0f, 3) == 4, "hysteresis must release past the band");

        // 6. Scene bounds contain the whole arc.
        const auto b = z::sceneBounds(0.0f);
        check(b.y0 <= z::dialCentreY - z::dialRadius && b.x0 <= z::dialCentreX - z::dialRadius
              && b.x1 >= z::dialCentreX + z::dialRadius, "scene bounds clip the meter");

        std::cout << "Zeus rig geometry passed (bolt stretch " << minStretch << ".." << maxStretch << ")\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
