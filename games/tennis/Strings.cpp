#include "Strings.h"

#include "kke/Mesh.h"
#include "kke/modules/PhysicsModule.h"

#include <algorithm>
#include <cmath>

namespace tennis {

namespace {

// Softer than real strings (see Strings.h): measured (KKE_TENNIS_STRINGTEST)
// a 30 m/s ball leaves a pocket ~2 cm deep that rings out in ~0.4 s.
kke::Material stringMaterial() {
    kke::Material m;
    m.density = 300.0f;
    m.stiffness = 1.0e4f;
    m.poissonsRatio = 0.3f;
    m.fractureStressThreshold = 1.0e9f; // never breaks
    m.plasticYieldThreshold = 1.0e9f;   // never stays dented
    m.plasticCreep = 0.0f;
    return m;
}

// The slab: one cell thick. Thinner than ~3 cm and FEMFX's solver jitters
// on the flat tets (measured: 12 mm never came to rest).
constexpr int kCellsX = 6, kCellsY = 7;
constexpr float kThickness = 0.04f;
constexpr float kPinnedFrom = 0.9f;   // (x/a)^2 + (y/b)^2 past this: held by the frame
constexpr float kStretch = 0.03f;     // how far the slab is stretched into the frame
constexpr float kBentFrom = 0.002f;   // m: less than this and the strings are straight
constexpr float kPush = 0.1f;         // on the ball's speed: how fast the middle of the pocket starts in (30 m/s: 17 mm deep)
constexpr float kStillFor = 0.25f;    // s straight and still, then it sleeps

} // namespace

void StringLayout::build(float spacing) {
    points.clear();
    first.clear();
    auto run = [&](bool main, float across) {
        // Half the string's length at this offset (inside the ellipse).
        const float r = main ? radii.x : radii.y, along = main ? radii.y : radii.x;
        const float k = 1.0f - (across * across) / (r * r);
        if (k <= 0.0f) return;
        const float half = along * std::sqrt(k);
        const int n = std::max(2, static_cast<int>(std::ceil(2.0f * half / spacing)) + 1);
        first.push_back(static_cast<uint32_t>(points.size()));
        for (int i = 0; i < n; ++i) {
            const float s = -half + 2.0f * half * static_cast<float>(i) / static_cast<float>(n - 1);
            points.push_back(main ? glm::vec3(centre.x + across, centre.y + s, 0.0f) : glm::vec3(centre.x + s, centre.y + across, 0.0f));
        }
    };
    for (int i = 0; i < mains; ++i) run(true, radii.x * ((static_cast<float>(i) + 0.5f) / static_cast<float>(mains) * 2.0f - 1.0f));
    for (int i = 0; i < crosses; ++i) run(false, radii.y * ((static_cast<float>(i) + 0.5f) / static_cast<float>(crosses) * 2.0f - 1.0f));
    first.push_back(static_cast<uint32_t>(points.size()));
}

void StringLayout::mesh(const std::vector<glm::vec3>& bent, float width, const glm::vec3& color, std::vector<kke::Vertex>& v,
                        std::vector<uint32_t>& idx) const {
    if (bent.size() != points.size()) return;
    for (size_t s = 0; s + 1 < first.size(); ++s) {
        // Mains sit a hair behind the crosses, so the weave doesn't flicker.
        const float z = s < static_cast<size_t>(mains) ? -0.0008f : 0.0008f;
        for (uint32_t i = first[s]; i + 1 < first[s + 1]; ++i) {
            const glm::vec3 a = bent[i] + glm::vec3(0, 0, z), b = bent[i + 1] + glm::vec3(0, 0, z);
            const glm::vec3 d = b - a;
            glm::vec3 side = glm::cross(d, glm::vec3(0, 0, 1));
            if (glm::length(side) < 1e-6f) continue;
            side = glm::normalize(side) * (0.5f * width);
            for (float face : { 1.0f, -1.0f }) {
                const uint32_t base = static_cast<uint32_t>(v.size());
                const glm::vec3 n(0.0f, 0.0f, face);
                v.push_back({ a - side, color, n, glm::vec2(0.0f) });
                v.push_back({ b - side, color, n, glm::vec2(0.0f) });
                v.push_back({ b + side, color, n, glm::vec2(0.0f) });
                v.push_back({ a + side, color, n, glm::vec2(0.0f) });
                // Counter-clockwise seen from the side the normal points to.
                if (glm::dot(glm::cross(d, side), n) < 0.0f) idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
                else idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
            }
        }
    }
}

StringBed::StringBed(kke::PhysicsModule& physics, const StringLayout& layout, int slot) : m_physics(physics), m_layout(layout), m_slot(slot) {
    // The slab: the head's rectangle (a cell of margin past the rim), its
    // middle at the head's middle. Slab frame = racket frame - centre. It
    // is made kStretch smaller than the head, then pulled out to size
    // with its rim pinned: stretched, like strings at tension, so it
    // springs back and rings instead of flopping.
    const float shrink = 1.0f - kStretch;
    const glm::vec2 size = 2.0f * layout.radii * 1.1f * shrink;
    kke::TetMeshData slab = kke::PhysicsModule::buildGridBox(kCellsX, kCellsY, 1, size.x, size.y, kThickness);
    kke::PhysicsModule::TetSpawnOptions o;
    o.drawOnlyCracks = true; // never drawn: the racket draws the strings
    const glm::vec2 rim = layout.radii * shrink;
    for (uint32_t i = 0; i < slab.vertices.size(); ++i) {
        const glm::vec3& p = slab.vertices[i];
        const float e = (p.x * p.x) / (rim.x * rim.x) + (p.y * p.y) / (rim.y * rim.y);
        if (e >= kPinnedFrom) o.pinnedVerts.push_back(i);
    }
    // 150 m over the sport center (FEMFX stops moving things ~900 m up),
    // 1.5 m apart (a slab is 0.3 m), so no two beds and nothing else touch.
    m_park = glm::vec3(-24.0f + 1.5f * static_cast<float>(slot % 32), 150.0f, -3.0f + 1.5f * static_cast<float>(slot / 32));
    std::vector<glm::vec3> inSlab;
    inSlab.reserve(layout.points.size());
    for (const glm::vec3& p : layout.points) inSlab.push_back(glm::vec3((glm::vec2(p) - layout.centre) * shrink, 0.0f));
    m_normals.assign(inSlab.size(), glm::vec3(0.0f, 0.0f, 1.0f));
    m_embed = kke::embedPoints(slab, inSlab);
    m_handle = physics.spawnTetMeshWithOptions(slab, m_park, stringMaterial(), o);
    if (!m_handle) return;
    const glm::vec3 park = m_park;
    physics.moveVertices(m_handle, [&](const glm::vec3& p) {
        const glm::vec3 d = p - park;
        return park + glm::vec3(d.x / shrink, d.y / shrink, d.z);
    });
}

StringBed::~StringBed() {
    if (m_handle) m_physics.removeObject(m_handle);
}

void StringBed::strike(const glm::vec3& at, const glm::vec3& velocity) {
    // Not settled into its frame yet (the first half second of a new bed).
    if (!m_handle || m_rest.empty()) return;
    // The game's swing meets the ball by timing (Play.cpp), so the drawn
    // racket can be a few centimetres off it: the pocket goes where the
    // ball crosses the face, kept inside the strings, and as deep as the
    // ball is fast.
    const float into = std::min(glm::length(velocity), 60.0f) * kPush;
    if (into <= 0.0f) return;
    glm::vec2 d = glm::vec2(at) - m_layout.centre;
    const float e = std::sqrt((d.x * d.x) / (m_layout.radii.x * m_layout.radii.x) + (d.y * d.y) / (m_layout.radii.y * m_layout.radii.y));
    if (e > 0.75f) d *= 0.75f / e;
    const glm::vec3 hit = m_park + glm::vec3(d, 0.0f);
    constexpr float kSpread = 0.045f; // m: about the ball's size, a bit more
    m_physics.changeVertexVelocities(m_handle, [&](const glm::vec3& p, const glm::vec3& v) {
        const glm::vec2 r = glm::vec2(p - hit);
        const float w = std::exp(-glm::dot(r, r) / (2.0f * kSpread * kSpread));
        return v - glm::vec3(0.0f, 0.0f, into * w);
    });
    m_bent = true;
    m_still = 0.0f;
}

bool StringBed::update(float dt, std::vector<glm::vec3>& bent) {
    if (!m_handle) return false;
    if (!m_bent && !m_rest.empty() && m_physics.isObjectAsleep(m_handle)) {
        m_depth = 0.0f;
        return false;
    }
    if (!m_physics.deformEmbedded(m_handle, m_embed, m_normals, m_out, m_outNormals) || m_out.size() != m_layout.points.size()) return false;
    if (m_rest.empty()) {
        // Settling: the stretched slab finds its shape in the frame. Once
        // it stops moving that shape is "straight", and it sleeps.
        float moved = 0.0f;
        if (m_settle.size() == m_out.size())
            for (size_t i = 0; i < m_out.size(); ++i) moved = std::max(moved, glm::length(m_out[i] - m_settle[i]));
        m_settle = m_out;
        m_still = moved < 0.0002f ? m_still + dt : 0.0f;
        if (m_still >= kStillFor) {
            m_rest.resize(m_out.size());
            for (size_t i = 0; i < m_out.size(); ++i) m_rest[i] = m_out[i].z - m_park.z;
            m_settle.clear();
            m_physics.sleepObject(m_handle);
        }
        return false;
    }
    bent.resize(m_out.size());
    m_depth = 0.0f;
    for (size_t i = 0; i < m_out.size(); ++i) {
        // Only the pocket: along the face's normal.
        const float dz = m_out[i].z - m_park.z - m_rest[i];
        m_depth = std::max(m_depth, std::fabs(dz));
        bent[i] = m_layout.points[i] + glm::vec3(0.0f, 0.0f, dz);
    }
    m_still = m_depth < kBentFrom ? m_still + dt : 0.0f;
    if (m_still >= kStillFor) {
        m_physics.sleepObject(m_handle);
        m_bent = false;
        m_depth = 0.0f;
        return false;
    }
    return true;
}

} // namespace tennis
