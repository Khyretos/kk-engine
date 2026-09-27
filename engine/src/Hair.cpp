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

// Deterministic 0..1 from two integers.
float rand01(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return float(h & 0xFFFFFFu) / float(0x1000000);
}

// A unit vector at right angles to d.
glm::vec3 perpendicular(const glm::vec3& d) {
    const glm::vec3 a = std::fabs(d.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    return glm::normalize(glm::cross(d, a));
}

// Andre Walker's hair types: 1 straight, 2 wavy, 3 curly, 4 coily; A to
// C from loose to tight. Waves (type 2) are in the guides' rest shape;
// coils (3 and 4) are drawn around every hair, too fine for guides.
HairStyle hairType(char kind, char grade) {
    HairStyle s;
    const int g = grade == 'b' ? 1 : (grade == 'c' ? 2 : 0);
    const float t = float(g) * 0.5f; // 0 = A .. 1 = C
    s.name = std::string(1, kind) + std::string(1, grade);
    auto lerp = [t](float a, float c) { return a + (c - a) * t; };
    switch (kind) {
    case '1': // straight: fine and silky (A) to coarse with a slight bend (C)
        s.length = lerp(0.45f, 0.35f);
        s.segments = 16;
        s.bend = lerp(8.0f, 3.0f);
        s.density = lerp(0.004f, 0.005f);
        s.hairWidth = lerp(0.0006f, 0.001f);
        s.shine = lerp(0.8f, 0.5f);
        s.clump = lerp(0.15f, 0.3f);
        if (g == 2) {
            s.curl = 1.5f;
            s.curlRadius = 0.008f;
            s.helix = false;
            s.frizz = 0.0005f;
        }
        break;
    case '2': // S-shaped waves, looser (A) to defined from the roots with frizz (C)
        s.length = lerp(0.4f, 0.35f);
        s.segments = g == 2 ? 18 : 16;
        s.curl = lerp(3.0f, 5.0f);
        s.curlRadius = lerp(0.01f, 0.018f);
        s.helix = false;
        s.bend = lerp(2.0f, 1.2f);
        s.clump = lerp(0.4f, 0.5f);
        s.frizz = lerp(0.0008f, 0.002f);
        s.shine = lerp(0.55f, 0.45f);
        break;
    case '3': // ringlets, as wide as sidewalk chalk (A) to a pencil (C)
        s.length = lerp(0.35f, 0.2f);
        s.segments = 16 - 2 * g;
        s.coil = lerp(28.0f, 75.0f);
        s.coilRadius = lerp(0.011f, 0.0045f);
        s.definition = lerp(0.9f, 0.65f);
        s.shrinkage = lerp(0.25f, 0.5f);
        s.stretch = 3.0f;
        s.bend = 1.5f;
        s.droop = lerp(0.05f, 0.14f);
        s.hold = lerp(0.0f, 0.3f);
        s.clump = lerp(0.7f, 0.6f);
        s.spread = lerp(0.6f, 0.8f);
        s.frizz = lerp(0.001f, 0.002f);
        s.hairWidth = 0.0009f;
        s.shine = lerp(0.3f, 0.2f);
        s.hairsPerGuide = 40;
        break;
    default: // '4': coils as fine as a crochet needle (A), Z-shaped bends (B), tight and less defined (C)
        s.length = lerp(0.13f, 0.1f);
        s.segments = 10 - g;
        s.coil = lerp(110.0f, 160.0f);
        s.coilRadius = lerp(0.0035f, 0.0025f);
        s.zigzag = g == 1 ? 1.0f : (g == 2 ? 0.35f : 0.0f);
        s.definition = lerp(0.55f, 0.1f);
        s.shrinkage = lerp(0.6f, 0.75f);
        s.stretch = 3.0f;
        s.bend = 1.0f;
        s.droop = lerp(0.3f, 0.5f);
        s.hold = lerp(0.5f, 0.65f);
        s.clump = lerp(0.5f, 0.2f);
        s.spread = 1.3f;
        s.frizz = lerp(0.002f, 0.003f);
        s.hairWidth = 0.0011f;
        s.shine = lerp(0.14f, 0.08f);
        s.hairsPerGuide = 40;
        break;
    }
    return s;
}

} // namespace

HairStyle hairStyle(const std::string& name) {
    if (name.size() == 2 && name[0] >= '1' && name[0] <= '4' && (name[1] == 'a' || name[1] == 'b' || name[1] == 'c')) return hairType(name[0], name[1]);
    //                        name        length segs curl  radius  helix  droop  kg/m    bend  damp  hold  hairs clump frizz
    if (name == "long")  return make("long",  0.55f, 16, 0.0f,  0.0f,   true,  0.07f, 0.005f, 6.0f, 0.2f, 0.0f, 32, 0.2f,  0.0f);
    if (name == "wavy")  return make("wavy",  0.4f,  16, 5.0f,  0.012f, false, 0.06f, 0.004f, 1.5f, 0.2f, 0.0f, 32, 0.45f, 0.001f);
    if (name == "curly") return make("curly", 0.3f,  24, 9.0f,  0.016f, true,  0.05f, 0.005f, 0.6f, 0.3f, 0.0f, 40, 0.6f,  0.003f);
    if (name == "short") return make("short", 0.07f, 5,  0.0f,  0.0f,   true,  0.02f, 0.002f, 0.3f, 0.3f, 0.4f, 24, 0.2f,  0.001f);
    if (name == "fur")   return make("fur",   0.035f, 3, 0.0f,  0.0f,   true,  0.03f, 0.001f, 0.2f, 0.4f, 0.6f, 24, 0.1f,  0.001f);
    if (name == "gel")   return make("gel",   0.1f,  6,  0.0f,  0.0f,   true,  0.5f,  0.002f, 0.1f, 0.3f, 0.85f, 24, 0.7f, 0.0f);
    return make("straight", 0.3f, 12, 0.0f, 0.0f, true, 0.06f, 0.004f, 4.0f, 0.2f, 0.0f, 32, 0.4f, 0.0f);
}

std::vector<std::string> hairStyleNames() {
    return { "1a", "1b", "1c", "2a", "2b", "2c", "3a", "3b", "3c", "4a", "4b", "4c", "straight", "long", "wavy", "curly", "short", "fur", "gel" };
}

void hairScalp(HairDesc& desc, const glm::vec3& center, float radius, int count, float crown, const glm::vec3& up, const glm::vec3& front, float hairline) {
    desc.roots.clear();
    desc.directions.clear();
    desc.headCenter = center;
    desc.headRadius = radius;
    const glm::vec3 u = glm::normalize(up);
    const glm::vec3 f = glm::normalize(front - u * glm::dot(front, u));
    const glm::vec3 r = glm::cross(u, f);
    desc.comb = -f * 0.6f;
    // Fibonacci points over the whole sphere (even spacing), the fewest of
    // them that put `count` where hair grows. (Taking every n-th point of
    // a denser set instead would leave bald stripes: the golden angle
    // times n lines them up in spiral arms.)
    const float golden = glm::pi<float>() * (3.0f - std::sqrt(5.0f));
    auto grow = [&](int total, bool keep) {
        int kept = 0;
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
            ++kept;
            if (keep) {
                desc.roots.push_back(center + d * radius);
                desc.directions.push_back(d);
            }
        }
        return kept;
    };
    if (count <= 0) return;
    int lo = count, hi = count;
    while (grow(hi, false) < count && hi < count * 1000) hi *= 2;
    while (lo < hi) { // the fewest points on the sphere that give `count`
        const int mid = lo + (hi - lo) / 2;
        if (grow(mid, false) >= count) hi = mid;
        else lo = mid + 1;
    }
    grow(lo, true);
    // One or two more than asked, at most: drop the lowest.
    desc.roots.resize(std::min(desc.roots.size(), size_t(count)));
    desc.directions.resize(desc.roots.size());
}

namespace {

// Which tie a root is gathered to: the nearest, if within its reach.
int tieFor(const HairDesc& desc, const glm::vec3& root) {
    int best = -1;
    float bestD = 1e30f;
    for (size_t i = 0; i < desc.ties.size(); ++i) {
        const float d = glm::distance(desc.ties[i].at, root);
        if (d <= desc.ties[i].reach && d < bestD) {
            bestD = d;
            best = int(i);
        }
    }
    return best;
}

// A tied strand's centre line: over the scalp (a great circle, clear of
// the head) to its tie, then out in a puff or wound into a knot.
void tiedLine(const HairDesc& desc, const HairDesc::Tie& tie, size_t g, const glm::vec3& start, float segLen, std::vector<glm::vec3>& line) {
    const HairStyle& s = desc.style;
    const int segs = int(line.size()) - 1;
    const glm::vec3 c = desc.headCenter;
    const float clearance = desc.headRadius + 1.5f * s.thickness;
    const bool onHead = desc.headRadius > 0.0f;
    const glm::vec3 from = onHead ? glm::normalize(start - c) : glm::vec3(0.0f);
    const glm::vec3 to = onHead ? glm::normalize(tie.at - c) : glm::normalize(tie.at - start);
    const float angle = onHead ? std::acos(std::clamp(glm::dot(from, to), -1.0f, 1.0f)) : 0.0f;
    const float over = onHead ? angle * clearance : glm::distance(start, tie.at);
    const glm::vec3 base = onHead ? c + to * clearance : tie.at;
    const glm::vec3 up = onHead ? to : -glm::normalize(desc.down);
    // The way it arrives at the tie, along the scalp (for a root at the
    // tie itself, a direction of its own).
    glm::vec3 arrive = onHead && angle > 1e-3f ? glm::normalize(to * std::cos(angle) - from) : glm::vec3(0.0f);
    arrive = arrive - up * glm::dot(arrive, up);
    if (glm::length(arrive) < 1e-3f) {
        const glm::vec3 a = perpendicular(up), b = glm::cross(up, a);
        const float r = float(g) * 2.39996f;
        arrive = a * std::cos(r) + b * std::sin(r);
    }
    arrive = glm::normalize(arrive);
    const glm::vec3 side = glm::cross(up, arrive);
    const float total = segLen * float(segs);
    const float beyond = std::max(total - over, 1e-4f);
    // Knots: every strand a little off the rope's centre.
    const float k1 = rand01(uint32_t(g), 1u) - 0.5f, k2 = rand01(uint32_t(g), 2u) - 0.5f;
    for (int i = 0; i <= segs; ++i) {
        const float d = float(i) * segLen;
        if (d <= over) {
            if (onHead) {
                const float a = angle > 1e-6f ? d / over * angle : 0.0f;
                const glm::vec3 axis = glm::cross(from, to);
                const glm::vec3 dir = glm::length(axis) > 1e-6f
                                          ? from * std::cos(a) + glm::cross(glm::normalize(axis), from) * std::sin(a) // turning from -> to
                                          : from;
                line[size_t(i)] = c + glm::normalize(dir) * glm::mix(glm::length(start - c), clearance, std::min(d / 0.01f, 1.0f));
            } else {
                line[size_t(i)] = start + to * d;
            }
            continue;
        }
        const float u = d - over;
        if (tie.shape == HairDesc::Tie::Shape::Puff) {
            // Bursting out every way from the tie (strands spread evenly
            // from straight up to just below level, turned the way each
            // came), into a ball.
            const float spread = std::acos(1.0f - 1.05f * rand01(uint32_t(g), 3u)); // uniform over the ball's top, down to ~93 degrees
            const float turn = (rand01(uint32_t(g), 4u) - 0.5f) * 2.4f;
            const glm::vec3 way = arrive * std::cos(turn) + side * std::sin(turn);
            line[size_t(i)] = base + (up * std::cos(spread) + way * std::sin(spread)) * u;
        } else {
            // A knot: out from the middle, round and round, narrowing as it rises.
            const float f = u / beyond;
            const float rho = 0.5f * tie.size * std::min(1.0f, u / (0.3f * tie.size)) * (1.0f - 0.7f * f);
            float theta = 0.0f;
            // Arc length of a spiral ~ rho dtheta: integrate in the steps taken.
            const int n = 16;
            for (int k = 0; k < n; ++k) {
                const float uu = (float(k) + 0.5f) / float(n) * u;
                const float ff = uu / beyond;
                const float rr = std::max(0.5f * tie.size * std::min(1.0f, uu / (0.3f * tie.size)) * (1.0f - 0.7f * ff), 0.1f * tie.size);
                theta += (u / float(n)) / rr;
            }
            const float h = tie.size * (0.15f + 0.8f * f);
            line[size_t(i)] = base + up * h + (arrive * std::cos(theta) + side * std::sin(theta)) * rho +
                              (arrive * k1 + side * k2) * (0.25f * tie.size);
        }
    }
}

} // namespace

std::vector<glm::vec3> hairRestPose(const HairDesc& desc) {
    const HairStyle& s = desc.style;
    const int segs = std::max(1, s.segments);
    const int per = hairStrandVertices(s);
    const glm::vec3 gravity = glm::length(desc.down) > 1e-6f ? glm::normalize(desc.down) : glm::vec3(0, -1, 0);
    const float clearance = desc.headRadius + 1.5f * s.thickness;
    std::vector<glm::vec3> out;
    out.reserve(desc.roots.size() * size_t(per));
    std::vector<glm::vec3> line(size_t(segs) + 1);
    for (size_t g = 0; g < desc.roots.size(); ++g) {
        const float cut = g < desc.lengths.size() ? std::max(desc.lengths[g], 0.01f) : 1.0f;
        const float segLen = std::max(s.length * cut, 1e-3f) / float(segs);
        const float follicle = std::clamp(0.3f * segLen, 0.003f, 0.01f);
        const glm::vec3 root = desc.roots[g];
        glm::vec3 dir = g < desc.directions.size() && glm::length(desc.directions[g]) > 0.0f ? glm::normalize(desc.directions[g]) : -gravity;
        out.push_back(root);
        glm::vec3 p = root + dir * follicle;
        line[0] = p;
        const int tie = tieFor(desc, root);
        if (tie >= 0) {
            tiedLine(desc, desc.ties[size_t(tie)], g, p, segLen, line);
        } else {
            // Out of the follicle the hair leans over into the way it
            // falls, so it lies on the scalp (a gel style, with a long
            // droop, stands up).
            {
                const glm::vec3 fall = glm::normalize(gravity + desc.comb + gravity * 1e-4f);
                glm::vec3 along = fall - dir * glm::dot(fall, dir);
                const float lift = std::clamp(s.droop * 3.0f, 0.25f, 1.0f);
                if (glm::length(along) > 0.1f) dir = glm::normalize(glm::mix(glm::normalize(along), dir, lift));
            }
            // The centre line: out of the scalp, turning towards `down`
            // within `droop`, laid over the head where it would go into it.
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
                // A curl turning in towards the head stays on its surface.
                const glm::vec3 fromC = q - desc.headCenter;
                const float dist = glm::length(fromC);
                if (desc.headRadius > 0.0f && i > 0 && dist < clearance && dist > 1e-6f) q = desc.headCenter + fromC * (clearance / dist);
            }
            out.push_back(q);
        }
    }
    return out;
}

bool hairstyleOnHead(HairDesc& desc, const std::string& name, const glm::vec3& center, float radius, int guides, const glm::vec3& up, const glm::vec3& front) {
    const glm::vec3 root = desc.style.rootColor, tip = desc.style.tipColor;
    const glm::vec3 u = glm::normalize(up);
    const glm::vec3 f = glm::normalize(front - u * glm::dot(front, u));
    const glm::vec3 r = glm::cross(u, f);
    desc.lengths.clear();
    desc.ties.clear();
    auto keepColours = [&] {
        desc.style.rootColor = root;
        desc.style.tipColor = tip;
    };
    auto names = hairStyleNames();
    if (std::find(names.begin(), names.end(), name) != names.end()) {
        desc.style = hairStyle(name);
        keepColours();
        hairScalp(desc, center, radius, guides, 1.9f, u, f);
        return true;
    }
    if (name == "afro") {
        // 4C grown out and picked out round: every strand stands out from
        // the scalp, longer on top so the shape sits high.
        desc.style = hairStyle("4c");
        desc.style.name = name;
        desc.style.length = 0.16f;
        desc.style.segments = 10;
        desc.style.droop = 1.0f;
        desc.style.hold = 0.85f;
        desc.style.gravity = 0.4f;
        hairScalp(desc, center, radius, guides, 1.9f, u, f);
        desc.comb = glm::vec3(0.0f);
        for (glm::vec3& d : desc.directions) {
            desc.lengths.push_back(0.75f + 0.25f * glm::dot(d, u));
            d = glm::normalize(d + u * 0.5f - f * (0.4f * std::max(glm::dot(d, f), 0.0f))); // up and back, off the face
        }
    } else if (name == "twist-out") {
        // Two-strand twists taken out: big defined coils, clumped.
        desc.style = hairStyle("4a");
        desc.style.name = name;
        desc.style.length = 0.14f;
        desc.style.coil = 70.0f;
        desc.style.coilRadius = 0.005f;
        desc.style.definition = 1.0f;
        desc.style.clump = 0.85f;
        desc.style.spread = 0.5f;
        desc.style.droop = 0.6f;
        desc.style.hold = 0.6f;
        hairScalp(desc, center, radius, guides, 1.9f, u, f);
        desc.comb = glm::vec3(0.0f);
        for (const glm::vec3& d : desc.directions) desc.lengths.push_back(0.85f + 0.15f * glm::dot(d, u));
    } else if (name == "puff") {
        // Everything gathered to the crown and tied: a ball of coils on top.
        desc.style = hairStyle("4c");
        desc.style.name = name;
        desc.style.segments = 16;
        desc.style.hold = 0.85f;
        desc.style.gravity = 0.5f;
        desc.style.clump = 0.0f; // the ball filled, not spikes
        desc.style.spread = 2.0f;
        desc.style.coilRadius = 0.004f;
        desc.style.definition = 0.0f;
        hairScalp(desc, center, radius, guides, 1.9f, u, f);
        HairDesc::Tie t;
        t.at = center + glm::normalize(u - f * 0.3f) * radius;
        t.reach = 10.0f * radius;
        desc.ties.push_back(t);
        // Every strand cut to reach the tie and then the same way out: a round ball.
        const float ball = 1.1f * radius, clearance = radius + 1.5f * desc.style.thickness;
        std::vector<float> want;
        float longest = 0.0f;
        for (const glm::vec3& p : desc.roots) {
            const float over = std::acos(std::clamp(glm::dot(glm::normalize(p - center), glm::normalize(t.at - center)), -1.0f, 1.0f)) * clearance;
            want.push_back(over + ball);
            longest = std::max(longest, over + ball);
        }
        desc.style.length = longest;
        for (float w : want) desc.lengths.push_back(w / longest);
    } else if (name == "bantu knots") {
        // Parted into sections, each twisted and wound into a knot.
        desc.style = hairStyle("4c");
        desc.style.name = name;
        desc.style.length = 0.22f;
        desc.style.segments = 18;
        desc.style.hold = 0.9f;
        desc.style.coilRadius = 0.0015f; // twisted tight before winding
        desc.style.definition = 0.9f;
        desc.style.clump = 0.8f;
        desc.style.spread = 0.7f;
        hairScalp(desc, center, radius, guides, 1.9f, u, f);
        const glm::vec3 at[] = { u, glm::normalize(u + f * 0.8f + r * 0.5f), glm::normalize(u + f * 0.8f - r * 0.5f), glm::normalize(u * 0.3f + r),
                                 glm::normalize(u * 0.3f - r), glm::normalize(u * 0.4f - f + r * 0.45f), glm::normalize(u * 0.4f - f - r * 0.45f) };
        for (const glm::vec3& d : at) {
            HairDesc::Tie t;
            t.at = center + d * radius;
            t.reach = 10.0f * radius; // every root goes to its nearest knot: the parts fall between them
            t.size = 0.55f * radius;
            t.shape = HairDesc::Tie::Shape::Knot;
            desc.ties.push_back(t);
        }
    } else if (name == "high-top fade") {
        // Hair only on top, standing straight up and cut flat; the sides
        // are faded to the skin (paint them: this is only the hair).
        desc.style = hairStyle("4c");
        desc.style.name = name;
        desc.style.segments = 8;
        desc.style.droop = 5.0f;
        desc.style.hold = 0.85f;
        hairScalp(desc, center, radius, guides, 0.95f, u, f, 0.95f);
        desc.comb = glm::vec3(0.0f);
        const float top = glm::dot(center, u) + radius + 0.12f;
        float longest = 0.0f;
        for (size_t i = 0; i < desc.roots.size(); ++i) {
            desc.directions[i] = u;
            longest = std::max(longest, top - glm::dot(desc.roots[i], u));
        }
        desc.style.length = longest;
        for (const glm::vec3& p : desc.roots) desc.lengths.push_back((top - glm::dot(p, u)) / longest);
    } else {
        return false;
    }
    keepColours();
    return true;
}

std::vector<std::string> hairstyleNames() { return { "afro", "puff", "high-top fade", "twist-out", "bantu knots" }; }

} // namespace kke
