#include "kke/HumanEyes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>
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

// A tapered strip from `root` along `dir` (unit, on the skin), lying in
// the plane whose normal is `n`; `bend` curls the tip towards `n`.
void strand(ModelMesh& mesh, const ModelVertex& under, const glm::vec3& root, const glm::vec3& dir, const glm::vec3& n,
            float length, float width, float bend) {
    constexpr int kSegs = 3;
    const glm::vec3 side = glm::normalize(glm::cross(n, dir));
    const auto base = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i <= kSegs; ++i) {
        const float t = static_cast<float>(i) / kSegs;
        const glm::vec3 c = root + dir * (length * t) + n * (bend * length * t * t);
        const float w = width * (1.0f - 0.8f * t) * 0.5f;
        for (float sgn : { -1.0f, 1.0f }) {
            ModelVertex v = under; // the skin weights
            v.position = c + side * (w * sgn);
            v.normal = n;
            v.uv = glm::vec2(0.5f);
            mesh.vertices.push_back(v);
        }
    }
    for (uint32_t i = 0; i < kSegs; ++i) {
        const uint32_t a = base + 2 * i;
        // side x dir == n: (a, a+2, a+1) winds anticlockwise seen from n.
        mesh.indices.insert(mesh.indices.end(), { a, a + 2, a + 1, a + 1, a + 2, a + 3 });
    }
}

// The skin vertex under (x, y) seen from the front: the nearest across,
// the front-most of near ones. Always one, so the hair count never
// changes with the face (update relies on it).
const ModelVertex* skinAt(const ModelMesh& skin, float x, float y, float minZ) {
    const ModelVertex* best = nullptr;
    float bestScore = 1e9f;
    for (const ModelVertex& v : skin.vertices) {
        if (v.position.z < minZ || v.normal.z < 0.2f) continue;
        const float d = std::hypot(v.position.x - x, v.position.y - y);
        const float score = d - 0.2f * v.position.z; // prefer the front
        if (score < bestScore) {
            bestScore = score;
            best = &v;
        }
    }
    return best;
}

} // namespace

bool addBrows(ModelData& model, const ModelMesh& skin, const glm::vec3& colour, bool update) {
    std::vector<Eye> eyes;
    bool skinned = false;
    for (const ModelMesh& m : model.meshes)
        if ((m.name == "helper-l-eye" || m.name == "helper-r-eye") && !m.vertices.empty()) {
            eyes.push_back(eyeOf(m));
            skinned = m.skinned;
        }
    if (eyes.empty() || skin.vertices.empty()) return false;

    ModelMesh brows, lashes;
    brows.name = "brows";
    lashes.name = "lashes";
    brows.skinned = lashes.skinned = skinned;
    std::mt19937 rng(20261025u);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (const Eye& e : eyes) {
        const float out = e.centre.x < 0.0f ? -1.0f : 1.0f; // away from the nose
        const float r = e.radius;

        // The brow: an arch over the eye, from just past its inner corner
        // to past its outer one, peaking two thirds of the way out.
        for (int i = 0; i < 260; ++i) {
            const float t = u(rng);
            const float thick = 0.0065f * (1.0f - 0.6f * t);
            const float x = e.centre.x + out * r * (-0.95f + 2.25f * t);
            const float arch = std::sin(kPi * std::min(1.0f, t / 0.66f) * 0.5f) - (t > 0.66f ? 1.4f * (t - 0.66f) : 0.0f);
            const float y = e.centre.y + r * (1.15f + 0.35f * arch) + (u(rng) - 0.5f) * thick;
            const ModelVertex* under = skinAt(skin, x, y, e.centre.z);
            if (!under) continue;
            const glm::vec3 n = glm::normalize(under->normal);
            // Inner hairs grow upwards, the rest out along the arch and a little down.
            glm::vec3 grow = glm::mix(glm::vec3(out * 0.6f, 1.0f, 0.0f), glm::vec3(out, -0.12f, 0.0f), std::min(1.0f, t * 3.0f));
            grow += glm::vec3(u(rng) - 0.5f, u(rng) - 0.5f, 0.0f) * 0.4f;
            grow = glm::normalize(grow - n * glm::dot(grow, n));
            // On the skin's plane at (x, y), not on the vertex: many hairs share one.
            const glm::vec3 at(x, y, under->position.z - (n.x * (x - under->position.x) + n.y * (y - under->position.y)) / n.z);
            const glm::vec3 root = at + n * 0.0006f;
            strand(brows, *under, root, grow, n, 0.0045f + 0.003f * u(rng), 0.0009f, 0.08f);
        }

        // The lashes: the upper lid's edge is where the skin meets the
        // eyeball's top half; each lash grows out from it and curls up.
        std::vector<const ModelVertex*> rim;
        for (const ModelVertex& v : skin.vertices) {
            const glm::vec3 d = v.position - e.centre;
            const float dist = glm::length(d);
            if (dist > r * 1.12f || dist < r * 0.9f || d.z < r * 0.45f || d.y < 0.0f) continue;
            rim.push_back(&v);
        }
        if (rim.size() < 2) continue;
        std::sort(rim.begin(), rim.end(), [](const ModelVertex* a, const ModelVertex* b) { return a->position.x < b->position.x; });
        for (int i = 0; i < 60; ++i) {
            // Evenly along the lid from corner to corner, between rim vertices.
            const float at = (static_cast<float>(i) + 0.5f) / 60.0f * static_cast<float>(rim.size() - 1);
            const auto k = std::min(static_cast<size_t>(at), rim.size() - 2);
            const ModelVertex* under = rim[k];
            const glm::vec3 p = glm::mix(rim[k]->position, rim[k + 1]->position, at - static_cast<float>(k));
            const glm::vec3 d = glm::normalize(p - e.centre);
            const glm::vec3 grow = glm::normalize(d * 0.8f + glm::vec3(0.0f, 0.35f, 0.0f));
            // Facing the front (a card seen edge on is gone), curling up and back.
            const glm::vec3 n = glm::normalize(glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), grow));
            strand(lashes, *under, p + d * 0.0004f, grow, n, 0.0045f + 0.002f * u(rng), 0.0005f, -0.35f);
        }
    }
    if (brows.vertices.empty()) return false;

    struct Part {
        ModelMesh* mesh;
        float shade, roughness;
    };
    for (const Part& p : { Part{ &brows, 1.0f, 0.6f }, Part{ &lashes, 0.35f, 0.5f } }) {
        if (update) {
            for (ModelMesh& m : model.meshes)
                if (m.name == p.mesh->name && m.vertices.size() == p.mesh->vertices.size()) m.vertices = std::move(p.mesh->vertices);
            continue;
        }
        ModelMaterial mat;
        mat.name = "Human_" + p.mesh->name;
        mat.baseColor = colour * p.shade;
        mat.roughness = p.roughness;
        model.materials.push_back(mat);
        p.mesh->material = static_cast<uint32_t>(model.materials.size() - 1);
        model.meshes.push_back(std::move(*p.mesh));
    }
    return true;
}

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
