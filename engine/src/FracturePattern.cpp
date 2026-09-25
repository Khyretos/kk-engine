#include "kke/FracturePattern.h"
#include "kke/VoxelTets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace kke {

namespace {

// Tiny, fast, repeatable RNG (xorshift32): same seed, same pattern on
// every platform — std::mt19937 distributions aren't portable.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float unit() { return (next() >> 8) * (1.0f / 16777216.0f); } // [0,1)
    float signedUnit() { return unit() * 2.0f - 1.0f; }
};

// The three vertex ids of tet face n (opposite corner n), sorted: a key
// that matches between the two tets sharing the face.
std::array<uint32_t, 3> faceKey(const std::array<uint32_t, 4>& tet, int face) {
    std::array<uint32_t, 3> k{};
    int j = 0;
    for (int i = 0; i < 4; ++i) if (i != face) k[j++] = tet[i];
    std::sort(k.begin(), k.end());
    return k;
}

struct KeyHash {
    size_t operator()(const std::array<uint32_t, 3>& k) const {
        return (static_cast<size_t>(k[0]) * 73856093u) ^ (static_cast<size_t>(k[1]) * 19349663u) ^ (static_cast<size_t>(k[2]) * 83492791u);
    }
};

// Where a tet "is" for chunking: the midpoint of its corners 0 and 3.
// For voxel meshes (Kuhn split) all 6 tets of a cell share exactly those
// two corners, so this is the cell centre and whole cells stay together.
// That matters: chunks that cut cells apart are joined through sliver-
// thin tets, and FEMFX's solver blew those up on impact (BUG-043). For
// any other tet mesh it's simply a point inside the tet.
glm::vec3 centroid(const TetMeshData& m, size_t t) {
    const auto& ids = m.tets[t];
    return (m.vertices[ids[0]] + m.vertices[ids[3]]) * 0.5f;
}

} // namespace

const char* fracturePatternName(FracturePattern p) {
    switch (p) {
    case FracturePattern::Shards: return "Shards";
    case FracturePattern::Voronoi: return "Voronoi chunks";
    case FracturePattern::Splinters: return "Splinters";
    case FracturePattern::Radial: return "Radial (glass)";
    case FracturePattern::Solid: return "Solid (no fracture)";
    }
    return "?";
}

std::vector<uint32_t> fractureChunks(const TetMeshData& mesh, FracturePattern pattern, float chunkSize, uint32_t seed) {
    const size_t n = mesh.tets.size();
    std::vector<uint32_t> chunk(n, 0);
    if (n == 0) return chunk;
    if (pattern == FracturePattern::Shards) {
        for (size_t t = 0; t < n; ++t) chunk[t] = static_cast<uint32_t>(t);
        return chunk;
    }
    if (pattern == FracturePattern::Solid) return chunk;

    std::vector<glm::vec3> c(n);
    glm::vec3 mn(std::numeric_limits<float>::max()), mx(-std::numeric_limits<float>::max());
    for (size_t t = 0; t < n; ++t) {
        c[t] = centroid(mesh, t);
        mn = glm::min(mn, c[t]);
        mx = glm::max(mx, c[t]);
    }
    const glm::vec3 size = glm::max(mx - mn, glm::vec3(1e-3f));
    chunkSize = std::max(chunkSize, 1e-3f);
    Rng rng(seed);

    if (pattern == FracturePattern::Radial) {
        // Glass: the thinnest axis is the sheet's normal; cracks radiate
        // from the middle of the sheet and rings get wider outward (like
        // a real impact star: small shards near the centre, long ones out).
        int normalAxis = (size.x <= size.y && size.x <= size.z) ? 0 : (size.y <= size.z ? 1 : 2);
        int ua = (normalAxis + 1) % 3, va = (normalAxis + 2) % 3;
        glm::vec3 center = (mn + mx) * 0.5f;
        const float ringGrowth = 1.7f;
        float ringPhase[64];
        for (float& p : ringPhase) p = rng.unit() * 6.2831853f;
        for (size_t t = 0; t < n; ++t) {
            glm::vec3 d = c[t] - center;
            float u = d[ua], v = d[va];
            float r = std::sqrt(u * u + v * v);
            int ring = std::min(63, static_cast<int>(std::log(1.0f + r / (chunkSize * 0.35f)) / std::log(ringGrowth)));
            int sectors = 5 + ring * 3;
            // Wobble the spokes a little so they aren't ruler-straight.
            float angle = std::atan2(v, u) + ringPhase[ring] + 0.25f * std::sin(r * 7.0f + ringPhase[ring]);
            float a01 = angle / 6.2831853f;
            a01 -= std::floor(a01);
            int sector = static_cast<int>(a01 * sectors) % sectors;
            chunk[t] = static_cast<uint32_t>(ring * 1000 + sector);
        }
        return chunk;
    }

    // Voronoi / splinters: seeds at random tet centroids (so every seed is
    // inside the object, even for thin or hollow shapes).
    glm::vec3 metric(1.0f);
    float cellVolume = chunkSize * chunkSize * chunkSize;
    if (pattern == FracturePattern::Splinters) {
        int grain = (size.x >= size.y && size.x >= size.z) ? 0 : (size.y >= size.z ? 1 : 2);
        metric[grain] = 0.25f; // distance along the grain counts 4x less: long chunks
        cellVolume *= 0.5f;    // more, thinner splinters
    }
    float objectVolume = 0.0f;
    for (size_t t = 0; t < n; ++t) {
        const auto& ids = mesh.tets[t];
        objectVolume += std::fabs(tetVolume(mesh.vertices[ids[0]], mesh.vertices[ids[1]], mesh.vertices[ids[2]], mesh.vertices[ids[3]]));
    }
    size_t seeds = static_cast<size_t>(std::round(objectVolume / cellVolume));
    // A chunk can't be finer than the tets it's made of: keep ~12 tets
    // (two voxel cells) per chunk on average, or chunks degenerate into
    // the single-tet shards this is meant to avoid.
    seeds = std::clamp<size_t>(seeds, 2, std::max<size_t>(2, std::min<size_t>(64, n / 12)));
    std::vector<glm::vec3> seedPos(seeds);
    for (glm::vec3& s : seedPos) {
        s = c[rng.next() % n] + glm::vec3(rng.signedUnit(), rng.signedUnit(), rng.signedUnit()) * (chunkSize * 0.25f);
    }
    for (size_t t = 0; t < n; ++t) {
        float best = std::numeric_limits<float>::max();
        for (size_t s = 0; s < seeds; ++s) {
            glm::vec3 d = (c[t] - seedPos[s]) * metric;
            float dd = glm::dot(d, d);
            if (dd < best) { best = dd; chunk[t] = static_cast<uint32_t>(s); }
        }
    }
    return chunk;
}

std::vector<uint16_t> fractureFlagsFromChunks(const TetMeshData& mesh, const std::vector<uint32_t>& chunkOfTet) {
    std::vector<uint16_t> flags(mesh.tets.size(), 0);
    std::unordered_map<std::array<uint32_t, 3>, std::pair<uint32_t, int>, KeyHash> open;
    open.reserve(mesh.tets.size() * 2);
    for (uint32_t t = 0; t < mesh.tets.size(); ++t) {
        for (int f = 0; f < 4; ++f) {
            auto key = faceKey(mesh.tets[t], f);
            auto it = open.find(key);
            if (it == open.end()) {
                open.emplace(key, std::make_pair(t, f));
                continue;
            }
            auto [other, otherFace] = it->second;
            if (chunkOfTet[t] == chunkOfTet[other]) {
                flags[t] |= static_cast<uint16_t>(0x1u << f);            // FM_TET_FLAG_FACE0_FRACTURE_DISABLED << f
                flags[other] |= static_cast<uint16_t>(0x1u << otherFace);
            }
            open.erase(it);
        }
    }
    return flags;
}

void jitterInteriorVertices(TetMeshData& mesh, float amount, uint32_t seed) {
    if (amount <= 0.0f || mesh.tets.empty()) return;
    // Surface vertices = those on a face used by only one tet.
    std::unordered_map<std::array<uint32_t, 3>, int, KeyHash> faceUse;
    for (const auto& t : mesh.tets) for (int f = 0; f < 4; ++f) ++faceUse[faceKey(t, f)];
    std::vector<uint8_t> surface(mesh.vertices.size(), 0);
    for (const auto& [key, uses] : faceUse) if (uses == 1) for (uint32_t v : key) surface[v] = 1;
    std::vector<std::vector<uint32_t>> incident(mesh.vertices.size());
    for (uint32_t t = 0; t < mesh.tets.size(); ++t) for (uint32_t v : mesh.tets[t]) incident[v].push_back(t);
    auto volume = [&](uint32_t t) {
        const auto& ids = mesh.tets[t];
        return tetVolume(mesh.vertices[ids[0]], mesh.vertices[ids[1]], mesh.vertices[ids[2]], mesh.vertices[ids[3]]);
    };
    std::vector<float> restVolume(mesh.tets.size());
    for (uint32_t t = 0; t < mesh.tets.size(); ++t) restVolume[t] = volume(t);
    Rng rng(seed);
    for (uint32_t v = 0; v < mesh.vertices.size(); ++v) {
        glm::vec3 offset(rng.signedUnit(), rng.signedUnit(), rng.signedUnit());
        if (surface[v]) continue;
        glm::vec3 old = mesh.vertices[v];
        mesh.vertices[v] = old + offset * amount;
        bool ok = true;
        for (uint32_t t : incident[v]) {
            float vol = volume(t);
            // Same sign and within 60%..160% of the original volume: FEMFX
            // is sensitive to badly shaped tets (they blew a chunked crate up
            // to 15,000 km on impact with a 25% bound, BUG-043).
            if (vol * restVolume[t] <= 0.0f || std::fabs(vol) < std::fabs(restVolume[t]) * 0.6f || std::fabs(vol) > std::fabs(restVolume[t]) * 1.6f) { ok = false; break; }
        }
        if (!ok) mesh.vertices[v] = old;
    }
}

std::vector<uint32_t> faceConnectedComponents(const TetMeshData& mesh) {
    const uint32_t n = static_cast<uint32_t>(mesh.tets.size());
    std::vector<uint32_t> parent(n);
    for (uint32_t i = 0; i < n; ++i) parent[i] = i;
    auto find = [&](uint32_t x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    };
    std::unordered_map<std::array<uint32_t, 3>, uint32_t, KeyHash> open;
    for (uint32_t t = 0; t < n; ++t) {
        for (int f = 0; f < 4; ++f) {
            auto key = faceKey(mesh.tets[t], f);
            auto it = open.find(key);
            if (it == open.end()) { open.emplace(key, t); continue; }
            uint32_t a = find(t), b = find(it->second);
            if (a != b) parent[a] = b;
            open.erase(it);
        }
    }
    std::vector<uint32_t> comp(n);
    for (uint32_t i = 0; i < n; ++i) comp[i] = find(i);
    return comp;
}

size_t chunkCount(const std::vector<uint32_t>& chunkOfTet) {
    return std::unordered_set<uint32_t>(chunkOfTet.begin(), chunkOfTet.end()).size();
}

} // namespace kke
