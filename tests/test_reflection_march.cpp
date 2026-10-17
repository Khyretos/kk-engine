#include "kke/ReflectionMarch.h"
#include "kke/Reflections.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <limits>
#include <vector>

// ENG-F02: the screen-space march of reflections (the CPU copy of
// shaders/reflect.glsl), on a ray-cast view: the camera 1.5 m over a floor,
// looking along -z at a wall 8 m away (2 m wide, 2.5 m high).

namespace {

struct Scene {
    int w = 320, h = 180;
    glm::mat4 proj;
    std::vector<float> z; // view-space z per pixel, 0 = sky
    Scene() {
        proj = glm::perspective(glm::radians(60.0f), float(w) / float(h), 0.1f, 100.0f);
        proj[1][1] *= -1.0f; // Vulkan's flipped Y, as the engine draws
        const glm::mat4 inv = glm::inverse(proj);
        z.resize(static_cast<size_t>(w * h));
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const glm::vec2 ndc = (glm::vec2(float(x), float(y)) + 0.5f) / glm::vec2(float(w), float(h)) * 2.0f - 1.0f;
                glm::vec4 p = inv * glm::vec4(ndc, 1.0f, 1.0f);
                const glm::vec3 d = glm::normalize(glm::vec3(p) / p.w);
                float best = std::numeric_limits<float>::max();
                if (d.y < 0.0f) best = -1.5f / d.y; // the floor
                if (d.z < 0.0f) {                    // the wall
                    const float t = -8.0f / d.z;
                    const glm::vec3 q = d * t;
                    if (std::abs(q.x) < 1.0f && q.y > -1.5f && q.y < 1.0f && t < best) best = t;
                }
                z[static_cast<size_t>(y * w + x)] = best < std::numeric_limits<float>::max() ? (d * best).z : 0.0f;
            }
    }
    kke::ReflectionMarchView view() const {
        kke::ReflectionMarchView v;
        v.width = w;
        v.height = h;
        v.proj = proj;
        const Scene* s = this;
        v.viewZ = [s](int x, int y) { return s->z[static_cast<size_t>(y * s->w + x)]; };
        return v;
    }
};

// The floor at point p reflects the view ray from the camera.
glm::vec3 mirrorOf(const glm::vec3& p) { return glm::reflect(glm::normalize(p), glm::vec3(0, 1, 0)); }

} // namespace

TEST(ReflectionMarch, TheFloorInFrontOfTheWallReflectsIt) {
    const Scene s;
    const glm::vec3 p(0.0f, -1.5f, -6.0f);
    const kke::ReflectionMarchResult r = kke::reflectionMarch(s.view(), p + glm::vec3(0, 0.01f, 0), mirrorOf(p), 48, 25.0f, 0.5f);
    ASSERT_TRUE(r.hit);
    EXPECT_FALSE(r.leftScreen);
    EXPECT_NEAR(s.view().viewZ(int(r.pixel.x), int(r.pixel.y)), -8.0f, 0.05f) << "it met the wall";
}

TEST(ReflectionMarch, ARayGoingOffScreenSaysSo) {
    // Close to the camera the reflection climbs over the wall and out of the top of the picture.
    const Scene s;
    const glm::vec3 p(0.0f, -1.5f, -2.0f);
    const kke::ReflectionMarchResult r = kke::reflectionMarch(s.view(), p + glm::vec3(0, 0.01f, 0), mirrorOf(p), 48, 25.0f, 0.5f);
    EXPECT_FALSE(r.hit);
    EXPECT_TRUE(r.leftScreen);
}

TEST(ReflectionMarch, BesideTheWallTheFloorSeesTheSky) {
    // Off to the side the reflected ray passes the wall and fades into the sky on screen.
    const Scene s;
    const glm::vec3 p(4.0f, -1.5f, -12.0f);
    const kke::ReflectionMarchResult r = kke::reflectionMarch(s.view(), p + glm::vec3(0, 0.01f, 0), mirrorOf(p), 48, 25.0f, 0.5f);
    EXPECT_FALSE(r.hit);
}

TEST(ReflectionMarch, ThinnerThanTheWallIsBehindIt) {
    // With no thickness to speak of the wall can't be met from in front of it in a few steps' gap:
    // the march needs its thickness to tell "behind the surface" from "past it".
    const Scene s;
    const glm::vec3 p(0.0f, -1.5f, -6.0f);
    EXPECT_FALSE(kke::reflectionMarch(s.view(), p + glm::vec3(0, 0.01f, 0), mirrorOf(p), 48, 25.0f, 0.0f).hit);
}

TEST(Reflections, NamesAndDefaults) {
    EXPECT_EQ(kke::reflectionsFromName("screen", kke::Reflections::Off), kke::Reflections::Screen);
    EXPECT_EQ(kke::reflectionsFromName("kke", kke::Reflections::Off), kke::Reflections::Kke);
    EXPECT_EQ(kke::reflectionsFromName("classic", kke::Reflections::Off), kke::Reflections::Classic);
    EXPECT_EQ(kke::reflectionsFromName("mirror", kke::Reflections::Screen), kke::Reflections::Screen);
    const kke::ReflectionSettings d;
    EXPECT_LT(d.maxRoughness, 0.5f);
    EXPECT_GT(d.steps, 16);
}
