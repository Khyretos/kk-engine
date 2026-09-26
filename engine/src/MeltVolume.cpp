#include "kke/MeltVolume.h"
#include "kke/ParticleFluid.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace kke {

MeltVolume::MeltVolume(const glm::ivec3& dims, const glm::vec3& origin, float cellSize)
    : m_dims(glm::max(dims, glm::ivec3(1))), m_origin(origin), m_cell(cellSize) {
    const size_t n = static_cast<size_t>(m_dims.x) * m_dims.y * m_dims.z;
    m_density.assign(n, 0.0f);
    m_temp.assign(n, 20.0f);
    m_meltAccum.assign(n, 0.0f);
}

void MeltVolume::clear() {
    std::fill(m_density.begin(), m_density.end(), 0.0f);
    m_distanceDirty = true;
    std::fill(m_meltAccum.begin(), m_meltAccum.end(), 0.0f);
    m_initialSolid = 0.0f;
    m_dirty = true;
}

void MeltVolume::fillBox(const glm::vec3& mn, const glm::vec3& mx, float temperature) {
    for (int z = 0; z < m_dims.z; ++z)
        for (int y = 0; y < m_dims.y; ++y)
            for (int x = 0; x < m_dims.x; ++x) {
                glm::vec3 c = center(x, y, z);
                if (glm::all(glm::greaterThanEqual(c, mn)) && glm::all(glm::lessThanEqual(c, mx))) {
                    m_density[idx(x, y, z)] = 1.0f;
                    m_temp[idx(x, y, z)] = temperature;
                }
            }
    m_initialSolid = 0.0f;
    for (float d : m_density) m_initialSolid += d;
    m_startTemperature = temperature;
    m_dirty = true;
    rebuildDistance();
}

float MeltVolume::at(const std::vector<float>& f, int x, int y, int z) const {
    if (x < 0 || y < 0 || z < 0 || x >= m_dims.x || y >= m_dims.y || z >= m_dims.z) {
        if (&f == &m_density) return 0.0f;
        if (&f == &m_distance) return m_cell; // outside the grid is empty
        return m_startTemperature;
    }
    return f[idx(x, y, z)];
}

float MeltVolume::trilinear(const std::vector<float>& f, const glm::vec3& p) const {
    glm::vec3 g = (p - m_origin) / m_cell - 0.5f; // voxel-centre lattice coordinates
    glm::ivec3 i = glm::ivec3(glm::floor(g));
    glm::vec3 t = g - glm::vec3(i);
    float c000 = at(f, i.x, i.y, i.z), c100 = at(f, i.x + 1, i.y, i.z);
    float c010 = at(f, i.x, i.y + 1, i.z), c110 = at(f, i.x + 1, i.y + 1, i.z);
    float c001 = at(f, i.x, i.y, i.z + 1), c101 = at(f, i.x + 1, i.y, i.z + 1);
    float c011 = at(f, i.x, i.y + 1, i.z + 1), c111 = at(f, i.x + 1, i.y + 1, i.z + 1);
    float x00 = c000 + (c100 - c000) * t.x, x10 = c010 + (c110 - c010) * t.x;
    float x01 = c001 + (c101 - c001) * t.x, x11 = c011 + (c111 - c011) * t.x;
    float y0 = x00 + (x10 - x00) * t.y, y1 = x01 + (x11 - x01) * t.y;
    return y0 + (y1 - y0) * t.z;
}

float MeltVolume::density(const glm::vec3& p) const { return trilinear(m_density, p); }
float MeltVolume::temperature(const glm::vec3& p) const { return trilinear(m_temp, p); }

void MeltVolume::rebuildDistance() {
    // Chamfer distance transform (Borgefors), in voxel units: distance of
    // each voxel centre to the nearest voxel of the other kind, two sweeps
    // with 3x3x3 neighbourhood weights (1, sqrt2, sqrt3). Solid voxels get
    // negative values. ~14k voxels: well under a millisecond, and only
    // after something melted.
    const size_t n = m_density.size();
    std::vector<float> in(n), out(n);
    const float big = 1e6f;
    for (size_t v = 0; v < n; ++v) {
        bool solid = m_density[v] > 0.5f;
        in[v] = solid ? big : 0.0f;   // distance to nearest empty (for solids)
        out[v] = solid ? 0.0f : big;  // distance to nearest solid (for empties)
    }
    auto sweep = [&](std::vector<float>& d, bool forward) {
        const int s = forward ? 1 : -1;
        const int z0 = forward ? 0 : m_dims.z - 1, z1 = forward ? m_dims.z : -1;
        const int y0 = forward ? 0 : m_dims.y - 1, y1 = forward ? m_dims.y : -1;
        const int x0 = forward ? 0 : m_dims.x - 1, x1 = forward ? m_dims.x : -1;
        for (int z = z0; z != z1; z += s)
            for (int y = y0; y != y1; y += s)
                for (int x = x0; x != x1; x += s) {
                    float& cur = d[idx(x, y, z)];
                    if (cur == 0.0f) continue;
                    for (int dz = -1; dz <= 0; ++dz)
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dx = -1; dx <= 1; ++dx) {
                                // Only already-visited neighbours in this sweep order.
                                if (dz == 0 && (dy > 0 || (dy == 0 && dx >= 0))) continue;
                                int xx = x + dx * s, yy = y + dy * s, zz = z + dz * s;
                                if (xx < 0 || yy < 0 || zz < 0 || xx >= m_dims.x || yy >= m_dims.y || zz >= m_dims.z) {
                                    // Outside the grid is empty: counts as distance 0 for "in".
                                    if (&d == &in) cur = std::min(cur, std::sqrt(float(dx * dx + dy * dy + dz * dz)));
                                    continue;
                                }
                                float w = std::sqrt(float(dx * dx + dy * dy + dz * dz));
                                cur = std::min(cur, d[idx(xx, yy, zz)] + w);
                            }
                }
    };
    sweep(in, true);
    sweep(in, false);
    sweep(out, true);
    sweep(out, false);
    m_distance.resize(n);
    for (size_t v = 0; v < n; ++v) {
        bool solid = m_density[v] > 0.5f;
        // Surface sits half a voxel from the boundary voxel centre.
        m_distance[v] = solid ? -(in[v] - 0.5f) * m_cell : (out[v] - 0.5f) * m_cell;
    }
    m_distanceDirty = false;
}

float MeltVolume::signedDistance(const glm::vec3& p, glm::vec3& normal) const {
    // Cheap reject outside the grid (plus a cell of margin).
    glm::vec3 lo = m_origin - m_cell, hi = m_origin + glm::vec3(m_dims) * m_cell + m_cell;
    if (glm::any(glm::lessThan(p, lo)) || glm::any(glm::greaterThan(p, hi)) || m_distance.empty()) {
        normal = glm::vec3(0, 1, 0);
        return 1e9f;
    }
    const float e = m_cell * 0.5f;
    glm::vec3 grad(trilinear(m_distance, p + glm::vec3(e, 0, 0)) - trilinear(m_distance, p - glm::vec3(e, 0, 0)),
                   trilinear(m_distance, p + glm::vec3(0, e, 0)) - trilinear(m_distance, p - glm::vec3(0, e, 0)),
                   trilinear(m_distance, p + glm::vec3(0, 0, e)) - trilinear(m_distance, p - glm::vec3(0, 0, e)));
    float len = glm::length(grad);
    normal = len > 1e-6f ? grad / len : glm::vec3(0, 1, 0);
    return trilinear(m_distance, p);
}

size_t MeltVolume::step(float dt, ParticleFluid& fluid) {
    if (m_initialSolid <= 0.0f || dt <= 0.0f) return 0;
    const MeltMaterial& mat = m_material;
    // 1. Heat exchange with nearby liquid (both ways: lava cools as it
    // melts the block, which is what makes it crust on top of it).
    auto& temps = fluid.temperatures();
    const auto& pos = fluid.positions();
    // Reach: a particle resting on the surface is its radius plus half a
    // voxel from the nearest voxel centre (plus the particle's own wobble).
    const float reach = fluid.params().radius + m_cell * 1.5f;
    const int span = static_cast<int>(std::ceil(reach / m_cell));
    const float k = std::min(0.5f, 15.0f * dt);
    for (size_t i = 0; i < pos.size(); ++i) {
        glm::ivec3 c = glm::ivec3(glm::floor((pos[i] - m_origin) / m_cell));
        // Search every voxel within `reach` (was +-1 cell, shorter than the
        // reach itself: particles resting on the top face never touched the
        // top layer and heat only got in where they dug in).
        for (int dz = -span; dz <= span; ++dz)
            for (int dy = -span; dy <= span; ++dy)
                for (int dx = -span; dx <= span; ++dx) {
                    int x = c.x + dx, y = c.y + dy, z = c.z + dz;
                    if (x < 0 || y < 0 || z < 0 || x >= m_dims.x || y >= m_dims.y || z >= m_dims.z) continue;
                    size_t v = idx(x, y, z);
                    if (m_density[v] < 0.05f) continue;
                    if (glm::length(center(x, y, z) - pos[i]) > reach) continue;
                    float q = k * (temps[i] - m_temp[v]) * 0.25f;
                    m_temp[v] += q / mat.heatCapacity;
                    temps[i] -= q / mat.liquidHeatCapacity;
                }
    }
    // 2. Conduction inside the solid.
    m_scratch = m_temp;
    const float cond = std::min(0.25f, mat.conduction * dt);
    for (int z = 0; z < m_dims.z; ++z)
        for (int y = 0; y < m_dims.y; ++y)
            for (int x = 0; x < m_dims.x; ++x) {
                size_t v = idx(x, y, z);
                if (m_density[v] <= 0.0f) continue;
                float sum = 0.0f;
                int n = 0;
                const int nb[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
                for (const auto& o : nb) {
                    int xx = x + o[0], yy = y + o[1], zz = z + o[2];
                    if (xx < 0 || yy < 0 || zz < 0 || xx >= m_dims.x || yy >= m_dims.y || zz >= m_dims.z) continue;
                    size_t w = idx(xx, yy, zz);
                    if (m_density[w] <= 0.0f) continue;
                    sum += m_temp[w];
                    ++n;
                }
                if (n) m_scratch[v] = m_temp[v] + cond * (sum / n - m_temp[v]);
            }
    m_temp.swap(m_scratch);
    // 3. Melting with latent heat, and emission of the melt.
    const float r = fluid.params().radius;
    const float particleShare = (2.0f * r) * (2.0f * r) * (2.0f * r) / (m_cell * m_cell * m_cell);
    size_t emitted = 0;
    for (int z = 0; z < m_dims.z; ++z)
        for (int y = 0; y < m_dims.y; ++y)
            for (int x = 0; x < m_dims.x; ++x) {
                size_t v = idx(x, y, z);
                if (m_density[v] <= 0.0f || m_temp[v] <= mat.meltingPoint) continue;
                float excess = m_temp[v] - mat.meltingPoint;
                float lost = std::min(m_density[v], excess * mat.meltRate);
                m_density[v] -= lost;
                m_temp[v] = mat.meltingPoint; // the heat went into melting
                if (m_density[v] < 0.02f) m_density[v] = 0.0f;
                m_meltAccum[v] += lost;
                m_dirty = true;
                m_distanceDirty = true;
                while (m_meltAccum[v] >= particleShare) {
                    m_meltAccum[v] -= particleShare;
                    glm::vec3 jitter(std::fmod(v * 0.618f, 1.0f) - 0.5f, 0.0f, std::fmod(v * 0.382f, 1.0f) - 0.5f);
                    if (!fluid.add(center(x, y, z) + jitter * m_cell * 0.5f, glm::vec3(0.0f), mat.liquidTemperature, mat.liquidMaterial)) break;
                    ++emitted;
                }
            }
    if (m_distanceDirty) rebuildDistance();
    return emitted;
}

float MeltVolume::solidFraction() const {
    if (m_initialSolid <= 0.0f) return 0.0f;
    float s = 0.0f;
    for (float d : m_density) s += d;
    return s / m_initialSolid;
}

bool MeltVolume::rebuildMesh() {
    if (!m_dirty) return false;
    m_dirty = false;
    m_meshPos.clear();
    m_meshNrm.clear();
    m_meshGlow.clear();
    m_meshIdx.clear();
    const float iso = 0.5f;
    // Sample lattice = voxel centres, padded by one empty sample each side
    // so the surface closes. Lattice point (i,j,k) in [-1, dims].
    const glm::ivec3 L = m_dims + 2;
    auto lid = [&](int x, int y, int z) { return (static_cast<uint64_t>(z + 1) * L.y + (y + 1)) * L.x + (x + 1); };
    std::unordered_map<uint64_t, uint32_t> edgeVert;
    edgeVert.reserve(4096);
    const float meltSpan = std::max(1.0f, m_material.meltingPoint - m_startTemperature);
    auto vertexOnEdge = [&](const glm::ivec3& a, const glm::ivec3& b) -> uint32_t {
        uint64_t ka = lid(a.x, a.y, a.z), kb = lid(b.x, b.y, b.z);
        uint64_t key = ka < kb ? (ka << 32) | kb : (kb << 32) | ka;
        auto it = edgeVert.find(key);
        if (it != edgeVert.end()) return it->second;
        float fa = at(m_density, a.x, a.y, a.z), fb = at(m_density, b.x, b.y, b.z);
        float t = std::fabs(fb - fa) > 1e-6f ? (iso - fa) / (fb - fa) : 0.5f;
        glm::vec3 pa = center(a.x, a.y, a.z), pb = center(b.x, b.y, b.z);
        glm::vec3 p = pa + (pb - pa) * std::clamp(t, 0.0f, 1.0f);
        glm::vec3 n;
        signedDistance(p, n);
        uint32_t id = static_cast<uint32_t>(m_meshPos.size());
        m_meshPos.push_back(p);
        m_meshNrm.push_back(n);
        float temp = std::max(at(m_temp, a.x, a.y, a.z), at(m_temp, b.x, b.y, b.z));
        m_meshGlow.push_back(std::clamp((temp - m_startTemperature) / meltSpan, 0.0f, 1.0f));
        edgeVert.emplace(key, id);
        return id;
    };
    auto emitTri = [&](uint32_t a, uint32_t b, uint32_t c, const glm::vec3& outward) {
        glm::vec3 n = glm::cross(m_meshPos[b] - m_meshPos[a], m_meshPos[c] - m_meshPos[a]);
        if (glm::dot(n, outward) < 0.0f) std::swap(b, c);
        m_meshIdx.insert(m_meshIdx.end(), { a, b, c });
    };
    static const int kCorner[8][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } };
    static const int kTets[6][4] = { { 0, 1, 2, 6 }, { 0, 2, 3, 6 }, { 0, 3, 7, 6 }, { 0, 7, 4, 6 }, { 0, 4, 5, 6 }, { 0, 5, 1, 6 } };
    for (int z = -1; z < m_dims.z; ++z)
        for (int y = -1; y < m_dims.y; ++y)
            for (int x = -1; x < m_dims.x; ++x) {
                float f[8];
                glm::ivec3 c[8];
                int inside = 0;
                for (int k = 0; k < 8; ++k) {
                    c[k] = glm::ivec3(x + kCorner[k][0], y + kCorner[k][1], z + kCorner[k][2]);
                    f[k] = at(m_density, c[k].x, c[k].y, c[k].z);
                    inside += f[k] > iso;
                }
                if (inside == 0 || inside == 8) continue;
                for (const auto& tet : kTets) {
                    int in[4], out[4], ni = 0, no = 0;
                    for (int k = 0; k < 4; ++k) (f[tet[k]] > iso ? in[ni++] : out[no++]) = tet[k];
                    if (ni == 0 || ni == 4) continue;
                    glm::vec3 inC(0.0f), outC(0.0f);
                    for (int k = 0; k < ni; ++k) inC += glm::vec3(c[in[k]]);
                    for (int k = 0; k < no; ++k) outC += glm::vec3(c[out[k]]);
                    glm::vec3 outward = outC / float(no) - inC / float(ni);
                    if (ni == 1 || ni == 3) {
                        int apex = ni == 1 ? in[0] : out[0];
                        const int* others = ni == 1 ? out : in;
                        emitTri(vertexOnEdge(c[apex], c[others[0]]), vertexOnEdge(c[apex], c[others[1]]), vertexOnEdge(c[apex], c[others[2]]), outward);
                    } else {
                        uint32_t a = vertexOnEdge(c[in[0]], c[out[0]]), b = vertexOnEdge(c[in[0]], c[out[1]]);
                        uint32_t d = vertexOnEdge(c[in[1]], c[out[1]]), e = vertexOnEdge(c[in[1]], c[out[0]]);
                        emitTri(a, b, d, outward);
                        emitTri(a, d, e, outward);
                    }
                }
            }
    return true;
}

} // namespace kke
