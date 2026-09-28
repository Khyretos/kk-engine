// The island, the sea and the rings (README.md "The island and the
// rings"): built from Course.h's numbers, drawn with DynamicMeshRenderer
// (vertex colours, no textures, so it looks the same with or without
// any art pack).

#include "FlyingModule.h"

#include "kke/Log.h"
#include "kke/SphereImpostors.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace flying {

namespace {

constexpr float kCell = 24.0f;        // m between the terrain's grid points
constexpr float kExtent = 1800.0f;    // m from the middle to the grid's edge (past the coast)

void pushTriangle(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx, const kke::Vertex& a, const kke::Vertex& b, const kke::Vertex& c) {
    // Wind so the face looks along its normal (the renderer culls back faces).
    const uint32_t base = static_cast<uint32_t>(v.size());
    v.push_back(a);
    v.push_back(b);
    v.push_back(c);
    const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
    if (glm::dot(n, a.normal) >= 0.0f) idx.insert(idx.end(), { base, base + 1, base + 2 });
    else idx.insert(idx.end(), { base, base + 2, base + 1 });
}

void appendBox(const glm::vec3& center, const glm::vec3& half, const glm::vec3& color, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx,
               const glm::mat3& turn = glm::mat3(1.0f)) {
    const glm::vec3 normals[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (const glm::vec3& n : normals) {
        const glm::vec3 u = std::abs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 w = glm::cross(n, u);
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (glm::vec2 k : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1) })
            v.push_back({ center + turn * ((n + u * k.x + w * k.y) * half), color, turn * n, glm::vec2(0.0f) });
        if (glm::dot(glm::cross(u, w), n) >= 0.0f) idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        else idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    }
}

// A low-poly tree: a trunk and two cones of leaves.
void appendTree(const glm::vec3& at, float size, const glm::vec3& leaf, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    appendBox(at + glm::vec3(0.0f, size * 0.25f, 0.0f), glm::vec3(0.18f, 0.25f, 0.18f) * size, { 0.36f, 0.25f, 0.16f }, v, idx);
    for (int layer = 0; layer < 2; ++layer) {
        const float base = size * (0.45f + 0.55f * static_cast<float>(layer)), r = size * (0.95f - 0.3f * static_cast<float>(layer));
        const glm::vec3 tip = at + glm::vec3(0.0f, base + size * 1.2f, 0.0f);
        constexpr int kSides = 6;
        for (int i = 0; i < kSides; ++i) {
            const float a0 = glm::two_pi<float>() * static_cast<float>(i) / kSides, a1 = glm::two_pi<float>() * static_cast<float>(i + 1) / kSides;
            const glm::vec3 p0 = at + glm::vec3(std::cos(a0) * r, base, std::sin(a0) * r), p1 = at + glm::vec3(std::cos(a1) * r, base, std::sin(a1) * r);
            const glm::vec3 n = glm::normalize(glm::cross(p1 - p0, tip - p0));
            const glm::vec3 outward = glm::dot(n, (p0 + p1) * 0.5f - at) >= 0.0f ? n : -n;
            pushTriangle(v, idx, { p0, leaf, outward, {} }, { p1, leaf, outward, {} }, { tip, leaf * 1.1f, outward, {} });
        }
    }
}

// The colour of the land: beach, grass, darker grass up the hills, rock
// on steep slopes and high up, snow on the mountain's top.
glm::vec3 landColour(float h, float steep) {
    const glm::vec3 sand(0.83f, 0.76f, 0.55f), grass(0.36f, 0.55f, 0.24f), hill(0.27f, 0.44f, 0.2f), rock(0.46f, 0.43f, 0.4f),
        snow(0.93f, 0.95f, 0.98f), seabed(0.62f, 0.6f, 0.45f);
    if (h < Island::kSea + 0.2f) return seabed;
    glm::vec3 c = h < 3.0f ? sand : glm::mix(grass, hill, std::clamp((h - 20.0f) / 90.0f, 0.0f, 1.0f));
    c = glm::mix(c, rock, std::clamp((steep - 0.45f) * 3.0f, 0.0f, 1.0f));
    c = glm::mix(c, rock, std::clamp((h - 170.0f) / 40.0f, 0.0f, 1.0f));
    return glm::mix(c, snow, std::clamp((h - 235.0f) / 25.0f, 0.0f, 1.0f));
}

uint32_t hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

} // namespace

void FlyingModule::buildWorld() {
    const uint32_t seed = m_island.seed();
    if (m_terrain && m_builtSeed == seed) return;
    m_builtSeed = seed;
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    // The land, one quad per grid cell (flat-shaded: the low-poly look).
    const int n = static_cast<int>(2.0f * kExtent / kCell);
    auto at = [&](int i, int j) {
        const float x = -kExtent + static_cast<float>(i) * kCell, z = -kExtent + static_cast<float>(j) * kCell;
        return glm::vec3(x, m_island.terrain(x, z), z);
    };
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const glm::vec3 p00 = at(i, j), p10 = at(i + 1, j), p01 = at(i, j + 1), p11 = at(i + 1, j + 1);
            if (std::max({ p00.y, p10.y, p01.y, p11.y }) < Island::kSea - 6.0f) continue; // deep under the sea: never seen
            for (const auto& tri : { std::array<glm::vec3, 3>{ p00, p01, p10 }, std::array<glm::vec3, 3>{ p10, p01, p11 } }) {
                glm::vec3 nrm = glm::normalize(glm::cross(tri[1] - tri[0], tri[2] - tri[0]));
                if (nrm.y < 0.0f) nrm = -nrm;
                const float h = (tri[0].y + tri[1].y + tri[2].y) / 3.0f;
                const glm::vec3 c = landColour(h, 1.0f - nrm.y);
                pushTriangle(v, idx, { tri[0], c, nrm, {} }, { tri[1], c, nrm, {} }, { tri[2], c, nrm, {} });
            }
        }
    // The runway: tarmac with a white centre line and threshold stripes,
    // a hair above the flattened ground.
    const Runway& w = m_island.runway();
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const float y = w.height() + 0.05f;
    auto quad = [&](float x0, float x1, float z0, float z1, float lift, const glm::vec3& c) {
        const glm::vec3 a(x0, y + lift, z0), b(x1, y + lift, z0), cc(x1, y + lift, z1), d(x0, y + lift, z1);
        pushTriangle(v, idx, { a, c, up, {} }, { b, c, up, {} }, { cc, c, up, {} });
        pushTriangle(v, idx, { a, c, up, {} }, { cc, c, up, {} }, { d, c, up, {} });
    };
    const float x0 = w.start.x - w.width * 0.5f, x1 = w.start.x + w.width * 0.5f;
    quad(x0, x1, w.start.z - w.length, w.start.z, 0.0f, { 0.2f, 0.21f, 0.23f });
    for (float z = w.start.z - 40.0f; z > w.start.z - w.length + 40.0f; z -= 30.0f) quad(w.start.x - 0.4f, w.start.x + 0.4f, z - 12.0f, z, 0.02f, { 0.92f, 0.92f, 0.9f });
    for (int end = 0; end < 2; ++end) {
        const float z = end == 0 ? w.start.z - 6.0f : w.start.z - w.length + 26.0f;
        for (int s = 0; s < 8; ++s) {
            const float sx = x0 + 2.0f + static_cast<float>(s) * (w.width - 4.0f) / 8.0f + (s >= 4 ? 1.0f : 0.0f);
            quad(sx, sx + 1.6f, z - 20.0f, z, 0.02f, { 0.92f, 0.92f, 0.9f });
        }
    }
    // A hangar and a tower by the runway.
    appendBox({ x1 + 45.0f, w.height() + 7.0f, w.start.z - 120.0f }, { 16.0f, 7.0f, 22.0f }, { 0.62f, 0.64f, 0.68f }, v, idx);
    appendBox({ x1 + 45.0f, w.height() + 14.5f, w.start.z - 120.0f }, { 16.5f, 0.6f, 22.5f }, { 0.55f, 0.18f, 0.16f }, v, idx);
    appendBox({ x1 + 30.0f, w.height() + 12.0f, w.start.z - 200.0f }, { 3.0f, 12.0f, 3.0f }, { 0.85f, 0.85f, 0.82f }, v, idx);
    appendBox({ x1 + 30.0f, w.height() + 25.0f, w.start.z - 200.0f }, { 4.5f, 2.0f, 4.5f }, { 0.25f, 0.45f, 0.6f }, v, idx);
    // Trees: scattered on the grass, never on the runway, the beach or the rock.
    uint32_t r = seed * 2246822519u + 3266489917u;
    auto rand01 = [&r]() {
        r = hash(r + 0x9e3779b9u);
        return static_cast<float>(r & 0xffffu) / 65535.0f;
    };
    int trees = 0;
    for (int tries = 0; tries < 9000 && trees < 1400; ++tries) {
        const float x = (rand01() * 2.0f - 1.0f) * Island::kRadius, z = (rand01() * 2.0f - 1.0f) * Island::kRadius;
        const float h = m_island.terrain(x, z);
        if (h < 4.0f || h > 180.0f || m_island.onRunway(x, z, 60.0f)) continue;
        const float slope = std::abs(m_island.terrain(x + 4.0f, z) - h) + std::abs(m_island.terrain(x, z + 4.0f) - h);
        if (slope > 3.0f) continue;
        const float size = 3.0f + rand01() * 3.5f;
        const glm::vec3 leaf = glm::mix(glm::vec3(0.16f, 0.36f, 0.15f), glm::vec3(0.3f, 0.5f, 0.18f), rand01());
        appendTree({ x, h - 0.3f, z }, size, leaf, v, idx);
        ++trees;
    }
    if (m_terrain) m_app->renderer().retire(std::move(m_terrain)); // frames in flight still draw it
    m_terrain = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_terrain->upload(v, idx);
    kke::log::get(name())->info("island {}: {} triangles, {} trees, runway at ({:.0f}, {:.0f}, {:.0f})", seed, idx.size() / 3, trees, w.start.x,
                                w.start.y, w.start.z);

    // The sea: one big sheet, a little translucent-looking blue.
    if (!m_sea) {
        std::vector<kke::Vertex> sv;
        std::vector<uint32_t> si;
        const float s = 30000.0f; // past the far plane: no edge on the horizon
        const glm::vec3 c(0.12f, 0.36f, 0.52f);
        pushTriangle(sv, si, { { -s, Island::kSea, -s }, c, up, {} }, { { s, Island::kSea, -s }, c, up, {} }, { { s, Island::kSea, s }, c, up, {} });
        pushTriangle(sv, si, { { -s, Island::kSea, -s }, c, up, {} }, { { s, Island::kSea, s }, c, up, {} }, { { -s, Island::kSea, s }, c, up, {} });
        m_sea = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
        m_sea->upload(sv, si);
    }
    // Debris of a crash: a few chunks (drawn scaled and spun per crash).
    if (!m_debris) {
        std::vector<kke::Vertex> dv;
        std::vector<uint32_t> di;
        appendBox(glm::vec3(0.0f), glm::vec3(0.5f), { 0.3f, 0.28f, 0.26f }, dv, di);
        m_debris = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
        m_debris->upload(dv, di);
    }
}

// The rings: a torus each, built once per course. The next ring for the
// player is drawn again, gold, over its white one (m_nextRingMesh is one
// torus of radius 1, placed and scaled per ring).
void FlyingModule::buildRings() {
    const int count = std::max(3, m_builtRings);
    m_rings = m_island.rings(count, m_builtRadius);
    auto torus = [](const glm::vec3& center, const glm::vec3& normal, float radius, float thickness, const glm::vec3& colour, std::vector<kke::Vertex>& v,
                    std::vector<uint32_t>& idx) {
        const glm::vec3 a = glm::normalize(std::abs(normal.y) < 0.9f ? glm::cross(normal, glm::vec3(0, 1, 0)) : glm::cross(normal, glm::vec3(1, 0, 0)));
        const glm::vec3 b = glm::cross(normal, a);
        constexpr int kAround = 28, kTube = 8;
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (int i = 0; i <= kAround; ++i) {
            const float u = glm::two_pi<float>() * static_cast<float>(i) / kAround;
            const glm::vec3 dir = a * std::cos(u) + b * std::sin(u);
            for (int j = 0; j <= kTube; ++j) {
                const float t = glm::two_pi<float>() * static_cast<float>(j) / kTube;
                const glm::vec3 nrm = dir * std::cos(t) + normal * std::sin(t);
                // Stripes round the ring, so it reads as a ring from far off.
                const glm::vec3 c = (i / 2) % 2 == 0 ? colour : colour * 0.75f;
                v.push_back({ center + dir * radius + nrm * thickness, c, nrm, {} });
            }
        }
        for (int i = 0; i < kAround; ++i)
            for (int j = 0; j < kTube; ++j) {
                const uint32_t p = base + static_cast<uint32_t>(i * (kTube + 1) + j), q = p + kTube + 1;
                // Outward faces: (p, p+1, q) winds along the normal for this parameterisation.
                const glm::vec3 n0 = glm::cross(v[p + 1].position - v[p].position, v[q].position - v[p].position);
                if (glm::dot(n0, v[p].normal) >= 0.0f) idx.insert(idx.end(), { p, p + 1, q, p + 1, q + 1, q });
                else idx.insert(idx.end(), { p, q, p + 1, p + 1, q, q + 1 });
            }
    };
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    for (const Ring& r : m_rings) torus(r.center, r.normal, r.radius, 0.9f, { 0.95f, 0.95f, 0.98f }, v, idx);
    if (!m_ringMesh) m_ringMesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_ringMesh->upload(v, idx);
    v.clear();
    idx.clear();
    torus(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), 1.0f, 0.1f, { 1.0f, 0.78f, 0.15f }, v, idx);
    if (!m_nextRingMesh) m_nextRingMesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_nextRingMesh->upload(v, idx);
}

} // namespace flying
