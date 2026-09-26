#include "kke/CameraRig.h"

#include <gtest/gtest.h>

namespace {
// A wall at z = +1 (everything behind the character, who looks along -Z).
float wallBehind(const glm::vec3& from, const glm::vec3& dir, float maxD) {
    if (dir.z <= 1e-6f) return maxD;
    float d = (1.0f - from.z) / dir.z;
    return d >= 0.0f && d < maxD ? d : maxD;
}
float nothing(const glm::vec3&, const glm::vec3&, float maxD) { return maxD; }
} // namespace

TEST(CameraRig, FirstPersonSitsAtEyeHeight) {
    kke::CameraRig rig;
    rig.mode = kke::CameraRig::Mode::FirstPerson;
    rig.pitch = 0.0f;
    kke::Camera cam;
    rig.update(0.016f, glm::vec3(2, 0, 3), nothing, cam);
    EXPECT_NEAR(cam.position.y, rig.settings.eyeHeight, 1e-5f);
    EXPECT_NEAR(cam.target.z - cam.position.z, -1.0f, 1e-4f); // looking along -Z
}

TEST(CameraRig, SpringArmShortensAtWallsAndGrowsBackSmoothly) {
    kke::CameraRig rig;
    rig.settings.shoulderOffset = 0.0f;
    rig.settings.positionLag = 0.0f;
    rig.pitch = 0.0f;
    kke::Camera cam;
    rig.update(0.016f, glm::vec3(0.0f), nothing, cam);
    EXPECT_NEAR(rig.currentArmLength(), rig.settings.armLength, 1e-4f);
    // A wall 1 m behind: the arm snaps in, the camera stays in front of it.
    rig.update(0.016f, glm::vec3(0.0f), wallBehind, cam);
    EXPECT_LT(cam.position.z, 1.0f);
    EXPECT_NEAR(rig.currentArmLength(), 1.0f - rig.settings.probeRadius, 1e-3f);
    // Wall gone: the arm grows back at armReturnSpeed, not instantly.
    rig.update(0.1f, glm::vec3(0.0f), nothing, cam);
    EXPECT_LT(rig.currentArmLength(), rig.settings.armLength);
    for (int i = 0; i < 100; ++i) rig.update(0.1f, glm::vec3(0.0f), nothing, cam);
    EXPECT_NEAR(rig.currentArmLength(), rig.settings.armLength, 1e-4f);
}

TEST(CameraRig, PitchIsClampedAndMovementIsHorizontal) {
    kke::CameraRig rig;
    rig.addLook(90.0f, 500.0f);
    EXPECT_FLOAT_EQ(rig.pitch, rig.settings.pitchMax);
    EXPECT_NEAR(rig.forward().y, 0.0f, 1e-6f);
    EXPECT_NEAR(rig.forward().x, 1.0f, 1e-5f); // yaw 90 = +X
    EXPECT_NEAR(glm::dot(rig.forward(), rig.right()), 0.0f, 1e-5f);
}

TEST(CameraRig, CinematicPathPassesThroughKeyframes) {
    kke::CameraRig rig;
    rig.mode = kke::CameraRig::Mode::Cinematic;
    rig.setCinematic({ { { 0, 1, 0 }, { 0, 0, -5 }, 0.0f }, { { 4, 1, 0 }, { 0, 0, -5 }, 2.0f }, { { 8, 3, 0 }, { 0, 0, -5 }, 4.0f } }, false);
    kke::Camera cam;
    rig.update(2.0f, glm::vec3(0.0f), nothing, cam);
    EXPECT_NEAR(cam.position.x, 4.0f, 1e-3f);
    rig.update(10.0f, glm::vec3(0.0f), nothing, cam); // past the end: holds the last key
    EXPECT_NEAR(cam.position.x, 8.0f, 1e-3f);
    EXPECT_NEAR(cam.position.y, 3.0f, 1e-3f);
}
