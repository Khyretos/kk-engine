#include "kke/Refraction.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

// ENG-F02 part 4: where the scene behind a glass surface is read. The camera
// is at the origin looking along -z; a glass pane 5 m away; a wall 8 m away
// (all the scene image shows, unless a test puts something nearer).

namespace {

struct View {
    glm::mat4 viewProj;
    View() {
        glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
        proj[1][1] *= -1.0f; // Vulkan's flipped Y
        viewProj = proj * glm::lookAt(glm::vec3(0.0f), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    }
    glm::vec2 uvOf(const glm::vec3& p) const {
        const glm::vec4 c = viewProj * glm::vec4(p, 1.0f);
        return glm::vec2(c) / c.w * 0.5f + 0.5f;
    }
};

float wall(const glm::vec2&) { return 8.0f; }

} // namespace

TEST(Refraction, FaceOnGlassDoesNotBend) {
    const View v;
    const glm::vec3 p(0.0f, 0.0f, -5.0f);
    const kke::RefractionSample s = kke::refractedSample(v.viewProj, glm::vec3(0.0f), p, glm::vec3(0, 0, 1), 1.5f, wall);
    EXPECT_TRUE(s.bent);
    EXPECT_NEAR(s.uv.x, v.uvOf(p).x, 1e-4f);
    EXPECT_NEAR(s.uv.y, v.uvOf(p).y, 1e-4f);
}

TEST(Refraction, TiltedGlassBendsTowardItsInside) {
    // The pane faces the camera turned 30 degrees about y: the ray bends toward -normal, so to the left on screen.
    const View v;
    const glm::vec3 p(0.0f, 0.0f, -5.0f);
    const glm::vec3 n = glm::normalize(glm::vec3(std::sin(glm::radians(30.0f)), 0.0f, std::cos(glm::radians(30.0f))));
    const kke::RefractionSample s = kke::refractedSample(v.viewProj, glm::vec3(0.0f), p, n, 1.5f, wall);
    EXPECT_TRUE(s.bent);
    EXPECT_LT(s.uv.x, v.uvOf(p).x - 0.01f);
    EXPECT_NEAR(s.uv.y, v.uvOf(p).y, 1e-3f);
}

TEST(Refraction, AirDoesNotBend) {
    const View v;
    const glm::vec3 p(0.0f, 0.0f, -5.0f);
    const glm::vec3 n = glm::normalize(glm::vec3(0.5f, 0.0f, 1.0f));
    const kke::RefractionSample s = kke::refractedSample(v.viewProj, glm::vec3(0.0f), p, n, 1.0f, wall);
    EXPECT_NEAR(s.uv.x, v.uvOf(p).x, 1e-4f);
}

TEST(Refraction, SomethingInFrontOfTheGlassIsNotSeenThroughIt) {
    // Left of the glass point, a post 3 m away (nearer than the glass): the bent sample lands on it.
    const View v;
    const glm::vec3 p(0.0f, 0.0f, -5.0f);
    const glm::vec3 n = glm::normalize(glm::vec3(std::sin(glm::radians(30.0f)), 0.0f, std::cos(glm::radians(30.0f))));
    const float straight = v.uvOf(p).x;
    auto post = [straight](const glm::vec2& uv) { return uv.x < straight - 0.005f ? 3.0f : 8.0f; };
    const kke::RefractionSample s = kke::refractedSample(v.viewProj, glm::vec3(0.0f), p, n, 1.5f, post);
    EXPECT_FALSE(s.bent);
    EXPECT_NEAR(s.uv.x, straight, 1e-4f);
}

TEST(Refraction, TheSkyBehindStillBends) {
    // Nothing behind (0 = sky): the ray is followed maxDepth metres.
    const View v;
    const glm::vec3 p(0.0f, 0.0f, -5.0f);
    const glm::vec3 n = glm::normalize(glm::vec3(std::sin(glm::radians(30.0f)), 0.0f, std::cos(glm::radians(30.0f))));
    const kke::RefractionSample s = kke::refractedSample(v.viewProj, glm::vec3(0.0f), p, n, 1.5f, [](const glm::vec2&) { return 0.0f; });
    EXPECT_TRUE(s.bent);
    EXPECT_LT(s.uv.x, v.uvOf(p).x - 0.005f);
}

TEST(Refraction, AFartherWallBendsMore) {
    const View v;
    const glm::vec3 p(0.0f, 0.0f, -5.0f);
    const glm::vec3 n = glm::normalize(glm::vec3(std::sin(glm::radians(30.0f)), 0.0f, std::cos(glm::radians(30.0f))));
    const float closer = kke::refractedSample(v.viewProj, glm::vec3(0.0f), p, n, 1.5f, [](const glm::vec2&) { return 5.5f; }).uv.x;
    const float farther = kke::refractedSample(v.viewProj, glm::vec3(0.0f), p, n, 1.5f, wall).uv.x;
    EXPECT_LT(farther, closer);
}
