#include "kke/Human.h"
#include "kke/HumanPack.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <stdexcept>

// MakeHuman's CC0 human (assets/humans/makehuman.kkhpack, cooked by
// tools/makehuman/cook_human.py): the pack loads whole, the sliders move
// the right targets, and a bake is a skinned model whose rest pose puts
// every vertex back where the sliders put it.

namespace {

const kke::HumanPack& pack() {
    static const kke::HumanPack p = [] {
        namespace fs = std::filesystem;
        const fs::path path = fs::path(KKE_SOURCE_DIR) / "assets/humans/makehuman.kkhpack";
        return fs::exists(path) ? kke::HumanPack::load(path.string()) : kke::HumanPack{};
    }();
    return p;
}

#define NEED_PACK()                                                                                                    \
    if (pack().positions().empty()) GTEST_SKIP() << "assets/humans/makehuman.kkhpack missing"

float height(const kke::Human& h) {
    const auto bake = h.bake();
    return bake.model.boundsMax.y - bake.model.boundsMin.y;
}

} // namespace

TEST(HumanPack, LoadsTheWholeHuman) {
    NEED_PACK();
    const kke::HumanPack& p = pack();
    EXPECT_EQ(p.positions().size(), 19158u);
    EXPECT_EQ(p.quads().size(), 18486u);
    EXPECT_EQ(p.targets().size(), 1280u);
    EXPECT_EQ(p.modifiers().size(), 291u);
    EXPECT_EQ(p.bones().size(), 163u);
    EXPECT_GE(p.findGroup("body"), 0);
    EXPECT_GE(p.findTarget("nose/nose-hump-incr"), 0);
    EXPECT_EQ(p.licence().rfind("CC0", 0), 0u);
    for (size_t b = 0; b < p.bones().size(); ++b) EXPECT_LT(p.bones()[b].parent, static_cast<int>(b)) << p.bones()[b].name;
    // The base mesh stands about 1.7 m tall once decimetres are metres.
    float lo = 1e9f, hi = -1e9f;
    for (const auto& v : p.positions()) {
        lo = std::min(lo, v.y);
        hi = std::max(hi, v.y);
    }
    EXPECT_GT(hi - lo, 1.4f);
    EXPECT_LT(hi - lo, 2.1f);
}

TEST(HumanPack, RejectsWhatIsNotAPack) {
    EXPECT_THROW(kke::HumanPack::parse({}, "empty"), std::runtime_error);
    std::vector<uint8_t> junk(64, 7);
    EXPECT_THROW(kke::HumanPack::parse(junk, "junk"), std::runtime_error);
    std::vector<uint8_t> cut = { 'K', 'K', 'H', 'P', 'A', 'C', 'K', '1', 1, 0, 0, 0, 3, 0, 0, 0, 'M', 'E', 'T', 'A', 99, 0, 0, 0 };
    EXPECT_THROW(kke::HumanPack::parse(cut, "cut"), std::runtime_error);
}

TEST(Human, TwoSidedSlidersPickTheirSide) {
    NEED_PACK();
    kke::Human h(pack());
    const auto weights = h.targetWeights(); // a copy: begin and end must come from one vector
    EXPECT_TRUE(std::none_of(weights.begin(), weights.end(),
                             [&](auto tw) { return pack().targets()[tw.first].name.rfind("nose/", 0) == 0; }));
    auto weightOf = [&](const std::string& target) {
        for (auto [t, w] : h.targetWeights())
            if (pack().targets()[t].name == target) return w;
        return 0.0f;
    };
    h.set("nose/nose-hump-decr|incr", 0.75f);
    EXPECT_FLOAT_EQ(weightOf("nose/nose-hump-incr"), 0.75f);
    EXPECT_FLOAT_EQ(weightOf("nose/nose-hump-decr"), 0.0f);
    h.set("nose/nose-hump-decr|incr", -3.0f); // clamped to -1
    EXPECT_FLOAT_EQ(h.get("nose/nose-hump-decr|incr"), -1.0f);
    EXPECT_FLOAT_EQ(weightOf("nose/nose-hump-decr"), 1.0f);
    EXPECT_EQ(h.values().size(), 1u);
    h.reset();
    EXPECT_TRUE(h.values().empty());
}

TEST(Human, MacroWeightsAddUp) {
    NEED_PACK();
    kke::Human h(pack());
    h.set("macrodetails/Gender", 0.3f);
    h.set("macrodetails/Age", 0.7f);
    h.set("macrodetails-universal/Muscle", 0.6f);
    h.set("macrodetails-universal/Weight", 0.2f);
    // The universal body targets are one per gender, age, muscle and weight
    // combination: their weights share out 1 between them.
    float universal = 0.0f, ancestry = 0.0f;
    for (auto [t, w] : h.targetWeights()) {
        const std::string& n = pack().targets()[t].name;
        if (n.rfind("macrodetails/universal-", 0) == 0) universal += w;
        if (n.rfind("macrodetails/a", 0) == 0 || n.rfind("macrodetails/caucasian", 0) == 0) ancestry += w;
    }
    EXPECT_NEAR(universal, 1.0f, 1e-4f);
    EXPECT_NEAR(ancestry, 1.0f, 1e-4f);
}

TEST(Human, AncestryAlwaysAddsUpToOne) {
    NEED_PACK();
    kke::Human h(pack());
    EXPECT_NEAR(h.get("macrodetails/African"), 1.0f / 3.0f, 1e-6f);
    h.set("macrodetails/African", 0.7f);
    EXPECT_NEAR(h.get("macrodetails/Asian"), 0.15f, 1e-5f);
    EXPECT_NEAR(h.get("macrodetails/Caucasian"), 0.15f, 1e-5f);
    h.set("macrodetails/Asian", 0.5f);
    EXPECT_NEAR(h.get("macrodetails/African") + h.get("macrodetails/Asian") + h.get("macrodetails/Caucasian"), 1.0f, 1e-5f);
    EXPECT_GT(h.get("macrodetails/African"), h.get("macrodetails/Caucasian"));
}

TEST(Human, BodyTypeSlidersChangeTheBody) {
    NEED_PACK();
    kke::Human h(pack());
    const float average = height(h);
    EXPECT_GT(average, 1.5f);
    EXPECT_LT(average, 1.95f);
    h.set("macrodetails/Age", 0.0f);
    EXPECT_LT(height(h), 0.95f); // a one-year-old
    h.set("macrodetails/Age", 0.5f);
    h.set("macrodetails-height/Height", 1.0f);
    const float tall = height(h);
    h.set("macrodetails-height/Height", 0.0f);
    EXPECT_GT(tall, height(h) + 0.15f);
    h.reset();
    h.set("macrodetails/Gender", 1.0f);
    const float man = height(h);
    h.set("macrodetails/Gender", 0.0f);
    EXPECT_GT(man, height(h));
}

TEST(Human, BakeIsASkinnedModelWhoseRestPoseIsTheShape) {
    NEED_PACK();
    kke::Human h(pack());
    h.set("macrodetails/Gender", 0.8f);
    h.set("macrodetails-universal/Muscle", 0.9f);
    h.set("head/head-square", 0.6f);
    const auto bake = h.bake();
    const kke::ModelData& m = bake.model;
    ASSERT_FALSE(m.meshes.empty());
    EXPECT_EQ(m.meshes[0].name, "body");
    EXPECT_EQ(m.bones.size(), 163u);
    EXPECT_TRUE(m.isSkinned());
    EXPECT_GT(m.triangleCount(), 26000u);

    std::vector<glm::mat4> rest(m.bones.size());
    for (size_t b = 0; b < m.bones.size(); ++b) rest[b] = m.bones[b].localRest;
    const std::vector<glm::mat4> skin = kke::computeSkinMatrices(m, rest);
    const std::vector<glm::vec3> shaped = h.shapedPositions();
    for (size_t mi = 0; mi < m.meshes.size(); ++mi) {
        const kke::ModelMesh& mesh = m.meshes[mi];
        ASSERT_EQ(bake.sourceVertex[mi].size(), mesh.vertices.size());
        for (size_t i = 0; i < mesh.vertices.size(); i += 97) {
            const kke::ModelVertex& v = mesh.vertices[i];
            EXPECT_NEAR(v.weights.x + v.weights.y + v.weights.z + v.weights.w, 1.0f, 1e-4f);
            EXPECT_LT(glm::length(v.position - shaped[bake.sourceVertex[mi][i]]), 1e-6f);
            glm::vec3 p, n;
            kke::skinVertex(v, skin, p, n);
            EXPECT_LT(glm::length(p - v.position), 1e-4f) << mesh.name << " vertex " << i;
        }
    }
    // The head sits on top of the neck, the feet at the bottom.
    const int head = m.findBone("head"), neck = m.findBone("neck01");
    ASSERT_GE(head, 0);
    ASSERT_GE(neck, 0);
    const auto world = kke::computeRestPose(m);
    EXPECT_GT(world[head][3].y, world[neck][3].y);
    EXPECT_GT(world[head][3].y, m.boundsMin.y + 0.8f * (m.boundsMax.y - m.boundsMin.y));
}

TEST(Human, ReshapeMatchesAFreshBake) {
    NEED_PACK();
    kke::Human h(pack());
    auto bake = h.bake();
    h.set("macrodetails-universal/Weight", 1.0f);
    h.set("nose/nose-scale-horiz-decr|incr", 0.8f);
    const auto start = std::chrono::steady_clock::now();
    h.reshape(bake);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const auto fresh = h.bake();
    ASSERT_EQ(fresh.model.meshes.size(), bake.model.meshes.size());
    for (size_t mi = 0; mi < fresh.model.meshes.size(); ++mi)
        for (size_t i = 0; i < fresh.model.meshes[mi].vertices.size(); i += 53) {
            EXPECT_LT(glm::length(fresh.model.meshes[mi].vertices[i].position - bake.model.meshes[mi].vertices[i].position), 1e-6f);
            EXPECT_LT(glm::length(fresh.model.meshes[mi].vertices[i].normal - bake.model.meshes[mi].vertices[i].normal), 1e-4f);
        }
    for (size_t b = 0; b < fresh.model.bones.size(); ++b)
        EXPECT_LT(glm::length(glm::vec3(fresh.model.bones[b].localRest[3] - bake.model.bones[b].localRest[3])), 1e-5f);
    // Fast enough to follow a dragged slider (a debug build, so generous).
    EXPECT_LT(ms, 500.0);
}
