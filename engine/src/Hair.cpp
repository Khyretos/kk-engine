#include "kke/Hair.h"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace kke {

namespace {

HairStyle make(const char* name, float length, int segments, float curl, float curlRadius, bool helix, float droop, float density,
               float bend, float damping, float hold, int hairsPerGuide, float clump, float frizz) {
    HairStyle s;
    s.name = name;
    s.length = length;
    s.segments = segments;
    s.curl = curl;
    s.curlRadius = curlRadius;
    s.helix = helix;
    s.droop = droop;
    s.density = density;
    s.bend = bend;
    s.damping = damping;
    s.hold = hold;
    s.hairsPerGuide = hairsPerGuide;
    s.clump = clump;
    s.frizz = frizz;
    return s;
}

// A unit vector at right angles to d.
glm::vec3 perpendicular(const glm::vec3& d) {
    const glm::vec3 a = std::fabs(d.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    return glm::normalize(glm::cross(d, a));
}

} // namespace

HairStyle hairStyle(const std::string& name) {
    //                        name        length segs curl  radius  helix  droop  kg/m    bend  damp  hold  hairs clump frizz
    if (name == "long")  return make("long",  0.55f, 16, 0.0f,  0.0f,   true,  0.07f, 0.005f, 6.0f, 0.2f, 0.0f, 32, 0.2f,  0.0f);
    if (name == "wavy")  return make("wavy",  0.4f,  16, 5.0f,  0.012f, false, 0.06f, 0.004f, 1.5f, 0.2f, 0.0f, 32, 0.45f, 0.001f);
    if (name == "curly") return make("curly", 0.3f,  24, 9.0f,  0.016f, true,  0.05f, 0.005f, 0.6f, 0.3f, 0.0f, 40, 0.6f,  0.003f);
    if (name == "short") return make("short", 0.07f, 5,  0.0f,  0.0f,   true,  0.02f, 0.002f, 0.3f, 0.3f, 0.4f, 24, 0.2f,  0.001f);
    if (name == "fur")   return make("fur",   0.035f, 3, 0.0f,  0.0f,   true,  0.03f, 0.001f, 0.2f, 0.4f, 0.6f, 24, 0.1f,  0.001f);
    if (name == "gel")   return make("gel",   0.1f,  6,  0.0f,  0.0f,   true,  0.5f,  0.002f, 0.1f, 0.3f, 0.85f, 24, 0.7f, 0.0f);
    return make("straight", 0.3f, 12, 0.0f, 0.0f, true, 0.06f, 0.004f, 4.0f, 0.2f, 0.0f, 32, 0.4f, 0.0f);
}

std::vector<std::string> hairStyleNames() { return { "straight", "long", "wavy", "curly", "short", "fur", "gel" }; }

void hairScalp(HairDesc& desc, const glm::vec3& center, float radius, int count, float crown, const glm::vec3& up, const glm::vec3& front, float hairline) {
    desc.roots.clear();
    desc.directions.clear();
    desc.headCenter = center;
    desc.headRadius = radius;
    const glm::vec3 u = glm::normalize(up);
    const glm::vec3 f = glm::normalize(front - u * glm::dot(front, u));
    const glm::vec3 r = glm::cross(u, f);
    desc.comb = -f * 0.6f;
    // Fibonacci points over the cap (even spacing), enough of them that
    // `count` land where hair grows.
    const float capArea = 1.0f - std::cos(std::min(crown, glm::pi<float>()));
    int total = std::max(count, int(float(count) * 2.0f / std::max(capArea, 1e-3f)));
    for (int attempt = 0; attempt < 8; ++attempt) {
        desc.roots.clear();
        desc.directions.clear();
        const float golden = glm::pi<float>() * (3.0f - std::sqrt(5.0f));
        for (int i = 0; i < total; ++i) {
            const float y = 1.0f - 2.0f * (float(i) + 0.5f) / float(total);
            const float ring = std::sqrt(std::max(0.0f, 1.0f - y * y));
            const float a = golden * float(i);
            const glm::vec3 d = u * y + r * (std::cos(a) * ring) + f * (std::sin(a) * ring);
            const float fromUp = std::acos(std::clamp(y, -1.0f, 1.0f));
            if (fromUp > crown) continue;
            // The face: towards the front, lower than the hairline.
            const float towardsFront = glm::dot(d, f);
            if (towardsFront > 0.25f && fromUp > hairline) continue;
            desc.roots.push_back(center + d * radius);
            desc.directions.push_back(d);
        }
        if (int(desc.roots.size()) >= count) break;
        total = total * 3 / 2 + 1;
    }
    // Keep `count` of them, evenly through the list (it runs top to bottom).
    if (int(desc.roots.size()) > count && count > 0) {
        std::vector<glm::vec3> roots, dirs;
        const float stepF = float(desc.roots.size()) / float(count);
        for (int i = 0; i < count; ++i) {
            const size_t k = std::min(desc.roots.size() - 1, size_t(float(i) * stepF));
            roots.push_back(desc.roots[k]);
            dirs.push_back(desc.directions[k]);
        }
        desc.roots.swap(roots);
        desc.directions.swap(dirs);
    }
}

std::vector<glm::vec3> hairRestPose(const HairDesc& desc) {
    const HairStyle& s = desc.style;
    const int segs = std::max(1, s.segments);
    const int per = hairStrandVertices(s);
    const float segLen = std::max(s.length, 1e-3f) / float(segs);
    const float follicle = std::clamp(0.3f * segLen, 0.003f, 0.01f);
    const glm::vec3 gravity = glm::length(desc.down) > 1e-6f ? glm::normalize(desc.down) : glm::vec3(0, -1, 0);
    const float clearance = desc.headRadius + 1.5f * s.thickness;
    std::vector<glm::vec3> out;
    out.reserve(desc.roots.size() * size_t(per));
    std::vector<glm::vec3> line(size_t(segs) + 1);
    for (size_t g = 0; g < desc.roots.size(); ++g) {
        const glm::vec3 root = desc.roots[g];
        glm::vec3 dir = g < desc.directions.size() && glm::length(desc.directions[g]) > 0.0f ? glm::normalize(desc.directions[g]) : -gravity;
        out.push_back(root);
        glm::vec3 p = root + dir * follicle;
        line[0] = p;
        // Out of the follicle the hair leans over into the way it falls, so
        // it lies on the scalp (a gel style, with a long droop, stands up).
        {
            const glm::vec3 fall = glm::normalize(gravity + desc.comb + gravity * 1e-4f);
            glm::vec3 along = fall - dir * glm::dot(fall, dir);
            const float lift = std::clamp(s.droop * 3.0f, 0.25f, 1.0f);
            if (glm::length(along) > 0.1f) dir = glm::normalize(glm::mix(glm::normalize(along), dir, lift));
        }
        // The centre line: out of the scalp, turning towards `down` within
        // `droop`, laid over the head where it would go into it.
        const float turn = std::clamp(segLen / std::max(s.droop, 1e-3f), 0.0f, 1.0f);
        for (int i = 1; i <= segs; ++i) {
            // Combed (desc.comb) while it lies on the head, then it falls.
            const float onHead = std::clamp(1.0f - float(i - 1) * segLen / std::max(1.5f * desc.headRadius, 1e-3f), 0.0f, 1.0f);
            const glm::vec3 fallTo = glm::normalize(gravity + desc.comb * onHead + gravity * 1e-4f);
            dir = glm::normalize(glm::mix(dir, fallTo, turn) + dir * 1e-4f);
            glm::vec3 q = p + dir * segLen;
            for (int k = 0; k < 3 && desc.headRadius > 0.0f; ++k) {
                const glm::vec3 fromC = q - desc.headCenter;
                const float dist = glm::length(fromC);
                if (dist >= clearance || dist < 1e-6f) break;
                q = desc.headCenter + fromC * (clearance / dist);
                q = p + glm::normalize(q - p) * segLen;
            }
            dir = glm::normalize(q - p);
            p = q;
            line[size_t(i)] = p;
        }
        // Curls around the centre line (a frame carried along it), growing
        // in over the first couple of centimetres.
        glm::vec3 t = glm::normalize(line[1] - line[0]);
        glm::vec3 n = perpendicular(t);
        for (int i = 0; i <= segs; ++i) {
            if (i > 0) {
                const glm::vec3 t2 = glm::normalize(line[size_t(i)] - line[size_t(i - 1)]);
                n = glm::normalize(n - t2 * glm::dot(n, t2));
                t = t2;
            }
            glm::vec3 q = line[size_t(i)];
            if (s.curl > 0.0f && s.curlRadius > 0.0f) {
                const float arc = float(i) * segLen;
                const float a = glm::two_pi<float>() * s.curl * arc + float(g) * 2.39996f;
                const float grow = std::clamp(arc / 0.02f, 0.0f, 1.0f);
                const glm::vec3 b = glm::cross(t, n);
                q += s.helix ? (n * std::cos(a) + b * std::sin(a)) * (s.curlRadius * grow) : n * (std::sin(a) * s.curlRadius * grow);
            }
            out.push_back(q);
        }
    }
    return out;
}

} // namespace kke
