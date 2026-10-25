#include "kke/Human.h"
#include "kke/HumanClothes.h"
#include "kke/HumanEyes.h"
#include "kke/HumanHair.h"
#include "kke/HumanLook.h"
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
    EXPECT_NEAR(m.boundsMin.y, 0.0f, 1e-5f); // standing on the floor

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

TEST(HumanLook, JsonKeepsEverything) {
    kke::HumanLook look;
    look.shape["nose/nose-hump-decr|incr"] = 0.25f;
    look.shape["macrodetails/Age"] = 0.7f;
    look.skin = { 0.4f, 0.3f, 0.2f };
    look.hair = "wavy";
    look.hairLength = 0.6f;
    look.clothes = { { "t-shirt", { 1.0f, 0.0f, 0.0f } }, { "tennis skirt", { 1.0f, 1.0f, 1.0f } } };
    kke::HumanLook back;
    ASSERT_TRUE(kke::lookFromJson(kke::lookToJson(look), back));
    ASSERT_EQ(back.clothes.size(), 2u);
    EXPECT_EQ(back.clothes[1].id, "tennis skirt");
    EXPECT_FLOAT_EQ(back.clothes[0].colour.r, 1.0f);
    EXPECT_EQ(back.shape, look.shape);
    EXPECT_NEAR(back.skin.r, 0.4f, 0.003f);
    EXPECT_EQ(back.hair, "wavy");
    EXPECT_FLOAT_EQ(back.hairLength, 0.6f);
    std::string error;
    EXPECT_FALSE(kke::lookFromJson("{\"format\":\"other\"}", back, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(kke::lookFromJson("not json", back));
}

TEST(HumanLook, ReadsMakeHumanSaves) {
    const kke::HumanLook look = kke::lookFromMhm("version v1.1.1\nmodifier nose/nose-hump-decr|incr 0.300000\n"
                                                 "modifier macrodetails/Gender 1.000000\nskinMaterial skins/default.mhmat\n");
    EXPECT_FLOAT_EQ(look.shape.at("nose/nose-hump-decr|incr"), 0.3f);
    EXPECT_FLOAT_EQ(look.shape.at("macrodetails/Gender"), 1.0f);
    EXPECT_EQ(look.shape.size(), 2u);
}

TEST(HumanLook, ShareCodeIsShortAndBuildsTheSameBody) {
    NEED_PACK();
    kke::Human h(pack());
    h.set("macrodetails/Gender", 0.83f);
    h.set("macrodetails/African", 0.6f);
    h.set("nose/nose-hump-decr|incr", -0.41f);
    h.set("eyes/r-eye-scale-decr|incr", 0.2f);
    kke::HumanLook look;
    kke::takeShape(h, look);
    look.hair = "afro";
    look.clothes = { { "shorts", { 0.2f, 0.4f, 0.8f } } };
    const std::string code = kke::shareCode(look, pack());
    EXPECT_EQ(code.rfind("kh1.", 0), 0u);
    EXPECT_LT(code.size(), 110u);

    kke::HumanLook theirs;
    ASSERT_TRUE(kke::fromShareCode(code, pack(), theirs));
    EXPECT_EQ(theirs.hair, "afro");
    ASSERT_EQ(theirs.clothes.size(), 1u);
    EXPECT_EQ(theirs.clothes[0].id, "shorts");
    EXPECT_NEAR(theirs.clothes[0].colour.b, 0.8f, 0.003f);
    for (const auto& [k, v] : look.shape) EXPECT_NEAR(theirs.shape.at(k), v, 2.0f / 254.0f + 1e-6f) << k;
    // The player's own body uses the rounded look too, so both machines
    // bake exactly the same vertices.
    const kke::HumanLook mine = kke::quantised(look, pack());
    kke::Human a(pack()), b(pack());
    kke::applyLook(a, mine);
    kke::applyLook(b, theirs);
    const auto pa = a.shapedPositions(), pb = b.shapedPositions();
    ASSERT_EQ(pa.size(), pb.size());
    EXPECT_TRUE(std::equal(pa.begin(), pa.end(), pb.begin()));

    std::string error;
    EXPECT_FALSE(kke::fromShareCode("kh1.!!", pack(), theirs, &error));
    EXPECT_FALSE(kke::fromShareCode("kh1." + code.substr(4, 6), pack(), theirs, &error));
    EXPECT_FALSE(kke::fromShareCode("hello", pack(), theirs, &error));
}

// A value a hair off the default rounds to the default, so the code read
// back and coded again is the same code (the creator's CI self-test found
// two codes for one look).
TEST(HumanLook, ShareCodeIsTheSameAfterARoundTrip) {
    NEED_PACK();
    kke::HumanLook look;
    look.shape["nose/nose-hump-decr|incr"] = 0.002f;
    look.shape["macrodetails/Gender"] = 0.7f;
    const std::string code = kke::shareCode(look, pack());
    kke::HumanLook back;
    ASSERT_TRUE(kke::fromShareCode(code, pack(), back));
    EXPECT_EQ(back.shape.count("nose/nose-hump-decr|incr"), 0u);
    EXPECT_EQ(kke::shareCode(back, pack()), code);
}

// Hair grows from the scalp the sliders made: above the brows and ears,
// behind the hairline, on a head of a believable size.
TEST(HumanHair, FindsTheScalp) {
    NEED_PACK();
    for (float gender : { 0.0f, 1.0f }) {
        kke::Human h(pack());
        h.set("macrodetails/Gender", gender);
        const std::vector<glm::vec3> shaped = h.shapedPositions();
        const kke::HumanScalp scalp = kke::findScalp(pack(), shaped);
        ASSERT_GT(scalp.verts.size(), 120u) << gender;
        EXPECT_GT(scalp.radius, 0.06f);
        EXPECT_LT(scalp.radius, 0.14f);
        EXPECT_GT(scalp.up.y, 0.9f);
        EXPECT_GT(scalp.front.z, 0.9f);
        const glm::vec3 eyes = 0.5f * (h.jointPosition("eye.L____head", shaped) + h.jointPosition("eye.R____head", shaped));
        for (uint32_t v : scalp.verts) {
            const glm::vec3 p = shaped[v] - eyes;
            ASSERT_FALSE(p.z > 0.0f && p.y < 0.03f) << "scalp on the face at vertex " << v;
            ASSERT_GT(p.y, -0.06f) << "scalp below the ears at vertex " << v;
        }
    }
}

TEST(HumanHair, RootsSitOnTheSkin) {
    NEED_PACK();
    kke::Human h(pack());
    h.set("head/head-scale-vert-decr|incr", 0.6f); // a taller head: the hair follows it
    const std::vector<glm::vec3> shaped = h.shapedPositions();
    const kke::HumanScalp scalp = kke::findScalp(pack(), shaped);
    for (const char* style : { "short", "long", "afro", "cornrows" }) {
        kke::HairDesc d;
        ASSERT_TRUE(kke::hairOnHuman(d, style, pack(), shaped, 160)) << style;
        kke::HairDesc sphere; // the same style on the scalp's sphere: most roots have skin under them
        ASSERT_TRUE(kke::hairstyleOnHead(sphere, style, scalp.centre, scalp.radius, 160, scalp.up, scalp.front));
        EXPECT_GE(d.roots.size(), sphere.roots.size() * 6 / 10) << style;
        EXPECT_EQ(d.directions.size(), d.roots.size());
        if (!d.lengths.empty()) {
            EXPECT_EQ(d.lengths.size(), d.roots.size());
        }
        if (!d.tiedTo.empty()) {
            EXPECT_EQ(d.tiedTo.size(), d.roots.size());
        }
        for (const glm::vec3& r : d.roots) {
            float nearest = 1e9f;
            for (uint32_t v : scalp.verts) nearest = std::min(nearest, glm::distance(r, shaped[v]));
            ASSERT_LT(nearest, 0.03f) << style; // on a scalp face (its corners are up to ~2.5 cm apart)
        }
    }
    kke::HairDesc d;
    EXPECT_FALSE(kke::hairOnHuman(d, "no such style", pack(), shaped, 160));
}

// Clothes are cut from the body suit and the long skirt every body
// carries: each garment has vertices, a top stays on the torso and arms,
// shoes on the feet, and a skirt is cloth pinned at its waistband.
TEST(HumanClothes, EveryGarmentHasItsPart) {
    NEED_PACK();
    kke::Human h(pack());
    const std::vector<glm::vec3> shaped = h.shapedPositions();
    for (const kke::GarmentInfo& g : kke::garments()) {
        const kke::GarmentShape s = kke::garmentShape(pack(), shaped, g.id);
        EXPECT_GT(s.verts.size(), 20u) << g.id;
        EXPECT_EQ(s.cloth, !s.pinned.empty()) << g.id;
    }
    const auto lowest = [&](const char* id) {
        float y = 1e9f;
        for (uint32_t v : kke::garmentShape(pack(), shaped, id).verts) y = std::min(y, shaped[v].y);
        return y;
    };
    const auto highest = [&](const char* id) {
        float y = -1e9f;
        for (uint32_t v : kke::garmentShape(pack(), shaped, id).verts) y = std::max(y, shaped[v].y);
        return y;
    };
    EXPECT_LT(highest("shoes"), 0.15f);                 // feet
    EXPECT_GT(lowest("t-shirt"), 0.7f);                 // down to the hips
    EXPECT_LT(lowest("trousers"), lowest("shorts") - 0.3f);
    EXPECT_LT(lowest("long skirt"), lowest("tennis skirt") - 0.4f);
    EXPECT_GT(lowest("tennis skirt"), 0.6f);            // above the knee
    EXPECT_GT(lowest("crop top"), lowest("t-shirt") + 0.05f); // the belly shows
    EXPECT_TRUE(kke::garmentShape(pack(), shaped, "no such garment").verts.empty());
}

// A game gets the person dressed as one skinned model: a mesh per
// garment on the body's own bones, in the garment's colour, every vertex
// outside the skin it covers.
TEST(HumanClothes, DressedBakeIsOneSkinnedModel) {
    NEED_PACK();
    kke::Human h(pack());
    h.set("macrodetails/Gender", 0.0f);
    kke::Human::Bake b = h.bake();
    const size_t bodyMeshes = b.model.meshes.size();
    const std::vector<kke::HumanLook::Worn> clothes = { { "t-shirt", { 1.0f, 0.0f, 0.0f } }, { "tennis skirt", {} }, { "shoes", {} } };
    kke::dress(b.model, h, clothes);
    ASSERT_EQ(b.model.meshes.size(), bodyMeshes + clothes.size());
    for (size_t i = 0; i < clothes.size(); ++i) {
        const kke::ModelMesh& m = b.model.meshes[bodyMeshes + i];
        EXPECT_EQ(m.name, clothes[i].id);
        EXPECT_TRUE(m.skinned);
        EXPECT_GT(m.indices.size(), 60u) << m.name;
        for (const kke::ModelVertex& v : m.vertices) {
            ASSERT_LT(v.joints.x, b.model.bones.size());
            ASSERT_NEAR(v.weights.x + v.weights.y + v.weights.z + v.weights.w, 1.0f, 1e-4f) << m.name;
        }
    }
    EXPECT_EQ(b.model.materials[b.model.meshes[bodyMeshes].material].baseColor, glm::vec3(1.0f, 0.0f, 0.0f));
    kke::dress(b.model, h, {}); // nothing worn: nothing added
    EXPECT_EQ(b.model.meshes.size(), bodyMeshes + clothes.size());
}

// The helper eyes are white balls: addIrises gives each a pupil, an iris
// and a rim, on the ball's front, skinned like the eye, and moves them
// again after a reshape instead of adding more.
TEST(HumanEyes, IrisesSitOnTheFrontOfEachEye) {
    NEED_PACK();
    kke::Human h(pack());
    kke::Human::Bake b = h.bake();
    const size_t before = b.model.meshes.size();
    ASSERT_TRUE(kke::addIrises(b.model, glm::vec3(0.2f, 0.4f, 0.7f)));
    ASSERT_EQ(b.model.meshes.size(), before + 3);
    float eyeFront = -1e9f;
    for (const kke::ModelMesh& m : b.model.meshes)
        if (m.name == "helper-l-eye")
            for (const kke::ModelVertex& v : m.vertices) eyeFront = std::max(eyeFront, v.position.z);
    const kke::ModelMesh& pupil = b.model.meshes[before];
    EXPECT_EQ(pupil.name, "eye-pupil");
    EXPECT_TRUE(pupil.skinned);
    float pupilFront = -1e9f;
    for (const kke::ModelVertex& v : pupil.vertices) {
        pupilFront = std::max(pupilFront, v.position.z);
        EXPECT_NEAR(v.weights.x + v.weights.y + v.weights.z + v.weights.w, 1.0f, 1e-4f);
    }
    EXPECT_GT(pupilFront, eyeFront);              // in front of the ball
    EXPECT_LT(pupilFront, eyeFront + 0.002f);     // but on it
    EXPECT_LT(b.model.materials[pupil.material].baseColor.b, 0.05f); // near black
    EXPECT_EQ(b.model.materials[b.model.meshes[before + 1].material].baseColor, glm::vec3(0.2f, 0.4f, 0.7f));

    h.set("macrodetails-height/Height", 1.0f);
    h.reshape(b);
    ASSERT_TRUE(kke::addIrises(b.model, glm::vec3(1.0f), /*update=*/true));
    EXPECT_EQ(b.model.meshes.size(), before + 3);
    float eyeTop = -1e9f, pupilTop = -1e9f;
    for (const kke::ModelVertex& v : b.model.meshes[before].vertices) pupilTop = std::max(pupilTop, v.position.y);
    for (const kke::ModelMesh& m : b.model.meshes)
        if (m.name == "helper-l-eye" || m.name == "helper-r-eye")
            for (const kke::ModelVertex& v : m.vertices) eyeTop = std::max(eyeTop, v.position.y);
    EXPECT_LT(pupilTop, eyeTop); // moved up with the taller body's eyes
    EXPECT_GT(pupilTop, eyeTop - 0.03f);
}

// addBrows grows each brow over its eye and the lashes along the lid,
// skinned like the face, the same count after a reshape so update moves
// them instead of adding more.
TEST(HumanEyes, BrowsAndLashesSitOverEachEye) {
    NEED_PACK();
    kke::Human h(pack());
    kke::Human::Bake b = h.bake();
    kke::ModelMesh skin;
    glm::vec3 eyeMin(1e9f), eyeMax(-1e9f);
    for (const kke::ModelMesh& m : b.model.meshes) {
        if (m.name == "body") skin = m;
        if (m.name == "helper-l-eye" || m.name == "helper-r-eye")
            for (const kke::ModelVertex& v : m.vertices) {
                eyeMin = glm::min(eyeMin, v.position);
                eyeMax = glm::max(eyeMax, v.position);
            }
    }
    const size_t before = b.model.meshes.size();
    ASSERT_TRUE(kke::addBrows(b.model, skin, glm::vec3(0.3f, 0.2f, 0.1f)));
    ASSERT_EQ(b.model.meshes.size(), before + 2);
    const kke::ModelMesh& brows = b.model.meshes[before];
    const kke::ModelMesh& lashes = b.model.meshes[before + 1];
    EXPECT_EQ(brows.name, "brows");
    EXPECT_EQ(lashes.name, "lashes");
    EXPECT_TRUE(brows.skinned);
    EXPECT_GT(brows.indices.size(), 1000u);
    EXPECT_GT(lashes.indices.size(), 300u);
    for (const kke::ModelVertex& v : brows.vertices) {
        EXPECT_GT(v.position.y, eyeMax.y - 0.002f); // above the eyes
        EXPECT_LT(v.position.y, eyeMax.y + 0.04f);  // but on the brow, not the forehead
        EXPECT_NEAR(v.weights.x + v.weights.y + v.weights.z + v.weights.w, 1.0f, 1e-4f);
    }
    for (const kke::ModelVertex& v : lashes.vertices) {
        EXPECT_GT(v.position.z, eyeMin.z); // in front of the eyes' backs
        EXPECT_GT(v.position.y, (eyeMin.y + eyeMax.y) * 0.5f - 0.004f); // the upper lids
    }
    EXPECT_EQ(b.model.materials[brows.material].baseColor, glm::vec3(0.3f, 0.2f, 0.1f));

    h.set("macrodetails-height/Height", 1.0f);
    h.reshape(b);
    for (const kke::ModelMesh& m : b.model.meshes)
        if (m.name == "body") skin = m;
    const glm::vec3 first = b.model.meshes[before].vertices[0].position;
    ASSERT_TRUE(kke::addBrows(b.model, skin, glm::vec3(1.0f), /*update=*/true));
    EXPECT_EQ(b.model.meshes.size(), before + 2);
    EXPECT_GT(b.model.meshes[before].vertices[0].position.y, first.y); // up with the taller face
}

// keepOutside lifts a garment pushed into the skin back over it.
TEST(HumanClothes, KeepOutsideLiftsClothOffTheSkin) {
    kke::ModelMesh skin, cloth;
    for (int i = 0; i < 4; ++i) {
        kke::ModelVertex v;
        v.position = glm::vec3(static_cast<float>(i), 0.0f, 0.0f);
        v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
        skin.vertices.push_back(v);
        v.position.z = i % 2 ? 0.01f : -0.005f; // two inside, two well clear
        cloth.vertices.push_back(v);
    }
    const std::vector<uint32_t> under = kke::skinUnder(cloth, skin);
    ASSERT_EQ(under, (std::vector<uint32_t>{ 0, 1, 2, 3 }));
    kke::keepOutside(cloth, skin, under, 0.003f);
    EXPECT_NEAR(cloth.vertices[0].position.z, 0.003f, 1e-6f);
    EXPECT_NEAR(cloth.vertices[1].position.z, 0.01f, 1e-6f);
    EXPECT_NEAR(cloth.vertices[2].position.z, 0.003f, 1e-6f);
}

// The anime face is a style layer: every name is a slider the pack has,
// it reshapes the face without touching the person's own values, and an
// empty style gives the realistic face back exactly.
TEST(HumanStyle, AnimeFaceReshapesWithoutChangingTheLook) {
    NEED_PACK();
    for (const auto& [name, offset] : kke::Human::animeFace()) EXPECT_GE(pack().findModifier(name), 0) << name;
    kke::Human h(pack());
    h.set("nose/nose-hump-decr|incr", 0.4f);
    const std::vector<glm::vec3> real = h.shapedPositions();
    const auto values = h.values();
    h.setStyle(kke::Human::animeFace());
    const std::vector<glm::vec3> anime = h.shapedPositions();
    EXPECT_EQ(h.values(), values);
    float moved = 0.0f;
    for (size_t i = 0; i < real.size(); ++i) moved = std::max(moved, glm::length(anime[i] - real[i]));
    EXPECT_GT(moved, 0.003f);
    h.setStyle({});
    EXPECT_EQ(h.shapedPositions(), real);
}

// hairLocks: one closed, tapered lock per guide, the same counts for the
// same guides (so a game copies new positions in each frame), wide at the
// root and a point at the tip.
TEST(HumanHair, LocksTaperFromRootToTip) {
    std::vector<glm::vec3> guides;
    for (int g = 0; g < 3; ++g)
        for (int i = 0; i < 5; ++i) guides.emplace_back(0.03f * static_cast<float>(g), 1.7f - 0.04f * static_cast<float>(i), 0.08f);
    const kke::ModelMesh a = kke::hairLocks(guides, 5, glm::vec3(0.03f, 1.6f, 0.0f), 0.03f);
    EXPECT_EQ(a.vertices.size(), 3u * 5u * 4u);
    EXPECT_EQ(a.indices.size(), 3u * 4u * 4u * 6u);
    const float root = glm::length(a.vertices[0].position - a.vertices[2].position);
    const float tip = glm::length(a.vertices[16].position - a.vertices[18].position);
    EXPECT_NEAR(root, 0.03f, 1e-3f);
    EXPECT_LT(tip, root * 0.1f);
    guides[4].x += 0.05f; // it swings
    const kke::ModelMesh b = kke::hairLocks(guides, 5, glm::vec3(0.03f, 1.6f, 0.0f), 0.03f);
    EXPECT_EQ(b.vertices.size(), a.vertices.size());
    EXPECT_EQ(b.indices, a.indices);
    EXPECT_NEAR(kke::lockWidth({ { 0, 0, 0 }, { 0.01f, 0, 0 } }), 0.026f, 1e-4f);
}
