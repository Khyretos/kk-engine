#include "kke/ScriptTerrain.h"
#include "kke/ScriptVM.h"
#include "kke/Terrain.h"

#include <gtest/gtest.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using kke::Terrain;
using kke::TerrainChunks;

namespace {

glm::vec3 faceNormal(const std::vector<kke::Vertex>& v, const std::vector<uint32_t>& idx, size_t t) {
    const glm::vec3 a = v[idx[t * 3]].position, b = v[idx[t * 3 + 1]].position, c = v[idx[t * 3 + 2]].position;
    return glm::cross(b - a, c - a);
}

} // namespace

TEST(Terrain, SameSeedSameLandAndItSpansTheHeight) {
    Terrain a(200.0f, 129), b(200.0f, 129), c(200.0f, 129);
    Terrain::Noise n;
    n.seed = 7;
    n.height = 30.0f;
    a.generate(n);
    b.generate(n);
    n.seed = 8;
    c.generate(n);
    EXPECT_EQ(a.heights(), b.heights());
    EXPECT_NE(a.heights(), c.heights());
    const auto [lo, hi] = std::minmax_element(a.heights().begin(), a.heights().end());
    EXPECT_FLOAT_EQ(*lo, 0.0f);
    EXPECT_NEAR(*hi, 30.0f, 1e-3f);
    // Hills, not noise: neighbours are close compared with the whole range.
    float maxStep = 0.0f;
    for (int z = 0; z < 128; ++z)
        for (int x = 0; x < 128; ++x) maxStep = std::max(maxStep, std::abs(a.sample(x + 1, z) - a.sample(x, z)));
    EXPECT_LT(maxStep, 4.0f);
    // Ridged makes different land from the same seed.
    Terrain r(200.0f, 129);
    n.seed = 7;
    n.ridged = true;
    r.generate(n);
    EXPECT_NE(r.heights(), a.heights());
}

TEST(Terrain, HeightAtFollowsTheCollisionTriangles) {
    Terrain t(8.0f, 9, glm::vec3(0.0f, 2.0f, 0.0f)); // 1 m cells, corner (-4, 2, -4)
    EXPECT_EQ(t.corner(), glm::vec3(-4.0f, 2.0f, -4.0f));
    t.setSample(4, 4, 1.0f); // a spike at the centre
    EXPECT_FLOAT_EQ(t.heightAt(0.0f, 0.0f), 3.0f);
    // In cell (3,3) the diagonal runs (3,3)-(4,4): half way along it is half the spike.
    EXPECT_NEAR(t.heightAt(-0.5f, -0.5f), 2.5f, 1e-5f);
    // Off the diagonal, on the triangle (3,3) (4,3) (4,4): u=0.75, v=0.25 -> 0.25 of the spike.
    EXPECT_NEAR(t.heightAt(-0.25f, -0.75f), 2.25f, 1e-5f);
    // The other triangle (3,3) (3,4) (4,4): u=0.25, v=0.75 -> 0.25 too.
    EXPECT_NEAR(t.heightAt(-0.75f, -0.25f), 2.25f, 1e-5f);
    // Outside: clamped to the edge.
    EXPECT_FLOAT_EQ(t.heightAt(100.0f, 100.0f), 2.0f);
    EXPECT_TRUE(t.contains(3.9f, -3.9f));
    EXPECT_FALSE(t.contains(4.1f, 0.0f));
}

TEST(Terrain, BrushesRaiseSmoothFlattenAndReportWhatTheyChanged) {
    Terrain t(64.0f, 65); // 1 m cells, centred on the origin
    const uint64_t v0 = t.version();
    Terrain::Rect r = t.raise(glm::vec3(0.0f), 5.0f, 3.0f);
    EXPECT_FALSE(r.empty());
    EXPECT_GT(t.version(), v0);
    EXPECT_NEAR(t.heightAt(0.0f, 0.0f), 3.0f, 1e-4f);
    EXPECT_FLOAT_EQ(t.heightAt(5.5f, 0.0f), 0.0f); // nothing past the radius
    EXPECT_GE(r.x0, 32 - 5);
    EXPECT_LE(r.x1, 32 + 5);
    // Lowering is raising by a negative amount.
    t.raise(glm::vec3(0.0f), 5.0f, -1.0f);
    EXPECT_NEAR(t.heightAt(0.0f, 0.0f), 2.0f, 1e-4f);
    // Smoothing takes the peak down and spreads it: the hill gets wider.
    const float shoulder = t.heightAt(6.0f, 0.0f);
    for (int i = 0; i < 20; ++i) t.smooth(glm::vec3(0.0f), 12.0f, 1.0f);
    EXPECT_LT(t.heightAt(0.0f, 0.0f), 1.5f);
    EXPECT_GT(t.heightAt(6.0f, 0.0f), shoulder + 0.05f);
    EXPECT_GE(*std::min_element(t.heights().begin(), t.heights().end()), 0.0f);
    // Flatten makes a level pad round the point.
    t.raise(glm::vec3(10.0f, 0.0f, 10.0f), 8.0f, 4.0f);
    t.flatten(glm::vec3(10.0f, 0.0f, 10.0f), 6.0f, 1.0f);
    for (glm::vec2 p : {glm::vec2(10, 10), glm::vec2(12, 10), glm::vec2(10, 7)}) EXPECT_NEAR(t.heightAt(p.x, p.y), 1.0f, 1e-4f) << p.x << "," << p.y;
    // A brush that misses the terrain changes nothing.
    const uint64_t v1 = t.version();
    EXPECT_TRUE(t.raise(glm::vec3(500.0f, 0.0f, 0.0f), 5.0f, 1.0f).empty());
    EXPECT_EQ(t.version(), v1);
    // Bad numbers are refused.
    EXPECT_TRUE(t.raise(glm::vec3(0.0f), 5.0f, NAN).empty());
    EXPECT_TRUE(t.raise(glm::vec3(0.0f), -1.0f, 1.0f).empty());
}

TEST(Terrain, PaintMovesWeightAndSteepSlopesShowRock) {
    Terrain t(32.0f, 33);
    EXPECT_EQ(t.weights(16, 16)[0], 255);
    t.paint(glm::vec3(0.0f), 4.0f, 2, 1.0f);
    const auto w = t.weights(16, 16);
    EXPECT_EQ(w[2], 255);
    EXPECT_EQ(int(w[0]) + w[1] + w[2] + w[3], 255);
    const auto edge = t.weights(16 + 3, 16); // part way out: a blend that still adds up
    EXPECT_GT(edge[0], 0);
    EXPECT_GT(edge[2], 0);
    EXPECT_EQ(int(edge[0]) + edge[1] + edge[2] + edge[3], 255);
    EXPECT_FALSE(t.paint(glm::vec3(0.0f), 4.0f, 9).x1 >= 0); // no such layer
    // Flat ground is the painted colour (give or take the speckle); a cliff is rock.
    const glm::vec3 flat = t.colorAt(16, 16);
    EXPECT_NEAR(flat.r / t.layers[2].color.r, 1.0f, 0.1f);
    for (int z = 0; z < 33; ++z)
        for (int x = 24; x < 33; ++x) t.setSample(x, z, float(x - 24) * 3.0f); // 72 degrees
    const glm::vec3 cliff = t.colorAt(28, 5);
    EXPECT_NEAR(cliff.r / t.layers[1].color.r, 1.0f, 0.1f);
    EXPECT_NEAR(cliff.g / t.layers[1].color.g, 1.0f, 0.1f);
    EXPECT_GT(t.slopeAt(28.0f - 16.0f, 5.0f - 16.0f), 60.0f);
}

TEST(Terrain, MeshFacesUpMatchesHeightsAndSkirtsFaceOut) {
    Terrain t(16.0f, 17);
    Terrain::Noise n;
    n.height = 4.0f;
    t.generate(n);
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    t.buildMesh(0, 0, 16, 16, 1, 0.0f, v, idx);
    ASSERT_EQ(v.size(), 17u * 17u);
    ASSERT_EQ(idx.size(), 16u * 16u * 6u);
    for (size_t i = 0; i < idx.size() / 3; ++i) EXPECT_GT(faceNormal(v, idx, i).y, 0.0f) << "triangle " << i;
    for (const kke::Vertex& p : v) {
        EXPECT_NEAR(p.position.y, t.heightAt(p.position.x, p.position.z), 1e-4f);
        EXPECT_GT(p.normal.y, 0.0f);
        EXPECT_EQ(p.uv, glm::vec2(0.0f));
    }
    // Every 4th sample: 5 x 5 vertices; plus skirts that face away from the middle.
    v.clear();
    idx.clear();
    t.buildMesh(0, 0, 16, 16, 4, 2.0f, v, idx);
    EXPECT_EQ(v.size(), 25u + 4u * 5u);
    const glm::vec3 mid = t.centre();
    for (size_t i = 32; i < idx.size() / 3; ++i) { // after the 16 quads of the top
        const glm::vec3 a = v[idx[i * 3]].position, b = v[idx[i * 3 + 1]].position, c = v[idx[i * 3 + 2]].position;
        const glm::vec3 centre = (a + b + c) / 3.0f;
        glm::vec3 out = centre - mid;
        out.y = 0.0f;
        EXPECT_GT(glm::dot(faceNormal(v, idx, i), out), 0.0f) << "skirt triangle " << i;
    }
    // A step that doesn't divide the chunk is halved until it does.
    v.clear();
    idx.clear();
    t.buildMesh(0, 0, 6, 6, 4, 0.0f, v, idx);
    EXPECT_EQ(v.size(), 16u); // step 2: 4 x 4
}

TEST(Terrain, LoadsAGreyscaleImage) {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "kke_terrain_test.png";
    // A left-to-right ramp, black to white.
    std::vector<uint8_t> px(64 * 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) px[size_t(y * 64 + x)] = uint8_t(x * 255 / 63);
    ASSERT_TRUE(stbi_write_png(file.string().c_str(), 64, 64, 1, px.data(), 64));
    Terrain t(100.0f, 33);
    std::string err;
    ASSERT_TRUE(t.loadImage(file, 20.0f, &err)) << err;
    EXPECT_NEAR(t.sample(0, 10), 0.0f, 1e-3f);
    EXPECT_NEAR(t.sample(32, 10), 20.0f, 1e-3f);
    EXPECT_NEAR(t.sample(16, 10), 10.0f, 0.2f);
    std::filesystem::remove(file);
    EXPECT_FALSE(t.loadImage(file, 20.0f, &err));
    EXPECT_NE(err.find("kke_terrain_test.png"), std::string::npos);
}

TEST(Terrain, ChunksGetCoarserWithDistanceAndRebuildWhereTouched) {
    Terrain t(256.0f, 257); // 1 m cells, 8 x 8 chunks of 32
    TerrainChunks chunks(t, 32, 40.0f);
    ASSERT_EQ(chunks.chunks().size(), 64u);
    const glm::vec3 camera(-120.0f, 5.0f, -120.0f); // over the first chunk
    EXPECT_EQ(chunks.stepFor(chunks.chunks()[0], camera), 1);
    EXPECT_EQ(chunks.stepFor(chunks.chunks()[63], camera), 8);
    // Everything wants building at first, nearest first, within the budget.
    std::vector<size_t> todo = chunks.update(camera, 10);
    ASSERT_EQ(todo.size(), 10u);
    EXPECT_EQ(todo[0], 0u);
    // Build them all; then nothing is left.
    for (int round = 0; round < 10; ++round)
        for (size_t i : chunks.update(camera, 64)) chunks.built(i, chunks.stepFor(chunks.chunks()[i], camera));
    EXPECT_TRUE(chunks.update(camera, 64).empty());
    // A brush near chunk 9 (x 32..64, z 32..64) dirties it and the neighbours its rim reaches.
    const Terrain::Rect r = t.raise(glm::vec3(-128.0f + 33.0f, 0.0f, -128.0f + 48.0f), 3.0f, 1.0f);
    chunks.touch(r);
    todo = chunks.update(camera, 64);
    EXPECT_NE(std::find(todo.begin(), todo.end(), size_t(9)), todo.end());
    EXPECT_NE(std::find(todo.begin(), todo.end(), size_t(8)), todo.end()); // the rim crosses x = 32
    EXPECT_EQ(std::find(todo.begin(), todo.end(), size_t(63)), todo.end());
    // Moving the camera changes which detail chunks want.
    for (size_t i : todo) chunks.built(i, chunks.stepFor(chunks.chunks()[i], camera));
    EXPECT_FALSE(chunks.update(glm::vec3(120.0f, 5.0f, 120.0f), 64).empty());
}

namespace {
std::string lastError(const kke::ScriptVM& vm) { return vm.errors().empty() ? std::string() : vm.errors().back().message; }
} // namespace

TEST(ScriptTerrain, LuaMakesSculptsAndPaintsIt) {
    kke::ScriptVM vm;
    kke::ScriptTerrain st(nullptr, nullptr); // no drawing, no collision
    st.bind(vm);
    ASSERT_TRUE(vm.runString(R"(
        assert(not terrain.exists())
        assert(terrain.create{ size = 128, resolution = 129, seed = 4, height = 25, pos = Vec(0, -5, 0) })
        local info = terrain.info()
        assert(info.resolution == 129 and info.spacing == 1 and info.owner == "world.lua")
        local y, on = terrain.height(10, 20)
        assert(on and y >= -5 and y <= 20.001, y)
        assert(select(2, terrain.height(500, 0)) == false)
        -- raise, then flatten a pad for a house, then read it back
        local before = terrain.height(Vec(0, 0, 0))
        terrain.raise(Vec(0, 0, 0), 6, 2)
        assert(math.abs(terrain.height(0, 0) - (before + 2)) < 1e-3)
        terrain.flatten(Vec(30, 4, 30), 8)
        assert(math.abs(terrain.height(31, 30) - 4) < 1e-3)
        assert(terrain.ground(Vec(31, 99, 30)).y == terrain.height(31, 30))
        terrain.lower(Vec(30, 0, 30), 3, 1)
        terrain.smooth(Vec(30, 0, 30), 8, 1)
        terrain.paint(Vec(-20, 0, -20), 5, "dirt")
        terrain.paint(Vec(-20, 0, -20), 5, 4, 0.5)
        terrain.layer("snow", { color = Vec(1, 1, 1) })
        assert(terrain.normal(0, 0).y > 0 and terrain.slope(0, 0) < 90)
    )", "world.lua")) << lastError(vm);
    ASSERT_NE(st.terrain(), nullptr);
    EXPECT_EQ(st.terrain()->samples(), 129);
    EXPECT_EQ(st.terrain()->corner(), glm::vec3(-64.0f, -5.0f, -64.0f));
    EXPECT_NEAR(st.terrain()->weights(44, 44)[3], 128, 1); // half way to snow from all-dirt
    EXPECT_EQ(st.terrain()->layers[3].color, glm::vec3(1.0f));
    // Another script may read it but not change it, or make its own.
    ASSERT_TRUE(vm.runString("h = terrain.height(0, 0)", "other.lua")) << lastError(vm);
    EXPECT_FALSE(vm.runString("terrain.raise(Vec(0,0,0), 3, 1)", "other.lua"));
    EXPECT_NE(lastError(vm).find("belongs to world.lua"), std::string::npos) << lastError(vm);
    EXPECT_FALSE(vm.runString("terrain.create{}", "other.lua"));
    EXPECT_FALSE(vm.runString("terrain.paint(Vec(0,0,0), 3, 'lava')", "world.lua"));
    EXPECT_NE(lastError(vm).find("no layer called 'lava'"), std::string::npos) << lastError(vm);
    EXPECT_FALSE(vm.runString("terrain.raise(Vec(0,0,0), 0, 1)", "world.lua"));
    // Unloading the script that made it takes it away.
    st.release("other.lua");
    EXPECT_NE(st.terrain(), nullptr);
    st.release("world.lua");
    EXPECT_EQ(st.terrain(), nullptr);
    EXPECT_FALSE(vm.runString("terrain.height(0, 0)", "other.lua"));
    EXPECT_NE(lastError(vm).find("there is no terrain"), std::string::npos);
    // Too big is clamped to maxSamples; a missing image is nil and why.
    st.maxSamples = 65;
    ASSERT_TRUE(vm.runString(R"(
        local ok, why = terrain.create{ image = "nope.png" }
        assert(ok == nil and why:find("nope.png"), why)
        assert(terrain.create{ resolution = 5000, flat = true })
        assert(terrain.info().resolution == 65)
        assert(terrain.height(3, 3) == 0)
    )", "world.lua")) << lastError(vm);
    EXPECT_FALSE(vm.runString("terrain.create{ image = '../secret.png' }", "world.lua"));
    // No models module here: scatter says so.
    EXPECT_FALSE(vm.runString("terrain.scatter{ model = 1 }", "world.lua"));
    EXPECT_NE(lastError(vm).find("no models module"), std::string::npos) << lastError(vm);
    EXPECT_EQ(st.scatteredCount(), 0u);
}

TEST(Terrain, ScatterFollowsTheRulesAndIsTheSameEveryTime) {
    Terrain t(100.0f, 101); // 1 m cells
    for (int z = 0; z <= 100; ++z)
        for (int x = 70; x <= 100; ++x) t.setSample(x, z, float(x - 70) * 2.0f); // a steep bank past x = 20
    t.paint(glm::vec3(-25.0f, 0.0f, -25.0f), 30.0f, 2); // a dirt patch, solid out to about 16 m
    Terrain::ScatterRule r;
    r.density = 4.0f; // one per 25 m^2
    r.seed = 9;
    const std::vector<Terrain::Placement> a = t.scatter(r), b = t.scatter(r);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) EXPECT_EQ(a[i].position, b[i].position);
    // About density x area, minus the steep bank (30 of 100 m wide).
    EXPECT_GT(a.size(), 230u);
    EXPECT_LT(a.size(), 300u);
    for (const Terrain::Placement& p : a) {
        EXPECT_LT(p.position.x, 21.0f) << "on the steep bank";
        EXPECT_NEAR(p.position.y, t.heightAt(p.position.x, p.position.z), 1e-4f);
        EXPECT_GE(p.scale, r.minScale);
        EXPECT_LE(p.scale, r.maxScale);
        EXPECT_EQ(p.up, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    r.seed = 10;
    EXPECT_NE(t.scatter(r)[0].position, a[0].position);
    // Only on dirt: inside the patch.
    r.layer = 2;
    const auto dirt = t.scatter(r);
    EXPECT_GT(dirt.size(), 20u);
    for (const Terrain::Placement& p : dirt) EXPECT_LT(glm::length(glm::vec2(p.position.x + 25.0f, p.position.z + 25.0f)), 17.0f);
    // A circle, a cap, and leaning with the ground.
    r.layer = -1;
    r.radius = 10.0f;
    r.centre = {0.0f, 0.0f};
    for (const Terrain::Placement& p : t.scatter(r)) EXPECT_LE(glm::length(glm::vec2(p.position.x, p.position.z)), 10.0f);
    r.radius = 0.0f;
    r.keepClear = {glm::vec3(0.0f, 0.0f, 12.0f)};
    for (const Terrain::Placement& p : t.scatter(r)) EXPECT_GE(glm::length(glm::vec2(p.position.x, p.position.z)), 12.0f);
    r.keepClear.clear();
    r.maxCount = 7;
    EXPECT_EQ(t.scatter(r).size(), 7u);
    r.maxCount = 100000;
    r.maxSlope = 90.0f;
    r.alignToSlope = true;
    bool leaned = false;
    for (const Terrain::Placement& p : t.scatter(r))
        if (p.position.x > 24.0f && p.position.x < 48.0f) { // on the bank, away from its edges
            leaned = true;
            EXPECT_LT(p.up.y, 0.6f);
            // The transform stands the thing up along the ground's normal.
            const glm::vec3 up = glm::normalize(glm::vec3(p.transform() * glm::vec4(0, 1, 0, 0)));
            EXPECT_NEAR(glm::dot(up, glm::normalize(p.up)), 1.0f, 1e-3f);
        }
    EXPECT_TRUE(leaned);
}
