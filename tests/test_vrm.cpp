// VRM avatars (kke/Vrm.h, kke/VrmSprings.h) on VRoid's CC0 sample
// avatar shipped for the anime style (assets/anime/, docs/ANIME.md).

#include "kke/Vrm.h"
#include "kke/VrmSprings.h"

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace kke;

namespace {

const VrmAvatar& female() {
    static VrmAvatar a = [] {
        const fs::path path = fs::path(KKE_SOURCE_DIR) / "assets/anime/HairSample_Female.vrm";
        return fs::exists(path) ? loadVrm(path.string()) : VrmAvatar{};
    }();
    return a;
}

#define NEED_AVATAR() \
    if (female().model.meshes.empty()) GTEST_SKIP() << "assets/anime/HairSample_Female.vrm missing"

} // namespace

TEST(Vrm, ReadsTheAvatarItsFaceAndItsMaterials) {
    NEED_AVATAR();
    const VrmAvatar& a = female();
    EXPECT_EQ(a.meta.licence, "CC0");
    EXPECT_GE(a.bone("hips"), 0);
    EXPECT_GE(a.bone("head"), 0);
    EXPECT_GE(a.bone("leftUpperArm"), 0);
    ASSERT_GE(a.expression("blink"), 0);
    ASSERT_GE(a.expression("joy"), 0);
    EXPECT_FALSE(a.expressions[static_cast<size_t>(a.expression("blink"))].binds.empty());
    EXPECT_FALSE(a.morphs.empty());
    EXPECT_FALSE(a.springs.empty());
    EXPECT_FALSE(a.colliderGroups.empty());
    ASSERT_EQ(a.toon.size(), a.model.materials.size());
    bool shaded = false;
    for (const VrmToon& t : a.toon) shaded |= !t.mainTexture.empty();
    EXPECT_TRUE(shaded);
    EXPECT_GT(a.eyeHeight, 1.2f);
    EXPECT_LT(a.eyeHeight, 1.8f);
}

TEST(Vrm, FacesPlusZ) {
    NEED_AVATAR();
    const VrmAvatar& a = female();
    // The toes are ahead of the heels, the face ahead of the back of the head.
    std::vector<std::vector<ModelVertex>> posed;
    a.pose(a.restLocals(), {}, posed);
    float front = -1e9f, back = 1e9f;
    for (size_t m = 0; m < posed.size(); ++m)
        for (const ModelVertex& v : posed[m])
            if (v.position.y > a.eyeHeight - 0.05f) {
                front = std::max(front, v.position.z);
                back = std::min(back, v.position.z);
            }
    EXPECT_GT(front, -back * 0.6f) << "the face should be on the +Z side";
    // Posed at rest gives the vertices as loaded.
    const std::vector<ModelVertex>& v0 = a.model.meshes[0].vertices;
    ASSERT_EQ(posed[0].size(), v0.size());
    for (size_t i = 0; i < v0.size(); i += 97) EXPECT_LT(glm::length(posed[0][i].position - v0[i].position), 1e-4f);
}

TEST(Vrm, BlinkClosesTheEyes) {
    NEED_AVATAR();
    const VrmAvatar& a = female();
    std::vector<float> ex(a.expressions.size(), 0.0f);
    ex[static_cast<size_t>(a.expression("blink"))] = 1.0f;
    const std::vector<float> w = a.weights(ex);
    std::vector<std::vector<ModelVertex>> open, shut;
    a.pose(a.restLocals(), {}, open);
    a.pose(a.restLocals(), w, shut);
    float moved = 0.0f;
    for (size_t m = 0; m < open.size(); ++m)
        for (size_t i = 0; i < open[m].size(); ++i) moved = std::max(moved, glm::length(open[m][i].position - shut[m][i].position));
    EXPECT_GT(moved, 0.003f); // the upper lid comes down millimetres, not nothing
    EXPECT_LT(moved, 0.05f);
}

TEST(VrmSprings, HairSwingsWhenTheFigureTurnsAndSettles) {
    NEED_AVATAR();
    const VrmAvatar& a = female();
    VrmSprings springs(a);
    ASSERT_GT(springs.joints(), 10u);
    std::vector<glm::mat4> rest = a.restLocals();
    std::vector<glm::mat4> locals = rest;
    springs.reset(locals, glm::mat4(1.0f));
    // A quick turn of the figure: the hair lags, so some spring bone turns.
    float swung = 0.0f;
    for (int f = 0; f < 30; ++f) {
        locals = rest;
        const glm::mat4 figure = glm::rotate(glm::mat4(1.0f), 0.08f * float(f), glm::vec3(0, 1, 0));
        springs.step(1.0f / 60.0f, locals, figure);
        for (size_t b = 0; b < locals.size(); ++b) {
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r) ASSERT_TRUE(std::isfinite(locals[b][c][r]));
            swung = std::max(swung, glm::length(glm::vec3(locals[b][1]) - glm::vec3(rest[b][1])));
        }
    }
    EXPECT_GT(swung, 0.01f);
    // Held still, it settles near rest (gravity may hold it a little off).
    const glm::mat4 still = glm::rotate(glm::mat4(1.0f), 2.4f, glm::vec3(0, 1, 0));
    std::vector<glm::mat4> a1 = rest, a2 = rest;
    for (int f = 0; f < 300; ++f) {
        a1 = rest;
        springs.step(1.0f / 60.0f, a1, still);
    }
    a2 = rest;
    springs.step(1.0f / 60.0f, a2, still);
    float change = 0.0f;
    for (size_t b = 0; b < a1.size(); ++b) change = std::max(change, glm::length(glm::vec3(a1[b][1]) - glm::vec3(a2[b][1])));
    EXPECT_LT(change, 0.002f);
}
