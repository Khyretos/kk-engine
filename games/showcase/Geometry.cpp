#include "Geometry.h"

#include <glm/gtc/constants.hpp>

#include <cmath>

namespace kke_showcase {

void appendBox(const glm::mat4& m, const glm::vec3& half, const glm::vec3& color, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    const glm::vec3 n[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    const glm::mat3 nm = glm::mat3(m);
    for (const glm::vec3& normal : n) {
        glm::vec3 u = std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        glm::vec3 w = glm::cross(normal, u);
        uint32_t base = static_cast<uint32_t>(v.size());
        // Slightly darker sides: shape reads better without textures.
        glm::vec3 c = color * (normal.y > 0.5f ? 1.0f : normal.y < -0.5f ? 0.6f : 0.85f);
        for (glm::vec2 k : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1) }) {
            glm::vec3 p = (normal + u * k.x + w * k.y) * half;
            v.push_back({ glm::vec3(m * glm::vec4(p, 1.0f)), c, glm::normalize(nm * normal), glm::vec2(0.0f) });
        }
        // Counter-clockwise from outside.
        glm::vec3 a = v[base].position, b = v[base + 1].position, cc = v[base + 2].position;
        if (glm::dot(glm::cross(b - a, cc - a), nm * normal) >= 0.0f) idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        else idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    }
}

void appendSphere(const glm::mat4& m, float radius, const glm::vec3& color, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    constexpr int kRings = 10, kSegments = 16;
    const glm::mat3 nm = glm::mat3(m);
    const uint32_t base = static_cast<uint32_t>(v.size());
    for (int r = 0; r <= kRings; ++r) {
        const float phi = glm::pi<float>() * static_cast<float>(r) / kRings;
        for (int s = 0; s <= kSegments; ++s) {
            const float theta = glm::two_pi<float>() * static_cast<float>(s) / kSegments;
            const glm::vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            v.push_back({ glm::vec3(m * glm::vec4(n * radius, 1.0f)), color, glm::normalize(nm * n), glm::vec2(0.0f) });
        }
    }
    for (int r = 0; r < kRings; ++r)
        for (int s = 0; s < kSegments; ++s) {
            const uint32_t a = base + static_cast<uint32_t>(r * (kSegments + 1) + s), b = a + kSegments + 1;
            idx.insert(idx.end(), { a, a + 1, b, a + 1, b + 1, b });
        }
}

void appendCylinder(const glm::mat4& m, float radius, float halfHeight, const glm::vec3& color, std::vector<kke::Vertex>& v,
                    std::vector<uint32_t>& idx, bool bands) {
    constexpr int kSides = 16;
    const glm::mat3 nm = glm::mat3(m);
    // The side in rings (heights as a share of the height, bottom to top);
    // with bands, two thin dark hoops.
    static const float kPlain[] = { 0.0f, 1.0f };
    static const float kBanded[] = { 0.0f, 0.2f, 0.24f, 0.76f, 0.8f, 1.0f };
    const float* rings = bands ? kBanded : kPlain;
    const int ringCount = bands ? 6 : 2;
    for (int k = 0; k + 1 < ringCount; ++k) {
        const bool hoop = bands && (k == 1 || k == 3);
        const glm::vec3 c = color * (hoop ? 0.45f : 0.85f);
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (int s = 0; s <= kSides; ++s) {
            const float a = glm::two_pi<float>() * static_cast<float>(s) / kSides;
            const glm::vec3 n(std::cos(a), 0.0f, std::sin(a));
            for (int e = 0; e < 2; ++e) {
                const float y = -halfHeight + 2.0f * halfHeight * rings[k + e];
                v.push_back({ glm::vec3(m * glm::vec4(n * radius + glm::vec3(0, y, 0), 1.0f)), c, glm::normalize(nm * n), glm::vec2(0.0f) });
            }
        }
        for (int s = 0; s < kSides; ++s) {
            const uint32_t a = base + static_cast<uint32_t>(s * 2);
            idx.insert(idx.end(), { a, a + 1, a + 2, a + 1, a + 3, a + 2 });
        }
    }
    // The two caps.
    for (int cap = 0; cap < 2; ++cap) {
        const float y = cap ? halfHeight : -halfHeight;
        const glm::vec3 n(0.0f, cap ? 1.0f : -1.0f, 0.0f);
        const glm::vec3 c = color * (cap ? 1.0f : 0.6f);
        const uint32_t centre = static_cast<uint32_t>(v.size());
        v.push_back({ glm::vec3(m * glm::vec4(0, y, 0, 1)), c, glm::normalize(nm * n), glm::vec2(0.0f) });
        for (int s = 0; s <= kSides; ++s) {
            const float a = glm::two_pi<float>() * static_cast<float>(s) / kSides;
            v.push_back({ glm::vec3(m * glm::vec4(std::cos(a) * radius, y, std::sin(a) * radius, 1.0f)), c, glm::normalize(nm * n), glm::vec2(0.0f) });
        }
        for (int s = 0; s < kSides; ++s) {
            const uint32_t a = centre + 1 + static_cast<uint32_t>(s);
            if (cap) idx.insert(idx.end(), { centre, a + 1, a });
            else idx.insert(idx.end(), { centre, a, a + 1 });
        }
    }
}

std::vector<glm::vec3> cylinderPoints(float radius, float halfHeight, int sides) {
    std::vector<glm::vec3> p;
    for (int s = 0; s < sides; ++s) {
        const float a = glm::two_pi<float>() * static_cast<float>(s) / static_cast<float>(sides);
        p.emplace_back(std::cos(a) * radius, -halfHeight, std::sin(a) * radius);
        p.emplace_back(std::cos(a) * radius, halfHeight, std::sin(a) * radius);
    }
    return p;
}

} // namespace kke_showcase
