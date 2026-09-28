#include "kke/EngineSound.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace kke;

namespace {

constexpr int kRate = 48000;

// Half a second at a steady setting (after a quarter second to settle).
std::vector<float> run(EngineSound& e, float rpm, float throttle, float slide, float speed) {
    std::vector<float> settle(kRate / 4), out(kRate / 2);
    e.set(rpm, throttle, slide, speed);
    e.render(settle.data(), settle.size(), kRate);
    e.render(out.data(), out.size(), kRate);
    return out;
}

float rms(const std::vector<float>& s) {
    double sum = 0.0;
    for (float v : s) sum += double(v) * v;
    return float(std::sqrt(sum / double(s.size())));
}

// Energy above ~700 Hz: the difference between each sample and the next.
float highs(const std::vector<float>& s) {
    double sum = 0.0;
    for (size_t i = 1; i < s.size(); ++i) sum += double(s[i] - s[i - 1]) * (s[i] - s[i - 1]);
    return float(std::sqrt(sum / double(s.size())));
}

// The strongest period (autocorrelation) between minLag and maxLag samples.
int period(const std::vector<float>& s, int minLag, int maxLag) {
    int best = minLag;
    double bestSum = -1e30;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double sum = 0.0;
        for (size_t i = 0; i + size_t(lag) < s.size(); ++i) sum += double(s[i]) * s[i + size_t(lag)];
        if (sum > bestSum) {
            bestSum = sum;
            best = lag;
        }
    }
    return best;
}

} // namespace

TEST(EngineSound, IsAudibleFiniteAndInRange) {
    for (const EngineSound::Params& p : { EngineSound::inline4(), EngineSound::inline6(), EngineSound::v8(), EngineSound::v10() }) {
        EngineSound e(p);
        for (float thr : { 0.0f, 1.0f })
            for (float rpm : { p.idleRpm, p.maxRpm }) {
                const std::vector<float> s = run(e, rpm, thr, 0.0f, 0.0f);
                for (float v : s) {
                    ASSERT_TRUE(std::isfinite(v));
                    ASSERT_LE(std::fabs(v), 1.0f);
                }
                EXPECT_GT(rms(s), 0.01f) << p.cylinders << " cylinders at " << rpm << " rpm, throttle " << thr;
            }
    }
}

TEST(EngineSound, ThrottleIsLouder) {
    EngineSound a(EngineSound::v8()), b(EngineSound::v8());
    EXPECT_GT(rms(run(a, 4000.0f, 1.0f, 0.0f, 0.0f)), rms(run(b, 4000.0f, 0.0f, 0.0f, 0.0f)) * 1.3f);
}

TEST(EngineSound, FiresAtTheRevs) {
    // An inline six at 3000 rpm fires 150 times a second: every 320 samples.
    EngineSound::Params p = EngineSound::inline6();
    p.unevenness = 0.0f;
    p.roughness = 0.0f;
    p.intakeNoise = 0.0f;
    EngineSound e(p);
    const int lag = period(run(e, 3000.0f, 1.0f, 0.0f, 0.0f), 200, 450);
    EXPECT_NEAR(lag, 320, 6);
}

TEST(EngineSound, SlidingTyresSqueal) {
    EngineSound a(EngineSound::inline4()), b(EngineSound::inline4());
    EXPECT_GT(highs(run(a, 3000.0f, 0.5f, 1.0f, 20.0f)), highs(run(b, 3000.0f, 0.5f, 0.0f, 20.0f)) * 1.3f);
}

TEST(EngineSound, SameSeedSameSound) {
    EngineSound a(EngineSound::v8()), b(EngineSound::v8());
    EXPECT_EQ(run(a, 3500.0f, 0.7f, 0.2f, 10.0f), run(b, 3500.0f, 0.7f, 0.2f, 10.0f));
}
