#include "kke/AutoRig.h"

#include "kke/AnimRig.h"
#include "kke/JigglePhysics.h"
#include "kke/ModelAsset.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
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

// The jiggle demo's own body (assets/bodies/vitruvian_voluptuous.glb, CC0,
// tools/vitruvian/build_body.py): human-sized, one skin material, and the
// skeleton and skin weights Vitruvian's artists made (Mixamo's 52 bones), so it
// bends like the artist meant and takes UAL's clips by bone name.
TEST(VitruvianBody, ShippedBodyIsRiggedAndTakesTheAnimationLibrary) {
    namespace fs = std::filesystem;
    const fs::path bodyFile = fs::path(KKE_SOURCE_DIR) / "assets/bodies/vitruvian_voluptuous.glb";
    const fs::path ualFile = fs::path(KKE_SOURCE_DIR) / "assets/animations/UAL1_Standard.fbx";
    ASSERT_TRUE(fs::exists(bodyFile)) << bodyFile; // shipped in the repository: not optional
    ASSERT_TRUE(fs::exists(ualFile)) << ualFile;
    ModelData body = kke::loadModel(bodyFile.string());
    ModelData ual = kke::loadModel(ualFile.string());
    ASSERT_TRUE(body.isSkinned());
    EXPECT_GE(body.bones.size(), 52u);
    size_t vertices = 0, loose = 0;
    bool skin = false;
    for (const kke::ModelMesh& m : body.meshes) {
        vertices += m.vertices.size();
        if (m.material < body.materials.size() && body.materials[m.material].name == "Skin") skin = true; // the demo's head search needs it
        for (const kke::ModelVertex& v : m.vertices)
            if (std::abs(v.weights.x + v.weights.y + v.weights.z + v.weights.w - 1.0f) > 0.02f) ++loose;
    }
    EXPECT_TRUE(skin);
    EXPECT_GT(vertices, 30000u);
    EXPECT_EQ(loose, 0u) << "every vertex has skin weights adding up to 1";
    const float height = body.boundsMax.y - body.boundsMin.y;
    EXPECT_GT(height, 1.6f);
    EXPECT_LT(height, 1.9f);

    // Mixamo's names reach UAL's: nearly every bone takes the clips.
    const kke::BoneMatch match = kke::matchBones(ual, body);
    EXPECT_GE(match.matched, 50);
    for (const char* name : { "pelvis", "spine_03", "upperarm_l", "lowerarm_r", "thigh_l", "calf_r", "foot_l", "ball_r", "hand_l", "index_02_l" }) {
        bool found = false;
        for (const kke::ModelBone& b : body.bones) found = found || kke::canonicalBoneName(b.name) == name;
        EXPECT_TRUE(found) << name;
    }
    // The soft-tissue bones the demo adds: breasts and glutes, found on her.
    kke::HumanoidSoftTissue tissue;
    tissue.bust = tissue.glutes = tissue.hips = 0.0f; // her shape is in the mesh
    const kke::HumanoidJiggleSetup setup = kke::addHumanoidSoftTissue(body, tissue);
    EXPECT_GE(setup.chains.size(), 4u) << "two breast and two glute bones";
    EXPECT_GE(setup.zones.size(), 2u);
}

namespace {

// How far the front of her left breast travels (metres) when she bounces
// `amplitude` m at `hz`, with the jiggle bones and (zones) the skin zones.
struct BreastTravel { float bone = 0, zone = 0, together = 0; size_t zones = 0, breastZones = 0; };
BreastTravel breastTravel(const ModelData& shipped, bool zones, float amplitude, float hz) {
    ModelData body = shipped;
    kke::HumanoidSoftTissue tissue;
    tissue.bust = tissue.glutes = tissue.hips = 0.0f; // her shape is in the mesh
    tissue.breastZones = zones;
    const kke::HumanoidJiggleSetup setup = kke::addHumanoidSoftTissue(body, tissue);
    kke::JiggleRig rig(body, setup.chains);
    kke::JiggleSkin skin(body, setup.zones, setup.zonePositions);
    const kke::Pose rest = kke::AnimationSet(body).restPose();
    const std::vector<glm::mat4> restModel = kke::poseToModel(body, rest);
    // The probe: the most forward vertex of her left breast.
    const glm::vec3 fwd = kke::modelForward(body);
    const kke::ModelVertex* probe = nullptr;
    float best = -1e9f;
    for (const kke::ModelMesh& m : body.meshes)
        for (const kke::ModelVertex& v : m.vertices)
            if (v.position.y > 1.15f && v.position.y < 1.4f && v.position.x > 0.03f && glm::dot(v.position, fwd) > best) {
                best = glm::dot(v.position, fwd);
                probe = &v;
            }
    BreastTravel out;
    out.zones = setup.zones.size();
    out.breastZones = setup.breastZones.size();
    if (!probe) return out;
    auto skinned = [&](const std::vector<glm::mat4>& bones) {
        glm::vec3 p(0.0f);
        for (int k = 0; k < 4; ++k)
            if (probe->weights[k] > 0.0f) p += glm::vec3(bones[probe->joints[k]] * body.bones[probe->joints[k]].inverseBind * glm::vec4(probe->position, 1.0f)) * probe->weights[k];
        return p;
    };
    const glm::vec3 at0 = skinned(restModel);
    for (int f = 0; f < 360; ++f) {
        const float t = static_cast<float>(f) / 60.0f;
        kke::Pose pose = rest;
        const glm::mat4 toWorld = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, amplitude * std::sin(t * 6.2831853f * hz), 0.0f));
        rig.apply(body, pose, toWorld, 1.0f / 60.0f);
        skin.apply(body, pose, toWorld, 1.0f / 60.0f);
        const glm::vec3 p = skinned(kke::poseToModel(body, pose));
        const glm::vec3 d = kke::skinJiggleDisplacement(skin.offsets(), p);
        out.bone = std::max(out.bone, glm::length(p - at0));
        out.zone = std::max(out.zone, glm::length(d));
        out.together = std::max(out.together, glm::length(p + d - at0));
    }
    return out;
}

} // namespace

// Her breasts have to move when she jogs: a bone alone swings them about a
// point inside the breast (a centimetre of skin at most), the skin zones carry
// them as a whole and let them lag.
TEST(VitruvianBody, BreastsTravelWhenSheBounces) {
    namespace fs = std::filesystem;
    const fs::path bodyFile = fs::path(KKE_SOURCE_DIR) / "assets/bodies/vitruvian_voluptuous.glb";
    ASSERT_TRUE(fs::exists(bodyFile)) << bodyFile;
    const ModelData shipped = kke::loadModel(bodyFile.string());
    for (float hz : { 2.0f, 3.0f }) {
        const BreastTravel bones = breastTravel(shipped, false, 0.035f, hz);
        const BreastTravel both = breastTravel(shipped, true, 0.035f, hz);
        EXPECT_EQ(bones.breastZones, 0u);
        ASSERT_EQ(both.breastZones, 2u) << "a skin zone on each breast";
        EXPECT_EQ(both.zones, bones.zones + 2u);
        EXPECT_GT(bones.bone, 0.004f) << "the bones alone move the skin a little (measured 6-9 mm)";
        EXPECT_GT(both.zone, 0.004f) << "the zones move it too (measured 6-9 mm)";
        EXPECT_GT(both.together, 1.5f * bones.bone) << "the zones add at least half again to the bones' motion (measured about 1.8x)";
        EXPECT_GT(both.together, 0.010f) << "a centimetre of travel at " << hz << " Hz in a plain 3.5 cm bounce (the tour's sprint gives 2-3 cm)";
        EXPECT_LT(both.together, 0.06f) << "inside the zone's travel limit plus the bone's swing";
    }
}
