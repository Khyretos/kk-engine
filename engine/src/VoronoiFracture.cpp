#include "kke/VoronoiFracture.h"
#include "kke/VoxelTets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace kke {

namespace {

using dvec3 = glm::dvec3;

// Same xorshift as FracturePattern.cpp: portable, so a seed gives the
// same pieces on every platform (std:: distributions are not portable).
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float unit() { return (next() >> 8) * (1.0f / 16777216.0f); } // [0,1)
    float signedUnit() { return unit() * 2.0f - 1.0f; }
};

// ---------------------------------------------------------------- seeds

struct MeshSampler {
    const TetMeshData& mesh;
    std::vector<float> cumulative; // running tet volume
    float volume = 0.0f;
    glm::vec3 mn{std::numeric_limits<float>::max()}, mx{-std::numeric_limits<float>::max()};

    explicit MeshSampler(const TetMeshData& m) : mesh(m) {
        cumulative.reserve(m.tets.size());
        for (const auto& t : m.tets) {
            volume += std::fabs(tetVolume(m.vertices[t[0]], m.vertices[t[1]], m.vertices[t[2]], m.vertices[t[3]]));
            cumulative.push_back(volume);
        }
        for (const glm::vec3& v : m.vertices) { mn = glm::min(mn, v); mx = glm::max(mx, v); }
    }
    // Uniform point inside the solid (so seeds land inside hollow or
    // concave shapes too): tet picked by volume, then a uniform point in
    // it (normalised exponentials = uniform barycentric coordinates).
    glm::vec3 randomPoint(Rng& rng) const {
        float pick = rng.unit() * volume;
        size_t t = static_cast<size_t>(std::lower_bound(cumulative.begin(), cumulative.end(), pick) - cumulative.begin());
        t = std::min(t, mesh.tets.size() - 1);
        float w[4], sum = 0.0f;
        for (float& x : w) { x = -std::log(std::max(1e-7f, rng.unit())); sum += x; }
        glm::vec3 p(0.0f);
        for (int i = 0; i < 4; ++i) p += mesh.vertices[mesh.tets[t][i]] * (w[i] / sum);
        return p;
    }
};

// Random points inside the mesh, spread out: candidates closer than
// `minDist` (in metric space) to an accepted one are rejected (a cheap
// Poisson-disc). Pure random seeds give a mix of huge chunks and tiny
// slivers; real breaks are more even than that.
std::vector<glm::vec3> scatter(const MeshSampler& sampler, Rng& rng, size_t count, float minDist, const glm::vec3& metric) {
    std::vector<glm::vec3> out;
    const size_t maxTries = count * 30;
    float d2 = minDist * minDist;
    for (size_t tries = 0; out.size() < count && tries < maxTries; ++tries) {
        glm::vec3 p = sampler.randomPoint(rng);
        bool ok = true;
        for (const glm::vec3& q : out) {
            glm::vec3 d = (p - q) * metric;
            if (glm::dot(d, d) < d2) { ok = false; break; }
        }
        if (ok) out.push_back(p);
        // Running out of room: relax the spacing rather than give up.
        if (tries > 0 && tries % (count * 10) == 0) d2 *= 0.5f;
    }
    return out;
}

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

} // namespace

uint32_t fractureSeed(uint32_t globalSeed, uint32_t objectSeed) {
    uint64_t z = (static_cast<uint64_t>(globalSeed) << 32 | objectSeed) + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    uint32_t s = static_cast<uint32_t>(z ^ (z >> 32));
    return s ? s : 1u;
}

FractureSeeds makeFractureSeeds(const TetMeshData& mesh, const FractureSeedOptions& o) {
    FractureSeeds out;
    if (mesh.tets.empty() || o.pattern == FracturePattern::Solid) return out;
    MeshSampler sampler(mesh);
    Rng rng(o.seed);
    const float chunk = std::max(o.chunkSize, 1e-3f);
    const glm::vec3 size = glm::max(sampler.mx - sampler.mn, glm::vec3(1e-4f));
    const size_t maxPieces = std::max<size_t>(2, o.maxPieces);
    auto countFor = [&](float cellVolume) {
        return std::clamp<size_t>(static_cast<size_t>(std::round(sampler.volume / cellVolume)), 2, maxPieces);
    };

    switch (o.pattern) {
    case FracturePattern::Voronoi: {
        // Stone, brick, concrete: even, chunky cells.
        size_t n = countFor(chunk * chunk * chunk);
        out.points = scatter(sampler, rng, n, chunk * 0.5f, out.metric);
        break;
    }
    case FracturePattern::Splinters: {
        // Wood: distance along the grain (the longest axis) counts 3.5x
        // less, so every cell is a long splinter along it.
        int grain = (size.x >= size.y && size.x >= size.z) ? 0 : (size.y >= size.z ? 1 : 2);
        out.metric[grain] = 1.0f / 3.5f;
        size_t n = countFor(chunk * chunk * chunk * 0.45f);
        out.points = scatter(sampler, rng, n, chunk * 0.35f, out.metric);
        break;
    }
    case FracturePattern::Shards: {
        // Ceramic, pottery, ice: many sharp pieces, stretched along a
        // random axis so they read as shards rather than pebbles.
        int axis = static_cast<int>(rng.next() % 3);
        out.metric[axis] = 0.5f;
        size_t n = countFor(chunk * chunk * chunk * 0.3f);
        out.points = scatter(sampler, rng, n, chunk * 0.3f, out.metric);
        break;
    }
    case FracturePattern::Radial: {
        // Glass: a spider-web. Seeds on rings around the impact point in
        // the sheet's mid-plane (thinnest axis = normal), rings growing
        // geometrically, more sectors further out, both jittered. Their
        // Voronoi cells are the wedges and arcs of a real impact star, and
        // every cell goes through the full thickness.
        int nAxis = (size.x <= size.y && size.x <= size.z) ? 0 : (size.y <= size.z ? 1 : 2);
        int ua = (nAxis + 1) % 3, va = (nAxis + 2) % 3;
        glm::vec3 center = (sampler.mn + sampler.mx) * 0.5f;
        if (o.hasImpactPoint) { center[ua] = o.impactPoint[ua]; center[va] = o.impactPoint[va]; }
        const float maxR = std::sqrt(size[ua] * size[ua] + size[va] * size[va]);
        out.points.push_back(center);
        float r = chunk * 0.3f;
        for (int ring = 0; out.points.size() < maxPieces && r < maxR; ++ring) {
            int sectors = 5 + 2 * ring;
            float phase = rng.unit() * 6.2831853f;
            for (int s = 0; s < sectors && out.points.size() < maxPieces; ++s) {
                float a = phase + (s + 0.35f * rng.signedUnit()) * 6.2831853f / sectors;
                float rr = r * (1.0f + 0.18f * rng.signedUnit());
                glm::vec3 p = center;
                p[ua] += std::cos(a) * rr;
                p[va] += std::sin(a) * rr;
                // Seeds beyond the pane only make empty cells.
                if (p[ua] < sampler.mn[ua] - chunk || p[ua] > sampler.mx[ua] + chunk || p[va] < sampler.mn[va] - chunk || p[va] > sampler.mx[va] + chunk) continue;
                out.points.push_back(p);
            }
            r *= 1.65f;
        }
        break;
    }
    case FracturePattern::Solid: break;
    }

    if (o.cellsPerCluster > 0 && out.points.size() >= 4) {
        // Clusters: the first k seeds (random, spread) are cluster centres.
        size_t k = std::max<size_t>(2, out.points.size() / static_cast<size_t>(o.cellsPerCluster));
        out.clusterOfSeed.resize(out.points.size());
        for (size_t i = 0; i < out.points.size(); ++i) {
            float best = std::numeric_limits<float>::max();
            for (size_t c = 0; c < k; ++c) {
                glm::vec3 d = (out.points[i] - out.points[c]) * out.metric;
                float dd = glm::dot(d, d);
                if (dd < best) { best = dd; out.clusterOfSeed[i] = static_cast<uint32_t>(c); }
            }
        }
    }
    return out;
}

VoronoiCut cutAlongVoronoi(const TetMeshData& mesh, const FractureSeeds& seeds) {
    VoronoiCut out;
    out.mesh = mesh;
    const size_t nTets = mesh.tets.size();
    const size_t nSeeds = seeds.points.size();
    out.chunkOfTet.assign(nTets, 0);
    out.tetStrength.assign(nTets, 1.0f);
    if (nTets == 0 || nSeeds < 2) return out;

    // Metric space: Voronoi there is plain Euclidean.
    const dvec3 metric(seeds.metric);
    std::vector<dvec3> q(mesh.vertices.size());
    for (size_t i = 0; i < q.size(); ++i) q[i] = dvec3(mesh.vertices[i]) * metric;
    std::vector<dvec3> s(nSeeds);
    for (size_t i = 0; i < nSeeds; ++i) s[i] = dvec3(seeds.points[i]) * metric;
    auto nearest = [&](const dvec3& p) {
        uint32_t best = 0;
        double bd = std::numeric_limits<double>::max();
        for (uint32_t i = 0; i < nSeeds; ++i) {
            double d = glm::dot(p - s[i], p - s[i]);
            if (d < bd) { bd = d; best = i; }
        }
        return best;
    };
    auto volume = [&](const std::array<uint32_t, 4>& id) {
        return glm::dot(q[id[1]] - q[id[0]], glm::cross(q[id[2]] - q[id[0]], q[id[3]] - q[id[0]]));
    };
    double edgeSum = 0.0;
    for (const auto& t : mesh.tets) edgeSum += glm::length(q[t[1]] - q[t[0]]) + glm::length(q[t[2]] - q[t[0]]) + glm::length(q[t[3]] - q[t[0]]);
    const double L = std::max(1e-9, edgeSum / (3.0 * nTets));

    // Face adjacency (tet, face) -> neighbour tet.
    std::vector<std::array<uint32_t, 4>> neighbour(nTets, { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX });
    {
        std::unordered_map<std::array<uint32_t, 3>, std::pair<uint32_t, int>, KeyHash> open;
        open.reserve(nTets * 2);
        for (uint32_t t = 0; t < nTets; ++t)
            for (int f = 0; f < 4; ++f) {
                auto key = faceKey(mesh.tets[t], f);
                auto it = open.find(key);
                if (it == open.end()) { open.emplace(key, std::make_pair(t, f)); continue; }
                neighbour[t][f] = it->second.first;
                neighbour[it->second.first][it->second.second] = t;
                open.erase(it);
            }
    }

    // 1. Each tet joins the cell its centre is in. Tets are small next to
    // the cells, so this is a staircase version of the Voronoi diagram;
    // step 3 flattens the stairs onto the real planes.
    std::vector<uint32_t>& chunk = out.chunkOfTet;
    for (uint32_t t = 0; t < nTets; ++t) {
        const auto& id = mesh.tets[t];
        chunk[t] = nearest((q[id[0]] + q[id[1]] + q[id[2]] + q[id[3]]) * 0.25);
    }

    // 2. Every piece must be one face-connected lump of a few tets. FEMFX
    // can't hold tets together through an edge or a corner, so a stray
    // tet joined to its cell only by an edge would fall off as a single
    // shard. Stray lumps (and cells too small to be a piece) join the
    // neighbouring cell they share the most faces with.
    constexpr size_t kMinPieceTets = 4;
    for (int pass = 0; pass < 6; ++pass) {
        std::vector<uint32_t> parent(nTets);
        for (uint32_t i = 0; i < nTets; ++i) parent[i] = i;
        auto find = [&](uint32_t x) { while (parent[x] != x) x = parent[x] = parent[parent[x]]; return x; };
        for (uint32_t t = 0; t < nTets; ++t)
            for (int f = 0; f < 4; ++f) {
                uint32_t n = neighbour[t][f];
                if (n != UINT32_MAX && chunk[n] == chunk[t]) { uint32_t a = find(t), b = find(n); if (a != b) parent[a] = b; }
            }
        std::unordered_map<uint32_t, size_t> compSize;
        for (uint32_t t = 0; t < nTets; ++t) ++compSize[find(t)];
        std::unordered_map<uint32_t, uint32_t> mainComp; // chunk -> its largest component
        for (uint32_t t = 0; t < nTets; ++t) {
            uint32_t c = find(t);
            auto it = mainComp.find(chunk[t]);
            if (it == mainComp.end() || compSize[c] > compSize[it->second]) mainComp[chunk[t]] = c;
        }
        // Components to move: not their cell's main lump, or too small.
        std::unordered_map<uint32_t, std::unordered_map<uint32_t, int>> votes; // component -> neighbour chunk -> shared faces
        for (uint32_t t = 0; t < nTets; ++t) {
            uint32_t c = find(t);
            if (mainComp[chunk[t]] == c && compSize[c] >= kMinPieceTets) continue;
            for (int f = 0; f < 4; ++f) {
                uint32_t n = neighbour[t][f];
                if (n != UINT32_MAX && chunk[n] != chunk[t]) ++votes[c][chunk[n]];
            }
        }
        if (votes.empty()) break;
        std::unordered_map<uint32_t, uint32_t> moveTo;
        for (const auto& [c, v] : votes) {
            uint32_t best = 0;
            int bestN = -1;
            for (const auto& [ch, n] : v) if (n > bestN || (n == bestN && ch < best)) { best = ch; bestN = n; }
            moveTo[c] = best;
        }
        for (uint32_t t = 0; t < nTets; ++t) {
            auto it = moveTo.find(find(t));
            if (it != moveTo.end()) { chunk[t] = it->second; ++out.mergedFragments; }
        }
    }

    // 3. Flatten the stairs: every vertex on a border between pieces moves
    // onto the Voronoi plane(s) between them - a plane for two pieces, a
    // line for three, a point for four - by a small least-squares solve.
    // Surface vertices also keep to the surface (one constraint per
    // distinct surface normal: flat parts slide, edges slide along the
    // edge, corners stay put), so the outer shape doesn't change. Moves
    // that would flip a tet or make it too thin for FEMFX (see aspect()
    // below) are shortened, then skipped: the crack stays a little rough there,
    // which is how real cracks look anyway.
    std::vector<std::vector<uint32_t>> incident(q.size());
    for (uint32_t t = 0; t < nTets; ++t) for (uint32_t v : mesh.tets[t]) incident[v].push_back(t);
    std::vector<std::vector<dvec3>> surfaceNormals(q.size());
    for (uint32_t t = 0; t < nTets; ++t)
        for (int f = 0; f < 4; ++f) {
            if (neighbour[t][f] != UINT32_MAX) continue;
            auto key = faceKey(mesh.tets[t], f);
            dvec3 n = glm::cross(q[key[1]] - q[key[0]], q[key[2]] - q[key[0]]);
            double len = glm::length(n);
            if (len < 1e-18) continue;
            n /= len;
            for (uint32_t v : key) {
                auto& list = surfaceNormals[v];
                bool known = false;
                for (const dvec3& m : list) known |= std::fabs(glm::dot(m, n)) > 0.95;
                if (!known && list.size() < 3) list.push_back(n);
            }
        }
    // FEMFX's own quality measure (FmComputeTetAspectRatio): longest edge
    // over the smallest corner-to-opposite-face height. It refuses meshes
    // above 25 (FmFinishTetMeshInit fails) and is unstable well before
    // that, so moves must keep every tet at <= 10 (or no worse than 1.25x
    // a tet that already started above that).
    auto aspect = [&](const std::array<uint32_t, 4>& id) {
        const dvec3 p[4] = { q[id[0]] / metric, q[id[1]] / metric, q[id[2]] / metric, q[id[3]] / metric }; // real space, not metric space
        double maxEdge = 0.0;
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j) maxEdge = std::max(maxEdge, glm::length(p[i] - p[j]));
        const double vol6 = std::fabs(glm::dot(p[1] - p[0], glm::cross(p[2] - p[0], p[3] - p[0])));
        double minHeight = std::numeric_limits<double>::max();
        for (int i = 0; i < 4; ++i) {
            const dvec3& a = p[(i + 1) % 4];
            const dvec3& b = p[(i + 2) % 4];
            const dvec3& c = p[(i + 3) % 4];
            double area2 = glm::length(glm::cross(b - a, c - a));
            if (area2 > 0.0) minHeight = std::min(minHeight, vol6 / area2);
        }
        return minHeight > 0.0 ? maxEdge / minHeight : std::numeric_limits<double>::max();
    };
    std::vector<double> rest(nTets), maxAspect(nTets);
    for (uint32_t t = 0; t < nTets; ++t) {
        rest[t] = volume(mesh.tets[t]);
        maxAspect[t] = std::max(10.0, aspect(mesh.tets[t]) * 1.25);
    }
    std::vector<uint8_t> snapped(q.size(), 0);
    for (int pass = 0; pass < 3; ++pass) {
        for (uint32_t v = 0; v < q.size(); ++v) {
            uint32_t cs[8];
            int nc = 0;
            for (uint32_t t : incident[v]) {
                uint32_t c = chunk[t];
                bool seen = false;
                for (int i = 0; i < nc; ++i) seen |= cs[i] == c;
                if (!seen && nc < 8) cs[nc++] = c;
            }
            if (nc < 2) continue;
            std::sort(cs, cs + nc);
            // Least squares in the directions the vertex may move in: the
            // surface's tangent plane (or edge line) for surface vertices,
            // anywhere for interior ones. Minimise the distance to each
            // bisector (a, other) plus a little of the move itself.
            dvec3 T[3];
            int m = 0;
            {
                dvec3 N[3];
                int k = 0;
                for (const dvec3& n : surfaceNormals[v]) {
                    dvec3 u = n;
                    for (int i = 0; i < k; ++i) u -= N[i] * glm::dot(u, N[i]);
                    double ul = glm::length(u);
                    if (ul > 1e-6) N[k++] = u / ul;
                }
                for (const dvec3& axis : { dvec3(1, 0, 0), dvec3(0, 1, 0), dvec3(0, 0, 1) }) {
                    if (k + m >= 3) break;
                    dvec3 u = axis;
                    for (int i = 0; i < k; ++i) u -= N[i] * glm::dot(u, N[i]);
                    for (int i = 0; i < m; ++i) u -= T[i] * glm::dot(u, T[i]);
                    double ul = glm::length(u);
                    if (ul > 1e-6) T[m++] = u / ul;
                }
            }
            if (m == 0) continue; // a corner: stays put
            glm::dmat3 A(1.0); // unused dimensions: identity, zero right side
            dvec3 rhs(0.0);
            for (int i = 0; i < m; ++i) A[i][i] = 1e-3;
            for (int i = 1; i < nc; ++i) {
                dvec3 n = s[cs[i]] - s[cs[0]];
                double nl = glm::length(n);
                if (nl < 1e-12) continue;
                n /= nl;
                double d = (glm::dot(s[cs[i]], s[cs[i]]) - glm::dot(s[cs[0]], s[cs[0]])) * 0.5 / nl;
                dvec3 g(0.0); // n in the tangent basis
                for (int j = 0; j < m; ++j) g[j] = glm::dot(n, T[j]);
                double r = d - glm::dot(n, q[v]);
                for (int a = 0; a < m; ++a) {
                    for (int c = 0; c < m; ++c) A[c][a] += g[a] * g[c];
                    rhs[a] += g[a] * r;
                }
            }
            if (std::fabs(glm::determinant(A)) < 1e-18) continue;
            dvec3 y = glm::inverse(A) * rhs;
            dvec3 move(0.0);
            for (int j = 0; j < m; ++j) move += T[j] * y[j];
            double len = glm::length(move);
            if (len < 1e-9 * L) continue;
            if (len > 0.75 * L) move *= 0.75 * L / len;
            const dvec3 old = q[v];
            bool done = false;
            for (double alpha : { 1.0, 0.5, 0.25 }) {
                q[v] = old + move * alpha;
                bool ok = true;
                for (uint32_t t : incident[v]) {
                    if (volume(mesh.tets[t]) * rest[t] <= 0.0 || aspect(mesh.tets[t]) > maxAspect[t]) { ok = false; break; }
                }
                if (ok) { done = true; break; }
            }
            if (!done) q[v] = old;
            else snapped[v] = 1;
        }
    }
    for (uint8_t f : snapped) out.snappedVertices += f;
    for (size_t i = 0; i < q.size(); ++i) out.mesh.vertices[i] = glm::vec3(q[i] / metric);

    // 4. Strength per tet for two-level (clustered) fracture.
    if (seeds.clusterOfSeed.size() == nSeeds) {
        constexpr float kClusterStrength = 3.0f;
        std::vector<uint8_t> coarse(nTets, 0), fine(nTets, 0);
        for (uint32_t t = 0; t < nTets; ++t)
            for (int f = 0; f < 4; ++f) {
                uint32_t n = neighbour[t][f];
                if (n == UINT32_MAX || chunk[n] == chunk[t]) continue;
                if (seeds.clusterOfSeed[chunk[t]] != seeds.clusterOfSeed[chunk[n]]) coarse[t] = 1;
                else fine[t] = 1;
            }
        for (size_t t = 0; t < nTets; ++t)
            if (fine[t] && !coarse[t]) out.tetStrength[t] = kClusterStrength;
    }
    return out;
}

BakedFracture bakeFracture(const TetMeshData& mesh, const FractureSeedOptions& options) {
    BakedFracture b;
    FractureSeeds seeds = makeFractureSeeds(mesh, options);
    b.cut = cutAlongVoronoi(mesh, seeds);
    b.seeds = std::move(seeds);
    b.flags = fractureFlagsFromChunks(b.cut.mesh, b.cut.chunkOfTet);
    b.pieces = chunkCount(b.cut.chunkOfTet);
    return b;
}

// ---------------------------------------------------------------- drawn surface

std::vector<uint32_t> splitSoupAtPieces(TriangleSoup& soup, const TetMeshData& mesh, const std::vector<uint32_t>& chunkOfTet,
                                        const FractureSeeds& seeds, size_t maxTriangles) {
    struct Tri {
        std::array<glm::vec3, 3> p, n;
        std::array<glm::vec2, 3> uv;
    };
    std::vector<Tri> pending;
    pending.reserve(soup.triangleCount());
    for (size_t t = 0; t < soup.triangleCount(); ++t) {
        Tri tri;
        for (int k = 0; k < 3; ++k) {
            tri.p[k] = soup.positions[t * 3 + k];
            tri.n[k] = soup.normals[t * 3 + k];
            tri.uv[k] = soup.uvs[t * 3 + k];
        }
        pending.push_back(tri);
    }
    std::vector<Tri> done;
    std::vector<uint32_t> piece;
    auto finish = [&]() {
        TriangleSoup out;
        for (const Tri& t : done)
            for (int k = 0; k < 3; ++k) {
                out.positions.push_back(t.p[k]);
                out.normals.push_back(t.n[k]);
                out.uvs.push_back(t.uv[k]);
            }
        soup = std::move(out);
        return piece;
    };
    if (mesh.tets.empty() || chunkOfTet.size() != mesh.tets.size()) {
        done = std::move(pending);
        piece.assign(done.size(), 0);
        return finish();
    }

    const glm::vec3 metric = seeds.metric;
    // Linear blend of two corners (normal renormalised).
    auto lerpVertex = [](const Tri& t, int a, int b, float s, glm::vec3& p, glm::vec3& n, glm::vec2& uv) {
        p = glm::mix(t.p[a], t.p[b], s);
        n = glm::mix(t.n[a], t.n[b], s);
        const float len = glm::length(n);
        n = len > 1e-12f ? n / len : t.n[a];
        uv = glm::mix(t.uv[a], t.uv[b], s);
    };
    auto area2 = [](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) { return glm::length(glm::cross(b - a, c - a)); };

    // Cut-off slivers under 1/10000 of the average input triangle are
    // dropped: invisible, and their normals are float rounding noise
    // (a back-facing sliver would be a pinhole).
    float minArea2 = 0.0f;
    for (const Tri& t : pending) minArea2 += area2(t.p[0], t.p[1], t.p[2]);
    minArea2 = pending.empty() ? 0.0f : minArea2 / static_cast<float>(pending.size()) * 1e-4f;

    // Two rounds: each cuts every mixed triangle once (a triangle over a
    // corner where 3 pieces meet needs two cuts).
    constexpr int kRounds = 2;
    for (int round = 0; round <= kRounds && !pending.empty(); ++round) {
        // Corners pulled 1% towards the centre, so a corner lying on a
        // crack (every cut makes some) counts for the side it bounds.
        std::vector<glm::vec3> probes;
        probes.reserve(pending.size() * 4);
        for (const Tri& t : pending) {
            const glm::vec3 c = (t.p[0] + t.p[1] + t.p[2]) / 3.0f;
            for (int k = 0; k < 3; ++k) probes.push_back(glm::mix(t.p[k], c, 0.01f));
            probes.push_back(c);
        }
        const TetEmbedding at = embedPoints(mesh, probes);
        std::vector<Tri> next;
        for (size_t i = 0; i < pending.size(); ++i) {
            const Tri& t = pending[i];
            const uint32_t c0 = chunkOfTet[at.tet[i * 4]], c1 = chunkOfTet[at.tet[i * 4 + 1]], c2 = chunkOfTet[at.tet[i * 4 + 2]];
            const uint32_t centre = chunkOfTet[at.tet[i * 4 + 3]];
            const bool mixed = c0 != c1 || c1 != c2;
            const bool full = done.size() + next.size() + (pending.size() - i) + 2 > maxTriangles;
            if (!mixed || round == kRounds || full) {
                done.push_back(t);
                piece.push_back(mixed ? centre : c0);
                continue;
            }
            const uint32_t a = c0, b = c0 != c1 ? c1 : c2;
            float f[3];
            bool neg = false, pos = false;
            if (a < seeds.points.size() && b < seeds.points.size()) {
                const glm::vec3 sa = seeds.points[a] * metric, sb = seeds.points[b] * metric;
                for (int k = 0; k < 3; ++k) {
                    const glm::vec3 q = t.p[k] * metric;
                    f[k] = glm::dot(q - sa, q - sa) - glm::dot(q - sb, q - sb); // < 0: a's side
                    neg |= f[k] < 0.0f;
                    pos |= f[k] > 0.0f;
                }
            }
            if (!(neg && pos)) {
                // The border here isn't on the plane (a rough spot, or a
                // lump moved to its neighbour): kept whole, with the piece
                // under its centre - the old behaviour, now only there.
                done.push_back(t);
                piece.push_back(centre);
                continue;
            }
            // Sutherland-Hodgman against the plane, both sides kept.
            for (int side = 0; side < 2; ++side) {
                glm::vec3 P[4], N[4];
                glm::vec2 U[4];
                int count = 0;
                auto inside = [&](int k) { return side == 0 ? f[k] <= 0.0f : f[k] >= 0.0f; };
                for (int k = 0; k < 3; ++k) {
                    const int j = (k + 1) % 3;
                    if (inside(k)) { P[count] = t.p[k]; N[count] = t.n[k]; U[count] = t.uv[k]; ++count; }
                    if ((f[k] < 0.0f && f[j] > 0.0f) || (f[k] > 0.0f && f[j] < 0.0f)) {
                        lerpVertex(t, k, j, f[k] / (f[k] - f[j]), P[count], N[count], U[count]);
                        ++count;
                    }
                }
                for (int k = 1; k + 1 < count; ++k) {
                    if (area2(P[0], P[k], P[k + 1]) <= minArea2) continue; // sliver from a corner (nearly) on the plane
                    Tri s;
                    s.p = { P[0], P[k], P[k + 1] };
                    s.n = { N[0], N[k], N[k + 1] };
                    s.uv = { U[0], U[k], U[k + 1] };
                    next.push_back(s);
                }
            }
        }
        pending = std::move(next);
    }
    return finish();
}

TetEmbedding embedTrianglesInPieces(const TetMeshData& mesh, const std::vector<glm::vec3>& soupPositions,
                                    const std::vector<uint32_t>& chunkOfTet, const std::vector<uint32_t>& pieceOfTriangle) {
    TetEmbedding e = embedTriangles(mesh, soupPositions);
    if (chunkOfTet.size() != mesh.tets.size() || pieceOfTriangle.size() * 3 != soupPositions.size()) return e;
    std::unordered_map<uint32_t, std::vector<uint32_t>> tetsOf; // piece -> tets, built on first need
    for (size_t t = 0; t < pieceOfTriangle.size(); ++t) {
        const uint32_t want = pieceOfTriangle[t];
        if (chunkOfTet[e.tet[t * 3]] == want) continue;
        if (tetsOf.empty())
            for (uint32_t i = 0; i < chunkOfTet.size(); ++i) tetsOf[chunkOfTet[i]].push_back(i);
        auto it = tetsOf.find(want);
        if (it == tetsOf.end()) continue;
        // Nearest tet of its own piece (by centre); corners extrapolate
        // from it, exact under the piece's (affine-per-tet) motion.
        const glm::vec3 c = (soupPositions[t * 3] + soupPositions[t * 3 + 1] + soupPositions[t * 3 + 2]) / 3.0f;
        uint32_t best = it->second.front();
        float bd = std::numeric_limits<float>::max();
        for (uint32_t tet : it->second) {
            const auto& id = mesh.tets[tet];
            const glm::vec3 tc = (mesh.vertices[id[0]] + mesh.vertices[id[1]] + mesh.vertices[id[2]] + mesh.vertices[id[3]]) * 0.25f;
            const float d = glm::dot(tc - c, tc - c);
            if (d < bd) { bd = d; best = tet; }
        }
        const auto& id = mesh.tets[best];
        for (int k = 0; k < 3; ++k) {
            e.tet[t * 3 + k] = best;
            e.weights[t * 3 + k] = barycentric(soupPositions[t * 3 + k], mesh.vertices[id[0]], mesh.vertices[id[1]], mesh.vertices[id[2]],
                                               mesh.vertices[id[3]]);
        }
    }
    return e;
}

} // namespace kke
