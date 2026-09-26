#include "kke/Buoyancy.h"

#include <gtest/gtest.h>

namespace {

kke::WaterSurface still(float height) {
    return [height](float, float, glm::vec3& flow) {
        flow = glm::vec3(0.0f);
        return height;
    };
}

glm::vec3 total(const std::vector<kke::BuoyancyPoint>& pts) {
    glm::vec3 f(0.0f);
    for (const auto& p : pts) f += p.force;
    return f;
}

} // namespace

// Fully under water: lifted by the weight of the water it displaces.
TEST(Buoyancy, SubmergedBoxIsLiftedByItsVolumeOfWater) {
    kke::BuoyancyBox b;
    b.halfExtents = glm::vec3(0.5f); // 1 m^3
    b.center = glm::vec3(0.0f, -2.0f, 0.0f);
    kke::BuoyancySettings s;
    std::vector<kke::BuoyancyPoint> pts;
    EXPECT_NEAR(kke::boxBuoyancy(b, s, still(0.0f), pts), 1.0f, 1e-5f);
    EXPECT_EQ(pts.size(), 27u);
    EXPECT_NEAR(total(pts).y, 1000.0f * 9.81f, 1.0f);
}

TEST(Buoyancy, AboveTheWaterNothing) {
    kke::BuoyancyBox b;
    b.center = glm::vec3(0.0f, 2.0f, 0.0f);
    std::vector<kke::BuoyancyPoint> pts;
    EXPECT_EQ(kke::boxBuoyancy(b, kke::BuoyancySettings{}, still(0.0f), pts), 0.0f);
    EXPECT_TRUE(pts.empty());
}

// Half in: half the lift. A box of wood (500 kg/m^3) floats like that.
TEST(Buoyancy, HalfInHalfTheLift) {
    kke::BuoyancyBox b;
    b.halfExtents = glm::vec3(0.5f);
    kke::BuoyancySettings s;
    std::vector<kke::BuoyancyPoint> pts;
    EXPECT_NEAR(kke::boxBuoyancy(b, s, still(0.0f), pts), 0.5f, 1e-5f);
    EXPECT_NEAR(total(pts).y, 0.5f * 1000.0f * 9.81f, 1.0f);
}

// Drag: sinking through still water is slowed; a current pushes along.
TEST(Buoyancy, DragFollowsTheWater) {
    kke::BuoyancyBox b;
    b.halfExtents = glm::vec3(0.5f);
    b.center = glm::vec3(0.0f, -2.0f, 0.0f);
    b.velocity = glm::vec3(0.0f, -1.0f, 0.0f);
    kke::BuoyancySettings s;
    s.gravity = glm::vec3(0.0f); // drag alone
    std::vector<kke::BuoyancyPoint> pts;
    kke::boxBuoyancy(b, s, still(0.0f), pts);
    EXPECT_NEAR(total(pts).y, 1000.0f * s.drag, 1.0f);
    b.velocity = glm::vec3(0.0f);
    kke::boxBuoyancy(b, s, [](float, float, glm::vec3& flow) { flow = glm::vec3(0.5f, 0, 0); return 0.0f; }, pts);
    EXPECT_NEAR(total(pts).x, 1000.0f * s.drag * 0.5f, 1.0f);
}

// A tilted box gets pushes that turn it back upright (net torque).
TEST(Buoyancy, TiltedBoxGetsARightingTorque) {
    kke::BuoyancyBox b;
    b.halfExtents = glm::vec3(1.0f, 0.2f, 0.5f); // a raft
    b.rotation = glm::angleAxis(glm::radians(20.0f), glm::vec3(0, 0, 1)); // +X side up
    kke::BuoyancySettings s;
    std::vector<kke::BuoyancyPoint> pts;
    kke::boxBuoyancy(b, s, still(0.0f), pts);
    glm::vec3 torque(0.0f);
    for (const auto& p : pts) torque += glm::cross(p.point - b.center, p.force);
    EXPECT_LT(torque.z, 0.0f); // turning back (-Z), toward level
}
