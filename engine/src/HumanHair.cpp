#include "kke/HumanHair.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace kke {

namespace {

glm::vec3 jointCentre(const HumanPack& pack, const std::vector<glm::vec3>& shaped, const std::string& joint) {
    auto it = pack.joints().find(joint);
    if (it == pack.joints().end() || it->second.empty()) return glm::vec3(0.0f);
    glm::vec3 sum(0.0f);
    for (uint32_t v : it->second) sum += shaped[v];
    return sum / static_cast<float>(it->second.size());
}

// The sphere x^2 + y^2 + z^2 = 2 c.x + 2 c.y + 2 c.z + d through the
// points, least squares (the normal equations of a 4x4 system).
bool fitSphere(const std::vector<glm::vec3>& points, glm::vec3& centre, float& radius) {
    if (points.size() < 4) return false;
    glm::dvec3 mean(0.0);
    for (const glm::vec3& p : points) mean += glm::dvec3(p);
    mean /= static_cast<double>(points.size());
    glm::dmat4 a(0.0);
    glm::dvec4 b(0.0);
    for (const glm::vec3& pf : points) {
        const glm::dvec3 p = glm::dvec3(pf) - mean; // centred: better conditioned
        const glm::dvec4 row(2.0 * p.x, 2.0 * p.y, 2.0 * p.z, 1.0);
        const double rhs = glm::dot(p, p);
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) a[j][i] += row[i] * row[j];
            b[i] += row[i] * rhs;
        }
    }
    if (std::abs(glm::determinant(a)) < 1e-30) return false;
    const glm::dvec4 x = glm::inverse(a) * b;
    const glm::dvec3 c(x.x, x.y, x.z);
    const double r2 = x.w + glm::dot(c, c);
    if (r2 <= 0.0) return false;
    centre = glm::vec3(c + mean);
    radius = static_cast<float>(std::sqrt(r2));
    return true;
}

} // namespace

HumanScalp findScalp(const HumanPack& pack, const std::vector<glm::vec3>& shaped) {
    HumanScalp scalp;
    auto head = pack.weights().find("head");
    const int body = pack.findGroup("body");
    if (head == pack.weights().end() || body < 0 || shaped.size() != pack.positions().size()) return scalp;
    std::set<uint32_t> skin;
    const HumanGroup& g = pack.groups()[static_cast<size_t>(body)];
    for (uint32_t f = g.firstFace; f < g.firstFace + g.faceCount; ++f)
        for (uint32_t v : pack.quads()[f].verts) skin.insert(v);

    // The head's own frame from its joints: up along the head bone, front
    // from between the ears (the head bone's start) to between the eyes.
    const glm::vec3 eyes = 0.5f * (jointCentre(pack, shaped, "eye.L____head") + jointCentre(pack, shaped, "eye.R____head"));
    const glm::vec3 base = jointCentre(pack, shaped, "head____head"), top = jointCentre(pack, shaped, "head____tail");
    if (glm::distance(base, top) < 1e-4f) return scalp;
    scalp.up = glm::normalize(top - base);
    glm::vec3 front = eyes - base;
    front -= scalp.up * glm::dot(front, scalp.up);
    scalp.front = glm::length(front) > 1e-5f ? glm::normalize(front) : glm::vec3(0, 0, 1);

    // Skin the head bone carries (not the neck), then by height over the
    // eyes: everywhere above the brows, at the back down to the ear tops,
    // and over the forehead only above the hairline. MakeHuman's head is
    // about 0.23 m tall; the numbers are fractions of the eyes-to-top
    // distance so they scale with the head.
    const float span = std::max(glm::dot(top - eyes, scalp.up), 0.02f);
    std::vector<glm::vec3> points;
    for (size_t i = 0; i < head->second.verts.size(); ++i) {
        const uint32_t v = head->second.verts[i];
        if (head->second.weights[i] < 0.9f || !skin.count(v)) continue;
        const glm::vec3 p = shaped[v] - eyes;
        const float above = glm::dot(p, scalp.up) / span;
        const float forward = glm::dot(p, scalp.front) / span;
        const bool forehead = forward > -0.25f;
        const bool grows = forehead ? above > 0.62f : above > 0.18f - 0.25f * std::clamp(-forward - 0.6f, 0.0f, 1.0f);
        if (!grows) continue;
        scalp.verts.push_back(v);
        points.push_back(shaped[v]);
    }
    if (!fitSphere(points, scalp.centre, scalp.radius)) {
        scalp.verts.clear();
        return scalp;
    }
    return scalp;
}

bool hairOnHuman(HairDesc& desc, const std::string& style, const HumanPack& pack, const std::vector<glm::vec3>& shaped, int guides) {
    const HumanScalp scalp = findScalp(pack, shaped);
    if (scalp.verts.empty()) return false;
    // The rest pose lies over a sphere: make it the one round the whole
    // scalp, so strands start out over the skin, not in it.
    float outer = 0.0f;
    for (uint32_t v : scalp.verts) outer = std::max(outer, glm::distance(shaped[v], scalp.centre));
    if (!hairstyleOnHead(desc, style, scalp.centre, outer, guides, scalp.up, scalp.front)) return false;
    // A real face stands out in front of that sphere: hair combed back
    // (the loose styles) starts back and up from the hairline, or it would
    // fall over the eyes.
    if (glm::dot(desc.comb, scalp.front) < 0.0f) {
        desc.comb = -scalp.front * 1.2f;
        for (glm::vec3& d : desc.directions)
            if (const float ahead = glm::dot(d, scalp.front); ahead > 0.0f)
                d = glm::normalize(d + (scalp.up * 0.8f - scalp.front * 0.9f) * ahead);
    }

    // Each root onto the skin straight under it: the ray from the centre
    // through it, against the faces hair grows on (all four corners on the
    // scalp). No face there: no scalp (the face, the ears), dropped.
    const std::set<uint32_t> on(scalp.verts.begin(), scalp.verts.end());
    std::vector<std::array<glm::vec3, 3>> tris;
    const HumanGroup& g = pack.groups()[static_cast<size_t>(pack.findGroup("body"))];
    for (uint32_t f = g.firstFace; f < g.firstFace + g.faceCount; ++f) {
        const auto& q = pack.quads()[f].verts;
        if (!on.count(q[0]) || !on.count(q[1]) || !on.count(q[2]) || !on.count(q[3])) continue;
        tris.push_back({ shaped[q[0]], shaped[q[1]], shaped[q[2]] });
        if (q[3] != q[2]) tris.push_back({ shaped[q[0]], shaped[q[2]], shaped[q[3]] });
    }
    // Distance from the centre to the skin along d (1e9: no scalp there).
    auto skinAlong = [&](const glm::vec3& d) {
        float best = 1e9f;
        for (const auto& t : tris) { // Moller-Trumbore, either side
            const glm::vec3 e1 = t[1] - t[0], e2 = t[2] - t[0], p = glm::cross(d, e2);
            const float det = glm::dot(e1, p);
            if (std::abs(det) < 1e-12f) continue;
            const glm::vec3 s0 = scalp.centre - t[0];
            const float u = glm::dot(s0, p) / det;
            if (u < 0.0f || u > 1.0f) continue;
            const glm::vec3 qv = glm::cross(s0, e1);
            const float v = glm::dot(d, qv) / det;
            if (v < 0.0f || u + v > 1.0f) continue;
            const float hit = glm::dot(e2, qv) / det;
            if (hit > 0.0f) best = std::min(best, hit);
        }
        return best;
    };
    // Braids and rows (roots tied to ties) are laid along the head's
    // sphere: make it the one fitted to the scalp, not the one round it,
    // or they float a finger's width over the head.
    const bool tied = !desc.ties.empty();
    if (tied) {
        desc.headCenter = scalp.centre;
        desc.headRadius = scalp.radius;
    }
    std::vector<size_t> keep;
    for (size_t r = 0; r < desc.roots.size(); ++r) {
        glm::vec3 d = glm::normalize(desc.roots[r] - scalp.centre);
        float best = skinAlong(d);
        // A row starting at the temple (in front of the hairline): start it
        // a little further back, where the scalp is, rather than lose it.
        for (int step = 0; tied && best > 1e8f && step < 12; ++step) {
            d = glm::normalize(d - scalp.front * 0.06f); // back along the row, not up: rows would bunch on the crown
            best = skinAlong(d);
        }
        if (best > 1e8f) continue;
        desc.roots[r] = scalp.centre + d * (best + 0.0015f); // just over the skin
        keep.push_back(r);
    }
    for (HairDesc::Tie& t : desc.ties) // the ends onto the skin too
        if (const glm::vec3 d = glm::normalize(t.at - scalp.centre); skinAlong(d) < 1e8f) t.at = scalp.centre + d * skinAlong(d);
    if (keep.empty()) return false;
    auto pick = [&](auto& list) {
        if (list.size() != desc.roots.size()) return;
        std::remove_reference_t<decltype(list)> out;
        out.reserve(keep.size());
        for (size_t r : keep) out.push_back(list[r]);
        list = std::move(out);
    };
    pick(desc.directions);
    pick(desc.lengths);
    pick(desc.tiedTo);
    pick(desc.roots);
    return true;
}

ModelMesh hairLocks(const std::vector<glm::vec3>& guides, size_t perGuide, const glm::vec3& headCentre, float width) {
    ModelMesh mesh;
    mesh.name = "hair-locks";
    if (perGuide < 2) return mesh;
    const size_t count = guides.size() / perGuide;
    constexpr uint32_t kSides = 4;
    mesh.vertices.reserve(count * perGuide * kSides);
    for (size_t g = 0; g < count; ++g) {
        const glm::vec3* p = &guides[g * perGuide];
        glm::vec3 out = p[0] - headCentre;
        out = glm::length(out) > 1e-6f ? glm::normalize(out) : glm::vec3(0.0f, 1.0f, 0.0f);
        const auto base = static_cast<uint32_t>(mesh.vertices.size());
        for (size_t i = 0; i < perGuide; ++i) {
            const glm::vec3 along = i + 1 < perGuide ? p[i + 1] - p[i] : p[i] - p[i - 1];
            const glm::vec3 t = glm::length(along) > 1e-7f ? glm::normalize(along) : glm::vec3(0.0f, -1.0f, 0.0f);
            // The broad side faces away from the head, wherever the lock hangs.
            glm::vec3 o = out - t * glm::dot(out, t);
            o = glm::length(o) > 1e-4f ? glm::normalize(o) : glm::normalize(glm::cross(t, std::abs(t.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
            const glm::vec3 s = glm::cross(t, o);
            const float f = static_cast<float>(i) / static_cast<float>(perGuide - 1);
            const float w = 0.5f * width * (1.0f - 0.95f * std::pow(f, 1.6f)); // half width, to a point
            const float d = w * 0.35f;
            const glm::vec3 c = p[i] - (i == 0 ? out * 0.004f : glm::vec3(0.0f)); // the root a little into the scalp
            const glm::vec3 corner[kSides] = { s * w, o * d, -s * w, -o * d * 0.4f };
            for (const glm::vec3& k : corner) {
                ModelVertex v;
                v.position = c + k;
                v.normal = glm::normalize(k / glm::vec3(std::max(w, 1e-6f)) + o * 0.3f); // rounded, leaning out
                v.uv = glm::vec2(0.5f, f);
                mesh.vertices.push_back(v);
            }
        }
        for (uint32_t i = 0; i + 1 < perGuide; ++i)
            for (uint32_t k = 0; k < kSides; ++k) {
                const uint32_t a = base + i * kSides + k, b = base + i * kSides + (k + 1) % kSides;
                const uint32_t c = a + kSides, d = b + kSides;
                // Seen from outside the lock: a, b down the ring, c, d the next ring.
                mesh.indices.insert(mesh.indices.end(), { a, c, b, b, c, d });
            }
    }
    return mesh;
}

float lockWidth(const std::vector<glm::vec3>& roots) {
    if (roots.size() < 2) return 0.03f;
    float sum = 0.0f;
    for (size_t i = 0; i < roots.size(); ++i) {
        float best = 1e9f;
        for (size_t j = 0; j < roots.size(); ++j)
            if (j != i) best = std::min(best, glm::length(roots[i] - roots[j]));
        sum += best;
    }
    return std::clamp(2.6f * sum / static_cast<float>(roots.size()), 0.008f, 0.07f);
}

} // namespace kke
