#include "kke/VoronoiFracture.h"
#include "kke/VoxelTets.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>

namespace {

// A w x h x d box of voxel tets (6 per cell), like the physics demo's
// brick and glass pane.
kke::TetMeshData box(glm::vec3 size, float cell) {
    std::vector<glm::vec3> p;
    for (int i = 0; i < 8; ++i) p.push_back(glm::vec3(i & 1, (i >> 1) & 1, (i >> 2) & 1) * size - size * 0.5f);
    std::vector<uint32_t> idx = { 0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5 };
    return kke::voxelizeToTets(p, idx, cell, 100000).mesh;
}

double totalVolume(const kke::TetMeshData& m) {
    double v = 0.0;
    for (const auto& t : m.tets) v += kke::tetVolume(m.vertices[t[0]], m.vertices[t[1]], m.vertices[t[2]], m.vertices[t[3]]);
    return v;
}

struct FaceInfo { uint32_t tet; int uses; };
std::map<std::array<uint32_t, 3>, FaceInfo> faces(const kke::TetMeshData& m) {
    std::map<std::array<uint32_t, 3>, FaceInfo> out;
    for (uint32_t t = 0; t < m.tets.size(); ++t)
        for (int f = 0; f < 4; ++f) {
            std::array<uint32_t, 3> k{};
            int j = 0;
            for (int i = 0; i < 4; ++i) if (i != f) k[j++] = m.tets[t][i];
            std::sort(k.begin(), k.end());
            auto& fi = out[k];
            fi.tet = t;
            ++fi.uses;
        }
    return out;
}

double area(const kke::TetMeshData& m, const std::array<uint32_t, 3>& k) {
    return 0.5 * glm::length(glm::cross(m.vertices[k[1]] - m.vertices[k[0]], m.vertices[k[2]] - m.vertices[k[0]]));
}

kke::FractureSeedOptions opts(kke::FracturePattern p, float chunk, uint32_t seed) {
    kke::FractureSeedOptions o;
    o.pattern = p;
    o.chunkSize = chunk;
    o.seed = seed;
    return o;
}

} // namespace

TEST(VoronoiFracture, SeedMixingIsStableAndSpreads) {
    EXPECT_EQ(kke::fractureSeed(7, 3), kke::fractureSeed(7, 3));
    EXPECT_NE(kke::fractureSeed(7, 3), kke::fractureSeed(7, 4));
    EXPECT_NE(kke::fractureSeed(7, 3), kke::fractureSeed(8, 3));
    EXPECT_NE(kke::fractureSeed(0, 0), 0u);
}

// Same tets, same volume (within the rounding of moved border vertices),
// every tet still valid, and the outside is still the box.
TEST(VoronoiFracture, CutKeepsTheShapeAndEveryTetValid) {
    kke::TetMeshData m = box(glm::vec3(1.0f), 0.2f);
    kke::BakedFracture b = kke::bakeFracture(m, opts(kke::FracturePattern::Voronoi, 0.35f, 42));
    const auto& cut = b.cut.mesh;
    EXPECT_EQ(cut.tets, m.tets); // no extra simulation cost
    EXPECT_GT(b.cut.snappedVertices, 0u);
    EXPECT_GE(b.pieces, 10u);
    EXPECT_NEAR(totalVolume(cut), 1.0, 1e-4);
    for (const auto& t : cut.tets)
        EXPECT_GT(kke::tetVolume(cut.vertices[t[0]], cut.vertices[t[1]], cut.vertices[t[2]], cut.vertices[t[3]]), 0.0f);
    double outside = 0.0;
    for (const auto& [k, fi] : faces(cut)) if (fi.uses == 1) outside += area(cut, k);
    EXPECT_NEAR(outside, 6.0, 1e-3);
    for (const glm::vec3& v : cut.vertices) EXPECT_LE(std::max({ std::fabs(v.x), std::fabs(v.y), std::fabs(v.z) }), 0.5f + 1e-5f);
    EXPECT_EQ(b.flags.size(), cut.tets.size());
}

// The whole point: cracks follow the Voronoi planes (random angles), not
// the voxel grid. Measured as the area-weighted distance of crack faces
// from the plane between their two pieces, against the voxel size.
TEST(VoronoiFracture, CracksFollowTheVoronoiPlanes) {
    const float cell = 0.2f;
    kke::TetMeshData m = box(glm::vec3(1.0f), cell);
    kke::BakedFracture b = kke::bakeFracture(m, opts(kke::FracturePattern::Voronoi, 0.35f, 7));
    const auto& cut = b.cut.mesh;
    std::map<std::array<uint32_t, 3>, std::vector<uint32_t>> owners;
    for (uint32_t t = 0; t < cut.tets.size(); ++t)
        for (int f = 0; f < 4; ++f) {
            std::array<uint32_t, 3> k{};
            int j = 0;
            for (int i = 0; i < 4; ++i) if (i != f) k[j++] = cut.tets[t][i];
            std::sort(k.begin(), k.end());
            owners[k].push_back(t);
        }
    double crackArea = 0.0, weighted = 0.0, obliqueArea = 0.0;
    for (const auto& [k, ts] : owners) {
        if (ts.size() != 2) continue;
        uint32_t a = b.cut.chunkOfTet[ts[0]], c = b.cut.chunkOfTet[ts[1]];
        if (a == c) continue;
        glm::vec3 sa = b.seeds.points[a], sc = b.seeds.points[c];
        glm::vec3 n = glm::normalize(sc - sa);
        float d = glm::dot(n, (sa + sc) * 0.5f);
        glm::vec3 centre = (cut.vertices[k[0]] + cut.vertices[k[1]] + cut.vertices[k[2]]) / 3.0f;
        double ar = area(cut, k);
        crackArea += ar;
        weighted += ar * std::fabs(glm::dot(n, centre) - d);
        glm::vec3 fn = glm::normalize(glm::cross(cut.vertices[k[1]] - cut.vertices[k[0]], cut.vertices[k[2]] - cut.vertices[k[0]]));
        if (std::max({ std::fabs(fn.x), std::fabs(fn.y), std::fabs(fn.z) }) < 0.97f) obliqueArea += ar;
    }
    ASSERT_GT(crackArea, 0.5);
    EXPECT_LT(weighted / crackArea, 0.2 * cell);   // on the plane, give or take a fifth of a voxel
    EXPECT_GT(obliqueArea / crackArea, 0.6);
}

// Tets stay well shaped: FEMFX's aspect ratio (longest edge / smallest
// height) stays <= 10. FEMFX refuses meshes over 25 outright and goes
// unstable before that (BUG-043).
TEST(VoronoiFracture, TetsStayWellShapedForFemfx) {
    auto aspect = [](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
        glm::vec3 p[4] = { a, b, c, d };
        float maxEdge = 0.0f;
        for (int i = 0; i < 4; ++i) for (int j = i + 1; j < 4; ++j) maxEdge = std::max(maxEdge, glm::length(p[i] - p[j]));
        float vol6 = std::fabs(glm::dot(b - a, glm::cross(c - a, d - a)));
        float minH = 1e30f;
        for (int i = 0; i < 4; ++i) minH = std::min(minH, vol6 / glm::length(glm::cross(p[(i + 2) % 4] - p[(i + 1) % 4], p[(i + 3) % 4] - p[(i + 1) % 4])));
        return maxEdge / minH;
    };
    kke::TetMeshData m = box(glm::vec3(1.0f), 0.2f);
    for (auto pattern : { kke::FracturePattern::Voronoi, kke::FracturePattern::Splinters, kke::FracturePattern::Shards, kke::FracturePattern::Radial }) {
        for (uint32_t seed = 1; seed <= 5; ++seed) {
            kke::BakedFracture b = kke::bakeFracture(m, opts(pattern, 0.35f, seed));
            const auto& v = b.cut.mesh.vertices;
            for (const auto& id : b.cut.mesh.tets) {
                EXPECT_GT(kke::tetVolume(v[id[0]], v[id[1]], v[id[2]], v[id[3]]), 0.0f);
                EXPECT_LE(aspect(v[id[0]], v[id[1]], v[id[2]], v[id[3]]), 10.01f) << kke::fracturePatternName(pattern) << " seed " << seed;
            }
        }
    }
}

// Every piece is one face-connected lump of several tets (no single-tet
// shards held on by an edge).
TEST(VoronoiFracture, PiecesAreConnectedLumps) {
    kke::TetMeshData m = box(glm::vec3(1.0f, 0.5f, 0.5f), 0.1f);
    kke::BakedFracture b = kke::bakeFracture(m, opts(kke::FracturePattern::Shards, 0.25f, 13));
    // Components among same-piece tets = pieces.
    kke::TetMeshData dummy = b.cut.mesh;
    std::vector<uint32_t> parent(dummy.tets.size());
    for (uint32_t i = 0; i < parent.size(); ++i) parent[i] = i;
    auto find = [&](uint32_t x) { while (parent[x] != x) x = parent[x] = parent[parent[x]]; return x; };
    std::map<std::array<uint32_t, 3>, uint32_t> open;
    for (uint32_t t = 0; t < dummy.tets.size(); ++t)
        for (int f = 0; f < 4; ++f) {
            std::array<uint32_t, 3> k{};
            int j = 0;
            for (int i = 0; i < 4; ++i) if (i != f) k[j++] = dummy.tets[t][i];
            std::sort(k.begin(), k.end());
            auto it = open.find(k);
            if (it == open.end()) { open[k] = t; continue; }
            if (b.cut.chunkOfTet[it->second] == b.cut.chunkOfTet[t]) parent[find(t)] = find(it->second);
        }
    std::map<uint32_t, size_t> sizes;
    for (uint32_t t = 0; t < parent.size(); ++t) ++sizes[find(t)];
    EXPECT_EQ(sizes.size(), b.pieces);
    for (const auto& [c, n] : sizes) EXPECT_GE(n, 4u);
}

TEST(VoronoiFracture, SameSeedSamePiecesOtherSeedOtherPieces) {
    kke::TetMeshData m = box(glm::vec3(1.0f, 0.5f, 0.5f), 0.125f);
    auto a = kke::bakeFracture(m, opts(kke::FracturePattern::Voronoi, 0.25f, 99));
    auto b = kke::bakeFracture(m, opts(kke::FracturePattern::Voronoi, 0.25f, 99));
    auto c = kke::bakeFracture(m, opts(kke::FracturePattern::Voronoi, 0.25f, 100));
    ASSERT_EQ(a.cut.mesh.vertices.size(), b.cut.mesh.vertices.size());
    EXPECT_EQ(a.cut.mesh.tets, b.cut.mesh.tets);
    EXPECT_EQ(a.cut.chunkOfTet, b.cut.chunkOfTet);
    EXPECT_NE(a.cut.chunkOfTet, c.cut.chunkOfTet);
    EXPECT_EQ(a.cut.mesh.vertices, b.cut.mesh.vertices);
}

// Wood: pieces are long along the grain (the plank's long axis).
TEST(VoronoiFracture, SplintersRunAlongTheGrain) {
    kke::TetMeshData m = box(glm::vec3(2.0f, 0.3f, 0.3f), 0.1f);
    auto b = kke::bakeFracture(m, opts(kke::FracturePattern::Splinters, 0.3f, 5));
    std::map<uint32_t, std::pair<glm::vec3, glm::vec3>> ext;
    for (uint32_t t = 0; t < b.cut.mesh.tets.size(); ++t) {
        auto& e = ext.try_emplace(b.cut.chunkOfTet[t], glm::vec3(1e9f), glm::vec3(-1e9f)).first->second;
        for (uint32_t v : b.cut.mesh.tets[t]) { e.first = glm::min(e.first, b.cut.mesh.vertices[v]); e.second = glm::max(e.second, b.cut.mesh.vertices[v]); }
    }
    double along = 0.0, across = 0.0;
    for (const auto& [id, e] : ext) {
        glm::vec3 s = e.second - e.first;
        along += s.x;
        across += std::max(s.y, s.z);
    }
    EXPECT_GE(ext.size(), 4u);
    EXPECT_GT(along / across, 1.8);
}

// Glass: small shards at the impact, big ones at the edge.
TEST(VoronoiFracture, GlassShardsGrowAwayFromTheImpact) {
    kke::TetMeshData m = box(glm::vec3(2.0f, 0.1f, 2.0f), 0.1f);
    kke::FractureSeedOptions o = opts(kke::FracturePattern::Radial, 0.4f, 3);
    o.hasImpactPoint = true;
    o.impactPoint = glm::vec3(0.0f);
    auto b = kke::bakeFracture(m, o);
    std::map<uint32_t, double> vol;
    std::map<uint32_t, glm::vec3> centre;
    for (uint32_t t = 0; t < b.cut.mesh.tets.size(); ++t) {
        const auto& id = b.cut.mesh.tets[t];
        double v = kke::tetVolume(b.cut.mesh.vertices[id[0]], b.cut.mesh.vertices[id[1]], b.cut.mesh.vertices[id[2]], b.cut.mesh.vertices[id[3]]);
        vol[b.cut.chunkOfTet[t]] += v;
        centre[b.cut.chunkOfTet[t]] += glm::vec3(b.cut.mesh.vertices[id[0]]) * static_cast<float>(v);
    }
    double nearSum = 0.0, farSum = 0.0;
    int nearN = 0, farN = 0;
    for (const auto& [c, v] : vol) {
        float r = glm::length(glm::vec2(centre[c].x, centre[c].z) / static_cast<float>(v));
        if (r < 0.35f) { nearSum += v; ++nearN; }
        else if (r > 0.7f) { farSum += v; ++farN; }
    }
    ASSERT_GT(nearN, 0);
    ASSERT_GT(farN, 0);
    EXPECT_LT(nearSum / nearN, farSum / farN);
    EXPECT_GE(b.pieces, 12u);
}

TEST(VoronoiFracture, ClustersMakeInnerCracksTougher) {
    kke::TetMeshData m = box(glm::vec3(1.0f), 0.2f);
    kke::FractureSeedOptions o = opts(kke::FracturePattern::Voronoi, 0.3f, 11);
    o.cellsPerCluster = 4;
    auto b = kke::bakeFracture(m, o);
    size_t tough = std::count_if(b.cut.tetStrength.begin(), b.cut.tetStrength.end(), [](float s) { return s > 1.0f; });
    EXPECT_GT(tough, 0u);
    EXPECT_LT(tough, b.cut.tetStrength.size());
}

TEST(VoronoiFracture, SolidIsUntouched) {
    kke::TetMeshData m = box(glm::vec3(1.0f), 0.25f);
    auto b = kke::bakeFracture(m, opts(kke::FracturePattern::Solid, 0.3f, 1));
    EXPECT_EQ(b.cut.mesh.tets, m.tets);
    EXPECT_EQ(b.pieces, 1u);
}
