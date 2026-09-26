// Broken pieces must be solid and closed, with insides in a deeper tone
// of the texture's main colour (user report: a pillar broke "hollow",
// its faces didn't close out).
#include "kke/InteriorColor.h"
#include "kke/VoronoiFracture.h"
#include "kke/VoxelTets.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>

namespace {

// Unit cube triangles; `bottom` = keep the two y=0 triangles.
void cube(std::vector<glm::vec3>& p, std::vector<uint32_t>& idx, bool bottom) {
    p.clear();
    for (int i = 0; i < 8; ++i) p.push_back(glm::vec3(i & 1, (i >> 1) & 1, (i >> 2) & 1));
    // x=0, x=1, y=1, z=0, z=1, then y=0 last.
    idx = { 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5, 2, 6, 3, 3, 6, 7, 0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4 };
    if (!bottom) idx.resize(idx.size() - 6);
}

// A pillar like the sandbox's: 0.5 x 2 x 0.5 m, no bottom cap (it
// stands on the ground), as Synty models it.
void openBottomPillar(std::vector<glm::vec3>& p, std::vector<uint32_t>& idx) {
    cube(p, idx, false);
    for (glm::vec3& v : p) v *= glm::vec3(0.5f, 2.0f, 0.5f);
}

// FEMFX's face numbering (PhysicsModule::prepareRenderData): face f is
// ids[3-f], ids[(5-f)%4], ids[(f+2)%4], opposite corner f.
std::array<uint32_t, 3> faceOf(const std::array<uint32_t, 4>& t, int f) { return { t[3 - f], t[(5 - f) % 4], t[(f + 2) % 4] }; }

std::vector<uint8_t> rgbaImage(int w, int h, const std::function<glm::u8vec4(int, int)>& px) {
    std::vector<uint8_t> img(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            glm::u8vec4 c = px(x, y);
            for (int k = 0; k < 4; ++k) img[(static_cast<size_t>(y) * w + x) * 4 + k] = c[k];
        }
    return img;
}

float lin(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

} // namespace

// ------------------------------------------------------------ solid volume

TEST(InteriorFill, OpenBottomBoxIsStillSolid) {
    std::vector<glm::vec3> p;
    std::vector<uint32_t> idx;
    cube(p, idx, false);
    kke::VoxelTetMesh v = kke::voxelizeToTets(p, idx, 0.25f, 1000);
    // The flood used to pour in through the missing bottom and leave a
    // shell: walls and lid only, the 2x3x2 core under it empty.
    EXPECT_EQ(v.dims, glm::ivec3(4));
    EXPECT_EQ(v.solidCells, 64u);
}

TEST(InteriorFill, OpenBottomPillarHasNoHollowCore) {
    std::vector<glm::vec3> p;
    std::vector<uint32_t> idx;
    openBottomPillar(p, idx);
    kke::VoxelTetMesh v = kke::voxelizeToTets(p, idx, 0.125f, 10000);
    EXPECT_EQ(v.solidCells, static_cast<size_t>(v.dims.x) * v.dims.y * v.dims.z);
    float volume = 0.0f;
    for (const auto& t : v.mesh.tets) volume += kke::tetVolume(v.mesh.vertices[t[0]], v.mesh.vertices[t[1]], v.mesh.vertices[t[2]], v.mesh.vertices[t[3]]);
    EXPECT_NEAR(volume, 0.5f * 2.0f * 0.5f, 1e-3f);
}

TEST(InteriorFill, OpenEndedTubeStaysHollow) {
    // A square pipe along x, open at both ends: its bore sees out two
    // ways, so it is a real opening, not a missing cap.
    std::vector<glm::vec3> p;
    std::vector<uint32_t> idx;
    cube(p, idx, true);
    idx.erase(idx.begin(), idx.begin() + 12); // drop x=0 and x=1
    for (glm::vec3& q : p) q *= glm::vec3(2.0f, 1.0f, 1.0f);
    kke::VoxelTetMesh v = kke::voxelizeToTets(p, idx, 0.25f, 10000);
    ASSERT_EQ(v.dims, glm::ivec3(8, 4, 4));
    // Walls one cell thick: 8 x (16 - 4) cells, the 2x2 bore stays empty.
    EXPECT_EQ(v.solidCells, 8u * 12u);
}

TEST(InteriorFill, ClosedMeshUnchanged) {
    std::vector<glm::vec3> p;
    std::vector<uint32_t> idx;
    cube(p, idx, true);
    EXPECT_EQ(kke::voxelizeToTets(p, idx, 0.25f, 1000).solidCells, 64u);
    // An L-shaped notch (open on two sides) is not filled: a flat plate
    // with a wall along one edge stays a plate and a wall.
    std::vector<glm::vec3> lp = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 1, 0, 1 }, { 0, 1, 0 }, { 0, 1, 1 } };
    std::vector<uint32_t> li = { 0, 2, 1, 1, 2, 3, 0, 4, 2, 2, 4, 5 };
    kke::VoxelTetMesh l = kke::voxelizeToTets(lp, li, 0.25f, 1000);
    EXPECT_EQ(l.solidCells, 16u + 12u);
}

// Every fractured piece is a closed surface facing outwards: each face
// that isn't shared with another tet of the same piece (what the renderer
// draws once the piece breaks off) has its edges matched by exactly one
// opposite edge, and its winding points away from its own tet (the
// physics pipeline culls back faces - a flipped face is a hole).
TEST(InteriorFill, FracturedPiecesAreClosedAndFaceOutwards) {
    std::vector<glm::vec3> p;
    std::vector<uint32_t> idx;
    openBottomPillar(p, idx);
    kke::VoxelTetMesh v = kke::voxelizeToTets(p, idx, 0.1f, 400);
    kke::fitSurfaceToMesh(v.mesh, p, idx, v.cellSize * 0.75f);
    kke::FractureSeedOptions o;
    o.chunkSize = 0.3f;
    o.seed = 7;
    kke::BakedFracture baked = kke::bakeFracture(v.mesh, o);
    ASSERT_GT(baked.pieces, 2u);
    const kke::TetMeshData& m = baked.cut.mesh;

    std::map<std::pair<uint32_t, std::array<uint32_t, 3>>, int> use;
    for (size_t t = 0; t < m.tets.size(); ++t)
        for (int f = 0; f < 4; ++f) {
            auto k = faceOf(m.tets[t], f);
            std::sort(k.begin(), k.end());
            ++use[{ baked.cut.chunkOfTet[t], k }];
        }
    std::map<uint32_t, std::map<std::pair<uint32_t, uint32_t>, int>> edges; // piece -> directed edge balance
    size_t outward = 0, total = 0;
    for (size_t t = 0; t < m.tets.size(); ++t) {
        const uint32_t chunk = baked.cut.chunkOfTet[t];
        for (int f = 0; f < 4; ++f) {
            auto face = faceOf(m.tets[t], f);
            auto k = face;
            std::sort(k.begin(), k.end());
            if (use[{ chunk, k }] != 1) continue; // inside the piece
            const glm::vec3 a = m.vertices[face[0]], b = m.vertices[face[1]], c = m.vertices[face[2]];
            const glm::vec3 opposite = m.vertices[m.tets[t][f]];
            ++total;
            if (glm::dot(glm::cross(b - a, c - a), (a + b + c) / 3.0f - opposite) > 0.0f) ++outward;
            for (int e = 0; e < 3; ++e) {
                uint32_t u = face[e], w = face[(e + 1) % 3];
                auto& bal = edges[chunk];
                if (u < w) ++bal[{ u, w }];
                else --bal[{ w, u }];
            }
        }
    }
    EXPECT_EQ(outward, total);
    for (const auto& [chunk, bal] : edges)
        for (const auto& [e, n] : bal) EXPECT_EQ(n, 0) << "piece " << chunk << " has an open edge " << e.first << "-" << e.second;
}

// ------------------------------------------------------ surface at the cracks

namespace {
// The cube's surface as a soup, split to <= maxEdge, plus the fracture
// under it: the sandbox's setup for a breakable prop.
struct CrackedCube {
    kke::VoxelTetMesh vox;
    kke::BakedFracture baked;
    kke::TriangleSoup soup;
};
CrackedCube crackedCube(uint32_t seed) {
    CrackedCube c;
    std::vector<glm::vec3> p;
    std::vector<uint32_t> idx;
    cube(p, idx, true);
    c.vox = kke::voxelizeToTets(p, idx, 0.125f, 1000);
    kke::FractureSeedOptions o;
    o.chunkSize = 0.4f;
    o.seed = seed;
    c.baked = kke::bakeFracture(c.vox.mesh, o);
    for (uint32_t i : idx) {
        c.soup.positions.push_back(p[i]);
        c.soup.normals.push_back(glm::vec3(0, 1, 0));
        c.soup.uvs.push_back(glm::vec2(p[i].x, p[i].z));
    }
    kke::subdivideSoup(c.soup, 0.0625f, 100000);
    return c;
}
float triArea(const kke::TriangleSoup& s, size_t t) {
    return 0.5f * glm::length(glm::cross(s.positions[t * 3 + 1] - s.positions[t * 3], s.positions[t * 3 + 2] - s.positions[t * 3]));
}
// Share of the surface glued to the wrong piece: the lips (surface
// hanging past a piece's crack face) and, on the neighbour, the holes
// they leave. Measured by sampling 6 points in every triangle.
double surfaceMismatch(const CrackedCube& c, const kke::TriangleSoup& soup, const kke::TetEmbedding& e) {
    static const glm::vec3 w[6] = { { 0.6f, 0.2f, 0.2f }, { 0.2f, 0.6f, 0.2f }, { 0.2f, 0.2f, 0.6f },
                                    { 0.45f, 0.45f, 0.1f }, { 0.1f, 0.45f, 0.45f }, { 0.45f, 0.1f, 0.45f } };
    std::vector<glm::vec3> pts;
    for (size_t t = 0; t < soup.triangleCount(); ++t)
        for (const glm::vec3& b : w) pts.push_back(soup.positions[t * 3] * b.x + soup.positions[t * 3 + 1] * b.y + soup.positions[t * 3 + 2] * b.z);
    const kke::TetEmbedding at = kke::embedPoints(c.baked.cut.mesh, pts);
    double wrong = 0.0, total = 0.0;
    for (size_t t = 0; t < soup.triangleCount(); ++t) {
        const uint32_t mine = c.baked.cut.chunkOfTet[e.tet[t * 3]];
        const double a = triArea(soup, t);
        total += a;
        for (int k = 0; k < 6; ++k)
            if (c.baked.cut.chunkOfTet[at.tet[t * 6 + k]] != mine) wrong += a / 6.0;
    }
    return wrong / total;
}
} // namespace

TEST(InteriorFill, SurfaceIsCutExactlyAtTheCracks) {
    for (uint32_t seed : { 3u, 11u, 42u }) {
        CrackedCube c = crackedCube(seed);
        ASSERT_GT(c.baked.pieces, 3u);
        const kke::TetMeshData& m = c.baked.cut.mesh;
        double before = 0.0;
        for (size_t t = 0; t < c.soup.triangleCount(); ++t) before += triArea(c.soup, t);
        // Old way: whole triangles, each with the piece under its centre.
        const double oldMismatch = surfaceMismatch(c, c.soup, kke::embedTriangles(m, c.soup.positions));

        kke::TriangleSoup soup = c.soup;
        std::vector<uint32_t> piece = kke::splitSoupAtPieces(soup, m, c.baked.cut.chunkOfTet, c.baked.seeds, 1000000);
        ASSERT_EQ(piece.size(), soup.triangleCount());
        double after = 0.0;
        for (size_t t = 0; t < soup.triangleCount(); ++t) after += triArea(soup, t);
        EXPECT_NEAR(after, before, before * 1e-4) << "cutting must not lose or add surface";
        const kke::TetEmbedding e = kke::embedTrianglesInPieces(m, soup.positions, c.baked.cut.chunkOfTet, piece);
        for (size_t t = 0; t < piece.size(); ++t) ASSERT_EQ(c.baked.cut.chunkOfTet[e.tet[t * 3]], piece[t]);
        const double newMismatch = surfaceMismatch(c, soup, e);
        // About 2% of the surface used to hang on the wrong piece (a lip
        // on one side of every crack, a hole on the other); what is left
        // (~0.6%) is where the crack couldn't be snapped fully flat.
        EXPECT_LT(newMismatch, 0.009) << "seed " << seed;
        EXPECT_LT(newMismatch, oldMismatch * 0.4) << "seed " << seed << ": old " << oldMismatch;
        // Winding (outward normals) kept by the cuts.
        for (size_t t = 0; t < soup.triangleCount(); ++t) {
            const glm::vec3 n = glm::cross(soup.positions[t * 3 + 1] - soup.positions[t * 3], soup.positions[t * 3 + 2] - soup.positions[t * 3]);
            const glm::vec3 ctr = (soup.positions[t * 3] + soup.positions[t * 3 + 1] + soup.positions[t * 3 + 2]) / 3.0f;
            ASSERT_GT(glm::dot(n, ctr - glm::vec3(0.5f)), 0.0f) << "triangle " << t;
        }
    }
}

TEST(InteriorFill, SplitRespectsTheTriangleBudget) {
    CrackedCube c = crackedCube(5);
    kke::TriangleSoup soup = c.soup;
    const size_t cap = c.soup.triangleCount() + 20;
    std::vector<uint32_t> piece = kke::splitSoupAtPieces(soup, c.baked.cut.mesh, c.baked.cut.chunkOfTet, c.baked.seeds, cap);
    EXPECT_LE(soup.triangleCount(), cap + 2); // a last cut may land just past it
    EXPECT_EQ(piece.size(), soup.triangleCount());
}

// ----------------------------------------------------------- interior colour

TEST(InteriorFill, UniformTextureGivesDeeperSameHue) {
    auto img = rgbaImage(64, 64, [](int, int) { return glm::u8vec4(200, 120, 60, 255); });
    kke::InteriorFill f = kke::interiorFillFromPixels(img.data(), 64, 64, {});
    ASSERT_TRUE(f.valid);
    EXPECT_NEAR(f.dominant.r, 200 / 255.0f, 0.02f);
    EXPECT_NEAR(f.dominant.g, 120 / 255.0f, 0.02f);
    EXPECT_NEAR(f.coverage, 1.0f, 1e-4f);
    // Deeper: darker, same hue order, at least as saturated.
    EXPECT_LT(f.color.r, f.dominant.r);
    EXPECT_GT(f.color.r, f.color.g);
    EXPECT_GT(f.color.g, f.color.b);
    EXPECT_NEAR(f.color.r, f.dominant.r * kke::kInteriorDarken, 0.02f);
    // tint x texel (both linear) = the deeper colour.
    for (int c = 0; c < 3; ++c) {
        const float texel = lin(img[c] / 255.0f);
        EXPECT_NEAR(lin(f.tint[c]) * texel, lin(f.color[c]), 0.01f);
    }
}

TEST(InteriorFill, AtlasUsesOnlyTheSwatchesTheObjectSamples) {
    // Left half grey stone, right half blue paint (a Synty-style atlas).
    auto img = rgbaImage(128, 64, [](int x, int) { return x < 64 ? glm::u8vec4(128, 128, 128, 255) : glm::u8vec4(30, 60, 200, 255); });
    // A blue crate: every triangle samples the blue half.
    std::vector<glm::vec2> blue = { { 0.8f, 0.5f }, { 0.7f, 0.2f }, { 0.9f, 0.9f } };
    kke::InteriorFill f = kke::interiorFillFromPixels(img.data(), 128, 64, blue);
    ASSERT_TRUE(f.valid);
    EXPECT_GT(f.dominant.b, 0.7f);
    EXPECT_GT(f.uv.x, 0.5f); // the texel it samples is blue too
    // Mostly stone by area, a big blue stripe of small triangles: area wins.
    std::vector<glm::vec2> mixed = { { 0.2f, 0.5f }, { 0.8f, 0.5f }, { 0.8f, 0.6f }, { 0.8f, 0.7f } };
    std::vector<float> area = { 10.0f, 1.0f, 1.0f, 1.0f };
    kke::InteriorFill g = kke::interiorFillFromPixels(img.data(), 128, 64, mixed, area);
    ASSERT_TRUE(g.valid);
    EXPECT_NEAR(g.dominant.r, 128 / 255.0f, 0.02f);
    EXPECT_LT(g.uv.x, 0.5f);
    EXPECT_NEAR(g.coverage, 10.0f / 13.0f, 1e-3f);
}

TEST(InteriorFill, SampleTexelIsInsideAUniformPatch) {
    // Mostly green, with a 1-texel red grid every 8 texels (mortar lines):
    // the chosen texel must not sit next to a line, or mipmaps blend red in.
    auto img = rgbaImage(64, 64, [](int x, int y) {
        return (x % 8 == 0 || y % 8 == 0) ? glm::u8vec4(220, 20, 20, 255) : glm::u8vec4(40, 160, 60, 255);
    });
    kke::InteriorFill f = kke::interiorFillFromPixels(img.data(), 64, 64, {});
    ASSERT_TRUE(f.valid);
    EXPECT_GT(f.dominant.g, f.dominant.r);
    const int x = static_cast<int>(f.uv.x * 64), y = static_cast<int>(f.uv.y * 64);
    EXPECT_NE(x % 8, 0);
    EXPECT_NE(y % 8, 0);
}

TEST(InteriorFill, TransparentTexelsAreIgnored) {
    // A leaf card: mostly transparent black, some opaque green.
    auto img = rgbaImage(32, 32, [](int x, int) { return x < 24 ? glm::u8vec4(0, 0, 0, 0) : glm::u8vec4(50, 140, 40, 255); });
    kke::InteriorFill f = kke::interiorFillFromPixels(img.data(), 32, 32, {});
    ASSERT_TRUE(f.valid);
    EXPECT_GT(f.dominant.g, 0.4f);
    auto clear = rgbaImage(8, 8, [](int, int) { return glm::u8vec4(255, 255, 255, 0); });
    EXPECT_FALSE(kke::interiorFillFromPixels(clear.data(), 8, 8, {}).valid);
}

TEST(InteriorFill, MissingFileIsReportedInvalid) {
    EXPECT_FALSE(kke::interiorFillFromTexture("does/not/exist.png", {}).valid);
    EXPECT_FALSE(kke::interiorFillFromTexture("", {}).valid);
    EXPECT_FALSE(kke::interiorFillFromPixels(nullptr, 4, 4, {}).valid);
}

TEST(InteriorFill, DeeperToneOfBlackAndWhite) {
    EXPECT_EQ(kke::deeperTone(glm::vec3(0.0f)), glm::vec3(0.0f));
    const glm::vec3 w = kke::deeperTone(glm::vec3(1.0f));
    EXPECT_NEAR(w.r, kke::kInteriorDarken, 1e-5f);
    EXPECT_NEAR(w.g, w.b, 1e-5f); // grey stays grey
}
