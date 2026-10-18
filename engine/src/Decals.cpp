#include "kke/Decals.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace kke {

glm::mat4 decalWorldToLocal(const Decal& d) {
    const float nl = glm::length(d.normal);
    const glm::vec3 n = nl > 1e-6f ? d.normal / nl : glm::vec3(0.0f, 1.0f, 0.0f);
    // Any direction across the face, then turned by the angle round the normal.
    const glm::vec3 helper = std::abs(n.y) < 0.95f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 x = glm::normalize(glm::cross(helper, n));
    glm::vec3 y = glm::cross(n, x);
    const float c = std::cos(d.angle), s = std::sin(d.angle);
    const glm::vec3 xr = x * c + y * s, yr = y * c - x * s;
    const float half = 0.5f * std::max(d.size, 1e-3f);
    const float reach = d.depth > 0.0f ? d.depth : 0.25f * std::max(d.size, 1e-3f);
    // Rows: each axis divided by the box's half size, about the spot.
    glm::mat4 m(1.0f);
    for (int col = 0; col < 3; ++col) {
        m[col][0] = xr[col] / half;
        m[col][1] = yr[col] / half;
        m[col][2] = n[col] / reach;
    }
    m[3][0] = -glm::dot(xr, d.position) / half;
    m[3][1] = -glm::dot(yr, d.position) / half;
    m[3][2] = -glm::dot(n, d.position) / reach;
    return m;
}

float decalWeight(const glm::vec3& local, float normalDot) {
    if (std::abs(local.x) > 1.0f || std::abs(local.y) > 1.0f || std::abs(local.z) > 1.0f) return 0.0f;
    const float depth = 1.0f - std::clamp((std::abs(local.z) - 0.6f) / 0.4f, 0.0f, 1.0f);
    const float facing = std::clamp((normalDot - 0.2f) / 0.3f, 0.0f, 1.0f);
    return depth * facing * facing * (3.0f - 2.0f * facing);
}

float decalFade(const Decal& d, float age) {
    if (d.life <= 0.0f) return 1.0f;
    if (age <= d.life) return 1.0f;
    return std::clamp(1.0f - (age - d.life) / std::max(d.fade, 1e-3f), 0.0f, 1.0f);
}

// --- Built-in textures -------------------------------------------------------

namespace {

float hash(int x, int y, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xffffffu) / 16777215.0f;
}

// Smooth value noise, 0..1, with `cells` across the texture (wraps).
float noise(float u, float v, int cells, uint32_t seed) {
    const float x = u * cells, y = v * cells;
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const float fx = x - x0, fy = y - y0;
    const float sx = fx * fx * (3.0f - 2.0f * fx), sy = fy * fy * (3.0f - 2.0f * fy);
    auto at = [&](int i, int j) { return hash(((i % cells) + cells) % cells, ((j % cells) + cells) % cells, seed); };
    const float a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * sx;
    const float b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * sx;
    return a + (b - a) * sy;
}

float fbm(float u, float v, uint32_t seed) {
    return 0.5f * noise(u, v, 4, seed) + 0.3f * noise(u, v, 9, seed + 1) + 0.2f * noise(u, v, 21, seed + 2);
}

float smooth(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

struct Texel {
    float r, g, b, a;
};

Texel texelOf(const std::string& name, float u, float v) {
    const float x = u * 2.0f - 1.0f, y = v * 2.0f - 1.0f;
    const float r = std::sqrt(x * x + y * y);
    const float a = std::atan2(y, x);
    if (name == "scorch") {
        // Soot: black in the middle, a ragged brown rim, flecks out past it.
        const float edge = 0.62f + 0.25f * (fbm(0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a), 11u) - 0.5f) * 2.0f;
        const float body = 1.0f - smooth(edge - 0.25f, edge, r);
        const float fleck = smooth(0.72f, 0.78f, fbm(u, v, 12u)) * (1.0f - smooth(0.85f, 1.0f, r));
        const float dark = 0.05f + 0.25f * smooth(0.0f, edge, r);
        return { dark * 1.1f, dark * 0.95f, dark * 0.8f, std::clamp(std::max(body * (0.95f - 0.3f * r), fleck * 0.7f), 0.0f, 1.0f) };
    }
    if (name == "splat") {
        // A white blob with drips and droplets (the tint gives it its colour: paint, slime, blood).
        float blob = 0.55f + 0.18f * (fbm(0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a), 21u) - 0.5f) * 2.0f;
        blob += 0.25f * std::pow(std::max(0.0f, std::cos(a * 5.0f + 1.3f)), 12.0f); // splashes out
        float alpha = 1.0f - smooth(blob - 0.04f, blob, r);
        for (int k = 0; k < 9; ++k) {
            const float da = hash(k, 1, 23u) * 6.2831853f, dr = 0.7f + 0.25f * hash(k, 2, 23u), size = 0.03f + 0.05f * hash(k, 3, 23u);
            const float dx = x - dr * std::cos(da), dy = y - dr * std::sin(da);
            alpha = std::max(alpha, 1.0f - smooth(size * 0.8f, size, std::sqrt(dx * dx + dy * dy)));
        }
        const float shade = 0.85f + 0.15f * fbm(u, v, 24u);
        return { shade, shade, shade, alpha };
    }
    if (name == "crack") {
        // Dark cracks branching out from the middle.
        float d = 1.0f;
        for (int k = 0; k < 7; ++k) {
            const float base = hash(k, 0, 31u) * 6.2831853f;
            const float wobble = 0.25f * (fbm(r * 0.5f + 0.1f * k, 0.3f * k, 32u) - 0.5f);
            float diff = std::abs(std::remainder(a - base - wobble * r * 3.0f, 6.2831853f));
            const float len = 0.55f + 0.4f * hash(k, 1, 31u);
            if (r > len) diff += (r - len) * 4.0f;
            d = std::min(d, diff * r);
        }
        const float width = 0.012f * (1.2f - r);
        const float alpha = (1.0f - smooth(width * 0.5f, width * 1.6f, d)) * (1.0f - smooth(0.85f, 1.0f, r));
        return { 0.08f, 0.07f, 0.06f, alpha };
    }
    if (name == "hole") {
        // A bullet hole: a dark pit, a ring of torn grey, scorch round it.
        const float pit = 1.0f - smooth(0.12f, 0.18f, r);
        const float ring = smooth(0.12f, 0.2f, r) * (1.0f - smooth(0.25f, 0.4f, r + 0.08f * (fbm(u, v, 41u) - 0.5f)));
        const float soot = (1.0f - smooth(0.2f, 0.75f, r)) * 0.6f;
        const float alpha = std::clamp(std::max({ pit, ring, soot }), 0.0f, 1.0f);
        const float c = pit > 0.5f ? 0.02f : ring > 0.3f ? 0.45f : 0.12f;
        return { c, c, c * 0.95f, alpha };
    }
    if (name == "puddle") {
        // Dark water, soft at its edge: use with a low roughness for the wet shine.
        const float edge = 0.7f + 0.22f * (fbm(0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a), 51u) - 0.5f) * 2.0f;
        const float alpha = (1.0f - smooth(edge - 0.15f, edge, r)) * 0.85f;
        return { 0.18f, 0.18f, 0.2f, alpha };
    }
    if (name == "footprint") {
        // A shoe print: sole and heel, ridged.
        auto oval = [&](float cx, float cy, float rx, float ry) {
            const float ex = (x - cx) / rx, ey = (y - cy) / ry;
            return 1.0f - smooth(0.9f, 1.0f, std::sqrt(ex * ex + ey * ey));
        };
        const float sole = oval(0.0f, -0.25f, 0.3f, 0.5f), heel = oval(0.0f, 0.6f, 0.26f, 0.26f);
        const float ridges = 0.6f + 0.4f * std::abs(std::sin(y * 40.0f));
        const float alpha = std::max(sole, heel) * ridges * 0.8f;
        return { 0.2f, 0.16f, 0.12f, alpha };
    }
    return { 0.0f, 0.0f, 0.0f, -1.0f };
}

} // namespace

const std::vector<std::string>& decalBuiltIns() {
    static const std::vector<std::string> names{ "scorch", "splat", "crack", "hole", "puddle", "footprint" };
    return names;
}

std::vector<uint8_t> decalTexture(const std::string& name, int size) {
    if (std::find(decalBuiltIns().begin(), decalBuiltIns().end(), name) == decalBuiltIns().end() || size <= 0) return {};
    std::vector<uint8_t> out(static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
    auto toByte = [](float linear) {
        const float c = std::clamp(linear, 0.0f, 1.0f);
        const float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; // stored sRGB
        return static_cast<uint8_t>(std::lround(s * 255.0f));
    };
    for (int j = 0; j < size; ++j)
        for (int i = 0; i < size; ++i) {
            const Texel t = texelOf(name, (static_cast<float>(i) + 0.5f) / static_cast<float>(size), (static_cast<float>(j) + 0.5f) / static_cast<float>(size));
            uint8_t* p = &out[(static_cast<size_t>(j) * static_cast<size_t>(size) + static_cast<size_t>(i)) * 4];
            p[0] = toByte(t.r);
            p[1] = toByte(t.g);
            p[2] = toByte(t.b);
            p[3] = static_cast<uint8_t>(std::lround(std::clamp(t.a, 0.0f, 1.0f) * 255.0f));
        }
    return out;
}

// --- The set -----------------------------------------------------------------

DecalId DecalSet::place(const Decal& d, float now, uint16_t layer) {
    if (m_decals.size() >= kMax) m_decals.erase(m_decals.begin()); // the oldest goes
    const DecalId id = m_next++;
    if (m_next == 0) m_next = 1;
    m_decals.push_back({ id, d, now, layer });
    return id;
}

bool DecalSet::remove(DecalId id) {
    const auto it = std::find_if(m_decals.begin(), m_decals.end(), [id](const Placed& p) { return p.id == id; });
    if (it == m_decals.end()) return false;
    m_decals.erase(it);
    return true;
}

void DecalSet::clear() { m_decals.clear(); }

void DecalSet::update(float now) {
    m_decals.erase(std::remove_if(m_decals.begin(), m_decals.end(),
                                  [now](const Placed& p) { return p.decal.life > 0.0f && decalFade(p.decal, now - p.placedAt) <= 0.0f; }),
                   m_decals.end());
}

void DecalSet::build(float now, std::vector<GpuDecal>& out, std::vector<glm::vec4>& spheres) const {
    out.clear();
    spheres.clear();
    for (const Placed& p : m_decals) {
        const Decal& d = p.decal;
        const float opacity = d.opacity * decalFade(d, now - p.placedAt);
        if (opacity <= 0.0f) continue;
        const float nl = glm::length(d.normal);
        const glm::vec3 n = nl > 1e-6f ? d.normal / nl : glm::vec3(0.0f, 1.0f, 0.0f);
        out.push_back({ decalWorldToLocal(d), glm::vec4(d.color, opacity), glm::vec4(static_cast<float>(p.layer), d.roughness, 0.0f, 0.0f),
                        glm::vec4(n, 0.0f) });
        const float half = 0.5f * d.size, reach = d.depth > 0.0f ? d.depth : 0.25f * d.size;
        spheres.emplace_back(d.position, std::sqrt(2.0f * half * half + reach * reach));
    }
}

} // namespace kke
