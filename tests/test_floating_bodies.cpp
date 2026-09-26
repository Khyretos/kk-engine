#include "kke/FloatingBodies.h"
#include "kke/Ocean.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {
// Calm water: no waves, surface at y = 0.
kke::OceanWaves calm() {
    kke::OceanWaves o;
    o.waves().clear();
    return o;
}
float simulate(kke::FloatingBodies& fb, const kke::OceanWaves& o, float seconds) {
    float t = 0.0f;
    for (; t < seconds; t += 1.0f / 60.0f) fb.step(1.0f / 60.0f, o, t);
    return t;
}
} // namespace

TEST(Ocean, CalmWaterIsFlatAndWavesStayBounded) {
    kke::OceanWaves o = calm();
    EXPECT_FLOAT_EQ(o.height({ 3.0f, -2.0f }, 1.0f), 0.0f);
    kke::OceanWaves w;
    w.setWind(8.0f, 0.4f);
    float maxAmp = 0.0f;
    for (const auto& wave : w.waves()) maxAmp += wave.amplitude;
    for (float x = -20; x < 20; x += 1.3f) {
        float h = w.height({ x, x * 0.3f }, 2.0f);
        EXPECT_LE(std::fabs(h), maxAmp + 1e-3f);
        EXPECT_GT(w.normal({ x, 0.0f }, 2.0f).y, 0.3f);
    }
}

TEST(Ocean, HeightMatchesDisplacedSurface) {
    // height(x) must land on the same surface displacement() draws.
    kke::OceanWaves w;
    w.setWind(8.0f, 0.4f);
    glm::vec2 p(4.0f, -1.5f);
    glm::vec3 d = w.displacement(p, 3.0f);
    EXPECT_NEAR(w.height({ p.x + d.x, p.y + d.z }, 3.0f), d.y, 0.02f);
}

TEST(FloatingBodies, DensityDecidesFloatOrSink) {
    kke::OceanWaves o = calm();
    kke::FloatingBodies fb;
    size_t wood = fb.add(glm::vec3(0.5f), 500.0f, { 0, 1, 0 });
    size_t ice = fb.add(glm::vec3(0.5f), 917.0f, { 5, 1, 0 });
    size_t iron = fb.add(glm::vec3(0.3f), 7800.0f, { 10, 1, 0 });
    simulate(fb, o, 15.0f);
    // Wood (500/1025) floats about half submerged: centre near +0.01 m.
    EXPECT_NEAR(fb.bodies()[wood].position.y, 0.5f - 1.0f * 500.0f / 1025.0f, 0.12f);
    // Ice floats with ~10% above water.
    EXPECT_NEAR(fb.bodies()[ice].position.y, 0.5f - 1.0f * 917.0f / 1025.0f, 0.12f);
    // Iron rests on the sea floor.
    EXPECT_NEAR(fb.bodies()[iron].position.y, fb.seaFloorY + 0.3f, 0.05f);
}

TEST(FloatingBodies, TiltedBoxRightsItself) {
    kke::OceanWaves o = calm();
    kke::FloatingBodies fb;
    // A flat raft (wide, low) starts tilted 30 degrees.
    size_t raft = fb.add(glm::vec3(1.0f, 0.15f, 0.6f), 400.0f, { 0, 0, 0 }, glm::angleAxis(0.52f, glm::vec3(0, 0, 1)));
    simulate(fb, o, 12.0f);
    glm::vec3 up = glm::mat3_cast(fb.bodies()[raft].orientation) * glm::vec3(0, 1, 0);
    EXPECT_GT(up.y, 0.97f);
}

TEST(FloatingBodies, WavesMoveFloatingThings) {
    kke::OceanWaves w;
    w.setWind(8.0f, 0.0f);
    kke::FloatingBodies fb;
    size_t crate = fb.add(glm::vec3(0.4f), 500.0f, { 0, 0.2f, 0 });
    float minY = 1e9f, maxY = -1e9f;
    for (float t = 0.0f; t < 12.0f; t += 1.0f / 60.0f) {
        fb.step(1.0f / 60.0f, w, t);
        if (t > 4.0f) { minY = std::min(minY, fb.bodies()[crate].position.y); maxY = std::max(maxY, fb.bodies()[crate].position.y); }
    }
    EXPECT_GT(maxY - minY, 0.2f);                  // it bobs with the swell
    EXPECT_TRUE(std::isfinite(fb.bodies()[crate].position.x));
}

TEST(FloatingBodies, LongFlatHullStaysUpright) {
    // The sea demo's boat: long, wide and low, light (hull + air). A narrow
    // sample grid (5x2x4) once tipped it over (point height bug).
    kke::OceanWaves o = calm();
    kke::FloatingBodies fb;
    size_t boat = fb.add(glm::vec3(1.6f, 0.35f, 0.6f), 280.0f, { 0, 0.2f, 0 });
    simulate(fb, o, 10.0f);
    glm::vec3 up = glm::mat3_cast(fb.bodies()[boat].orientation) * glm::vec3(0, 1, 0);
    EXPECT_GT(up.y, 0.99f);
    EXPECT_NEAR(fb.bodies()[boat].position.y, 0.35f - 0.7f * 280.0f / 1025.0f, 0.06f);
}

TEST(FloatingBodies, BallastedBoatRidesSwellWithoutCapsizing) {
    // The sea demo's boat in its default 7 m/s wind swell for 20 s. Without
    // heave damping it was launched 2.7 m off a crest and capsized; without
    // a low centre of mass it heeled ~50 degrees.
    kke::OceanWaves w;
    w.setWind(7.0f, 0.4f, 0.6f);
    kke::FloatingBodies fb;
    size_t boat = fb.add(glm::vec3(1.6f, 0.35f, 0.6f), 280.0f, { 0, 0.2f, 0 });
    fb.bodies()[boat].linearDrag = 0.8f;
    fb.bodies()[boat].centerOfMassOffset = glm::vec3(0.0f, -0.3f, 0.0f);
    float minUp = 1.0f, maxY = -1e9f;
    for (float t = 0.0f; t < 20.0f; t += 1.0f / 60.0f) {
        fb.step(1.0f / 60.0f, w, t);
        minUp = std::min(minUp, (glm::mat3_cast(fb.bodies()[boat].orientation) * glm::vec3(0, 1, 0)).y);
        maxY = std::max(maxY, fb.bodies()[boat].position.y);
    }
    EXPECT_GT(minUp, 0.8f);   // never heels past ~37 degrees
    EXPECT_LT(maxY, 2.0f);    // never launched far above the crests (sum of amplitudes ~1.6 m)
}
