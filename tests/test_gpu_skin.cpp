// kke/GpuSkin.h: GPU skinning (ENG-G08). The packing helpers lay out what
// shaders/skin.comp reads, and skinPackedVertex is a line-for-line CPU copy
// of the shader, so these tests pin the shader's maths to the CPU path
// (skinVertex plus skinJiggleDisplacement in ModelModule::skinInstance).

#include "kke/GpuSkin.h"
#include "kke/Mesh.h"

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cstddef>
#include <vector>

using namespace kke;

// The shader writes kke::Vertex as 11 tightly packed floats.
static_assert(sizeof(Vertex) == kSkinFloatsPerVertex * sizeof(float), "skin.comp writes Vertex as 11 floats");
static_assert(offsetof(Vertex, position) == 0, "skin.comp: position at float 0");
static_assert(offsetof(Vertex, color) == 3 * sizeof(float), "skin.comp: overlay position at float 3");
static_assert(offsetof(Vertex, normal) == 6 * sizeof(float), "skin.comp: normal at float 6");
static_assert(offsetof(Vertex, uv) == 9 * sizeof(float), "skin.comp: uv at float 9");
static_assert(sizeof(SkinPush) == 16, "skin.comp's push constants are four uints");

namespace {

void expectNear(const glm::vec3& a, const glm::vec3& b, float tol) {
    EXPECT_NEAR(a.x, b.x, tol);
    EXPECT_NEAR(a.y, b.y, tol);
    EXPECT_NEAR(a.z, b.z, tol);
}

ModelVertex vertex(glm::vec3 p, glm::vec3 n, glm::vec2 uv, glm::uvec4 j, glm::vec4 w) {
    ModelVertex v;
    v.position = p;
    v.normal = n;
    v.uv = uv;
    v.joints = j;
    v.weights = w;
    return v;
}

// Two bones: one turned and lifted, one moved along x.
std::vector<glm::mat4> twoBones() {
    const glm::mat4 a = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f)) * glm::rotate(glm::mat4(1.0f), 0.7f, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::mat4 b = glm::translate(glm::mat4(1.0f), glm::vec3(0.3f, 0.0f, -0.2f)) * glm::rotate(glm::mat4(1.0f), -0.4f, glm::vec3(1.0f, 0.0f, 0.0f));
    return { a, b };
}

std::vector<ModelVertex> testVertices() {
    return {
        vertex({ 0.1f, 0.2f, 0.3f }, { 0.0f, 1.0f, 0.0f }, { 0.25f, 0.75f }, { 0, 0, 0, 0 }, { 1, 0, 0, 0 }),
        vertex({ -0.4f, 1.1f, 0.2f }, { 0.6f, 0.8f, 0.0f }, { 0.5f, 0.5f }, { 0, 1, 0, 0 }, { 0.3f, 0.7f, 0, 0 }),
        vertex({ 0.0f, 0.9f, -0.5f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 1, 0, 1, 0 }, { 0.5f, 0.25f, 0.25f, 0 }),
        // A joint past the skeleton is skipped, as in skinVertex.
        vertex({ 0.2f, 0.4f, 0.1f }, { 1.0f, 0.0f, 0.0f }, { 0.1f, 0.2f }, { 7, 1, 0, 0 }, { 0.5f, 0.5f, 0, 0 }),
        // No weights: the position is the origin and the normal stays the bind normal.
        vertex({ 0.3f, 0.3f, 0.3f }, { 0.0f, 0.0f, -1.0f }, { 0.9f, 0.9f }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }),
    };
}

} // namespace

TEST(GpuSkin, BindVerticesPackFourVec4sEach) {
    const std::vector<ModelVertex> in = testVertices();
    const std::vector<glm::vec4> packed = packSkinBind(in);
    ASSERT_EQ(packed.size(), in.size() * kSkinBindVec4s);
    const glm::vec4* v = &packed[kSkinBindVec4s * 1];
    EXPECT_EQ(v[0], glm::vec4(-0.4f, 1.1f, 0.2f, 0.5f)); // position, uv.x
    EXPECT_EQ(v[1], glm::vec4(0.6f, 0.8f, 0.0f, 0.5f));  // normal, uv.y
    EXPECT_EQ(v[2], glm::vec4(0.3f, 0.7f, 0.0f, 0.0f));  // weights
    EXPECT_EQ(v[3], glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));  // joints as floats
    EXPECT_EQ(packed[kSkinBindVec4s * 3 + 3], glm::vec4(7.0f, 1.0f, 0.0f, 0.0f));
    EXPECT_TRUE(packSkinBind({}).empty());
}

TEST(GpuSkin, SkinDataIsMatrixColumnsThenZones) {
    const std::vector<glm::mat4> skin = twoBones();
    std::vector<SkinJiggleOffset> zones(1);
    zones[0].center = { 0.0f, 1.0f, 0.0f };
    zones[0].radius = 0.4f;
    zones[0].offset = { 0.05f, 0.0f, -0.02f };
    std::vector<glm::vec4> out{ glm::vec4(9.0f) }; // replaced, not appended to
    packSkinData(skin, zones, out);
    ASSERT_EQ(out.size(), skin.size() * kSkinVec4sPerBone + zones.size() * kSkinVec4sPerZone);
    for (size_t b = 0; b < skin.size(); ++b)
        for (int c = 0; c < 4; ++c) EXPECT_EQ(out[b * kSkinVec4sPerBone + static_cast<size_t>(c)], skin[b][c]);
    EXPECT_EQ(out[8], glm::vec4(0.0f, 1.0f, 0.0f, 0.4f));    // centre, radius
    EXPECT_EQ(out[9], glm::vec4(0.05f, 0.0f, -0.02f, 0.0f)); // offset
    packSkinData(skin, {}, out);
    EXPECT_EQ(out.size(), skin.size() * kSkinVec4sPerBone);
}

TEST(GpuSkin, ShaderMirrorMatchesCpuSkinning) {
    const std::vector<ModelVertex> in = testVertices();
    const std::vector<glm::mat4> skin = twoBones();
    std::vector<SkinJiggleOffset> zones(2);
    zones[0].center = { 0.0f, 0.9f, 0.0f };
    zones[0].radius = 0.8f;
    zones[0].offset = { 0.04f, -0.03f, 0.02f };
    zones[1].center = { -0.5f, 0.8f, 0.3f };
    zones[1].radius = 0.6f;
    zones[1].offset = { -0.01f, 0.05f, 0.0f };
    const std::vector<glm::vec4> bind = packSkinBind(in);
    for (const bool jiggle : { false, true }) {
        const std::vector<SkinJiggleOffset> z = jiggle ? zones : std::vector<SkinJiggleOffset>{};
        std::vector<glm::vec4> data;
        packSkinData(skin, z, data);
        const SkinPush pc{ static_cast<uint32_t>(in.size()), static_cast<uint32_t>(skin.size()), static_cast<uint32_t>(z.size()), 0u };
        for (uint32_t v = 0; v < in.size(); ++v) {
            glm::vec3 p, n;
            skinVertex(in[v], skin, p, n);
            if (!z.empty()) p += skinJiggleDisplacement(z, p);
            float out[kSkinFloatsPerVertex];
            skinPackedVertex(bind.data(), data.data(), pc, v, out);
            SCOPED_TRACE(testing::Message() << "vertex " << v << (jiggle ? " with jiggle" : ""));
            expectNear({ out[0], out[1], out[2] }, p, 1e-5f);
            expectNear({ out[3], out[4], out[5] }, in[v].position, 0.0f); // overlay: bind pose
            expectNear({ out[6], out[7], out[8] }, n, 1e-5f);
            EXPECT_EQ(out[9], in[v].uv.x);
            EXPECT_EQ(out[10], in[v].uv.y);
        }
    }
}

TEST(GpuSkin, JiggleMovesOnlyVerticesInsideAZone) {
    const std::vector<ModelVertex> in = testVertices();
    const std::vector<glm::mat4> skin = twoBones();
    std::vector<SkinJiggleOffset> zones(1);
    zones[0].center = { 100.0f, 0.0f, 0.0f }; // far from every vertex
    zones[0].radius = 0.5f;
    zones[0].offset = { 1.0f, 1.0f, 1.0f };
    const std::vector<glm::vec4> bind = packSkinBind(in);
    std::vector<glm::vec4> with, without;
    packSkinData(skin, zones, with);
    packSkinData(skin, {}, without);
    for (uint32_t v = 0; v < in.size(); ++v) {
        float a[kSkinFloatsPerVertex], b[kSkinFloatsPerVertex];
        skinPackedVertex(bind.data(), with.data(), SkinPush{ 5u, 2u, 1u, 0u }, v, a);
        skinPackedVertex(bind.data(), without.data(), SkinPush{ 5u, 2u, 0u, 0u }, v, b);
        for (uint32_t i = 0; i < kSkinFloatsPerVertex; ++i) EXPECT_EQ(a[i], b[i]);
    }
}
