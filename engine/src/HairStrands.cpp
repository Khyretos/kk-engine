#include "kke/HairStrands.h"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace kke {

namespace {

// Deterministic 0..1 from two integers (the same hairs every run).
float rand01(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return float(h & 0xFFFFFFu) / float(0x1000000);
}

glm::vec3 perpendicular(const glm::vec3& d) {
    const glm::vec3 a = std::fabs(d.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    return glm::normalize(glm::cross(d, a));
}

} // namespace

void HairStrands::build(const HairDesc& desc) {
    m_style = desc.style;
    m_invBind = glm::inverse(desc.bindPose);
    m_headCenter = desc.headCenter;
    m_headRadius = desc.headRadius;
    m_strand = hairStrandVertices(desc.style);
    m_points = 2 * (m_strand - 2) + 1; // the follicle to the tip, two per guide segment (a smooth curve through the guide)
    m_hairs.clear();
    m_restSegment.clear();
    m_rootAcross.clear();
    m_indexHairs = 0;
    const size_t guides = desc.roots.size();
    if (guides == 0) return;
    const glm::mat3 toHead(m_invBind);
    // The guides at rest: how long each segment is (the coils unwind as
    // it stretches) and which way is across each at the root.
    const std::vector<glm::vec3> rest = hairRestPose(desc);
    const size_t strand = size_t(m_strand);
    std::vector<float> restLength(guides, 0.0f);
    m_restSegment.resize(guides * (strand - 2));
    for (size_t g = 0; g < guides; ++g) {
        const glm::vec3* q = rest.data() + g * strand + 1; // from the follicle
        for (size_t k = 0; k + 2 < strand; ++k) {
            const float len = std::max(glm::distance(q[k + 1], q[k]), 1e-6f);
            m_restSegment[g * (strand - 2) + k] = len;
            restLength[g] += len;
        }
        m_rootAcross.push_back(glm::normalize(toHead * perpendicular(glm::normalize(q[1] - q[0] + glm::vec3(0.0f, 1e-7f, 0.0f)))));
    }
    // Each guide's nearest neighbour sets how far its hairs spread.
    std::vector<uint32_t> nearest(guides, 0);
    std::vector<float> spacing(guides, 0.02f);
    for (size_t g = 0; g < guides; ++g) {
        float best = 1e30f;
        for (size_t o = 0; o < guides; ++o) {
            if (o == g) continue;
            const glm::vec3 d = desc.roots[o] - desc.roots[g];
            const float l2 = glm::dot(d, d);
            if (l2 < best) {
                best = l2;
                nearest[g] = uint32_t(o);
            }
        }
        if (best < 1e29f) spacing[g] = std::sqrt(best);
    }
    const int per = std::max(1, desc.style.hairsPerGuide);
    m_hairs.reserve(guides * size_t(per));
    for (size_t g = 0; g < guides; ++g) {
        const glm::vec3 dir = g < desc.directions.size() ? glm::normalize(desc.directions[g]) : glm::vec3(0, 1, 0);
        const glm::vec3 a = perpendicular(dir), b = glm::cross(dir, a);
        for (int h = 0; h < per; ++h) {
            Hair hair;
            hair.guide = uint32_t(g);
            // Even spread over a disc around the root (sunflower pattern),
            // flat on the scalp.
            const float r = 0.5f * desc.style.spread * spacing[g] * std::sqrt((float(h) + 0.5f) / float(per));
            const float ang = float(h) * 2.39996f + rand01(uint32_t(g), 7u) * glm::two_pi<float>();
            glm::vec3 off = (a * std::cos(ang) + b * std::sin(ang)) * r;
            // Hairs lean towards the neighbouring guide on their side, so
            // the gaps between clumps fill as the guides move apart.
            const uint32_t o = nearest[g];
            const glm::vec3 toO = desc.roots[o] - desc.roots[g];
            const float along = glm::dot(off, toO) / std::max(glm::dot(toO, toO), 1e-10f);
            hair.other = o;
            hair.blend = std::clamp(along, 0.0f, 0.5f);
            off -= toO * hair.blend;
            hair.offset = toHead * off;
            hair.length = 1.0f - 0.2f * rand01(uint32_t(g), uint32_t(h) * 3u + 1u);
            hair.color = glm::vec3(0.85f + 0.3f * rand01(uint32_t(g), uint32_t(h) * 3u + 2u));
            hair.phase = rand01(uint32_t(h), uint32_t(g) * 5u + 3u) * glm::two_pi<float>();
            if (desc.style.coil > 0.0f && desc.style.coilRadius > 0.0f) {
                // Defined: a clump's hairs coil together (a ringlet);
                // undefined: each on its own, sizes varying.
                const float loose = 1.0f - std::clamp(desc.style.definition, 0.0f, 1.0f);
                hair.coilTurns = desc.style.coil * hair.length * restLength[g];
                hair.coilPhase = (rand01(uint32_t(g), 11u) + loose * rand01(uint32_t(h), uint32_t(g) * 7u + 5u)) * glm::two_pi<float>();
                hair.coilScale = 1.0f + loose * 0.5f * (rand01(uint32_t(g) * 3u + 1u, uint32_t(h)) - 0.5f);
                // Enough points for every turn to stay round (up to a limit).
                constexpr int kMostPoints = 97;
                m_points = std::max(m_points, std::min(kMostPoints, int(std::ceil(hair.coilTurns * 8.0f)) + 1));
            }
            m_hairs.push_back(hair);
        }
    }
}

glm::vec4 HairStrands::headSphere(const glm::mat4& head) const {
    if (m_headRadius <= 0.0f) return glm::vec4(0.0f);
    return glm::vec4(glm::vec3(head * m_invBind * glm::vec4(m_headCenter, 1.0f)), m_headRadius + 0.5f * m_style.hairWidth);
}

void HairStrands::frames(const std::vector<glm::vec3>& guides, const glm::mat4& head, std::vector<glm::vec4>& out) const {
    out.resize(guides.size());
    const size_t strand = size_t(std::max(m_strand, 3));
    const size_t segs = strand - 2;
    const size_t count = std::min(guides.size() / strand, m_rootAcross.size());
    const glm::mat3 rot(head);
    for (size_t g = 0; g < count; ++g) {
        const glm::vec3* p = guides.data() + g * strand + 1; // from the follicle
        glm::vec4* o = out.data() + g * strand + 1;
        glm::vec3 n = rot * m_rootAcross[g];
        for (size_t k = 0; k <= segs; ++k) {
            glm::vec3 t = p[std::min(k + 1, segs)] - p[k > 0 ? k - 1 : 0];
            const float tl = glm::length(t);
            if (tl > 1e-9f) {
                t /= tl;
                const glm::vec3 m = n - t * glm::dot(n, t);
                if (glm::dot(m, m) > 1e-12f) n = glm::normalize(m);
            }
            const size_t seg = std::min(k, segs - 1);
            const float stretch = glm::distance(p[seg + 1], p[seg]) / m_restSegment[g * segs + seg];
            o[k] = glm::vec4(n, stretch);
        }
        o[-1] = o[0]; // the root, as the follicle
    }
    for (size_t i = count * strand; i < out.size(); ++i) out[i] = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
}

glm::vec3 HairStrands::coilOffset(const HairStyle& style, const Hair& hair, float s, const glm::vec3& t, const glm::vec4& frame, glm::vec3* change) {
    if (change) *change = glm::vec3(0.0f);
    if (hair.coilTurns <= 0.0f) return glm::vec3(0.0f);
    // Stretched k times its rest length, a coil unwinds: the hair's own
    // length stays the same, so its radius shrinks, to nothing when it is
    // pulled straight (1 / (1 - shrinkage) x).
    const float pulled = 1.0f / (1.0f - std::clamp(style.shrinkage, 0.0f, 0.9f));
    const float k = std::max(frame.w, 1e-3f);
    const float unwind = pulled > 1.0001f ? k * std::sqrt(std::max((pulled / k) * (pulled / k) - 1.0f, 0.0f)) / std::sqrt(pulled * pulled - 1.0f) : 1.0f;
    const float grow = std::clamp(s * hair.coilTurns * 2.0f, 0.0f, 1.0f); // the first half turn grows in from the root
    const float r = style.coilRadius * hair.coilScale * unwind * grow;
    glm::vec3 n = glm::vec3(frame) - t * glm::dot(glm::vec3(frame), t);
    n = glm::dot(n, n) > 1e-12f ? glm::normalize(n) : perpendicular(t);
    const glm::vec3 b = glm::cross(t, n);
    const float da = glm::two_pi<float>() * hair.coilTurns;
    const float a = da * s + hair.coilPhase;
    const float ca = std::cos(a), sa = std::sin(a);
    // Round: a spiral. Zig-zag: a triangle wave whose plane turns a quarter
    // of the way round each turn, so the bends go every way (4B).
    const glm::vec3 spiral = n * ca + b * sa;
    const float th = 0.25f * a;
    const glm::vec3 plane = n * std::cos(th) + b * std::sin(th), planeTurn = b * std::cos(th) - n * std::sin(th);
    const float tri = std::asin(std::clamp(sa, -1.0f, 1.0f)) * (2.0f / glm::pi<float>());
    const float z = std::clamp(style.zigzag, 0.0f, 1.0f);
    if (change) {
        const glm::vec3 spiralD = (b * ca - n * sa) * da;
        const glm::vec3 zigD = plane * ((ca >= 0.0f ? 1.0f : -1.0f) * (2.0f / glm::pi<float>()) * da) + planeTurn * (0.25f * da * tri);
        *change = r * glm::mix(spiralD, zigD, z);
    }
    return r * glm::mix(spiral, plane * tri, z);
}

void HairStrands::ribbons(const std::vector<glm::vec3>& guides, const glm::mat4& head, const glm::vec3& camera, float pixel,
                          std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    const size_t pts = size_t(std::max(m_points, 2));
    const size_t strands = m_strand > 0 ? guides.size() / size_t(m_strand) : 0;
    const int segs = m_strand - 2;
    if (m_hairs.empty() || strands == 0) {
        vertices.clear();
        indices.clear();
        return;
    }
    // Indices depend only on the counts: made once.
    if (m_indexHairs != m_hairs.size() || indices.size() != m_hairs.size() * (pts - 1) * 6) {
        indices.clear();
        indices.reserve(m_hairs.size() * (pts - 1) * 6);
        for (uint32_t h = 0; h < m_hairs.size(); ++h) {
            const uint32_t base = h * uint32_t(pts) * 2;
            for (uint32_t i = 0; i + 1 < pts; ++i) {
                const uint32_t v = base + i * 2;
                indices.insert(indices.end(), { v, v + 1, v + 2, v + 1, v + 3, v + 2 });
            }
        }
        m_indexHairs = uint32_t(m_hairs.size());
    }
    vertices.resize(m_hairs.size() * pts * 2);
    const glm::mat3 rot(head);
    const glm::vec4 sphere = headSphere(head);
    const glm::vec3 root = m_style.rootColor, tip = m_style.tipColor; // sRGB
    m_line.resize(pts);
    Vertex* out = vertices.data();
    for (const Hair& hair : m_hairs) {
        if (hair.guide >= strands || hair.other >= strands) {
            for (size_t i = 0; i < pts * 2; ++i) *out++ = Vertex{};
            continue;
        }
        const glm::vec3* g = guides.data() + size_t(hair.guide) * size_t(m_strand) + 1; // from the follicle
        const glm::vec3* o = guides.data() + size_t(hair.other) * size_t(m_strand) + 1;
        const glm::vec3 off = rot * hair.offset;
        // A guide at f segments from its follicle, on a Catmull-Rom curve
        // through its points (as shaders/hair_common.glsl draws it).
        auto curve = [segs](const glm::vec3* q, float f) {
            const int k = std::clamp(int(f), 0, segs - 1);
            const float t = std::clamp(f - float(k), 0.0f, 1.0f);
            const glm::vec3 p0 = q[std::max(k - 1, 0)], p1 = q[k], p2 = q[k + 1], p3 = q[std::min(k + 2, segs)];
            const float t2 = t * t, t3 = t2 * t;
            return 0.5f * ((2.0f * p1) + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (3.0f * p1 - p0 - 3.0f * p2 + p3) * t3);
        };
        // The centre line: the guide (leaning to its neighbour), sampled
        // over this hair's length, the offset shrinking as tips clump.
        for (size_t i = 0; i < pts; ++i) {
            const float s = float(i) / float(pts - 1);
            const float f = s * hair.length * float(segs);
            const glm::vec3 pg = curve(g, f), po = curve(o, f);
            glm::vec3 p = glm::mix(pg, po, hair.blend) + off * (1.0f - m_style.clump * s);
            if (m_style.frizz > 0.0f) {
                const float w = hair.phase + float(i) * 2.1f;
                p += m_style.frizz * s * glm::vec3(std::sin(w), std::cos(w * 1.3f), std::sin(w * 0.7f + 1.0f));
            }
            m_line[i] = p;
        }
        // Coils around the centre line.
        if (hair.coilTurns > 0.0f) {
            if (&hair == &m_hairs.front() || m_frames.size() != guides.size()) frames(guides, head, m_frames);
            const glm::vec4* fr = m_frames.data() + size_t(hair.guide) * size_t(m_strand) + 1;
            glm::vec3 prev = m_line[0];
            for (size_t i = 0; i < pts; ++i) {
                const float s = float(i) / float(pts - 1);
                const float f = s * hair.length * float(segs);
                const int k = std::clamp(int(f), 0, segs - 1);
                const glm::vec4 frame = glm::mix(fr[k], fr[k + 1], std::clamp(f - float(k), 0.0f, 1.0f));
                glm::vec3 t = m_line[std::min(i + 1, pts - 1)] - (i > 0 ? prev : m_line[0]);
                t = glm::length(t) > 1e-9f ? glm::normalize(t) : glm::vec3(0, -1, 0);
                prev = m_line[i];
                m_line[i] += coilOffset(m_style, hair, s, t, frame);
            }
        }
        // Out of the head.
        if (sphere.w > 0.0f) {
            for (glm::vec3& p : m_line) {
                const glm::vec3 d = p - glm::vec3(sphere);
                const float l = glm::length(d);
                if (l < sphere.w && l > 1e-9f) p = glm::vec3(sphere) + d * (sphere.w / l);
            }
        }
        for (size_t i = 0; i < pts; ++i) {
            const float s = float(i) / float(pts - 1);
            const glm::vec3 p = m_line[i];
            glm::vec3 t = m_line[std::min(i + 1, pts - 1)] - m_line[i > 0 ? i - 1 : 0];
            const float tl = glm::length(t);
            t = tl > 1e-9f ? t / tl : glm::vec3(0, -1, 0);
            const glm::vec3 view = camera - p;
            const float dist = glm::length(view);
            glm::vec3 side = glm::cross(t, view);
            const float sl = glm::length(side);
            side = sl > 1e-9f ? side / sl : perpendicular(t);
            // Tapers to the tip, never thinner than most of a pixel (a
            // thinner ribbon would flicker in and out between pixels).
            const float w = 0.5f * std::max(m_style.hairWidth * (1.0f - 0.6f * s), 0.75f * pixel * dist);
            const glm::vec3 c = glm::mix(root, tip, s) * hair.color;
            *out++ = Vertex{ p - side * w, c, t, glm::vec2(-1.0f, s) };
            *out++ = Vertex{ p + side * w, c, t, glm::vec2(1.0f, s) };
        }
    }
}

} // namespace kke
