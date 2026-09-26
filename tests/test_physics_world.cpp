// kke/PhysicsWorld.h: asking every physics engine at once (issue #31).
#include "kke/PhysicsWorld.h"

#include <gtest/gtest.h>

namespace {

// A world with one wall across the ray at `wallAt` (x), and a count of blasts.
class FakeWorld : public kke::IPhysicsWorld {
public:
    FakeWorld(const char* name, float wallAt, size_t bodies) : m_name(name), m_wall(wallAt), m_bodies(bodies) {}
    const char* physicsEngineName() const override { return m_name; }
    Stats physicsStats() const override { return { m_bodies, m_bodies / 2, 0.5 }; }
    Hit physicsRaycast(const glm::vec3& o, const glm::vec3& d, float maxDistance) const override {
        Hit h;
        if (d.x <= 0.0f) return h;
        const float t = (m_wall - o.x) / d.x;
        if (t < 0.0f || t > maxDistance) return h;
        h.hit = true;
        h.distance = t;
        h.point = o + d * t;
        h.normal = glm::vec3(-1, 0, 0);
        h.body = 7;
        h.world = this;
        return h;
    }
    size_t physicsBlast(const glm::vec3&, float, float) override { return ++blasts; }
    void physicsBoundsInBox(const glm::vec3&, const glm::vec3&, std::vector<std::pair<glm::vec3, glm::vec3>>&) const override {}
    size_t blasts = 0;

private:
    const char* m_name;
    float m_wall;
    size_t m_bodies;
};

} // namespace

TEST(PhysicsWorld, RaycastTakesTheClosestWorld) {
    FakeWorld femfx("FEMFX", 5.0f, 10), jolt("Jolt", 3.0f, 100);
    std::vector<kke::IPhysicsWorld*> worlds{ &femfx, &jolt };
    kke::IPhysicsWorld::Hit h = kke::physicsRaycast(worlds, glm::vec3(0.0f), glm::vec3(1, 0, 0), 10.0f);
    ASSERT_TRUE(h.hit);
    EXPECT_FLOAT_EQ(h.distance, 3.0f);
    EXPECT_EQ(h.world, &jolt);
    // Out of reach: nothing.
    EXPECT_FALSE(kke::physicsRaycast(worlds, glm::vec3(0.0f), glm::vec3(1, 0, 0), 2.0f).hit);
    // No worlds, or a null one: nothing, no crash.
    EXPECT_FALSE(kke::physicsRaycast({}, glm::vec3(0.0f), glm::vec3(1, 0, 0), 10.0f).hit);
    EXPECT_FALSE(kke::physicsRaycast({ nullptr }, glm::vec3(0.0f), glm::vec3(1, 0, 0), 10.0f).hit);
}

TEST(PhysicsWorld, BlastAndStatsReachEveryWorld) {
    FakeWorld femfx("FEMFX", 5.0f, 10), jolt("Jolt", 3.0f, 100);
    std::vector<kke::IPhysicsWorld*> worlds{ &femfx, nullptr, &jolt };
    EXPECT_EQ(kke::physicsBlast(worlds, glm::vec3(0.0f), 2.0f, 5.0f), 2u);
    EXPECT_EQ(femfx.blasts, 1u);
    EXPECT_EQ(jolt.blasts, 1u);
    const kke::IPhysicsWorld::Stats s = kke::physicsStats(worlds);
    EXPECT_EQ(s.bodies, 110u);
    EXPECT_EQ(s.awake, 55u);
    EXPECT_DOUBLE_EQ(s.stepMs, 1.0);
}

TEST(PhysicsWorld, BlastVelocityFadesWithDistance) {
    const glm::vec3 c(1, 0, 0);
    EXPECT_NEAR(glm::length(kke::blastVelocity(c, 4.0f, 10.0f, c + glm::vec3(0, 0, 2))), 5.0f, 1e-5f); // half way: half speed
    EXPECT_NEAR(kke::blastVelocity(c, 4.0f, 10.0f, c + glm::vec3(0, 0, 2)).z, 5.0f, 1e-5f);           // away from the centre
    EXPECT_EQ(kke::blastVelocity(c, 4.0f, 10.0f, c + glm::vec3(0, 0, 4.5f)), glm::vec3(0.0f));       // outside
    EXPECT_NEAR(kke::blastVelocity(c, 4.0f, 10.0f, c).y, 10.0f, 1e-5f);                               // centre: up
    EXPECT_EQ(kke::blastVelocity(c, 0.0f, 10.0f, c), glm::vec3(0.0f));
}

TEST(PhysicsWorld, RagdollProviderPicksTheBest) {
    struct Provider : kke::IRagdollPhysics {
        explicit Provider(int q) : quality(q) {}
        RagdollHandle createRagdoll(const kke::RagdollDesc&, const glm::vec3&) override { return 0; }
        void destroyRagdoll(RagdollHandle) override {}
        bool ragdollBodyTransforms(RagdollHandle, std::vector<glm::mat4>&) const override { return false; }
        void pushRagdollBody(RagdollHandle, int, const glm::vec3&) override {}
        int ragdollQuality() const override { return quality; }
        int quality;
    };
    Provider femfx(0), jolt(1);
    EXPECT_EQ(kke::bestRagdollPhysics({ &femfx, &jolt }), &jolt);
    EXPECT_EQ(kke::bestRagdollPhysics({ &jolt, &femfx }), &jolt);
    EXPECT_EQ(kke::bestRagdollPhysics({ &femfx }), &femfx);
    EXPECT_EQ(kke::bestRagdollPhysics({}), nullptr);
}
