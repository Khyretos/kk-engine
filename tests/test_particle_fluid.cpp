#include "kke/ParticleFluid.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace {

// A 6x6x6 block of particles at lattice spacing, dropped into a 0.8 m box.
kke::ParticleFluid makeBlock() {
    kke::ParticleFluid::Params p;
    p.radius = 0.04f;
    p.boundsMin = glm::vec3(-0.4f, -1.0f, -0.4f);
    p.boundsMax = glm::vec3(0.4f, 10.0f, 0.4f);
    kke::ParticleFluid f(p, 1000);
    for (int x = 0; x < 6; ++x)
        for (int y = 0; y < 6; ++y)
            for (int z = 0; z < 6; ++z) f.add(glm::vec3(x - 2.5f, y + 2.0f, z - 2.5f) * 0.08f, glm::vec3(0.0f), 20.0f);
    return f;
}

} // namespace

TEST(ParticleFluid, SettlesInsideContainerWithoutExploding) {
    kke::ParticleFluid f = makeBlock();
    for (int i = 0; i < 180; ++i) f.step(1.0f / 60.0f); // 3 s
    float maxSpeed = 0.0f, minY = 1e9f;
    for (size_t i = 0; i < f.size(); ++i) {
        maxSpeed = std::max(maxSpeed, glm::length(f.velocities()[i]));
        minY = std::min(minY, f.positions()[i].y);
        ASSERT_TRUE(std::isfinite(f.positions()[i].x));
    }
    EXPECT_GE(minY, f.params().radius - 1e-4f);  // never through the ground
    EXPECT_LT(maxSpeed, 1.0f);                    // came to rest-ish
    // Incompressibility: nobody squeezed far past rest density.
    float maxRho = *std::max_element(f.densities().begin(), f.densities().end());
    EXPECT_LT(maxRho, f.params().restDensity * 1.3f);
}

TEST(ParticleFluid, CollidersKeepParticlesOut) {
    kke::ParticleFluid f = makeBlock();
    const glm::vec3 c(0.0f, 0.2f, 0.0f);
    const float R = 0.2f;
    f.addCollider([&](const glm::vec3& p, glm::vec3& n) {
        glm::vec3 d = p - c;
        float len = glm::length(d);
        n = len > 1e-6f ? d / len : glm::vec3(0, 1, 0);
        return len - R;
    });
    for (int i = 0; i < 120; ++i) f.step(1.0f / 60.0f);
    for (const glm::vec3& p : f.positions()) EXPECT_GE(glm::length(p - c), R + f.params().radius - 0.01f);
}

TEST(ParticleFluid, CoolsTowardAmbientAndFreezes) {
    // Two identical falling particles; only one of them can solidify.
    auto run = [](float solidify, float& temperature) {
        kke::ParticleFluid::Params p;
        p.coolingRate = 2.0f;
        p.ambientTemperature = 20.0f;
        p.solidifyTemperature = solidify;
        kke::ParticleFluid f(p, 10);
        f.add(glm::vec3(0, 100, 0), glm::vec3(1, 0, 0), 1200.0f); // high up: falling the whole time
        for (int i = 0; i < 60; ++i) f.step(1.0f / 60.0f);
        temperature = f.temperatures()[0];
        float y0 = f.positions()[0].y;
        for (int i = 0; i < 30; ++i) f.step(1.0f / 60.0f);
        return y0 - f.positions()[0].y;
    };
    float tSolid = 0.0f, tFree = 0.0f;
    float fellSolid = run(500.0f, tSolid);
    float fellFree = run(-1e9f, tFree);
    EXPECT_LT(tSolid, 500.0f);                  // cooled toward ambient
    EXPECT_GT(fellSolid, 0.0f);                 // solid, but not pinned in mid-air
    EXPECT_LT(fellSolid, fellFree * 0.5f);      // heavily damped compared to liquid
}

TEST(ParticleFluid, CapacityIsABudget) {
    kke::ParticleFluid f(kke::ParticleFluid::Params{}, 3);
    EXPECT_TRUE(f.add(glm::vec3(0), glm::vec3(0), 0));
    EXPECT_TRUE(f.add(glm::vec3(1), glm::vec3(0), 0));
    EXPECT_TRUE(f.add(glm::vec3(2), glm::vec3(0), 0));
    EXPECT_FALSE(f.add(glm::vec3(3), glm::vec3(0), 0));
    f.removeIf([&](size_t i) { return f.positions()[i].x > 0.5f; });
    EXPECT_EQ(f.size(), 1u);
}
