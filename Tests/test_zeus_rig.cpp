// Zeus lightning-dial geometry tests (no JUCE). Verifies the Design V1 interaction contract:
// the vector bolt starts on Zeus's fist and its tip lands exactly on the gold meter at every value,
// 100% strikes horizontally at 3 o'clock, the nine poses progress in order with hysteresis, and
// the bolt is never degenerate (NaN, zero width) or clipped by the scene.
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
        // 1. For every value and every pose that can be selected around it (including the hysteresis
        //    neighbour): grip exactly on the fist, tip exactly on the meter, outline well formed and
        //    inside the scene (no clipping).
        const auto scene = z::sceneBounds(48.0f);   // = the dial component's area (kArcMarginCanvas)
        const float glow = z::boltGlowWidth * 0.5f;
        float minLength = 1e9f, maxLength = 0.0f;
        for (int step = 0; step <= 1000; ++step) {
            const float intensity = static_cast<float>(step) * 0.1f;
            const auto expected = z::meterPoint(intensity);
            check(near(std::hypot(expected.x - z::dialCentreX, expected.y - z::dialCentreY), z::dialRadius, 0.01f),
                  "meter point not on the arc");
            const int pose = z::poseForIntensity(intensity);
            for (int candidate : { pose - 1, pose, pose + 1 }) {
                if (candidate < 0 || candidate >= z::numPoses) continue;
                const auto g = z::boltGeometry(candidate, intensity);
                const auto& p = z::poses[static_cast<size_t>(candidate)];
                check(near(g.centre.front().x, p.fist.x, 0.001f) && near(g.centre.front().y, p.fist.y, 0.001f),
                      "bolt detached from fist at " + std::to_string(intensity));
                check(near(g.centre.back().x, expected.x, 0.001f) && near(g.centre.back().y, expected.y, 0.001f),
                      "bolt tip misses meter at " + std::to_string(intensity));
                const auto outline = z::boltOutline(g, z::boltWidth);
                check(near(outline[z::boltPoints - 1].x, expected.x, 0.001f)
                      && near(outline[z::boltPoints - 1].y, expected.y, 0.001f), "outline tip is not the meter point");
                for (size_t i = 0; i < outline.size(); ++i) {
                    const auto& q = outline[i];
                    check(std::isfinite(q.x) && std::isfinite(q.y), "NaN in bolt outline");
                    check(q.x - glow >= scene.x0 && q.x + glow <= scene.x1 && q.y - glow >= scene.y0
                          && q.y + glow <= scene.y1, "bolt would be clipped by the scene");
                    if (i + 1 < z::boltPoints) {   // left/right edges are apart: real width, not a sliver
                        const auto& mirror = outline[outline.size() - 1 - i];
                        check(std::hypot(q.x - mirror.x, q.y - mirror.y) > z::boltWidth * 0.5f, "bolt too thin");
                    }
                }
                if (candidate == pose) {
                    minLength = std::min(minLength, g.length);
                    maxLength = std::max(maxLength, g.length);
                }
            }
        }
        check(minLength > 100.0f, "bolt too short to read as lightning");

        // 2. 100%: pose 9, bolt horizontal straight forward to the right, tip on the meter's end at
        //    3 o'clock. 50%: pose 5 (raised).
        const int p100 = z::poseForIntensity(100.0f);
        check(p100 == 8, "100% must show pose 9 (max tune)");
        const auto g100 = z::boltGeometry(p100, 100.0f);
        check(near(g100.centre.back().x, z::dialCentreX + z::dialRadius, 0.01f)
              && near(g100.centre.back().y, z::dialCentreY, 0.01f), "100% tip is not at 3 o'clock on the meter");
        check(near(g100.centre.front().y, g100.centre.back().y, 0.01f) && g100.centre.back().x > g100.centre.front().x,
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

        std::cout << "Zeus rig geometry passed (bolt length " << minLength << ".." << maxLength << " canvas px)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
