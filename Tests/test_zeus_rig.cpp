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
        check(minStretch > 0.4f && maxStretch < 2.4f, "bolt stretch out of documented range");

        // 2. 50%: pose 5 (raised), bolt exactly vertical, tip on the very top of the meter.
        const int p50 = z::poseForIntensity(50.0f);
        check(p50 == 4, "50% must show pose 5 (raised)");
        const auto t50 = z::boltTransform(p50, 50.0f);
        check(near(t50.tip.x, z::dialCentreX, 0.01f) && near(t50.tip.y, z::dialCentreY - z::dialRadius, 0.01f),
              "50% tip is not at the top of the meter");
        check(near(z::poses[4].fist.x, z::dialCentreX, 0.01f), "50% bolt is not vertical");
        check(near(RetuneDialMapping::retuneMsFromIntensity(50.0f), 31.25f, 0.01f), "50% != 31.25 ms");

        // 3. Ends: 0% lower-left (Natural), 100% lower-right (Hard Tune).
        const auto p0 = z::meterPoint(0.0f), p100 = z::meterPoint(100.0f);
        check(p0.x < z::dialCentreX && p0.y > z::dialCentreY, "0% must point lower-left");
        check(p100.x > z::dialCentreX && p100.y > z::dialCentreY, "100% must point lower-right");

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
