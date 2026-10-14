#include "kke/ParticleLibrary.h"
#include "kke/ParticleSim.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <string>

// ENG-G18: the particle simulation without a GPU, and the library's v2
// layers (curves, vortex, attract, colliders, sub-effects) played into it.

using kke::ParticleSim;

namespace {
ParticleSim::Particle still(ParticleSim::Kind kind = ParticleSim::Kind::Glow) {
    ParticleSim::Particle p;
    p.kind = kind;
    p.drag = 0.0f;
    p.rise = 0.0f;
    p.growth = 0.0f;
    p.life = 10.0f;
    return p;
}
} // namespace

TEST(ParticleSim, FullReplacesTheOldest) {
    ParticleSim sim(16);
    for (int i = 0; i < 16; ++i) {
        ParticleSim::Particle p = still();
        p.age = static_cast<float>(i) * 0.1f;
        p.position.x = static_cast<float>(i);
        sim.emit(p);
    }
    ParticleSim::Particle young = still();
    young.position.x = 100.0f;
    sim.emit(young);
    ASSERT_EQ(sim.count(), 16u);
    bool oldestGone = true, youngThere = false;
    for (const auto& p : sim.particles()) {
        if (p.position.x == 15.0f) oldestGone = false;
        if (p.position.x == 100.0f) youngThere = true;
    }
    EXPECT_TRUE(oldestGone);
    EXPECT_TRUE(youngThere);
}

TEST(ParticleSim, SparksBounceOffTheirFloor) {
    ParticleSim sim;
    ParticleSim::Particle p = still(ParticleSim::Kind::Spark);
    p.position = glm::vec3(0.0f, 0.05f, 0.0f);
    p.velocity = glm::vec3(1.0f, -4.0f, 0.0f);
    p.age = 0.1f;
    sim.emit(p);
    sim.update(0.02f);
    const auto& q = sim.particles()[0];
    EXPECT_NEAR(q.position.y, 0.02f, 1e-5f);
    EXPECT_GT(q.velocity.y, 0.0f); // going up again
    EXPECT_NEAR(q.velocity.x, 0.6f, 1e-4f);
}

TEST(ParticleSim, VortexSwirlsWithoutDriftingOut) {
    ParticleSim sim;
    ParticleSim::Particle p = still();
    p.origin = glm::vec3(1.0f, 0.0f, 1.0f);
    p.position = glm::vec3(3.0f, 0.5f, 1.0f); // 2 m east of the line
    p.vortex = 2.0f;                         // 2 m/s round it
    sim.emit(p);
    for (int i = 0; i < 100; ++i) sim.update(0.01f); // 1 s: 2 m of arc = 1 radian
    const auto& q = sim.particles()[0];
    const glm::vec2 r(q.position.x - 1.0f, q.position.z - 1.0f);
    EXPECT_NEAR(glm::length(r), 2.0f, 1e-3f);
    EXPECT_NEAR(std::atan2(r.y, r.x), 1.0f, 1e-3f);
    EXPECT_NEAR(q.position.y, 0.5f, 1e-6f);
}

TEST(ParticleSim, AttractPullsTowardTheOrigin) {
    ParticleSim sim;
    ParticleSim::Particle p = still();
    p.position = glm::vec3(5.0f, 0.0f, 0.0f);
    p.attract = 4.0f;
    sim.emit(p);
    sim.update(0.5f);
    EXPECT_NEAR(sim.particles()[0].velocity.x, -2.0f, 1e-5f);
    ParticleSim::Particle q = still();
    q.position = glm::vec3(0.0f, 0.0f, 5.0f);
    q.attract = -4.0f; // pushes
    sim.clear();
    sim.emit(q);
    sim.update(0.5f);
    EXPECT_NEAR(sim.particles()[0].velocity.z, 2.0f, 1e-5f);
}

TEST(ParticleSim, CurvesSampleEvenlySpacedKeys) {
    EXPECT_FLOAT_EQ(ParticleSim::sample(std::vector<float>{}, 0.3f, 7.0f), 7.0f);
    EXPECT_FLOAT_EQ(ParticleSim::sample(std::vector<float>{ 2.0f }, 0.3f, 7.0f), 2.0f);
    const std::vector<float> keys{ 0.0f, 1.0f, 3.0f };
    EXPECT_FLOAT_EQ(ParticleSim::sample(keys, 0.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ParticleSim::sample(keys, 0.25f, 0.0f), 0.5f);
    EXPECT_FLOAT_EQ(ParticleSim::sample(keys, 0.75f, 0.0f), 2.0f);
    EXPECT_FLOAT_EQ(ParticleSim::sample(keys, 1.5f, 0.0f), 3.0f);
    const std::vector<glm::vec3> colors{ glm::vec3(1, 0, 0), glm::vec3(0, 0, 1) };
    EXPECT_EQ(ParticleSim::sample(colors, 0.5f, glm::vec3(0.0f)), glm::vec3(0.5f, 0.0f, 0.5f));
    ParticleSim sim;
    const uint16_t c = sim.addCurve({ keys, colors, {} });
    EXPECT_EQ(c, 1);
    ASSERT_NE(sim.curve(c), nullptr);
    EXPECT_EQ(sim.curve(c)->size, keys);
    EXPECT_EQ(sim.curve(0), nullptr);
    EXPECT_EQ(sim.curve(2), nullptr);
}

TEST(ParticleSim, CollidersBounceWithinTheirRayBudget) {
    ParticleSim sim;
    int rays = 0;
    // A wall at x = 1 facing -x.
    sim.setCollider(
        [&](const glm::vec3& from, const glm::vec3& to, glm::vec3& hit, glm::vec3& normal) {
            ++rays;
            if (from.x < 1.0f && to.x >= 1.0f) {
                hit = glm::vec3(1.0f, from.y, from.z);
                normal = glm::vec3(-1.0f, 0.0f, 0.0f);
                return true;
            }
            return false;
        },
        2);
    for (int i = 0; i < 3; ++i) {
        ParticleSim::Particle p = still(ParticleSim::Kind::Glow);
        p.position = glm::vec3(0.9f, 1.0f, static_cast<float>(i));
        p.velocity = glm::vec3(10.0f, 0.0f, 0.0f);
        p.collide = true;
        sim.emit(p);
    }
    ParticleSim::Particle free = still();
    free.velocity = glm::vec3(10.0f, 0.0f, 0.0f); // does not collide: costs no ray
    sim.emit(free);
    sim.update(0.1f);
    EXPECT_EQ(rays, 2);
    const auto& ps = sim.particles();
    EXPECT_NEAR(ps[0].position.x, 0.99f, 1e-5f);
    EXPECT_NEAR(ps[0].velocity.x, -3.5f, 1e-4f);
    EXPECT_NEAR(ps[1].position.x, 0.99f, 1e-5f);
    EXPECT_GT(ps[2].position.x, 1.0f); // past the budget: through this frame
    sim.update(0.01f);
    EXPECT_EQ(rays, 4); // the next frame starts after the last one checked
}

TEST(ParticleSim, DeathsAreReportedOnlyForOnDeath) {
    ParticleSim sim;
    ParticleSim::Particle a = still();
    a.life = 0.05f;
    a.onDeath = 3;
    a.position = glm::vec3(1.0f, 2.0f, 3.0f);
    a.floor = -1.0f;
    ParticleSim::Particle b = still();
    b.life = 0.05f;
    sim.emit(a);
    sim.emit(b);
    sim.update(0.1f);
    EXPECT_EQ(sim.count(), 0u);
    ASSERT_EQ(sim.deaths().size(), 1u);
    EXPECT_EQ(sim.deaths()[0].onDeath, 3);
    EXPECT_EQ(sim.deaths()[0].position, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_FLOAT_EQ(sim.deaths()[0].floor, -1.0f);
    sim.update(0.1f);
    EXPECT_TRUE(sim.deaths().empty());
}

TEST(ParticleLibraryV2, LayersCarryCurvesForcesAndSubEffects) {
    ParticleSim sim;
    kke::ParticleLibrary lib(sim);
    std::string error;
    ASSERT_TRUE(lib.add(nlohmann::json::parse(R"({
        "pop": { "layers": [ { "kind": "glow", "count": 5, "life": [0.5, 0.5] } ] },
        "shell": { "layers": [ { "kind": "spark", "count": 2, "life": [0.2, 0.2], "speed": [0, 0], "onDeath": "pop",
                                 "vortex": 1.5, "attract": 2, "collide": true,
                                 "sizeOverLife": [1, 2], "colorOverLife": [[1, 0, 0], [0, 0, 1]], "opacityOverLife": [1, 0] } ] }
    })"), &error)) << error;
    ASSERT_TRUE(lib.play("shell", { glm::vec3(0.0f, 5.0f, 0.0f) }));
    ASSERT_EQ(sim.count(), 2u);
    const ParticleSim::Particle& p = sim.particles()[0];
    EXPECT_FLOAT_EQ(p.vortex, 1.5f);
    EXPECT_FLOAT_EQ(p.attract, 2.0f);
    EXPECT_TRUE(p.collide);
    EXPECT_EQ(p.origin, glm::vec3(0.0f, 5.0f, 0.0f));
    ASSERT_NE(sim.curve(p.curve), nullptr);
    EXPECT_EQ(sim.curve(p.curve)->size, (std::vector<float>{ 1.0f, 2.0f }));
    EXPECT_NE(p.onDeath, 0);
    lib.update(0.3f); // both shells die and each pops
    EXPECT_EQ(sim.count(), 10u);
    for (const auto& q : sim.particles()) EXPECT_EQ(q.kind, ParticleSim::Kind::Glow);
}

TEST(ParticleLibraryV2, SubEffectChainsAreCapped) {
    ParticleSim sim(20000);
    kke::ParticleLibrary lib(sim);
    std::string error;
    // Every particle makes ten more when it dies: without a cap it doubles forever.
    ASSERT_TRUE(lib.add(nlohmann::json::parse(R"({
        "bloom": { "layers": [ { "kind": "glow", "count": 10, "life": [0.1, 0.1], "onDeath": "bloom" } ] }
    })"), &error)) << error;
    lib.play("bloom", {});
    for (int i = 0; i < 5; ++i) lib.update(0.2f);
    EXPECT_LE(sim.count(), static_cast<size_t>(kke::ParticleLibrary::kMaxDeathsPerUpdate) * 10u);
}

TEST(ParticleLibraryV2, MistakesInV2FieldsAreNamed) {
    std::string error;
    EXPECT_FALSE(kke::ParticleLibrary::validate(nlohmann::json::parse(R"({"a": {"layers": [{"count": 1, "sizeOverLife": []}]}})"), &error));
    EXPECT_NE(error.find("sizeOverLife"), std::string::npos);
    EXPECT_FALSE(kke::ParticleLibrary::validate(nlohmann::json::parse(R"({"a": {"layers": [{"count": 1, "colorOverLife": [[1, 0]]}]}})"), &error));
    EXPECT_NE(error.find("colorOverLife"), std::string::npos);
    EXPECT_FALSE(kke::ParticleLibrary::validate(nlohmann::json::parse(R"({"a": {"layers": [{"count": 1, "onDeath": 3}]}})"), &error));
    EXPECT_NE(error.find("onDeath"), std::string::npos);
    EXPECT_TRUE(kke::ParticleLibrary::validate(nlohmann::json::parse(R"({"a": {"layers": [{"count": 1, "vortex": 2, "collide": true}]}})"), &error))
        << error;
}
