// GPU particles stepped each frame (ENG-F11): the step shaders/effect_step.glsl
// runs, its turbulence and its bounce, against kke::ParticleSim (the CPU
// fallback) and the library's rules for gpu layers.

#include "kke/ParticleLibrary.h"
#include "kke/ParticleStep.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cmath>

using namespace kke;
using Kind = ParticleSim::Kind;

TEST(ParticleStep, TurbulenceIsDivergenceFreeAndBounded) {
    const float h = 1e-3f, size = 2.0f, t = 1.7f;
    for (const glm::vec3 p : { glm::vec3(0.3f, 1.1f, -2.0f), glm::vec3(5.0f, -0.4f, 3.3f), glm::vec3(-7.2f, 2.5f, 0.9f) }) {
        const float div = (particleTurbulence(p + glm::vec3(h, 0, 0), size, t).x - particleTurbulence(p - glm::vec3(h, 0, 0), size, t).x +
                           particleTurbulence(p + glm::vec3(0, h, 0), size, t).y - particleTurbulence(p - glm::vec3(0, h, 0), size, t).y +
                           particleTurbulence(p + glm::vec3(0, 0, h), size, t).z - particleTurbulence(p - glm::vec3(0, 0, h), size, t).z) /
                          (2.0f * h);
        EXPECT_NEAR(div, 0.0f, 2e-2f) << p.x; // the field's own gradients are ~3 per m: this is round-off
        EXPECT_LE(glm::length(particleTurbulence(p, size, t)), 1.0f + 1e-5f);
    }
    // It swirls: different from place to place and from moment to moment.
    EXPECT_GT(glm::length(particleTurbulence(glm::vec3(0.0f), size, 0.0f) - particleTurbulence(glm::vec3(0.7f, 0, 0), size, 0.0f)), 0.05f);
    EXPECT_GT(glm::length(particleTurbulence(glm::vec3(0.0f), size, 0.0f) - particleTurbulence(glm::vec3(0.0f), size, 1.0f)), 0.05f);
}

namespace {

// The same particle through ParticleSim::update and particleStep, 90 steps at 60 Hz.
void expectSameAsCpu(ParticleSim::Particle p) {
    p.life = 100.0f;
    ParticleSim sim;
    sim.emit(p);
    StepParticle s{ p.position, p.velocity, 0.0f, false };
    StepParams params;
    params.kind = p.kind;
    params.drag = p.drag;
    params.rise = p.rise;
    params.flutter = p.flutter;
    params.seed = p.seed;
    params.floor = p.floor;
    params.origin = p.origin;
    params.vortex = p.vortex;
    params.attract = p.attract;
    params.turbulence = p.turbulence;
    params.turbulenceSize = p.turbulenceSize;
    const glm::vec3 wind(1.5f, 0.0f, -0.5f);
    for (int i = 0; i < 90; ++i) {
        sim.update(1.0f / 60.0f, wind);
        particleStep(s, params, 1.0f / 60.0f, wind, sim.time());
    }
    ASSERT_EQ(sim.count(), 1u);
    const ParticleSim::Particle& c = sim.particles()[0];
    EXPECT_NEAR(glm::length(c.position - s.position), 0.0f, 1e-3f) << static_cast<int>(p.kind);
    EXPECT_NEAR(glm::length(c.velocity - s.velocity), 0.0f, 1e-3f) << static_cast<int>(p.kind);
}

} // namespace

TEST(ParticleStep, MatchesTheCpuSimulationForEveryKind) {
    for (Kind kind : { Kind::Smoke, Kind::Spark, Kind::Glow, Kind::Flake, Kind::Ring }) {
        ParticleSim::Particle p;
        p.kind = kind;
        p.position = glm::vec3(0.5f, 2.0f, -0.3f);
        p.velocity = glm::vec3(1.0f, 3.0f, 0.5f);
        p.drag = 1.1f;
        p.rise = kind == Kind::Spark || kind == Kind::Flake ? -9.81f : 0.7f;
        p.flutter = 0.4f;
        p.seed = 0.37f;
        p.floor = 0.0f;
        p.origin = glm::vec3(0.0f, 1.0f, 0.0f);
        p.vortex = 1.5f;
        p.attract = 0.8f;
        p.turbulence = 1.3f;
        p.turbulenceSize = 1.5f;
        expectSameAsCpu(p);
    }
}

TEST(ParticleStep, SparkBouncesOffAWall) {
    StepParticle p{ glm::vec3(0.0f), glm::vec3(-4.0f, 1.0f, 0.0f), 0.2f, false };
    particleBounce(p, Kind::Spark, glm::vec3(-1.0f, 0.5f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_NEAR(p.velocity.x, 4.0f * 0.35f, 1e-5f); // back out, a third as fast
    EXPECT_NEAR(p.velocity.y, 1.0f * 0.6f, 1e-5f);  // along the wall, slowed
    EXPECT_NEAR(p.position.x, -0.99f, 1e-5f);       // just in front of it
    EXPECT_FALSE(p.resting);
}

TEST(ParticleStep, FlakeComesToRestOnGroundSmokeSlides) {
    StepParticle flake{ glm::vec3(0.0f), glm::vec3(0.3f, -2.0f, 0.0f), 1.0f, false };
    particleBounce(flake, Kind::Flake, glm::vec3(0.0f, 0.4f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_TRUE(flake.resting);
    EXPECT_FLOAT_EQ(glm::length(flake.velocity), 0.0f);
    // Resting, it stays put whatever the wind.
    StepParams s;
    s.kind = Kind::Flake;
    const glm::vec3 at = flake.position;
    particleStep(flake, s, 0.1f, glm::vec3(5.0f, 0.0f, 0.0f), 1.0f);
    EXPECT_EQ(flake.position, at);

    StepParticle smoke{ glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 2.0f), 1.0f, false };
    particleBounce(smoke, Kind::Smoke, glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    EXPECT_NEAR(smoke.velocity.z, 0.0f, 1e-6f); // no bounce: slides along
    EXPECT_NEAR(smoke.velocity.y, 1.0f, 1e-6f);
}

TEST(ParticleStep, GpuLayersMayNowCollideAttractAndSwirl) {
    const auto layer = [](const char* extra) {
        return nlohmann::json::parse(std::string(R"({"x": {"layers": [{"kind": "spark", "gpu": true, "count": 5, )") + extra + "}]}}");
    };
    std::string error;
    EXPECT_TRUE(ParticleLibrary::validate(layer(R"("collide": true)"), &error)) << error;
    EXPECT_TRUE(ParticleLibrary::validate(layer(R"("attract": 2)"), &error)) << error;
    EXPECT_TRUE(ParticleLibrary::validate(layer(R"("turbulence": 1.5, "turbulenceSize": 3)"), &error)) << error;
    EXPECT_FALSE(ParticleLibrary::validate(layer(R"("turbulenceSize": 0)"), &error));
    EXPECT_FALSE(ParticleLibrary::validate(layer(R"("onDeath": "sparks")"), &error)); // still the CPU's
    EXPECT_NE(error.find("onDeath"), std::string::npos);
}

TEST(ParticleStep, BuiltInSteppedEffects) {
    ParticleSim sim;
    ParticleLibrary library(sim);
    ASSERT_TRUE(library.has("fireflies"));
    ASSERT_TRUE(library.has("spark_shower"));
    EXPECT_GT(library.effect("fireflies")->layers[0].turbulence, 0.0f);
    EXPECT_TRUE(library.effect("spark_shower")->layers[0].collide);
    EXPECT_TRUE(library.effect("spark_shower")->layers[0].gpu);
}
