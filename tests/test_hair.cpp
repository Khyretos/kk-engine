#include "kke/Hair.h"
#include "kke/HairStrands.h"
#include "kke/RigidWorld.h"

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace {

struct Head {
    kke::RigidWorld world;
    kke::RigidWorld::BodyId body{};
    kke::RigidWorld::HairId hair = 0;
    kke::HairDesc desc;
    glm::vec3 neck{0.0f, 1.5f, 0.0f}, centre{0.0f, 1.62f, 0.0f};
    static constexpr float kRadius = 0.1f;

    explicit Head(const char* style, int guides) : world([] {
        kke::RigidWorld::Settings s;
        s.threads = 0;
        return s;
    }()) {
        kke::RigidWorld::BodyDesc b;
        b.shape = kke::RigidWorld::Shape::Sphere;
        b.motion = kke::RigidWorld::Motion::Kinematic;
        b.clothOnly = true;
        b.radius = kRadius;
        b.position = centre;
        body = world.add(b);
        desc.style = kke::hairStyle(style);
        kke::hairScalp(desc, centre, kRadius, guides);
        hair = world.addHair(desc);
    }
    // Turns and nods about the neck; returns the head's matrix.
    glm::mat4 move(float t, float dt) {
        const glm::quat q = glm::angleAxis(0.9f * std::sin(t * 2.5f), glm::vec3(0, 1, 0)) * glm::angleAxis(0.3f * std::sin(t * 1.7f), glm::vec3(1, 0, 0));
        const glm::mat4 m = glm::translate(glm::mat4(1.0f), neck) * glm::mat4_cast(q) * glm::translate(glm::mat4(1.0f), -neck);
        world.moveKinematic(body, glm::vec3(m * glm::vec4(centre, 1.0f)), q, dt);
        world.setHairJoint(hair, m);
        return m;
    }
};

} // namespace

TEST(Hair, ScalpPutsRootsOnTheHeadAwayFromTheFace) {
    kke::HairDesc d;
    kke::hairScalp(d, glm::vec3(0.0f, 1.6f, 0.0f), 0.1f, 200);
    ASSERT_EQ(d.roots.size(), 200u);
    for (size_t i = 0; i < d.roots.size(); ++i) {
        EXPECT_NEAR(glm::length(d.roots[i] - glm::vec3(0.0f, 1.6f, 0.0f)), 0.1f, 1e-4f);
        const glm::vec3 dir = d.directions[i];
        const float fromUp = std::acos(std::clamp(dir.y, -1.0f, 1.0f));
        EXPECT_FALSE(dir.z > 0.25f && fromUp > 1.0f) << "root " << i << " is on the face";
    }
}

TEST(Hair, StaysOutOfATurningHeadAndDoesNotStretch) {
    Head h("long", 160);
    const int per = kke::hairStrandVertices(h.desc.style);
    const std::vector<glm::vec3> rest = kke::hairRestPose(h.desc);
    std::vector<glm::vec3> p;
    float t = 0.0f;
    int worstInside = 0;
    float worstStretch = 0.0f;
    for (int f = 0; f < 300; ++f) {
        t += 1.0f / 60.0f;
        const glm::mat4 m = h.move(t, 1.0f / 60.0f);
        h.world.setWind(glm::vec3(3.0f, 0.0f, 0.0f));
        h.world.step(1.0f / 60.0f);
        ASSERT_TRUE(h.world.hairPositions(h.hair, p));
        ASSERT_EQ(p.size(), rest.size());
        const glm::vec3 c = glm::vec3(m * glm::vec4(h.centre, 1.0f));
        int inside = 0;
        for (const glm::vec3& v : p) {
            ASSERT_TRUE(std::isfinite(v.x + v.y + v.z));
            if (glm::length(v - c) < Head::kRadius - 0.002f) ++inside;
        }
        worstInside = std::max(worstInside, inside);
        for (size_t g = 0; g < rest.size() / size_t(per); ++g) {
            float now = 0.0f, was = 0.0f;
            for (int k = 1; k + 1 < per; ++k) {
                now += glm::length(p[g * size_t(per) + size_t(k + 1)] - p[g * size_t(per) + size_t(k)]);
                was += glm::length(rest[g * size_t(per) + size_t(k + 1)] - rest[g * size_t(per) + size_t(k)]);
            }
            worstStretch = std::max(worstStretch, now / was);
        }
    }
    EXPECT_EQ(worstInside, 0) << "guide vertices inside the head";
    EXPECT_LT(worstStretch, 1.12f);
    const kke::HairStats st = h.world.hairStats(h.hair);
    EXPECT_EQ(st.guides, 160u);
    EXPECT_EQ(st.vertices, uint32_t(160 * per));
}

TEST(Hair, WindBlowsItDownwind) {
    auto meanTipX = [](float wind) {
        Head h("long", 64);
        const int per = kke::hairStrandVertices(h.desc.style);
        for (int f = 0; f < 180; ++f) {
            h.world.setWind(glm::vec3(wind, 0.0f, 0.0f));
            h.world.step(1.0f / 60.0f);
        }
        std::vector<glm::vec3> p;
        h.world.hairPositions(h.hair, p);
        float x = 0.0f;
        for (size_t i = size_t(per) - 1; i < p.size(); i += size_t(per)) x += p[i].x;
        return x / float(p.size() / size_t(per));
    };
    EXPECT_GT(meanTipX(6.0f), meanTipX(0.0f) + 0.03f);
}

TEST(Hair, StrandsFollowTheirGuides) {
    Head h("wavy", 60);
    h.desc.style.hairsPerGuide = 8;
    kke::HairStrands strands;
    strands.build(h.desc);
    EXPECT_EQ(strands.hairs(), 60u * 8u);
    for (int f = 0; f < 60; ++f) h.world.step(1.0f / 60.0f);
    std::vector<glm::vec3> p;
    h.world.hairPositions(h.hair, p);
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    strands.ribbons(p, glm::mat4(1.0f), glm::vec3(0.0f, 1.6f, 2.0f), 0.001f, v, idx);
    ASSERT_EQ(v.size(), strands.hairs() * size_t(strands.pointsPerHair()) * 2u);
    ASSERT_EQ(idx.size(), strands.hairs() * size_t(strands.pointsPerHair() - 1) * 6u);
    // Every drawn point is near its head of hair (no stray vertices).
    for (const kke::Vertex& x : v) {
        ASSERT_TRUE(std::isfinite(x.position.x + x.position.y + x.position.z));
        EXPECT_LT(glm::length(x.position - h.centre), 0.1f + h.desc.style.length * 1.2f);
    }
}

TEST(Hair, RemovingHairLeavesNoBodies) {
    Head h("short", 200); // more than one part
    EXPECT_EQ(h.world.hairCount(), 1u);
    EXPECT_EQ(h.world.clothCount(), 0u);
    const size_t bodies = h.world.bodyCount();
    h.world.removeHair(h.hair);
    EXPECT_EQ(h.world.hairCount(), 0u);
    EXPECT_EQ(h.world.bodyCount(), bodies);
    std::vector<glm::vec3> p;
    EXPECT_FALSE(h.world.hairPositions(h.hair, p));
}
