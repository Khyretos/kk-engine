#include "kke/GpuParticleRing.h"
#include "kke/ParticleLibrary.h"
#include "kke/ParticleMotion.h"
#include "kke/ParticleSim.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>

// ENG-G18 part 4: GPU particles move by closed-form motion (the CPU copy of
// shaders/effect_gpu.vert), checked here against the stepped simulation.

using kke::ParticleSim;

namespace {

ParticleSim::Particle born(ParticleSim::Kind kind) {
    ParticleSim::Particle p;
    p.kind = kind;
    p.position = glm::vec3(1.0f, 2.0f, 3.0f);
    p.velocity = glm::vec3(2.0f, 5.0f, -1.0f);
    p.drag = 1.5f;
    p.rise = -3.0f;
    p.growth = 0.8f;
    p.radius = 0.3f;
    p.spin = 0.7f;
    p.flip = 2.0f;
    p.life = 10.0f;
    p.floor = -1000.0f;
    return p;
}

// The stepped simulation, in small steps, `seconds` long.
ParticleSim::Particle stepped(const ParticleSim::Particle& p, float seconds, const glm::vec3& wind) {
    ParticleSim sim;
    sim.emit(p);
    const int steps = 20000;
    for (int i = 0; i < steps; ++i) sim.update(seconds / steps, wind);
    return sim.particles()[0];
}

void expectNear(const glm::vec3& a, const glm::vec3& b, float tol) {
    EXPECT_NEAR(a.x, b.x, tol);
    EXPECT_NEAR(a.y, b.y, tol);
    EXPECT_NEAR(a.z, b.z, tol);
}

} // namespace

TEST(ParticleMotion, MatchesTheSteppedSimForEveryKind) {
    const glm::vec3 wind(1.0f, 0.0f, 0.5f);
    for (ParticleSim::Kind kind : { ParticleSim::Kind::Smoke, ParticleSim::Kind::Glow, ParticleSim::Kind::Spark,
                                    ParticleSim::Kind::Flake, ParticleSim::Kind::Ring }) {
        SCOPED_TRACE(static_cast<int>(kind));
        const ParticleSim::Particle p = born(kind);
        const ParticleSim::Particle s = stepped(p, 1.0f, wind);
        const kke::ParticleMotion m = kke::particleMotion(p, 1.0f, wind);
        expectNear(m.position, s.position, 2e-3f);
        expectNear(m.velocity, s.velocity, 2e-3f);
        EXPECT_NEAR(m.radius, s.radius, 2e-3f);
        EXPECT_NEAR(m.angle, s.angle, 1e-3f); // 20000 float additions on the stepped side
        EXPECT_NEAR(m.flipAngle, s.flipAngle, 1e-3f);
    }
}

TEST(ParticleMotion, WithoutDragItIsBallistic) {
    ParticleSim::Particle p = born(ParticleSim::Kind::Spark);
    p.drag = 0.0f;
    const kke::ParticleMotion m = kke::particleMotion(p, 2.0f, glm::vec3(0.0f));
    expectNear(m.position, glm::vec3(1.0f + 4.0f, 2.0f + 10.0f - 6.0f, 3.0f - 2.0f), 1e-5f);
    expectNear(m.velocity, glm::vec3(2.0f, 5.0f - 6.0f, -1.0f), 1e-5f);
}

TEST(ParticleMotion, AStillParticleSwirlsExactly) {
    ParticleSim::Particle p = born(ParticleSim::Kind::Glow);
    p.velocity = glm::vec3(0.0f);
    p.drag = 0.0f;
    p.rise = 0.0f;
    p.origin = glm::vec3(1.0f, 0.0f, 1.0f);
    p.position = glm::vec3(3.0f, 0.5f, 1.0f);
    p.vortex = 2.0f;
    const ParticleSim::Particle s = stepped(p, 1.0f, glm::vec3(0.0f));
    const kke::ParticleMotion m = kke::particleMotion(p, 1.0f, glm::vec3(0.0f));
    expectNear(m.position, s.position, 1e-4f);
}

TEST(ParticleMotion, SparksAndFlakesStopAtTheirFloor) {
    ParticleSim::Particle p = born(ParticleSim::Kind::Spark);
    p.floor = 0.0f;
    EXPECT_FALSE(kke::particleMotion(p, 0.1f, glm::vec3(0.0f)).landed);
    EXPECT_FLOAT_EQ(kke::particleMotion(p, 5.0f, glm::vec3(0.0f)).position.y, 0.02f);
    EXPECT_TRUE(kke::particleMotion(p, 5.0f, glm::vec3(0.0f)).landed);
    p.kind = ParticleSim::Kind::Flake;
    EXPECT_FLOAT_EQ(kke::particleMotion(p, 5.0f, glm::vec3(0.0f)).position.y, 1e-3f);
    EXPECT_TRUE(kke::particleMotion(p, 5.0f, glm::vec3(0.0f)).landed);
}

TEST(GpuParticleRing, HandsOutSlotsInTurnAndGrowsWhenFull) {
    kke::GpuParticleRing ring(4, 16);
    for (uint32_t i = 0; i < 4; ++i) EXPECT_EQ(ring.allocate(0.0f, 1.0f), i);
    EXPECT_EQ(ring.alive(0.5f), 4u);
    // Slot 0 is still alive at 0.5: the ring doubles and goes on at 4.
    EXPECT_EQ(ring.allocate(0.5f, 2.0f), 4u);
    EXPECT_EQ(ring.capacity(), 8u);
    EXPECT_EQ(ring.used(), 5u);
    // After 1.0 the first four are free again; the head carries on to 5..7, then wraps to 0.
    EXPECT_EQ(ring.allocate(1.5f, 3.0f), 5u);
    EXPECT_EQ(ring.allocate(1.5f, 3.0f), 6u);
    EXPECT_EQ(ring.allocate(1.5f, 3.0f), 7u);
    EXPECT_EQ(ring.allocate(1.5f, 3.0f), 0u);
    EXPECT_EQ(ring.capacity(), 8u);
    EXPECT_EQ(ring.alive(1.5f), 5u);
    ring.clear();
    EXPECT_EQ(ring.alive(1.5f), 0u);
    EXPECT_EQ(ring.used(), 0u);
}

TEST(GpuParticleRing, AtItsLimitTheOldestGoes) {
    kke::GpuParticleRing ring(2, 2);
    EXPECT_EQ(ring.allocate(0.0f, 9.0f), 0u);
    EXPECT_EQ(ring.allocate(0.0f, 9.0f), 1u);
    EXPECT_EQ(ring.allocate(0.0f, 9.0f), 0u); // still alive, but there is no more room
    EXPECT_EQ(ring.capacity(), 2u);
}

TEST(ParticleSim, GpuBornParticlesAreHandedOverWithTheirBirthTime) {
    ParticleSim sim;
    ParticleSim::Particle p = born(ParticleSim::Kind::Spark);
    sim.emitGpu(p); // GPU birth off: a CPU particle
    EXPECT_EQ(sim.count(), 1u);
    sim.clear();
    sim.setGpuBorn(true);
    sim.update(0.25f);
    sim.emitGpu(p);
    EXPECT_EQ(sim.count(), 0u);
    std::vector<ParticleSim::Born> out;
    sim.takeBorn(out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_FLOAT_EQ(out[0].time, 0.25f);
    EXPECT_EQ(out[0].particle.position, p.position);
    sim.takeBorn(out);
    EXPECT_TRUE(out.empty());
}

TEST(ParticleLibraryV2, GpuLayersGoToTheGpuAndRefuseCpuOnlyFields) {
    ParticleSim sim;
    sim.setGpuBorn(true);
    kke::ParticleLibrary lib(sim);
    std::string error;
    ASSERT_TRUE(lib.add(nlohmann::json::parse(R"({"drops": {"layers": [{"kind": "spark", "count": 7, "gpu": true}]}})"), &error)) << error;
    lib.play("drops", {});
    EXPECT_EQ(sim.count(), 0u);
    std::vector<ParticleSim::Born> out;
    sim.takeBorn(out);
    EXPECT_EQ(out.size(), 7u);
    for (const char* field : { R"("collide": true)", R"("onDeath": "drops")", R"("attract": 1)", R"("sizeOverLife": [1, 2])" }) {
        const std::string effect = std::string(R"({"x": {"layers": [{"count": 1, "gpu": true, )") + field + "}]}}";
        EXPECT_FALSE(kke::ParticleLibrary::validate(nlohmann::json::parse(effect), &error)) << field;
        EXPECT_NE(error.find("gpu"), std::string::npos) << error;
    }
}
