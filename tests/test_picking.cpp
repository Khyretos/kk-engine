#include "kke/Picking.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

TEST(Picking, CenterOfScreenLooksAtTarget) {
    glm::mat4 view = glm::lookAt(glm::vec3(0, 5, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    glm::mat4 proj = kke::engineProjection(60.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    kke::Ray r = kke::screenToRay({ 640, 360 }, { 1280, 720 }, view, proj);
    glm::vec3 toTarget = glm::normalize(glm::vec3(0, 0, 0) - glm::vec3(0, 5, 10));
    EXPECT_NEAR(glm::dot(r.direction, toTarget), 1.0f, 1e-4f);
    float t = kke::rayPlaneY(r, 0.0f);
    EXPECT_NEAR(glm::length(r.at(t)), 0.0f, 1e-3f);
}

TEST(Picking, TopOfScreenIsUpBottomIsDown) {
    // Guards the Vulkan Y flip: pixel row 0 is the top of the screen.
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    glm::mat4 proj = kke::engineProjection(60.0f, 1.0f, 0.1f, 100.0f);
    kke::Ray top = kke::screenToRay({ 400, 10 }, { 800, 800 }, view, proj);
    kke::Ray bottom = kke::screenToRay({ 400, 790 }, { 800, 800 }, view, proj);
    kke::Ray right = kke::screenToRay({ 790, 400 }, { 800, 800 }, view, proj);
    EXPECT_GT(top.direction.y, 0.1f);
    EXPECT_LT(bottom.direction.y, -0.1f);
    EXPECT_GT(right.direction.x, 0.1f);
}

TEST(Picking, RayPlaneParallelAndBehind) {
    kke::Ray flat{ { 0, 1, 0 }, { 1, 0, 0 } };
    EXPECT_LT(kke::rayPlaneY(flat, 0.0f), 0.0f);
    kke::Ray up{ { 0, 1, 0 }, { 0, 1, 0 } };
    EXPECT_LT(kke::rayPlaneY(up, 0.0f), 0.0f);
}

TEST(Picking, RayAabbHitMissInside) {
    kke::Ray r{ { -5, 0.5f, 0.5f }, { 1, 0, 0 } };
    EXPECT_NEAR(kke::rayAabb(r, { 0, 0, 0 }, { 1, 1, 1 }), 5.0f, 1e-5f);
    kke::Ray miss{ { -5, 2.0f, 0.5f }, { 1, 0, 0 } };
    EXPECT_LT(kke::rayAabb(miss, { 0, 0, 0 }, { 1, 1, 1 }), 0.0f);
    kke::Ray inside{ { 0.5f, 0.5f, 0.5f }, { 0, 0, 1 } };
    EXPECT_FLOAT_EQ(kke::rayAabb(inside, { 0, 0, 0 }, { 1, 1, 1 }), 0.0f);
    kke::Ray away{ { -5, 0.5f, 0.5f }, { -1, 0, 0 } };
    EXPECT_LT(kke::rayAabb(away, { 0, 0, 0 }, { 1, 1, 1 }), 0.0f);
}

TEST(Picking, TransformAabbMatchesCorners) {
    glm::mat4 m = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(3, 0, 0)), glm::radians(45.0f), glm::vec3(0, 1, 0));
    glm::vec3 mn, mx;
    kke::transformAabb({ -1, 0, -1 }, { 1, 2, 1 }, m, mn, mx);
    float h = std::sqrt(2.0f);
    EXPECT_NEAR(mn.x, 3 - h, 1e-4f);
    EXPECT_NEAR(mx.x, 3 + h, 1e-4f);
    EXPECT_NEAR(mn.y, 0.0f, 1e-5f);
    EXPECT_NEAR(mx.y, 2.0f, 1e-5f);
    EXPECT_NEAR(mx.z, h, 1e-4f);
}

TEST(Picking, SnapTo) {
    EXPECT_FLOAT_EQ(kke::snapTo(1.26f, 0.5f), 1.5f);
    EXPECT_FLOAT_EQ(kke::snapTo(-0.74f, 0.5f), -0.5f);
    EXPECT_FLOAT_EQ(kke::snapTo(1.26f, 0.0f), 1.26f);
}
