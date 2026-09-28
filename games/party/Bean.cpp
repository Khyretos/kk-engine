#include "Bean.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace party {

namespace {

constexpr float kPi = 3.14159265f;

// A triangle facing the way its vertices' normals point (whatever order
// they came in): every primitive here is built from these, so none of
// them shows its inside.
void tri(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx, uint32_t a, uint32_t b, uint32_t c) {
    const glm::vec3 n = glm::cross(v[b].position - v[a].position, v[c].position - v[a].position);
    const glm::vec3 want = v[a].normal + v[b].normal + v[c].normal;
    if (glm::dot(n, want) >= 0.0f) idx.insert(idx.end(), { a, b, c });
    else idx.insert(idx.end(), { a, c, b });
}

uint32_t vert(std::vector<kke::Vertex>& v, const glm::vec3& p, const glm::vec3& c, const glm::vec3& n) {
    v.push_back({ p, c, glm::normalize(n), glm::vec2(0.0f) });
    return static_cast<uint32_t>(v.size() - 1);
}

glm::vec3 lighter(const glm::vec3& c, float k) { return glm::mix(c, glm::vec3(1.0f), k); }
glm::vec3 darker(const glm::vec3& c, float k) { return c * (1.0f - k); }

// The bean's outline: radius at height y (feet at 0, head at kBeanHeight),
// a little wider at the bottom, like a jelly bean standing up.
constexpr float kBottomR = kBeanRadius + 0.02f, kTopR = kBeanRadius - 0.04f;
float beanRadius(float y) {
    const float H = kBeanHeight;
    if (y < kBottomR) {
        const float d = kBottomR - y;
        return std::sqrt(std::max(0.0f, kBottomR * kBottomR - d * d));
    }
    if (y > H - kTopR) {
        const float d = y - (H - kTopR);
        return std::sqrt(std::max(0.0f, kTopR * kTopR - d * d));
    }
    const float t = (y - kBottomR) / (H - kTopR - kBottomR);
    return kBottomR + (kTopR - kBottomR) * t;
}

// The body colour at a point of its surface (direction around, height).
glm::vec3 patternColour(const BeanLook& look, float angle, float y, const glm::vec3& dir) {
    const glm::vec3 base = beanColour(look.colour);
    const glm::vec3 second = glm::length(base) > 1.4f ? darker(base, 0.35f) : lighter(base, 0.55f);
    switch (look.pattern) {
    case 1: // Stripes: bands around the body
        return static_cast<int>(std::floor(y / 0.16f)) % 2 == 0 ? base : second;
    case 2: { // Spots: a dotted pattern by direction
        const glm::vec3 cells = glm::floor(dir * 3.2f + glm::vec3(0.0f, y * 2.5f, 0.0f));
        const glm::vec3 f = dir * 3.2f + glm::vec3(0.0f, y * 2.5f, 0.0f) - cells - glm::vec3(0.5f);
        const float h = std::fmod(std::abs(cells.x * 12.9898f + cells.y * 78.233f + cells.z * 37.719f), 1.0f);
        return glm::length(f) < 0.28f + 0.1f * h ? second : base;
    }
    case 3: // Two-tone: the top half the other colour
        return y > kBeanHeight * 0.55f ? second : base;
    case 4: // Belly: a pale front
        return (dir.z < -0.35f && y > 0.2f && y < 0.8f) ? lighter(base, 0.7f) : base;
    case 5: { // Stars: five points around the middle
        const float star = 0.5f + 0.5f * std::cos(angle * 5.0f);
        return std::abs(y - 0.55f) < 0.08f + 0.12f * star ? glm::vec3(1.0f, 0.9f, 0.35f) : base;
    }
    default: return base;
    }
}

} // namespace

const std::vector<NamedColour>& beanColours() {
    static const std::vector<NamedColour> c = {
        { "Bubblegum", { 1.0f, 0.45f, 0.7f } }, { "Sky", { 0.35f, 0.65f, 1.0f } },   { "Lime", { 0.55f, 0.9f, 0.3f } },
        { "Tangerine", { 1.0f, 0.55f, 0.15f } }, { "Grape", { 0.6f, 0.4f, 0.95f } }, { "Lemon", { 1.0f, 0.88f, 0.25f } },
        { "Mint", { 0.45f, 0.95f, 0.75f } },    { "Cherry", { 0.92f, 0.2f, 0.25f } }, { "Cocoa", { 0.45f, 0.28f, 0.18f } },
        { "Caramel", { 0.8f, 0.55f, 0.3f } },   { "Snow", { 0.95f, 0.95f, 0.97f } }, { "Midnight", { 0.18f, 0.2f, 0.35f } },
    };
    return c;
}
const std::vector<std::string>& beanPatterns() {
    static const std::vector<std::string> p = { "Plain", "Stripes", "Spots", "Two-tone", "Belly", "Stars" };
    return p;
}
const std::vector<std::string>& beanFaces() {
    static const std::vector<std::string> f = { "Happy", "Sleepy", "Fierce", "Shades", "Surprised" };
    return f;
}
const std::vector<std::string>& beanHats() {
    static const std::vector<std::string> h = { "None", "Crown", "Cap", "Top hat", "Beanie", "Horns", "Party hat", "Propeller", "Bow" };
    return h;
}
glm::vec3 beanColour(int colour) {
    const auto& c = beanColours();
    return c[static_cast<size_t>(std::clamp(colour, 0, static_cast<int>(c.size()) - 1))].rgb;
}

void MeshBuilder::box(const glm::vec3& center, const glm::vec3& half, const glm::vec3& color, const glm::mat3& rot) {
    const glm::vec3 normals[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (const glm::vec3& n : normals) {
        const glm::vec3 u = std::abs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 w = glm::cross(n, u);
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (glm::vec2 k : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1) })
            vert(v, center + rot * ((n + u * k.x + w * k.y) * half), color, rot * n);
        tri(v, idx, base, base + 1, base + 2);
        tri(v, idx, base, base + 2, base + 3);
    }
}

void MeshBuilder::ellipsoid(const glm::vec3& center, const glm::vec3& radii, const glm::vec3& color, int slices, int stacks) {
    const uint32_t base = static_cast<uint32_t>(v.size());
    for (int i = 0; i <= stacks; ++i) {
        const float phi = kPi * static_cast<float>(i) / static_cast<float>(stacks);
        for (int j = 0; j <= slices; ++j) {
            const float th = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(slices);
            const glm::vec3 d(std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th));
            vert(v, center + d * radii, color, d / radii);
        }
    }
    const uint32_t row = static_cast<uint32_t>(slices + 1);
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < slices; ++j) {
            const uint32_t a = base + static_cast<uint32_t>(i) * row + static_cast<uint32_t>(j);
            if (i > 0) tri(v, idx, a, a + row, a + 1);
            if (i + 1 < stacks) tri(v, idx, a + 1, a + row, a + row + 1);
        }
}

void MeshBuilder::cylinder(const glm::vec3& base, float radius, float topRadius, float height, const glm::vec3& color, int slices, bool caps) {
    const uint32_t first = static_cast<uint32_t>(v.size());
    const float slope = (radius - topRadius) / std::max(height, 1e-4f);
    for (int j = 0; j <= slices; ++j) {
        const float th = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(slices);
        const glm::vec3 d(std::cos(th), 0.0f, std::sin(th));
        const glm::vec3 n = d + glm::vec3(0.0f, slope, 0.0f);
        vert(v, base + d * radius, color, n);
        vert(v, base + d * topRadius + glm::vec3(0.0f, height, 0.0f), color, n);
    }
    for (int j = 0; j < slices; ++j) {
        const uint32_t a = first + static_cast<uint32_t>(j) * 2;
        if (radius > 0.0f) tri(v, idx, a, a + 1, a + 2);
        if (topRadius > 0.0f) tri(v, idx, a + 1, a + 3, a + 2);
    }
    if (!caps) return;
    for (int end = 0; end < 2; ++end) {
        const float r = end ? topRadius : radius;
        if (r <= 0.0f) continue;
        const glm::vec3 n(0.0f, end ? 1.0f : -1.0f, 0.0f);
        const glm::vec3 c = base + glm::vec3(0.0f, end ? height : 0.0f, 0.0f);
        const uint32_t centre = vert(v, c, color, n);
        for (int j = 0; j <= slices; ++j) {
            const float th = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(slices);
            vert(v, c + glm::vec3(std::cos(th), 0.0f, std::sin(th)) * r, color, n);
        }
        for (int j = 0; j < slices; ++j) tri(v, idx, centre, centre + 1 + static_cast<uint32_t>(j), centre + 2 + static_cast<uint32_t>(j));
    }
}

void MeshBuilder::hexPrism(const glm::vec3& center, float radius, float halfHeight, const glm::vec3& top, const glm::vec3& side) {
    glm::vec3 corner[6];
    for (int k = 0; k < 6; ++k) {
        const float a = kPi / 3.0f * static_cast<float>(k);
        corner[k] = glm::vec3(std::cos(a) * radius, 0.0f, std::sin(a) * radius);
    }
    for (int end = 0; end < 2; ++end) {
        const glm::vec3 n(0.0f, end ? 1.0f : -1.0f, 0.0f);
        const glm::vec3 c = center + n * halfHeight;
        const uint32_t centre = vert(v, c, end ? top : side, n);
        for (int k = 0; k < 6; ++k) vert(v, c + corner[k], end ? top : side, n);
        for (int k = 0; k < 6; ++k) tri(v, idx, centre, centre + 1 + static_cast<uint32_t>(k), centre + 1 + static_cast<uint32_t>((k + 1) % 6));
    }
    for (int k = 0; k < 6; ++k) {
        const glm::vec3 a = corner[k], b = corner[(k + 1) % 6];
        const glm::vec3 n = glm::normalize((a + b) * 0.5f);
        const uint32_t s = static_cast<uint32_t>(v.size());
        vert(v, center + a - glm::vec3(0, halfHeight, 0), side, n);
        vert(v, center + b - glm::vec3(0, halfHeight, 0), side, n);
        vert(v, center + b + glm::vec3(0, halfHeight, 0), side, n);
        vert(v, center + a + glm::vec3(0, halfHeight, 0), side, n);
        tri(v, idx, s, s + 1, s + 2);
        tri(v, idx, s, s + 2, s + 3);
    }
}

void MeshBuilder::transform(size_t from, const glm::mat4& m) {
    const glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(m)));
    for (size_t i = from; i < v.size(); ++i) {
        v[i].position = glm::vec3(m * glm::vec4(v[i].position, 1.0f));
        v[i].normal = glm::normalize(nm * v[i].normal);
    }
}

void buildBean(const BeanLook& look, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    MeshBuilder mb{ v, idx };
    // The body: a lathe of the outline, coloured by the pattern.
    constexpr int kSlices = 28, kRings = 22;
    const uint32_t base = static_cast<uint32_t>(v.size());
    for (int i = 0; i <= kRings; ++i) {
        const float y = kBeanHeight * static_cast<float>(i) / static_cast<float>(kRings);
        const float r = beanRadius(y);
        // The outline's slope, for the normal (numerically).
        const float dy = 0.01f;
        const float dr = (beanRadius(std::min(kBeanHeight, y + dy)) - beanRadius(std::max(0.0f, y - dy))) / (2.0f * dy);
        for (int j = 0; j <= kSlices; ++j) {
            const float th = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(kSlices);
            const glm::vec3 d(std::cos(th), 0.0f, std::sin(th));
            glm::vec3 n = d - glm::vec3(0.0f, dr, 0.0f);
            if (i == 0) n = glm::vec3(0, -1, 0);
            if (i == kRings) n = glm::vec3(0, 1, 0);
            vert(v, d * r + glm::vec3(0.0f, y, 0.0f), patternColour(look, th, y, d), n);
        }
    }
    const uint32_t row = kSlices + 1;
    for (int i = 0; i < kRings; ++i)
        for (int j = 0; j < kSlices; ++j) {
            const uint32_t a = base + static_cast<uint32_t>(i) * row + static_cast<uint32_t>(j);
            if (i > 0) tri(v, idx, a, a + row, a + 1);
            if (i + 1 < kRings) tri(v, idx, a + 1, a + row, a + row + 1);
        }

    // The face: a pale visor on the front (-Z), eyes on it.
    const float faceY = 0.98f;
    const float front = beanRadius(faceY);
    const glm::vec3 visor(0.0f, faceY, -front + 0.07f);
    mb.ellipsoid(visor, { 0.3f, 0.17f, 0.1f }, glm::vec3(0.97f, 0.95f, 0.9f), 20, 10);
    const glm::vec3 ink(0.06f, 0.06f, 0.09f);
    for (int s = -1; s <= 1; s += 2) {
        const float x = 0.1f * static_cast<float>(s);
        const glm::vec3 eye(x, faceY + 0.01f, -front - 0.02f);
        switch (look.face) {
        case 1: // Sleepy: half-closed lids
            mb.ellipsoid(eye, { 0.05f, 0.022f, 0.03f }, ink, 10, 6);
            break;
        case 2: { // Fierce: eyes and slanted brows
            mb.ellipsoid(eye, { 0.045f, 0.055f, 0.03f }, ink, 10, 6);
            const size_t from = v.size();
            mb.box(glm::vec3(0.0f), { 0.07f, 0.014f, 0.015f }, ink);
            mb.transform(from, glm::rotate(glm::translate(glm::mat4(1.0f), eye + glm::vec3(0.0f, 0.085f, -0.01f)), 0.35f * static_cast<float>(-s),
                                           glm::vec3(0, 0, 1)));
            break;
        }
        case 3: // Shades: one dark bar
            if (s < 0) mb.box(glm::vec3(0.0f, faceY + 0.015f, -front - 0.02f), { 0.24f, 0.05f, 0.025f }, glm::vec3(0.05f, 0.05f, 0.08f));
            break;
        case 4: // Surprised: big round eyes and an O
            mb.ellipsoid(eye, { 0.055f, 0.07f, 0.03f }, ink, 10, 6);
            mb.ellipsoid(eye + glm::vec3(0.015f, 0.025f, -0.02f), { 0.018f, 0.018f, 0.012f }, glm::vec3(1.0f), 8, 5);
            if (s > 0) mb.ellipsoid(glm::vec3(0.0f, faceY - 0.1f, -front + 0.02f), { 0.035f, 0.035f, 0.02f }, ink, 10, 6);
            break;
        default: // Happy: tall eyes with a shine
            mb.ellipsoid(eye, { 0.04f, 0.065f, 0.03f }, ink, 10, 6);
            mb.ellipsoid(eye + glm::vec3(0.012f, 0.025f, -0.02f), { 0.014f, 0.014f, 0.01f }, glm::vec3(1.0f), 8, 5);
            break;
        }
    }

    // The hat, on top.
    const float top = kBeanHeight;
    const glm::vec3 gold(1.0f, 0.8f, 0.2f), red(0.9f, 0.15f, 0.2f), black(0.08f, 0.08f, 0.1f);
    const glm::vec3 own = beanColour(look.colour);
    const glm::vec3 other = glm::length(own - red) < 0.5f ? glm::vec3(0.2f, 0.45f, 1.0f) : red;
    switch (look.hat) {
    case 1: { // Crown: a gold band with five points
        const glm::vec3 b(0.0f, top - 0.08f, 0.0f);
        mb.cylinder(b, 0.24f, 0.24f, 0.12f, gold, 20, false);
        for (int k = 0; k < 5; ++k) {
            const float a = 2.0f * kPi * static_cast<float>(k) / 5.0f;
            mb.cylinder(b + glm::vec3(std::cos(a) * 0.21f, 0.12f, std::sin(a) * 0.21f), 0.05f, 0.0f, 0.12f, gold, 8);
            mb.ellipsoid(b + glm::vec3(std::cos(a) * 0.235f, 0.06f, std::sin(a) * 0.235f), glm::vec3(0.025f), other, 8, 5);
        }
        break;
    }
    case 2: // Cap: a dome and a brim to the front
        mb.ellipsoid(glm::vec3(0.0f, top - 0.1f, 0.0f), { 0.33f, 0.2f, 0.33f }, other, 18, 8);
        mb.box(glm::vec3(0.0f, top - 0.08f, -0.36f), { 0.2f, 0.015f, 0.14f }, other);
        break;
    case 3: // Top hat
        mb.cylinder(glm::vec3(0.0f, top - 0.06f, 0.0f), 0.36f, 0.36f, 0.03f, black, 22);
        mb.cylinder(glm::vec3(0.0f, top - 0.05f, 0.0f), 0.22f, 0.22f, 0.36f, black, 22);
        mb.cylinder(glm::vec3(0.0f, top + 0.0f, 0.0f), 0.225f, 0.225f, 0.06f, other, 22, false);
        break;
    case 4: // Beanie with a pompom
        mb.ellipsoid(glm::vec3(0.0f, top - 0.1f, 0.0f), { 0.34f, 0.24f, 0.34f }, other, 18, 8);
        mb.cylinder(glm::vec3(0.0f, top - 0.16f, 0.0f), 0.345f, 0.345f, 0.08f, lighter(other, 0.5f), 22, false);
        mb.ellipsoid(glm::vec3(0.0f, top + 0.15f, 0.0f), glm::vec3(0.08f), glm::vec3(1.0f), 10, 6);
        break;
    case 5: // Horns
        for (int s = -1; s <= 1; s += 2) {
            const size_t from = v.size();
            mb.cylinder(glm::vec3(0.0f), 0.07f, 0.0f, 0.26f, glm::vec3(0.95f, 0.92f, 0.85f), 10);
            mb.transform(from, glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.2f * static_cast<float>(s), top - 0.1f, 0.0f)),
                                           -0.5f * static_cast<float>(s), glm::vec3(0, 0, 1)));
        }
        break;
    case 6: // Party hat, tilted
    {
        const size_t from = v.size();
        mb.cylinder(glm::vec3(0.0f), 0.17f, 0.0f, 0.45f, other, 16);
        mb.ellipsoid(glm::vec3(0.0f, 0.46f, 0.0f), glm::vec3(0.05f), gold, 8, 5);
        mb.transform(from, glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.06f, top - 0.06f, 0.0f)), -0.25f, glm::vec3(0, 0, 1)));
        break;
    }
    case 7: // Propeller cap
        mb.ellipsoid(glm::vec3(0.0f, top - 0.1f, 0.0f), { 0.3f, 0.17f, 0.3f }, glm::vec3(1.0f, 0.85f, 0.2f), 18, 8);
        mb.cylinder(glm::vec3(0.0f, top + 0.05f, 0.0f), 0.02f, 0.02f, 0.1f, black, 8);
        mb.box(glm::vec3(0.0f, top + 0.16f, 0.0f), { 0.28f, 0.012f, 0.04f }, red);
        mb.box(glm::vec3(0.0f, top + 0.16f, 0.0f), { 0.04f, 0.012f, 0.28f }, glm::vec3(0.2f, 0.45f, 1.0f));
        break;
    case 8: // Bow, on the side of the head
        for (int s = -1; s <= 1; s += 2) {
            const size_t from = v.size();
            mb.ellipsoid(glm::vec3(0.0f), { 0.1f, 0.07f, 0.04f }, other, 10, 6);
            mb.transform(from, glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.18f + 0.09f * static_cast<float>(s), top - 0.02f, 0.0f)),
                                           0.3f * static_cast<float>(s), glm::vec3(0, 0, 1)));
        }
        mb.ellipsoid(glm::vec3(0.18f, top - 0.02f, 0.0f), glm::vec3(0.04f), lighter(other, 0.3f), 8, 5);
        break;
    default: break;
    }
}

void buildLimb(const BeanLook& look, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    MeshBuilder{ v, idx }.ellipsoid(glm::vec3(0.0f), glm::vec3(1.0f), darker(beanColour(look.colour), 0.12f), 12, 8);
}

} // namespace party
