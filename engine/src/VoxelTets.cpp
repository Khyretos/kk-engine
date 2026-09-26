#include "kke/VoxelTets.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <limits>
#include <map>

namespace kke {

float tetVolume(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d) {
    return glm::dot(b - a, glm::cross(c - a, d - a)) / 6.0f;
}

glm::vec4 barycentric(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d) {
    glm::mat3 m(b - a, c - a, d - a);
    float det = glm::determinant(m);
    if (std::fabs(det) < 1e-20f) return glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 w = glm::inverse(m) * (p - a);
    return glm::vec4(1.0f - w.x - w.y - w.z, w.x, w.y, w.z);
}

namespace {

// One voxelization attempt at a fixed cell size. Returns the solid mask
// over a grid padded by one empty cell on every side (so the flood fill
// always has an outside to start from).
std::vector<uint8_t> solidMask(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& tris, const glm::vec3& origin,
                               const glm::vec3& cell, const glm::ivec3& dims) {
    const glm::ivec3 pd = dims + 2;
    auto index = [&](int x, int y, int z) { return (static_cast<size_t>(z) * pd.y + y) * pd.x + x; };
    std::vector<uint8_t> state(static_cast<size_t>(pd.x) * pd.y * pd.z, 0); // 0 unknown, 1 surface, 2 outside
    auto mark = [&](const glm::vec3& p) {
        glm::ivec3 c = glm::ivec3(glm::floor((p - origin) / cell)) + 1;  // cell: per-axis size
        c = glm::clamp(c, glm::ivec3(1), dims);
        state[index(c.x, c.y, c.z)] = 1;
    };
    // 1. Surface cells: sample each triangle at <= half a cell.
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        const glm::vec3 &a = positions[tris[t]], &b = positions[tris[t + 1]], &c = positions[tris[t + 2]];
        float longest = std::max({ glm::length(b - a), glm::length(c - b), glm::length(a - c) });
        const float smallest = std::min({ cell.x, cell.y, cell.z });
        int n = std::max(1, static_cast<int>(std::ceil(longest / (smallest * 0.5f))));
        for (int i = 0; i <= n; ++i) {
            for (int j = 0; j <= n - i; ++j) {
                float u = static_cast<float>(i) / n, v = static_cast<float>(j) / n;
                mark(a + (b - a) * u + (c - a) * v);
            }
        }
    }
    // 2. Flood the outside (6-connected) from the padded corner. An
    // explicit stack: no recursion depth limit, no allocations per cell.
    std::vector<glm::ivec3> stack{ glm::ivec3(0) };
    state[index(0, 0, 0)] = 2;
    const glm::ivec3 steps[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    while (!stack.empty()) {
        glm::ivec3 c = stack.back();
        stack.pop_back();
        for (const glm::ivec3& s : steps) {
            glm::ivec3 n = c + s;
            if (n.x < 0 || n.y < 0 || n.z < 0 || n.x >= pd.x || n.y >= pd.y || n.z >= pd.z) continue;
            uint8_t& st = state[index(n.x, n.y, n.z)];
            if (st != 0) continue;
            st = 2;
            stack.push_back(n);
        }
    }
    // 3. Holes. The flood leaks in through any gap in the surface, and
    // Synty props are full of them: pillars, crates and walls have no
    // bottom (they stand on the ground), parts meet with slits between
    // them. A leak left the volume a thin shell, and broken pieces were
    // hollow inside. So a flooded cell still counts as inside when the
    // surface blocks it along at least 5 of the 6 axis directions (one
    // missing cap), the "enclosed from almost everywhere" rule of
    // generalized winding numbers without their cost. A doorway, a pipe
    // or an L-shaped notch sees out along 2+ directions and stays empty.
    {
        const size_t n = state.size();
        std::vector<uint8_t> blocked(n, 0); // count of directions with surface ahead
        auto scan = [&](int axis) {
            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
            glm::ivec3 c(0);
            for (c[u] = 0; c[u] < pd[u]; ++c[u]) {
                for (c[v] = 0; c[v] < pd[v]; ++c[v]) {
                    bool seen = false; // surface behind, in the - direction
                    for (c[axis] = 0; c[axis] < pd[axis]; ++c[axis]) {
                        const size_t i = index(c.x, c.y, c.z);
                        if (state[i] == 1) seen = true;
                        else if (seen) ++blocked[i];
                    }
                    seen = false; // the + direction
                    for (c[axis] = pd[axis] - 1; c[axis] >= 0; --c[axis]) {
                        const size_t i = index(c.x, c.y, c.z);
                        if (state[i] == 1) seen = true;
                        else if (seen) ++blocked[i];
                    }
                }
            }
        };
        for (int axis = 0; axis < 3; ++axis) scan(axis);
        for (size_t i = 0; i < n; ++i)
            if (state[i] == 2 && blocked[i] >= 5) state[i] = 0;
    }
    // 4. Solid = not reached from outside.
    for (uint8_t& st : state) st = st == 2 ? 0 : 1;
    return state;
}

} // namespace

VoxelTetMesh voxelizeToTets(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& tris, float cellSize,
                            size_t maxSolidCells) {
    VoxelTetMesh out;
    if (positions.empty() || tris.size() < 3 || cellSize <= 0.0f) return out;
    glm::vec3 mn(std::numeric_limits<float>::max()), mx(-std::numeric_limits<float>::max());
    for (const glm::vec3& p : positions) {
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
    }
    maxSolidCells = std::max<size_t>(maxSolidCells, 1);
    // Flat things (a floor tile, a pane) still need some thickness.
    glm::vec3 size = mx - mn;
    const float minThickness = cellSize * 0.2f;
    for (int i = 0; i < 3; ++i) {
        if (size[i] < minThickness) {
            mn[i] -= (minThickness - size[i]) * 0.5f;
            size[i] = minThickness;
        }
    }

    // Cells per axis are sized to fit the bounds exactly (cells may be
    // non-cubic): a 5 x 3 x 0.1 m wall becomes a slab exactly 0.1 m thick,
    // not a 0.5 m voxel wall whose crack faces stick out of the mesh.
    float cell = cellSize;
    std::vector<uint8_t> solid;
    glm::ivec3 dims(1);
    glm::vec3 cell3(cell);
    glm::vec3 origin = mn;
    size_t count = 0;
    // Grow the cell until the budget holds (converges in 1-3 passes).
    for (int attempt = 0; attempt < 12; ++attempt) {
        dims = glm::max(glm::ivec3(glm::round(size / cell)), glm::ivec3(1));
        cell3 = size / glm::vec3(dims);
        origin = mn;
        solid = solidMask(positions, tris, origin, cell3, dims);
        count = static_cast<size_t>(std::count(solid.begin(), solid.end(), uint8_t(1)));
        if (count <= maxSolidCells) break;
        cell *= std::max(1.1f, std::cbrt(static_cast<float>(count) / static_cast<float>(maxSolidCells)));
    }

    const glm::ivec3 pd = dims + 2;
    auto cellIndex = [&](int x, int y, int z) { return (static_cast<size_t>(z) * pd.y + y) * pd.x + x; };
    // Corner vertices on the (dims+1)^3 lattice, created on demand.
    const glm::ivec3 cd = dims + 1;
    std::vector<uint32_t> corner(static_cast<size_t>(cd.x) * cd.y * cd.z, UINT32_MAX);
    auto vert = [&](int x, int y, int z) {
        uint32_t& id = corner[(static_cast<size_t>(z) * cd.y + y) * cd.x + x];
        if (id == UINT32_MAX) {
            id = static_cast<uint32_t>(out.mesh.vertices.size());
            out.mesh.vertices.push_back(origin + glm::vec3(x, y, z) * cell3);
        }
        return id;
    };
    for (int z = 0; z < dims.z; ++z) {
        for (int y = 0; y < dims.y; ++y) {
            for (int x = 0; x < dims.x; ++x) {
                if (!solid[cellIndex(x + 1, y + 1, z + 1)]) continue;
                // Same Kuhn split (and winding) as PhysicsModule::buildGridBox.
                uint32_t v0 = vert(x, y, z), v1 = vert(x + 1, y, z), v2 = vert(x + 1, y + 1, z), v3 = vert(x, y + 1, z);
                uint32_t v4 = vert(x, y, z + 1), v5 = vert(x + 1, y, z + 1), v6 = vert(x + 1, y + 1, z + 1), v7 = vert(x, y + 1, z + 1);
                out.mesh.tets.push_back({ v0, v1, v2, v6 });
                out.mesh.tets.push_back({ v0, v2, v3, v6 });
                out.mesh.tets.push_back({ v0, v3, v7, v6 });
                out.mesh.tets.push_back({ v0, v7, v4, v6 });
                out.mesh.tets.push_back({ v0, v4, v5, v6 });
                out.mesh.tets.push_back({ v0, v5, v1, v6 });
            }
        }
    }
    out.cellSize = std::min({ cell3.x, cell3.y, cell3.z });
    out.cellSize3 = cell3;
    out.dims = dims;
    out.origin = origin;
    out.solidCells = count;
    return out;
}

glm::vec3 closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;
    const glm::vec3 bp = p - b;
    float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) return a + ab * (d1 / (d1 - d3));
    const glm::vec3 cp = p - c;
    float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) return a + ac * (d2 / (d2 - d6));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    float denom = 1.0f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

size_t fitSurfaceToMesh(TetMeshData& tets, const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& tris, float maxDistance) {
    if (tets.tets.empty() || tris.size() < 3) return 0;
    // Surface vertices: on a face used by exactly one tet.
    std::map<std::array<uint32_t, 3>, int> faceUse;
    for (const auto& t : tets.tets) {
        for (int f = 0; f < 4; ++f) {
            std::array<uint32_t, 3> k{};
            int j = 0;
            for (int i = 0; i < 4; ++i) if (i != f) k[j++] = t[i];
            std::sort(k.begin(), k.end());
            ++faceUse[k];
        }
    }
    std::vector<uint8_t> surface(tets.vertices.size(), 0);
    for (const auto& [k, n] : faceUse) if (n == 1) for (uint32_t v : k) surface[v] = 1;
    std::vector<std::vector<uint32_t>> incident(tets.vertices.size());
    for (uint32_t t = 0; t < tets.tets.size(); ++t) for (uint32_t v : tets.tets[t]) incident[v].push_back(t);
    auto volume = [&](uint32_t t) {
        const auto& ids = tets.tets[t];
        return tetVolume(tets.vertices[ids[0]], tets.vertices[ids[1]], tets.vertices[ids[2]], tets.vertices[ids[3]]);
    };
    std::vector<float> rest(tets.tets.size());
    for (uint32_t t = 0; t < tets.tets.size(); ++t) rest[t] = volume(t);
    size_t moved = 0;
    for (uint32_t v = 0; v < tets.vertices.size(); ++v) {
        if (!surface[v]) continue;
        const glm::vec3 p = tets.vertices[v];
        glm::vec3 best = p;
        float bestD = maxDistance * maxDistance;
        for (size_t i = 0; i + 2 < tris.size(); i += 3) {
            glm::vec3 q = closestPointOnTriangle(p, positions[tris[i]], positions[tris[i + 1]], positions[tris[i + 2]]);
            glm::vec3 d = q - p;
            float dd = glm::dot(d, d);
            if (dd < bestD) { bestD = dd; best = q; }
        }
        if (best == p) continue;
        for (float step : { 1.0f, 0.5f, 0.25f }) {
            tets.vertices[v] = p + (best - p) * step;
            bool ok = true;
            for (uint32_t t : incident[v]) {
                float vol = volume(t);
                if (vol * rest[t] <= 0.0f || std::fabs(vol) < std::fabs(rest[t]) * 0.5f || std::fabs(vol) > std::fabs(rest[t]) * 1.5f) { ok = false; break; }
            }
            if (ok) { ++moved; break; }
            tets.vertices[v] = p;
        }
    }
    return moved;
}

void subdivideSoup(TriangleSoup& soup, float maxEdge, size_t maxTriangles) {
    if (maxEdge <= 0.0f) return;
    const float maxEdge2 = maxEdge * maxEdge;
    TriangleSoup out;
    // Work list of triangles (indices into a growing vertex array).
    std::vector<glm::vec3> P = soup.positions, N = soup.normals;
    std::vector<glm::vec2> U = soup.uvs;
    std::vector<std::array<uint32_t, 3>> todo;
    for (uint32_t i = 0; i + 2 < P.size(); i += 3) todo.push_back({ i, i + 1, i + 2 });
    size_t done = 0;
    auto emit = [&](const std::array<uint32_t, 3>& t) {
        for (uint32_t v : t) {
            out.positions.push_back(P[v]);
            out.normals.push_back(N[v]);
            out.uvs.push_back(U[v]);
        }
        ++done;
    };
    while (!todo.empty()) {
        auto t = todo.back();
        todo.pop_back();
        float l[3];
        for (int e = 0; e < 3; ++e) {
            glm::vec3 d = P[t[(e + 1) % 3]] - P[t[e]];
            l[e] = glm::dot(d, d);
        }
        int e = (l[0] >= l[1] && l[0] >= l[2]) ? 0 : (l[1] >= l[2] ? 1 : 2);
        if (l[e] <= maxEdge2 || done + todo.size() + 1 >= maxTriangles) { emit(t); continue; }
        uint32_t a = t[e], b = t[(e + 1) % 3], c = t[(e + 2) % 3];
        uint32_t m = static_cast<uint32_t>(P.size());
        P.push_back((P[a] + P[b]) * 0.5f);
        glm::vec3 n = N[a] + N[b];
        N.push_back(glm::length(n) > 1e-12f ? glm::normalize(n) : N[a]);
        U.push_back((U[a] + U[b]) * 0.5f);
        todo.push_back({ a, m, c });
        todo.push_back({ m, b, c });
    }
    soup = std::move(out);
}

TetEmbedding embedTriangles(const TetMeshData& mesh, const std::vector<glm::vec3>& soupPositions) {
    std::vector<glm::vec3> centres;
    for (size_t i = 0; i + 2 < soupPositions.size(); i += 3)
        centres.push_back((soupPositions[i] + soupPositions[i + 1] + soupPositions[i + 2]) / 3.0f);
    TetEmbedding byCentre = embedPoints(mesh, centres);
    TetEmbedding e;
    e.tet.resize(soupPositions.size());
    e.weights.resize(soupPositions.size());
    for (size_t t = 0; t < centres.size(); ++t) {
        const uint32_t tet = byCentre.tet[t];
        const auto& ids = mesh.tets[tet];
        for (size_t k = 0; k < 3; ++k) {
            e.tet[t * 3 + k] = tet;
            e.weights[t * 3 + k] = barycentric(soupPositions[t * 3 + k], mesh.vertices[ids[0]], mesh.vertices[ids[1]],
                                               mesh.vertices[ids[2]], mesh.vertices[ids[3]]);
        }
    }
    return e;
}

TetEmbedding embedPoints(const TetMeshData& mesh, const std::vector<glm::vec3>& points) {
    TetEmbedding e;
    e.tet.resize(points.size(), 0);
    e.weights.resize(points.size(), glm::vec4(1, 0, 0, 0));
    if (mesh.tets.empty()) return e;
    // Per tet: inverse edge matrix (barycentrics become one mat3 multiply)
    // and bounds. Tets are rasterised into a fine grid (a third of the
    // average tet size): a point inside the volume only tests the few
    // tets whose bounds cover its cell (the one containing it is always
    // among them); a point outside uses the tets of the nearest covered
    // cell, found for every empty cell up front by one BFS. O(1) per
    // point. The previous 1-tet-size bins plus a ring search cost ~9 us
    // per point (~110 ms for a 12,000-point pillar); this is ~20x faster
    // (OPTIMIZATION.md log #15, #30).
    const size_t n = mesh.tets.size();
    std::vector<glm::mat3> inv(n);
    std::vector<glm::vec3> origin(n), bmin(n), bmax(n);
    glm::vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
    float avgSize = 0.0f;
    for (size_t t = 0; t < n; ++t) {
        const auto& ids = mesh.tets[t];
        const glm::vec3 &a = mesh.vertices[ids[0]], &b = mesh.vertices[ids[1]], &c = mesh.vertices[ids[2]], &d = mesh.vertices[ids[3]];
        glm::mat3 m(b - a, c - a, d - a);
        inv[t] = std::fabs(glm::determinant(m)) > 1e-20f ? glm::inverse(m) : glm::mat3(0.0f);
        origin[t] = a;
        bmin[t] = glm::min(glm::min(a, b), glm::min(c, d));
        bmax[t] = glm::max(glm::max(a, b), glm::max(c, d));
        lo = glm::min(lo, bmin[t]);
        hi = glm::max(hi, bmax[t]);
        avgSize += glm::length(bmax[t] - bmin[t]);
    }
    float cell = std::max(avgSize / static_cast<float>(n) / 3.0f, 1e-4f);
    // One empty cell of padding all round, and at most ~2M cells.
    lo -= glm::vec3(cell);
    hi += glm::vec3(cell);
    glm::vec3 extent = hi - lo;
    for (;;) {
        const glm::vec3 d = glm::ceil(extent / cell);
        if (static_cast<double>(d.x) * d.y * d.z <= 2.0e6) break;
        cell *= 1.25f;
    }
    const glm::ivec3 dims = glm::max(glm::ivec3(glm::ceil(extent / cell)), glm::ivec3(1));
    const size_t cells = static_cast<size_t>(dims.x) * dims.y * dims.z;
    auto cellOf = [&](const glm::vec3& p) { return glm::clamp(glm::ivec3(glm::floor((p - lo) / cell)), glm::ivec3(0), dims - 1); };
    auto key = [&](const glm::ivec3& c) { return (static_cast<size_t>(c.z) * dims.y + c.y) * dims.x + c.x; };
    // Cell -> tets, as one flat array (count, prefix sum, fill).
    std::vector<uint32_t> start(cells + 1, 0), ids;
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<uint32_t> fill;
        if (pass == 1) {
            for (size_t i = 0; i < cells; ++i) start[i + 1] += start[i];
            ids.resize(start[cells]);
            fill.assign(start.begin(), start.end() - 1);
        }
        for (size_t t = 0; t < n; ++t) {
            const glm::ivec3 c0 = cellOf(bmin[t]), c1 = cellOf(bmax[t]);
            for (int z = c0.z; z <= c1.z; ++z)
                for (int y = c0.y; y <= c1.y; ++y)
                    for (int x = c0.x; x <= c1.x; ++x) {
                        const size_t k = key({ x, y, z });
                        if (pass == 0) ++start[k + 1];
                        else ids[fill[k]++] = static_cast<uint32_t>(t);
                    }
        }
    }
    // Empty cell -> nearest covered cell (multi-source BFS, 6-connected).
    std::vector<uint32_t> source(cells, UINT32_MAX);
    {
        std::vector<uint32_t> queue;
        queue.reserve(cells);
        for (size_t i = 0; i < cells; ++i)
            if (start[i + 1] > start[i]) { source[i] = static_cast<uint32_t>(i); queue.push_back(static_cast<uint32_t>(i)); }
        const long sx = 1, sy = dims.x, sz = static_cast<long>(dims.x) * dims.y;
        for (size_t head = 0; head < queue.size(); ++head) {
            const long i = queue[head];
            const long x = i % dims.x, y = (i / dims.x) % dims.y, z = i / sz;
            const long nb[6] = { x > 0 ? i - sx : -1, x + 1 < dims.x ? i + sx : -1, y > 0 ? i - sy : -1,
                                 y + 1 < dims.y ? i + sy : -1, z > 0 ? i - sz : -1, z + 1 < dims.z ? i + sz : -1 };
            for (long j : nb)
                if (j >= 0 && source[j] == UINT32_MAX) { source[j] = source[i]; queue.push_back(static_cast<uint32_t>(j)); }
        }
    }
    for (size_t i = 0; i < points.size(); ++i) {
        const glm::vec3& p = points[i];
        const size_t k = source[key(cellOf(p))];
        float bestScore = -std::numeric_limits<float>::max();
        uint32_t bestTet = ids[start[k]];
        glm::vec4 bestW(1, 0, 0, 0);
        for (uint32_t j = start[k]; j < start[k + 1]; ++j) {
            const uint32_t t = ids[j];
            const glm::vec3 w = inv[t] * (p - origin[t]);
            const glm::vec4 w4(1.0f - w.x - w.y - w.z, w.x, w.y, w.z);
            const float score = std::min({ w4.x, w4.y, w4.z, w4.w }); // >= 0: inside; else how far outside
            if (score > bestScore) { bestScore = score; bestTet = t; bestW = w4; }
            if (score >= 0.0f) break;
        }
        e.tet[i] = bestTet;
        e.weights[i] = bestW;
    }
    return e;
}

} // namespace kke
