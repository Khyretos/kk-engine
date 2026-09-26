#include "kke/JigglePhysics.h"

#include "kke/AnimRig.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <map>

using kke::ModelData;
using kke::Pose;

namespace {

kke::ModelBone bone(const char* name, int parent, const glm::mat4& local) {
    kke::ModelBone b;
    b.name = name;
    b.parent = parent;
    b.localRest = local;
    return b;
}

glm::mat4 at(float x, float y, float z) { return glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)); }

// A body at 1 m with a tail of three 0.2 m bones hanging along -Z.
ModelData tail() {
    ModelData m;
    m.bones.push_back(bone("Body", -1, at(0, 1, 0)));
    m.bones.push_back(bone("Tail1", 0, at(0, 0, -0.2f)));
    m.bones.push_back(bone("Tail2", 1, at(0, 0, -0.2f)));
    m.bones.push_back(bone("Tail3", 2, at(0, 0, -0.2f)));
    return m;
}

// A torso with a skinned mesh: a grid of vertices on its chest (z = 0.1),
// all weighted to the chest bone, geometry space = model space.
ModelData torso() {
    ModelData m;
    m.bones.push_back(bone("Hips", -1, at(0, 1, 0)));
    m.bones.push_back(bone("Chest", 0, at(0, 0.4f, 0)));
    m.bones.push_back(bone("UpperArm", 1, at(0.25f, 0.1f, 0)));
    const auto rest = kke::restModelTransforms(m);
    for (size_t b = 0; b < m.bones.size(); ++b) m.bones[b].inverseBind = glm::inverse(rest[b]);
    kke::ModelMesh mesh;
    mesh.skinned = true;
    const int n = 11;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            kke::ModelVertex v;
            v.position = glm::vec3(-0.25f + 0.5f * i / (n - 1), 1.15f + 0.5f * j / (n - 1), 0.1f);
            v.normal = glm::vec3(0, 0, 1);
            // The right edge column belongs to the arm (an arm against the chest).
            v.joints = glm::uvec4(i == n - 1 ? 2u : 1u, 0u, 0u, 0u);
            v.weights = glm::vec4(1, 0, 0, 0);
            mesh.vertices.push_back(v);
        }
    for (int j = 0; j + 1 < n; ++j)
        for (int i = 0; i + 1 < n; ++i) {
            const uint32_t a = j * n + i, b = a + 1, c = a + n, d = c + 1;
            mesh.indices.insert(mesh.indices.end(), { a, b, d, a, d, c });
        }
    m.meshes.push_back(mesh);
    kke::ModelAnimation anim;
    anim.name = "Idle";
    anim.duration = 1.0f;
    anim.frames.assign(2, { m.bones[0].localRest, m.bones[1].localRest, m.bones[2].localRest });
    m.animations.push_back(anim);
    return m;
}

Pose restPose(const ModelData& m) { return kke::AnimationSet(m).restPose(); }

glm::vec3 skinned(const ModelData& m, const kke::ModelVertex& v, const std::vector<glm::mat4>& modelBones) {
    glm::vec3 p(0.0f);
    for (int k = 0; k < 4; ++k)
        if (v.weights[k] > 0.0f) p += glm::vec3(modelBones[v.joints[k]] * m.bones[v.joints[k]].inverseBind * glm::vec4(v.position, 1.0f)) * v.weights[k];
    return p;
}

kke::JiggleSettings noGravity() {
    kke::JiggleSettings s;
    s.gravity = 0.0f;
    return s;
}

bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

} // namespace

TEST(Jiggle, ClockRunsFixedStepsAndCapsHitches) {
    kke::JiggleClock clock(100.0f, 4);
    EXPECT_EQ(clock.advance(0.025f), 2);
    EXPECT_NEAR(clock.alpha(), 0.5f, 1e-3f);
    EXPECT_EQ(clock.advance(1.0f), 4); // a one-second hitch doesn't run 100 steps
    EXPECT_EQ(clock.advance(0.0f), 0);
    EXPECT_EQ(clock.advance(-1.0f), 0);
}

TEST(Jiggle, ExtractRotationFindsTheRotationOfAStretchedMatrix) {
    const glm::quat truth = glm::angleAxis(1.1f, glm::normalize(glm::vec3(0.3f, 1.0f, -0.5f)));
    const glm::mat3 a = glm::mat3_cast(truth) * glm::mat3(glm::scale(glm::mat4(1.0f), glm::vec3(1.4f, 0.8f, 1.1f)));
    glm::quat q(1, 0, 0, 0);
    kke::extractRotation(a, q, 40);
    EXPECT_GT(std::abs(glm::dot(q, truth)), 0.999f);
}

TEST(Jiggle, StillRigStaysOnItsPoseAndSleeps) {
    ModelData m = tail();
    kke::JiggleRig rig(m, { { 1, {}, noGravity() } });
    ASSERT_EQ(rig.pointCount(), 4u); // three bones + the tip
    const Pose rest = restPose(m);
    Pose p = rest;
    for (int f = 0; f < 120; ++f) {
        p = rest;
        rig.apply(m, p, glm::mat4(1.0f), 1.0f / 60.0f);
    }
    const auto a = kke::poseToModel(m, rest), b = kke::poseToModel(m, p);
    for (size_t i = 0; i < a.size(); ++i) EXPECT_LT(glm::length(glm::vec3(a[i][3]) - glm::vec3(b[i][3])), 1e-4f);
    EXPECT_TRUE(rig.sleeping());
    const uint64_t steps = rig.stepsRun();
    p = rest;
    rig.apply(m, p, glm::mat4(1.0f), 1.0f / 60.0f);
    EXPECT_EQ(rig.stepsRun(), steps); // asleep: no work
}

TEST(Jiggle, TailLagsWhenTheBodyMovesThenSettles) {
    ModelData m = tail();
    kke::JiggleRig rig(m, { { 1, {}, noGravity() } });
    const Pose rest = restPose(m);
    Pose p = rest;
    rig.apply(m, p, glm::mat4(1.0f), 1.0f / 60.0f);
    // The body slides +X at 3 m/s for 0.2 s.
    float x = 0.0f;
    for (int f = 0; f < 12; ++f) {
        x += 3.0f / 60.0f;
        p = rest;
        rig.apply(m, p, at(x, 0, 0), 1.0f / 60.0f);
    }
    const glm::vec3 tip = glm::vec3(at(x, 0, 0) * kke::poseToModel(m, p)[3][3]);
    EXPECT_LT(tip.x, x - 0.02f); // trailing behind the body
    EXPECT_FALSE(rig.sleeping());
    // Bones keep their length (stretch is small).
    const auto w = kke::poseToModel(m, p);
    EXPECT_NEAR(glm::length(glm::vec3(w[3][3]) - glm::vec3(w[2][3])), 0.2f, 0.03f);
    // Stop: it swings back and settles on the pose.
    for (int f = 0; f < 360; ++f) {
        p = rest;
        rig.apply(m, p, at(x, 0, 0), 1.0f / 60.0f);
    }
    const glm::vec3 settled = glm::vec3(at(x, 0, 0) * kke::poseToModel(m, p)[3][3]);
    EXPECT_LT(glm::length(settled - glm::vec3(x, 1, -0.6f)), 0.005f);
}

TEST(Jiggle, GravityMakesItSag) {
    ModelData m = tail();
    kke::JiggleSettings s;
    s.gravity = 1.0f;
    s.stiffness = 0.05f;
    kke::JiggleRig rig(m, { { 1, {}, s } });
    const Pose rest = restPose(m);
    Pose p = rest;
    for (int f = 0; f < 240; ++f) {
        p = rest;
        rig.apply(m, p, glm::mat4(1.0f), 1.0f / 60.0f);
    }
    EXPECT_LT(kke::poseToModel(m, p)[3][3].y, 0.99f);
}

TEST(Jiggle, TeleportResetsInsteadOfWhipping) {
    ModelData m = tail();
    kke::JiggleRig rig(m, { { 1, {}, noGravity() } });
    const Pose rest = restPose(m);
    Pose p = rest;
    rig.apply(m, p, glm::mat4(1.0f), 1.0f / 60.0f);
    p = rest;
    rig.apply(m, p, at(50, 0, 0), 1.0f / 60.0f);
    const auto a = kke::poseToModel(m, rest), b = kke::poseToModel(m, p);
    EXPECT_LT(glm::length(glm::vec3(a[3][3]) - glm::vec3(b[3][3])), 1e-4f);
}

TEST(Jiggle, CollidersPushPointsOut) {
    ModelData m = tail();
    kke::JiggleSettings s = noGravity();
    s.radius = 0.02f;
    kke::JiggleRig rig(m, { { 1, {}, s } });
    const Pose rest = restPose(m);
    // A sphere sitting where the tail tip hangs.
    kke::JiggleCollider c;
    c.a = c.b = glm::vec3(0, 1, -0.6f);
    c.radius = 0.1f;
    Pose p = rest;
    for (int f = 0; f < 60; ++f) {
        p = rest;
        rig.apply(m, p, glm::mat4(1.0f), 1.0f / 60.0f, { c });
    }
    EXPECT_GE(glm::length(rig.pointPosition(3) - c.a), c.radius + s.radius - 1e-3f);
}

TEST(Jiggle, AddedBoneTakesNearbySkinAndChangesNothingAtRest) {
    ModelData m = torso();
    const auto restBefore = kke::restModelTransforms(m);
    std::vector<glm::vec3> before;
    for (const auto& v : m.meshes[0].vertices) before.push_back(skinned(m, v, restBefore));

    const glm::vec3 centre(0.1f, 1.4f, 0.1f);
    const int b = kke::addJiggleBone(m, 1, "Breast_L", centre, glm::vec3(0, 0, 1), 0.15f);
    ASSERT_EQ(b, 3);
    EXPECT_EQ(m.bones.size(), 4u);
    EXPECT_EQ(m.animations[0].frames[0].size(), 4u);
    EXPECT_EQ(kke::addJiggleBone(m, 99, "bad", centre, glm::vec3(0, 0, 1), 0.1f), -1);

    const auto rest = kke::restModelTransforms(m);
    // Local +Y of the new bone points along the axis (forward).
    EXPECT_GT(glm::dot(glm::normalize(glm::vec3(rest[b][1])), glm::vec3(0, 0, 1)), 0.999f);
    int taken = 0;
    for (size_t i = 0; i < m.meshes[0].vertices.size(); ++i) {
        const auto& v = m.meshes[0].vertices[i];
        EXPECT_NEAR(v.weights.x + v.weights.y + v.weights.z + v.weights.w, 1.0f, 1e-4f);
        EXPECT_LT(glm::length(skinned(m, v, rest) - before[i]), 1e-4f);
        bool usesNew = false;
        for (int k = 0; k < 4; ++k) usesNew |= v.joints[k] == static_cast<uint32_t>(b) && v.weights[k] > 0.0f;
        if (usesNew) {
            ++taken;
            EXPECT_LT(glm::length(before[i] - centre), 0.15f);
            EXPECT_NE(v.joints.x, 2u) << "the arm's skin must stay the arm's";
        }
    }
    EXPECT_GT(taken, 3);

    // Turning the new bone moves only its skin.
    Pose p = restPose(m);
    p[b].r = glm::angleAxis(0.4f, glm::vec3(1, 0, 0));
    const auto posed = kke::poseToModel(m, p);
    for (size_t i = 0; i < m.meshes[0].vertices.size(); ++i) {
        const float moved = glm::length(skinned(m, m.meshes[0].vertices[i], posed) - before[i]);
        if (glm::length(before[i] - centre) > 0.16f) {
            EXPECT_LT(moved, 1e-4f);
        }
    }
}

TEST(Jiggle, InflateSkinSwellsOnlyTheRegion) {
    ModelData m = torso();
    const glm::vec3 centre(-0.1f, 1.4f, 0.1f);
    std::vector<glm::vec3> before;
    for (const auto& v : m.meshes[0].vertices) before.push_back(v.position);
    const size_t moved = kke::inflateSkin(m, centre, 0.12f, 0.05f, glm::vec3(0, 0, 1), 0.7f);
    EXPECT_GT(moved, 3u);
    float most = 0.0f;
    for (size_t i = 0; i < before.size(); ++i) {
        const glm::vec3 d = m.meshes[0].vertices[i].position - before[i];
        if (glm::length(before[i] - centre) >= 0.12f) EXPECT_LT(glm::length(d), 1e-6f);
        else EXPECT_GE(d.z, -1e-6f); // outward (forward), never inward
        most = std::max(most, d.z);
        EXPECT_NEAR(glm::length(m.meshes[0].vertices[i].normal), 1.0f, 1e-3f);
    }
    EXPECT_NEAR(most, 0.05f, 0.01f);
}

TEST(Jiggle, SkinDisplacementFallsOffSmoothly) {
    std::vector<kke::SkinJiggleOffset> zones = { { glm::vec3(0), 0.2f, glm::vec3(0, 0.1f, 0) } };
    EXPECT_NEAR(kke::skinJiggleDisplacement(zones, glm::vec3(0)).y, 0.1f, 1e-6f);
    const float mid = kke::skinJiggleDisplacement(zones, glm::vec3(0.1f, 0, 0)).y;
    EXPECT_GT(mid, 0.0f);
    EXPECT_LT(mid, 0.1f);
    EXPECT_EQ(kke::skinJiggleDisplacement(zones, glm::vec3(0.25f, 0, 0)).y, 0.0f);
}

TEST(Jiggle, SkinZoneLagsAndStaysWithinItsLimit) {
    ModelData m = torso();
    kke::JiggleSkin::Zone z;
    z.bone = 0;
    z.radius = 0.15f;
    z.maxOffset = 0.03f;
    z.settings = noGravity();
    kke::JiggleSkin skin(m, { z }, { glm::vec3(0, 1.1f, 0.1f) });
    ASSERT_TRUE(skin.valid());
    const Pose rest = restPose(m);
    skin.apply(m, rest, glm::mat4(1.0f), 1.0f / 60.0f);
    EXPECT_LT(glm::length(skin.offsets()[0].offset), 1e-6f);
    float y = 0.0f, most = 0.0f;
    for (int f = 0; f < 10; ++f) {
        y += 4.0f / 60.0f; // a hop
        skin.apply(m, rest, at(0, y, 0), 1.0f / 60.0f);
        most = std::max(most, -skin.offsets()[0].offset.y);
        EXPECT_LE(glm::length(skin.offsets()[0].offset), 0.03f + 1e-5f);
    }
    EXPECT_GT(most, 0.005f); // the belly lags below the body going up
    EXPECT_NEAR(skin.offsets()[0].center.y, 1.1f, 1e-4f); // model space
}

TEST(Jiggle, JellySettlesAndABallBouncesOffIt) {
    kke::JellyBody::Params p;
    kke::JellyBody jelly(p);
    std::vector<kke::JellyBody::Ball> none;
    for (int f = 0; f < 120; ++f) jelly.step(1.0f / 60.0f, none);
    EXPECT_LT(jelly.deformation(), 0.02f); // holds its shape under its own weight

    kke::JellyBody::Ball ball;
    ball.pos = glm::vec3(0.1f, 1.4f, 0.0f);
    ball.radius = 0.1f;
    std::vector<kke::JellyBody::Ball> balls = { ball };
    float lowest = 10.0f, most = 0.0f;
    bool bounced = false, hit = false;
    for (int f = 0; f < 180; ++f) {
        jelly.step(1.0f / 60.0f, balls);
        lowest = std::min(lowest, balls[0].pos.y);
        most = std::max(most, jelly.deformation());
        if (balls[0].pos.y < p.max.y + 0.25f) hit = true;
        if (hit && balls[0].vel.y > 0.5f) bounced = true;
        ASSERT_TRUE(finite(balls[0].pos));
    }
    EXPECT_TRUE(bounced);
    EXPECT_GT(lowest, p.max.y - 0.25f); // dents the jelly, doesn't go through it
    EXPECT_GT(most, 0.01f);             // the jelly visibly deformed
    for (const glm::vec3& x : jelly.particles()) ASSERT_TRUE(finite(x));
}

TEST(Jiggle, JellySurfaceIsClosedAndFacesOut) {
    kke::JellyBody::Params p;
    kke::JellyBody jelly(p);
    jelly.buildSurface(6);
    EXPECT_EQ(jelly.surfaceIndices().size(), 6u * 6u * 6u * 2u * 3u);
    // Welded: every edge is shared by exactly two triangles.
    std::map<std::pair<uint32_t, uint32_t>, int> edges;
    const auto& idx = jelly.surfaceIndices();
    for (size_t t = 0; t < idx.size(); t += 3)
        for (int e = 0; e < 3; ++e) {
            uint32_t a = idx[t + e], b = idx[t + (e + 1) % 3];
            edges[{ std::min(a, b), std::max(a, b) }]++;
        }
    for (const auto& [e, n] : edges) EXPECT_EQ(n, 2);
    const glm::vec3 mid = (p.min + p.max) * 0.5f;
    for (size_t i = 0; i < jelly.surfacePositions().size(); ++i)
        EXPECT_GT(glm::dot(jelly.surfaceNormals()[i], jelly.surfacePositions()[i] - mid), 0.0f);
    // A poke dents the surface.
    jelly.poke(glm::vec3(0, p.max.y, 0), glm::vec3(0, -2.0f, 0), 0.3f);
    std::vector<kke::JellyBody::Ball> none;
    jelly.step(1.0f / 30.0f, none);
    jelly.deform();
    EXPECT_GT(jelly.deformation(), 0.001f);
}

TEST(Jiggle, JellyNeverSwallowsBalls) {
    kke::JellyBody::Params p;
    p.min = glm::vec3(-0.42f, 0.0f, -0.42f);
    p.max = glm::vec3(0.42f, 0.5f, 0.42f);
    p.cells = glm::ivec3(7, 4, 7);
    kke::JellyBody jelly(p);
    std::vector<kke::JellyBody::Ball> balls;
    uint32_t seed = 12345;
    auto r01 = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) * (1.0f / 16777216.0f);
    };
    for (int f = 0; f < 600; ++f) {
        if (f % 20 == 0 && balls.size() < 14) {
            kke::JellyBody::Ball b;
            b.radius = 0.05f + r01() * 0.07f;
            b.mass = 0.3f * std::pow(b.radius / 0.1f, 3.0f);
            b.pos = glm::vec3((r01() - 0.5f) * 0.7f, 2.5f + r01(), (r01() - 0.5f) * 0.7f);
            b.vel = glm::vec3(0.0f, -3.0f, 0.0f); // fast: ~8 m/s on arrival
            balls.push_back(b);
        }
        jelly.step(1.0f / 60.0f, balls);
        for (const auto& b : balls) {
            ASSERT_TRUE(finite(b.pos));
            const bool over = std::abs(b.pos.x) < 0.3f && std::abs(b.pos.z) < 0.3f;
            // Dents, never more than ~20 cm into a 50 cm jelly.
            if (over) {
                EXPECT_GT(b.pos.y - b.radius, 0.3f) << "ball swallowed at frame " << f << " y " << b.pos.y << " r " << b.radius << " vy " << b.vel.y << " deform " << jelly.deformation();
            }
        }
    }
}
