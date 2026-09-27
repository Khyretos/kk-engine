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

namespace {

// Every guide's length now over its length at rest (follicle to tip): the most.
float mostStretched(const std::vector<glm::vec3>& p, const std::vector<glm::vec3>& rest, int per) {
    float worst = 0.0f;
    for (size_t g = 0; g < rest.size() / size_t(per); ++g) {
        float now = 0.0f, was = 0.0f;
        for (int k = 1; k + 1 < per; ++k) {
            now += glm::length(p[g * size_t(per) + size_t(k + 1)] - p[g * size_t(per) + size_t(k)]);
            was += glm::length(rest[g * size_t(per) + size_t(k + 1)] - rest[g * size_t(per) + size_t(k)]);
        }
        worst = std::max(worst, now / was);
    }
    return worst;
}

} // namespace

TEST(Hair, TypesGoFromStraightToTightCoils) {
    // Andre Walker's chart: 1 straight, 2 wavy (in the guides), 3 and 4
    // coiled (drawn), tighter and shrinking more from 3A to 4C.
    for (const char* n : { "1a", "1b" }) EXPECT_EQ(kke::hairStyle(n).curl + kke::hairStyle(n).coil, 0.0f) << n;
    for (const char* n : { "2a", "2b", "2c" }) {
        EXPECT_GT(kke::hairStyle(n).curl, 0.0f) << n;
        EXPECT_FALSE(kke::hairStyle(n).helix) << n;
    }
    const char* coiled[] = { "3a", "3b", "3c", "4a", "4b", "4c" };
    for (int i = 0; i < 6; ++i) {
        const kke::HairStyle s = kke::hairStyle(coiled[i]);
        EXPECT_EQ(s.name, coiled[i]);
        EXPECT_GT(s.coil, 0.0f);
        if (i == 0) continue;
        const kke::HairStyle was = kke::hairStyle(coiled[i - 1]);
        EXPECT_LE(s.coilRadius, was.coilRadius) << coiled[i];
        EXPECT_GE(s.coil, was.coil) << coiled[i];
        EXPECT_GE(s.shrinkage, was.shrinkage) << coiled[i];
    }
    EXPECT_NEAR(kke::hairStyle("4c").shrinkage, 0.75f, 1e-6f);
    EXPECT_GT(kke::hairStyle("4b").zigzag, 0.9f); // Z-shaped bends
}

TEST(Hair, EveryTypeAndHairstyleStaysOnATurningHead) {
    std::vector<std::string> names = kke::hairStyleNames();
    for (const std::string& n : kke::hairstyleNames()) names.push_back(n);
    for (const std::string& name : names) {
        kke::RigidWorld::Settings set;
        set.threads = 0;
        kke::RigidWorld world(set);
        const glm::vec3 neck(0.0f, 1.5f, 0.0f), centre(0.0f, 1.62f, 0.0f);
        kke::RigidWorld::BodyDesc b;
        b.shape = kke::RigidWorld::Shape::Sphere;
        b.motion = kke::RigidWorld::Motion::Kinematic;
        b.clothOnly = true;
        b.radius = 0.1f;
        b.position = centre;
        const auto body = world.add(b);
        kke::HairDesc d;
        ASSERT_TRUE(kke::hairstyleOnHead(d, name, centre, 0.1f, 120)) << name;
        const auto hair = world.addHair(d);
        const std::vector<glm::vec3> rest = kke::hairRestPose(d);
        // The rest pose is clear of the head.
        for (const glm::vec3& v : rest) EXPECT_GE(glm::length(v - centre), 0.1f - 1e-4f) << name;
        kke::HairStrands strands;
        d.style.hairsPerGuide = 4;
        strands.build(d);
        std::vector<glm::vec3> p;
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        int inside = 0;
        float drawnInside = 0.0f, far = 0.0f;
        for (int f = 0; f < 150; ++f) {
            const float t = float(f) / 60.0f;
            const glm::quat q = glm::angleAxis(0.9f * std::sin(t * 2.5f), glm::vec3(0, 1, 0)) * glm::angleAxis(0.3f * std::sin(t * 1.7f), glm::vec3(1, 0, 0));
            const glm::mat4 m = glm::translate(glm::mat4(1.0f), neck) * glm::mat4_cast(q) * glm::translate(glm::mat4(1.0f), -neck);
            world.moveKinematic(body, glm::vec3(m * glm::vec4(centre, 1.0f)), q, 1.0f / 60.0f);
            world.setHairJoint(hair, m);
            world.step(1.0f / 60.0f);
            ASSERT_TRUE(world.hairPositions(hair, p));
            const glm::vec3 c = glm::vec3(m * glm::vec4(centre, 1.0f));
            for (const glm::vec3& x : p) {
                ASSERT_TRUE(std::isfinite(x.x + x.y + x.z)) << name;
                if (glm::length(x - c) < 0.1f - 0.002f) ++inside;
            }
            if (f % 50 == 49) {
                strands.ribbons(p, m, c + glm::vec3(0.0f, 0.0f, 2.0f), 0.001f, v, idx);
                for (const kke::Vertex& x : v) {
                    ASSERT_TRUE(std::isfinite(x.position.x + x.position.y + x.position.z)) << name;
                    drawnInside = std::max(drawnInside, 0.1f - glm::length(x.position - c));
                    far = std::max(far, glm::length(x.position - c));
                }
            }
        }
        EXPECT_EQ(inside, 0) << name << ": guide vertices inside the head";
        EXPECT_LT(drawnInside, 0.001f) << name << ": drawn hairs inside the head";
        EXPECT_LT(far, 0.1f + d.style.length * 1.3f + 0.03f) << name << ": stray drawn hairs";
    }
}

TEST(Hair, CoilsSpringBackAndUnwindWhenPulled) {
    // A quick drop of the head pulls 3B ringlets out; they spring back.
    auto run = [](const char* style, float& most, float& after) {
        Head h(style, 80);
        const int per = kke::hairStrandVertices(h.desc.style);
        const std::vector<glm::vec3> rest = kke::hairRestPose(h.desc);
        std::vector<glm::vec3> p;
        most = 0.0f;
        for (int f = 0; f < 240; ++f) {
            const float t = float(f) / 60.0f;
            // Up, then yanked down fast, then still.
            const float y = t < 0.5f ? 0.0f : (t < 0.6f ? 0.3f * (t - 0.5f) / 0.1f : (t < 0.7f ? 0.3f - 0.6f * (t - 0.6f) / 0.1f : -0.3f));
            const glm::vec3 next = h.centre + glm::vec3(0.0f, y, 0.0f);
            h.world.moveKinematic(h.body, next, glm::quat(1, 0, 0, 0), 1.0f / 60.0f);
            h.world.setHairJoint(h.hair, glm::translate(glm::mat4(1.0f), next - h.centre));
            h.world.step(1.0f / 60.0f);
            h.world.hairPositions(h.hair, p);
            // Moved with the head, so compare lengths only.
            most = std::max(most, mostStretched(p, rest, per));
        }
        after = mostStretched(p, rest, per);
    };
    float coilMost, coilAfter, longMost, longAfter;
    run("3b", coilMost, coilAfter);
    run("long", longMost, longAfter);
    EXPECT_GT(coilMost, longMost + 0.12f); // the coils were pulled out, far more than straight hair
    EXPECT_LT(coilAfter, 1.1f);            // and sprang back
    EXPECT_LT(longMost, 1.2f);

    // Drawn, a coil unwinds as its guide is pulled: at full stretch it is straight.
    const kke::HairStyle s = kke::hairStyle("4c");
    kke::HairStrands::Hair hair;
    hair.coilTurns = 10.0f;
    const glm::vec3 t(0, -1, 0);
    const float atRest = glm::length(kke::HairStrands::coilOffset(s, hair, 0.5f, t, glm::vec4(1, 0, 0, 1.0f)));
    const float half = glm::length(kke::HairStrands::coilOffset(s, hair, 0.5f, t, glm::vec4(1, 0, 0, 2.0f)));
    const float straight = glm::length(kke::HairStrands::coilOffset(s, hair, 0.5f, t, glm::vec4(1, 0, 0, 1.0f / (1.0f - s.shrinkage))));
    EXPECT_GT(atRest, 0.3f * s.coilRadius);
    EXPECT_LT(half, atRest * 1.01f + 1e-6f);
    EXPECT_LT(straight, 1e-5f);
}

TEST(Hair, TiedHairGathersToItsTies) {
    kke::HairDesc d;
    const glm::vec3 c(0.0f, 1.6f, 0.0f);
    ASSERT_TRUE(kke::hairstyleOnHead(d, "puff", c, 0.1f, 200));
    ASSERT_EQ(d.ties.size(), 1u);
    const int per = kke::hairStrandVertices(d.style);
    std::vector<glm::vec3> rest = kke::hairRestPose(d);
    // Every tip is out beyond the tie, above the head: a ball on top.
    for (size_t g = 0; g < d.roots.size(); ++g) {
        const glm::vec3 tip = rest[g * size_t(per) + size_t(per - 1)];
        EXPECT_GT(tip.y, c.y + 0.05f) << "strand " << g;
    }
    ASSERT_TRUE(kke::hairstyleOnHead(d, "bantu knots", c, 0.1f, 200));
    EXPECT_EQ(d.ties.size(), 7u);
    rest = kke::hairRestPose(d);
    for (size_t g = 0; g < d.roots.size(); ++g) {
        const glm::vec3 tip = rest[g * size_t(per) + size_t(per - 1)];
        float nearest = 1e9f; // in knot sizes
        for (const kke::HairDesc::Tie& t : d.ties) nearest = std::min(nearest, glm::distance(tip, t.at) / t.size);
        EXPECT_LT(nearest, 1.5f) << "strand " << g << " ends away from every knot";
    }
}
