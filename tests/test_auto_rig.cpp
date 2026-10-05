#include "kke/AutoRig.h"

#include "kke/AnimRig.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <string>
#include <vector>

using kke::ModelData;

namespace {

// A UE-style skeleton like UAL's mannequin: T-pose, facing -Z (its left
// at -X), 1.83 m tall.
ModelData referenceSkeleton() {
    ModelData m;
    std::vector<glm::vec3> world;
    auto add = [&](const std::string& name, int parent, glm::vec3 pos) {
        kke::ModelBone b;
        b.name = name;
        b.parent = parent;
        b.localRest = glm::translate(glm::mat4(1.0f), parent >= 0 ? pos - world[size_t(parent)] : pos);
        m.bones.push_back(b);
        world.push_back(pos);
        return static_cast<int>(m.bones.size() - 1);
    };
    const int root = add("root", -1, { 0, 0, 0 });
    const int pelvis = add("pelvis", root, { 0, 0.917f, 0.05f });
    const int s1 = add("spine_01", pelvis, { 0, 1.051f, 0.016f });
    const int s2 = add("spine_02", s1, { 0, 1.174f, 0 });
    const int s3 = add("spine_03", s2, { 0, 1.315f, 0.005f });
    const int neck = add("neck_01", s3, { 0, 1.488f, 0.011f });
    add("Head", neck, { 0, 1.569f, -0.005f });
    for (int side : { -1, 1 }) { // _l at -X
        const std::string sfx = side < 0 ? "_l" : "_r";
        const float x = static_cast<float>(side);
        const int clav = add("clavicle" + sfx, s3, { 0.019f * x, 1.458f, -0.071f });
        const int upper = add("upperarm" + sfx, clav, { 0.192f * x, 1.441f, 0.065f });
        const int lower = add("lowerarm" + sfx, upper, { 0.466f * x, 1.441f, 0.07f });
        const int hand = add("hand" + sfx, lower, { 0.739f * x, 1.441f, 0.065f });
        const int mid = add("middle_01" + sfx, hand, { 0.86f * x, 1.441f, 0.06f });
        add("middle_04_leaf" + sfx, mid, { 0.971f * x, 1.441f, 0.065f });
        const int thigh = add("thigh" + sfx, pelvis, { 0.089f * x, 0.932f, 0 });
        const int calf = add("calf" + sfx, thigh, { 0.089f * x, 0.532f, 0 });
        const int foot = add("foot" + sfx, calf, { 0.089f * x, 0.104f, 0.036f });
        const int ball = add("ball" + sfx, foot, { 0.089f * x, 0.015f, -0.113f });
        add("ball_leaf" + sfx, ball, { 0.089f * x, 0.015f, -0.192f });
    }
    m.boundsMin = glm::vec3(-0.97f, 0.0f, -0.21f);
    m.boundsMax = glm::vec3(0.97f, 1.83f, 0.16f);
    return m;
}

// A tube of square rings from a to b (8 points a ring, `rings` rings).
void tube(kke::ModelMesh& mesh, glm::vec3 a, glm::vec3 b, float r, int rings) {
    const glm::vec3 axis = glm::normalize(b - a);
    const glm::vec3 side = std::abs(axis.y) > 0.9f ? glm::vec3(1, 0, 0) : glm::normalize(glm::cross(axis, glm::vec3(0, 1, 0)));
    const glm::vec3 other = glm::cross(axis, side);
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i <= rings; ++i)
        for (int k = 0; k < 8; ++k) {
            const float t = 6.2831853f * static_cast<float>(k) / 8.0f;
            kke::ModelVertex v;
            const glm::vec3 out = side * std::cos(t) + other * std::sin(t);
            v.position = glm::mix(a, b, static_cast<float>(i) / static_cast<float>(rings)) + out * r;
            v.normal = out;
            mesh.vertices.push_back(v);
        }
    for (int i = 0; i < rings; ++i)
        for (int k = 0; k < 8; ++k) {
            const uint32_t p = base + uint32_t(i * 8 + k), q = base + uint32_t(i * 8 + (k + 1) % 8);
            mesh.indices.insert(mesh.indices.end(), { p, q, p + 8, q, q + 8, p + 8 });
        }
}

// A 1.75 m person of tubes in an A-pose (arms 40 degrees down), facing
// +Z (her left at +X), as an OBJ export would have her: no bones.
ModelData aPosePerson() {
    ModelData m;
    m.meshes.emplace_back();
    kke::ModelMesh& mesh = m.meshes.back();
    for (float x : { -0.09f, 0.09f }) tube(mesh, { x, 0.0f, 0.0f }, { x, 0.9f, 0.0f }, 0.06f, 30);
    tube(mesh, { 0, 0.85f, 0 }, { 0, 1.48f, 0 }, 0.14f, 30);
    tube(mesh, { 0, 1.46f, 0 }, { 0, 1.75f, 0 }, 0.08f, 15);
    const float drop = glm::radians(40.0f);
    for (float x : { -1.0f, 1.0f }) {
        const glm::vec3 shoulder(0.19f * x, 1.42f, 0.0f);
        tube(mesh, shoulder, shoulder + glm::vec3(std::cos(drop) * x, -std::sin(drop), 0.0f) * 0.76f, 0.04f, 40);
    }
    for (kke::ModelVertex& v : mesh.vertices) {
        m.boundsMin = glm::min(m.boundsMin, v.position);
        m.boundsMax = glm::max(m.boundsMax, v.position);
    }
    return m;
}

int strongest(const kke::ModelVertex& v) {
    int best = 0;
    for (int k = 1; k < 4; ++k)
        if (v.weights[k] > v.weights[best]) best = k;
    return static_cast<int>(v.joints[best]);
}

} // namespace

TEST(AutoRig, FitsTheSkeletonAndLiftsTheArmsIntoTheTPose) {
    const ModelData ref = referenceSkeleton();
    ModelData body = aPosePerson();
    const kke::AutoRigReport r = kke::autoRigHumanoid(body, ref);
    ASSERT_TRUE(r.ok) << r.reason;
    EXPECT_TRUE(body.isSkinned());
    ASSERT_EQ(body.bones.size(), ref.bones.size());
    EXPECT_NEAR(r.armDropDegrees[0], 40.0f, 3.0f);
    EXPECT_NEAR(r.armDropDegrees[1], 40.0f, 3.0f);
    EXPECT_NEAR(r.scale, 1.75f / 1.83f, 0.01f);

    const std::vector<glm::mat4> rest = kke::computeRestPose(body);
    const int handL = body.findBone("hand_l"), handR = body.findBone("hand_r"), upperL = body.findBone("upperarm_l");
    // Turned to face as the reference does: her left is now at -X.
    EXPECT_LT(kke::modelForward(body).z, -0.9f);
    EXPECT_LT(rest[size_t(handL)][3].x, -0.4f);
    // The arms are up in the T: the hand at shoulder height.
    EXPECT_NEAR(rest[size_t(handL)][3].y, rest[size_t(upperL)][3].y, 0.03f);
    // Bone rotations are the reference's (here none), so clips copy over unchanged.
    EXPECT_NEAR(glm::length(glm::vec3(rest[size_t(handL)][0]) - glm::vec3(1, 0, 0)), 0.0f, 1e-4f);

    const kke::ModelMesh& mesh = body.meshes[0];
    const kke::ModelVertex* tip = nullptr;
    for (const kke::ModelVertex& v : mesh.vertices) {
        EXPECT_NEAR(v.weights.x + v.weights.y + v.weights.z + v.weights.w, 1.0f, 1e-4f);
        if (!tip || v.position.x < tip->position.x) tip = &v;
        // No point of one side moves with the other side's limbs.
        for (int k = 0; k < 4; ++k) {
            if (v.weights[k] <= 0.0f) continue;
            const std::string& name = body.bones[v.joints[k]].name;
            const bool rightBone = name.size() > 2 && name.compare(name.size() - 2, 2, "_r") == 0;
            const bool leftBone = name.size() > 2 && name.compare(name.size() - 2, 2, "_l") == 0;
            if (v.position.x < -0.02f) {
                EXPECT_FALSE(rightBone) << name;
            }
            if (v.position.x > 0.02f) {
                EXPECT_FALSE(leftBone) << name;
            }
        }
    }
    ASSERT_NE(tip, nullptr);
    EXPECT_EQ(strongest(*tip), handL); // the left fingertip, now out along -X at shoulder height
    EXPECT_NEAR(tip->position.y, rest[size_t(upperL)][3].y, 0.06f);
    EXPECT_NE(handR, -1);

    // The feet go with the feet.
    for (const kke::ModelVertex& v : mesh.vertices) {
        if (v.position.y > 0.03f) continue;
        const std::string& name = body.bones[size_t(strongest(v))].name;
        EXPECT_TRUE(name.rfind("foot", 0) == 0 || name.rfind("ball", 0) == 0 || name.rfind("calf", 0) == 0) << name;
    }
}

TEST(AutoRig, SaysWhyItCannotRig) {
    ModelData body = aPosePerson();
    ModelData noSkeleton;
    const kke::AutoRigReport r = kke::autoRigHumanoid(body, noSkeleton);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.reason.empty());
    EXPECT_FALSE(body.isSkinned()); // left as it was
}
