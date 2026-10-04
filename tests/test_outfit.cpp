#include "kke/Outfit.h"

#include <gtest/gtest.h>

using kke::BodyRegion;

namespace {

// A tiny skinned "person": one triangle per bone, each fully on its bone.
kke::ModelData person() {
    kke::ModelData m;
    for (const char* b : { "pelvis", "spine_01", "head", "upperarm_l", "lowerarm_l", "hand_l", "thigh_r", "foot_r" }) {
        kke::ModelBone bone;
        bone.name = b;
        m.bones.push_back(bone);
    }
    kke::ModelMesh mesh;
    mesh.skinned = true;
    for (uint32_t b = 0; b < m.bones.size(); ++b)
        for (int k = 0; k < 3; ++k) {
            kke::ModelVertex v;
            v.position = glm::vec3(static_cast<float>(b), static_cast<float>(k), 0.0f);
            v.joints = glm::uvec4(b, 0, 0, 0);
            v.weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size()));
            mesh.vertices.push_back(v);
        }
    m.meshes.push_back(mesh);
    m.materials.push_back({});
    return m;
}

} // namespace

TEST(Outfit, BonesDressTheRightPart) {
    EXPECT_EQ(kke::regionOfBone("head"), BodyRegion::Skin);
    EXPECT_EQ(kke::regionOfBone("Hand_L"), BodyRegion::Skin);
    EXPECT_EQ(kke::regionOfBone("index_01_r"), BodyRegion::Skin);
    EXPECT_EQ(kke::regionOfBone("spine_02"), BodyRegion::Top);
    EXPECT_EQ(kke::regionOfBone("upperarm_l"), BodyRegion::Top);
    EXPECT_EQ(kke::regionOfBone("lowerarm_l"), BodyRegion::Skin);
    EXPECT_EQ(kke::regionOfBone("lowerarm_l", true), BodyRegion::Top) << "long sleeves";
    EXPECT_EQ(kke::regionOfBone("thigh_r"), BodyRegion::Bottom);
    EXPECT_EQ(kke::regionOfBone("calf_l"), BodyRegion::Bottom);
    EXPECT_EQ(kke::regionOfBone("foot_r"), BodyRegion::Shoes);
    EXPECT_EQ(kke::regionOfBone("ball_l"), BodyRegion::Shoes);
}

TEST(Outfit, EveryTriangleKeptInItsRegionsColour) {
    const kke::ModelData m = person();
    kke::Outfit o;
    o.skin = glm::vec3(0.3f, 0.2f, 0.1f);
    o.top = glm::vec3(1.0f, 0.0f, 0.0f);
    const kke::ModelData d = kke::dressModel(m, o);
    size_t triangles = 0;
    for (const kke::ModelMesh& mesh : d.meshes) {
        triangles += mesh.indices.size() / 3;
        ASSERT_LT(mesh.material, d.materials.size());
        EXPECT_TRUE(d.materials[mesh.material].albedoTexture.empty());
        for (uint32_t i : mesh.indices) ASSERT_LT(i, mesh.vertices.size());
    }
    EXPECT_EQ(triangles, m.triangleCount()) << "nothing lost, nothing added";
    EXPECT_EQ(d.bones.size(), m.bones.size());
    // The head (bone 2) is skin, the spine (bone 1) the top.
    for (const kke::ModelMesh& mesh : d.meshes)
        for (const kke::ModelVertex& v : mesh.vertices) {
            if (v.joints.x == 2) {
                EXPECT_EQ(d.materials[mesh.material].baseColor, o.skin);
            }
            if (v.joints.x == 1) {
                EXPECT_EQ(d.materials[mesh.material].baseColor, o.top);
            }
            if (v.joints.x == 7) {
                EXPECT_EQ(d.materials[mesh.material].baseColor, o.shoes);
            }
        }
}

TEST(Outfit, SkinTonesSpanLightestToDeepest) {
    const auto& tones = kke::skinTones();
    ASSERT_GE(tones.size(), 8u);
    auto lum = [](const glm::vec3& c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; };
    for (size_t i = 1; i < tones.size(); ++i) EXPECT_LT(lum(tones[i].rgb), lum(tones[i - 1].rgb)) << tones[i].name;
    EXPECT_GT(lum(tones.front().rgb), 0.7f);
    EXPECT_LT(lum(tones.back().rgb), 0.2f);
}

TEST(Outfit, TheSameClothesHaveTheSameKey) {
    kke::Outfit a, b;
    EXPECT_EQ(kke::outfitKey(a), kke::outfitKey(b));
    b.shoes = kke::clothColours()[2].rgb;
    EXPECT_NE(kke::outfitKey(a), kke::outfitKey(b));
    b = a;
    b.sleeves = true;
    EXPECT_NE(kke::outfitKey(a), kke::outfitKey(b));
}
