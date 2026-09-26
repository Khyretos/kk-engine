#include "kke/PhysicsBridge.h"

#include <gtest/gtest.h>
#include <glm/gtc/constants.hpp>

namespace {

kke::BridgeBox crate() {
    kke::BridgeBox b;
    b.center = glm::vec3(0.0f, 0.5f, 0.0f);
    b.halfExtents = glm::vec3(0.5f);
    b.movable = true;
    return b;
}

const glm::vec3 kGravity(0.0f, -9.81f, 0.0f);
constexpr float kDt = 1.0f / 60.0f;

} // namespace

TEST(PhysicsBridge, BoundsOfATurnedBox) {
    kke::BridgeBox b = crate();
    b.rotation = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3(0, 1, 0));
    glm::vec3 lo, hi;
    kke::boxBounds(b, lo, hi);
    EXPECT_NEAR(hi.x, 0.7071f, 1e-3f); // the corner sticks out
    EXPECT_NEAR(hi.y, 1.0f, 1e-5f);
    EXPECT_TRUE(kke::boundsOverlap(lo, hi, glm::vec3(0.6f, 0.0f, -0.1f), glm::vec3(0.8f, 0.2f, 0.1f)));
    EXPECT_FALSE(kke::boundsOverlap(lo, hi, glm::vec3(0.8f, 0.0f, -0.1f), glm::vec3(0.9f, 0.2f, 0.1f)));
}

TEST(PhysicsBridge, SurfaceNormalAndDistance) {
    const kke::BridgeBox b = crate();
    glm::vec3 n;
    float d;
    kke::boxSurface(b, glm::vec3(0.0f, 1.1f, 0.0f), n, d); // above the top
    EXPECT_NEAR(d, 0.1f, 1e-5f);
    EXPECT_NEAR(n.y, 1.0f, 1e-5f);
    kke::boxSurface(b, glm::vec3(0.45f, 0.5f, 0.0f), n, d); // just inside the +X face
    EXPECT_NEAR(d, -0.05f, 1e-5f);
    EXPECT_NEAR(n.x, 1.0f, 1e-5f);
}

// A shard lying still on the crate: the contact holds it up against
// gravity every step, so the crate carries its weight (m g dt per step).
TEST(PhysicsBridge, ARestingPiecePressesDown) {
    const kke::BridgeBox b = crate();
    std::vector<kke::BridgeVertex> v = { { glm::vec3(0.1f, 1.001f, 0.0f), glm::vec3(0.0f), 0.5f },
                                         { glm::vec3(-0.1f, 1.001f, 0.1f), glm::vec3(0.0f), 0.5f } };
    glm::vec3 at;
    const glm::vec3 j = kke::boxContactImpulse(b, v, kGravity, kDt, 0.01f, &at);
    EXPECT_NEAR(j.y, -1.0f * 9.81f * kDt, 1e-4f);
    EXPECT_NEAR(j.x, 0.0f, 1e-6f);
    EXPECT_NEAR(at.y, 1.001f, 1e-4f);
}

// A shard flying into the crate's side and stopping pushes it along.
TEST(PhysicsBridge, AHitPushesTheBoxAlong) {
    const kke::BridgeBox b = crate();
    // Moving -X at 4 m/s, stopped by the +X face (still falling as usual).
    std::vector<kke::BridgeVertex> v = { { glm::vec3(0.505f, 0.5f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f) + kGravity * kDt, 0.2f } };
    const glm::vec3 j = kke::boxContactImpulse(b, v, kGravity, kDt, 0.01f);
    EXPECT_NEAR(j.x, -0.8f, 1e-4f);
    EXPECT_NEAR(j.y, 0.0f, 1e-5f);
}

// Nothing near, or moving away on its own: no push.
TEST(PhysicsBridge, FarOrLeavingVerticesDoNothing) {
    const kke::BridgeBox b = crate();
    std::vector<kke::BridgeVertex> v = {
        { glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f), 1.0f },                          // far above
        { glm::vec3(0.0f, 1.001f, 0.0f), kGravity * kDt, 1.0f },                          // falling freely past the top
        { glm::vec3(0.505f, 0.5f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f) + kGravity * kDt, 1.0f }, // pulled into it by its own mesh
    };
    glm::vec3 at;
    const glm::vec3 j = kke::boxContactImpulse(b, v, kGravity, kDt, 0.01f, &at);
    EXPECT_EQ(j, glm::vec3(0.0f));
    EXPECT_EQ(at, b.center);
}
