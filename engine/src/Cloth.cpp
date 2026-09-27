#include "kke/Cloth.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace kke {

namespace {

// Behaviour first (what it weighs, how it stretches, bends and meets the
// air), then the look. Stiffness values are dimensionless softness (see
// Fabric): 0 = as stiff as the solver can make it.
Fabric make(const char* name, float density, float stretch, float shear, float bend, float damping, float friction,
            float thickness, float maxStretch, glm::vec3 color, float roughness, float sheen, int weave, float weaveScale,
            float fuzz, float specular) {
    Fabric f;
    f.name = name;
    f.density = density;
    f.stretch = stretch;
    f.shear = shear;
    f.bend = bend;
    f.damping = damping;
    f.friction = friction;
    f.thickness = thickness;
    f.maxStretch = maxStretch;
    f.color = color;
    f.roughness = roughness;
    f.sheenColor = glm::mix(color, glm::vec3(1.0f), 0.6f);
    f.sheen = sheen;
    f.weave = weave;
    f.weaveScale = weaveScale;
    f.fuzz = fuzz;
    f.specular = specular;
    return f;
}

} // namespace

Fabric clothFabric(const std::string& name) {
    //            name       kg/m2  stretch shear  bend   damp  fric  thick   tether colour                       rough sheen weave scale fuzz spec
    if (name == "silk")    return make("silk",    0.06f, 0.0f,  0.5f,  400.0f, 0.03f, 0.25f, 0.004f, 1.03f, {0.72f, 0.08f, 0.14f}, 0.32f, 0.25f, 2, 140.0f, 0.0f, 0.9f);
    if (name == "satin")   return make("satin",   0.09f, 0.0f,  0.4f,  200.0f, 0.04f, 0.25f, 0.004f, 1.03f, {0.9f, 0.84f, 0.62f},  0.28f, 0.2f,  2, 120.0f, 0.0f, 1.0f);
    if (name == "linen")   return make("linen",   0.2f,  0.0f,  0.1f,  15.0f,  0.1f,  0.55f, 0.006f, 1.03f, {0.83f, 0.77f, 0.64f}, 0.9f,  0.25f, 0, 70.0f,  0.1f, 0.2f);
    if (name == "denim")   return make("denim",   0.45f, 0.0f,  0.05f, 2.0f,   0.15f, 0.65f, 0.008f, 1.02f, {0.16f, 0.25f, 0.45f}, 0.9f,  0.15f, 1, 90.0f,  0.1f, 0.15f);
    if (name == "wool")    return make("wool",    0.35f, 0.08f, 0.6f,  12.0f,  0.25f, 0.85f, 0.016f, 1.12f, {0.64f, 0.52f, 0.44f}, 0.95f, 0.8f,  3, 45.0f,  0.85f, 0.05f);
    if (name == "fleece")  return make("fleece",  0.25f, 0.05f, 0.5f,  20.0f,  0.25f, 0.8f,  0.013f, 1.08f, {0.3f, 0.52f, 0.36f},  0.95f, 0.9f,  0, 60.0f,  1.0f, 0.05f);
    if (name == "leather") return make("leather", 0.9f,  0.0f,  0.02f, 0.6f,   0.2f,  0.7f,  0.006f, 1.01f, {0.36f, 0.2f, 0.11f},  0.55f, 0.05f, 5, 30.0f,  0.0f, 0.4f);
    if (name == "canvas")  return make("canvas",  0.35f, 0.0f,  0.05f, 3.0f,   0.15f, 0.7f,  0.007f, 1.02f, {0.72f, 0.66f, 0.5f},  0.95f, 0.1f,  0, 40.0f,  0.05f, 0.1f);
    if (name == "net")     return make("net",     0.03f, 0.02f, 8.0f,  60.0f,  0.08f, 0.5f,  0.005f, 1.05f, {0.92f, 0.92f, 0.9f},  0.8f,  0.1f,  4, 1.0f,   0.0f, 0.2f);
    if (name == "rubber")  return make("rubber",  1.0f,  2.0f,  2.0f,  5.0f,   0.05f, 0.95f, 0.006f, 1.6f,  {0.12f, 0.12f, 0.13f}, 0.6f,  0.0f,  5, 30.0f,  0.0f, 0.5f);
    return make("cotton", 0.15f, 0.0f, 0.2f, 25.0f, 0.1f, 0.5f, 0.006f, 1.03f, {0.84f, 0.85f, 0.88f}, 0.85f, 0.3f, 0, 80.0f, 0.15f, 0.2f);
}

std::vector<std::string> clothFabricNames() {
    return { "silk", "satin", "cotton", "linen", "denim", "wool", "fleece", "leather", "canvas", "net", "rubber" };
}

ClothMesh clothGrid(const glm::vec3& center, float width, float height, int columns, int rows, const glm::vec3& right, const glm::vec3& down) {
    ClothMesh m;
    columns = std::max(columns, 2);
    rows = std::max(rows, 2);
    m.columns = columns;
    m.rows = rows;
    const glm::vec3 r = glm::normalize(right), d = glm::normalize(down);
    const glm::vec3 origin = center - r * (width * 0.5f) - d * (height * 0.5f);
    m.positions.reserve(static_cast<size_t>(columns * rows));
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < columns; ++x) {
            const float u = width * float(x) / float(columns - 1), v = height * float(y) / float(rows - 1);
            m.positions.push_back(origin + r * u + d * v);
            m.uvs.emplace_back(u, v); // metres: the weave is threads per metre
        }
    for (int y = 0; y + 1 < rows; ++y)
        for (int x = 0; x + 1 < columns; ++x) {
            const uint32_t a = static_cast<uint32_t>(y * columns + x), b = a + 1, c = a + static_cast<uint32_t>(columns), e = c + 1;
            // Alternate the diagonal so the cloth has no preferred fold direction.
            if ((x + y) % 2 == 0) m.indices.insert(m.indices.end(), { a, c, b, b, c, e });
            else m.indices.insert(m.indices.end(), { a, c, e, a, e, b });
        }
    return m;
}

ClothMesh clothNet(const glm::vec3& center, float width, float height, int columns, int rows, const glm::vec3& right, const glm::vec3& down) {
    ClothMesh m = clothGrid(center, width, height, columns, rows, right, down);
    // Threads along every row and column; the triangles stay (Jolt builds
    // the bend and shear constraints from them) but aren't drawn.
    for (int y = 0; y < m.rows; ++y)
        for (int x = 0; x < m.columns; ++x) {
            const uint32_t a = clothGridIndex(m, x, y);
            if (x + 1 < m.columns) m.lines.insert(m.lines.end(), { a, a + 1 });
            if (y + 1 < m.rows) m.lines.insert(m.lines.end(), { a, a + static_cast<uint32_t>(m.columns) });
        }
    return m;
}

void clothNormals(const std::vector<glm::vec3>& p, const std::vector<uint32_t>& idx, std::vector<glm::vec3>& n) {
    n.assign(p.size(), glm::vec3(0.0f));
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        const uint32_t a = idx[i], b = idx[i + 1], c = idx[i + 2];
        const glm::vec3 f = glm::cross(p[b] - p[a], p[c] - p[a]); // area weighted
        n[a] += f;
        n[b] += f;
        n[c] += f;
    }
    for (glm::vec3& v : n) {
        const float len = glm::length(v);
        v = len > 1e-12f ? v / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

} // namespace kke
