#include "kke/HumanEyes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace kke {

namespace {

constexpr float kPi = 3.14159265f;
constexpr int kSides = 32;

// Angles from the eye's front, in degrees. MakeHuman's eyeball is about
// 31 mm across: an iris 12 mm across on it is 23 degrees, the pupil 3 to
// 4 mm. Larger, the iris filled the whole opening between the lids.
constexpr float kPupil = 7.0f, kIris = 19.0f, kLimbus = 23.0f;

struct Eye {
    glm::vec3 centre{ 0.0f };
    float radius = 0.0f;
    ModelVertex front; // the ball's vertex nearest the front: its skin weights
};

Eye eyeOf(const ModelMesh& mesh) {
    Eye e;
    for (const ModelVertex& v : mesh.vertices) e.centre += v.position;
    e.centre /= static_cast<float>(mesh.vertices.size());
    float best = -2.0f;
    for (const ModelVertex& v : mesh.vertices) {
        const glm::vec3 d = v.position - e.centre;
        e.radius = std::max(e.radius, glm::length(d));
        if (const float ahead = d.z / std::max(glm::length(d), 1e-6f); ahead > best) {
            best = ahead;
            e.front = v;
        }
    }
    return e;
}

// A point on the dome `lift` over the ball, `angle` degrees from the front
// (+Z: the eyes look along the body's front), at `side` of kSides round.
ModelVertex onEye(const Eye& e, float angle, int side, float lift) {
    const float a = angle * kPi / 180.0f, phi = 2.0f * kPi * static_cast<float>(side) / kSides;
    const glm::vec3 dir(std::sin(a) * std::cos(phi), std::sin(a) * std::sin(phi), std::cos(a));
    ModelVertex v = e.front; // its skin weights
    v.position = e.centre + dir * (e.radius * lift);
    v.normal = dir;
    v.uv = glm::vec2(0.5f);
    return v;
}

// A disc (inner == 0: a fan round the front) or a ring between two angles.
void ring(ModelMesh& mesh, const Eye& e, float inner, float outer, float lift) {
    const auto base = static_cast<uint32_t>(mesh.vertices.size());
    if (inner <= 0.0f) {
        mesh.vertices.push_back(onEye(e, 0.0f, 0, lift));
        for (int s = 0; s < kSides; ++s) mesh.vertices.push_back(onEye(e, outer, s, lift));
        for (uint32_t s = 0; s < kSides; ++s) mesh.indices.insert(mesh.indices.end(), { base, base + 1 + s, base + 1 + (s + 1) % kSides });
        return;
    }
    for (int s = 0; s < kSides; ++s) {
        mesh.vertices.push_back(onEye(e, inner, s, lift));
        mesh.vertices.push_back(onEye(e, outer, s, lift));
    }
    for (uint32_t s = 0; s < kSides; ++s) {
        const uint32_t a = base + 2 * s, b = base + 2 * ((s + 1) % kSides);
        mesh.indices.insert(mesh.indices.end(), { a, a + 1, b + 1, a, b + 1, b });
    }
}

} // namespace

bool addIrises(ModelData& model, const glm::vec3& colour, bool update) {
    std::vector<Eye> eyes;
    bool skinned = false;
    for (const ModelMesh& m : model.meshes)
        if ((m.name == "helper-l-eye" || m.name == "helper-r-eye") && !m.vertices.empty()) {
            eyes.push_back(eyeOf(m));
            skinned = m.skinned;
        }
    if (eyes.empty()) return false;

    // Over the ball (its flat faces lie inside the sphere through its
    // vertices), each part a little further out than the one under it.
    struct Part {
        const char* name;
        float inner, outer, lift, shade, roughness;
    };
    const Part parts[] = { { "eye-pupil", 0.0f, kPupil, 1.016f, 0.03f, 0.1f },
                           { "eye-iris", kPupil - 1.0f, kIris, 1.012f, 1.0f, 0.3f },
                           { "eye-limbus", kIris - 1.0f, kLimbus, 1.008f, 0.45f, 0.3f } };
    for (const Part& p : parts) {
        ModelMesh mesh;
        mesh.name = p.name;
        mesh.skinned = skinned;
        for (const Eye& e : eyes) ring(mesh, e, p.inner, p.outer, p.lift);
        if (update) {
            for (ModelMesh& m : model.meshes)
                if (m.name == p.name && m.vertices.size() == mesh.vertices.size()) m.vertices = std::move(mesh.vertices);
            continue;
        }
        ModelMaterial mat;
        mat.name = std::string("Human_") + p.name;
        mat.baseColor = colour * p.shade;
        mat.roughness = p.roughness;
        model.materials.push_back(mat);
        mesh.material = static_cast<uint32_t>(model.materials.size() - 1);
        model.meshes.push_back(std::move(mesh));
    }
    return true;
}

} // namespace kke
