#include "Combat.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace flying {

namespace {

constexpr float kCell = 40.0f; // m: the town's lookup grid

uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float unit(uint32_t& state) {
    state = mix(state + 0x9e3779b9u);
    return static_cast<float>(state & 0xffffffu) / static_cast<float>(0x1000000);
}

// Where a segment first enters a sphere (0..1), or -1.
float segmentSphere(const glm::vec3& from, const glm::vec3& to, const glm::vec3& c, float r) {
    const glm::vec3 d = to - from, m = from - c;
    const float a = glm::dot(d, d);
    const float cc = glm::dot(m, m) - r * r;
    if (cc <= 0.0f) return 0.0f; // starts inside
    if (a < 1e-12f) return -1.0f;
    const float b = glm::dot(m, d);
    const float disc = b * b - a * cc;
    if (disc < 0.0f) return -1.0f;
    const float t = (-b - std::sqrt(disc)) / a;
    return t >= 0.0f && t <= 1.0f ? t : -1.0f;
}

// A segment through a box (slabs): where it enters (0..1), or -1.
float segmentBox(const glm::vec3& from, const glm::vec3& to, const glm::vec3& lo, const glm::vec3& hi) {
    float t0 = 0.0f, t1 = 1.0f;
    const glm::vec3 d = to - from;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-9f) {
            if (from[i] < lo[i] || from[i] > hi[i]) return -1.0f;
            continue;
        }
        float a = (lo[i] - from[i]) / d[i], b = (hi[i] - from[i]) / d[i];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return -1.0f;
    }
    return t0;
}

} // namespace

PlaneShape planeShape(float span) {
    PlaneShape s;
    s.spheres = { { { 0.0f, 0.45f, -2.0f }, 1.0f }, { { 0.0f, 0.6f, 0.0f }, 1.1f }, { { 0.0f, 0.8f, 2.4f }, 0.9f } };
    // Along each wing, a sphere every 1.6 m or so, the last at the tip.
    const float half = std::max(span * 0.5f - 0.7f, 1.9f);
    const int count = std::max(2, static_cast<int>(std::ceil((half - 1.9f) / 1.6f)) + 1);
    for (int i = 0; i < count; ++i) {
        const float x = 1.9f + (half - 1.9f) * static_cast<float>(i) / static_cast<float>(count - 1);
        const float r = i + 1 == count ? 0.8f : 1.0f;
        s.spheres.push_back({ { -x, 0.7f, -0.8f }, r });
        s.spheres.push_back({ { x, 0.7f, -0.8f }, r });
    }
    for (const HitSphere& h : s.spheres) s.bound = std::max(s.bound, glm::length(h.at) + h.radius);
    return s;
}

PlaneContact planesTouch(const PlaneShape& shape, const glm::vec3& a0, const glm::vec3& a1, const glm::quat& rotA, const glm::vec3& b0,
                         const glm::vec3& b1, const glm::quat& rotB) {
    PlaneContact out;
    // Their closest approach this step (both moving in straight lines).
    const glm::vec3 r0 = a0 - b0, dr = (a1 - b1) - r0;
    const float dd = glm::dot(dr, dr);
    const float t = dd > 1e-9f ? std::clamp(-glm::dot(r0, dr) / dd, 0.0f, 1.0f) : 1.0f;
    const glm::vec3 r = r0 + dr * t;
    if (glm::length(r) > 2.0f * shape.bound) return out;
    const glm::vec3 pa = glm::mix(a0, a1, t), pb = glm::mix(b0, b1, t);
    for (const HitSphere& sa : shape.spheres) {
        const glm::vec3 ca = pa + rotA * sa.at;
        for (const HitSphere& sb : shape.spheres) {
            const glm::vec3 cb = pb + rotB * sb.at;
            const glm::vec3 d = ca - cb;
            const float dist = glm::length(d);
            const float depth = sa.radius + sb.radius - dist;
            if (depth <= out.depth) continue;
            out.hit = true;
            out.t = t;
            out.depth = depth;
            out.normal = dist > 1e-4f ? d / dist : glm::vec3(0.0f, 1.0f, 0.0f);
            out.point = cb + out.normal * (sb.radius - depth * 0.5f);
        }
    }
    return out;
}

bool bulletHits(const PlaneShape& shape, const glm::vec3& from, const glm::vec3& to, const glm::vec3& at, const glm::quat& rot, float& t,
                glm::vec3& point) {
    // Nowhere near: one test against the whole plane first.
    if (segmentSphere(from, to, at, shape.bound) < 0.0f && glm::length(from - at) > shape.bound) return false;
    float best = 2.0f;
    for (const HitSphere& s : shape.spheres) {
        const float k = segmentSphere(from, to, at + rot * s.at, s.radius);
        if (k >= 0.0f && k < best) best = k;
    }
    if (best > 1.0f) return false;
    t = best;
    point = from + (to - from) * best;
    return true;
}

glm::vec3 leadPoint(const glm::vec3& from, const glm::vec3& target, const glm::vec3& targetVelocity, float speed) {
    const float time = glm::length(target - from) / std::max(speed, 1.0f);
    return target + targetVelocity * time;
}

// ---- the town

Town::Town(const Island& island, bool district, const std::vector<glm::vec3>& houses, const std::vector<Ring>& rings) : m_district(district) {
    const Runway& w = island.runway();
    const float x1 = w.start.x + w.width * 0.5f, ground = w.height();
    // The airfield: a hangar (its roof a little wider) and the control
    // tower (its cab on top), as World.cpp draws them.
    Building hangar;
    hangar.lo = glm::vec3(x1 + 29.0f, ground - 1.0f, w.start.z - 142.5f);
    hangar.hi = glm::vec3(x1 + 61.5f, ground + 15.1f, w.start.z - 97.5f);
    hangar.colour = { 0.62f, 0.64f, 0.68f };
    m_buildings.push_back(hangar);
    Building tower;
    tower.lo = glm::vec3(x1 + 25.5f, ground - 1.0f, w.start.z - 204.5f);
    tower.hi = glm::vec3(x1 + 34.5f, ground + 27.0f, w.start.z - 195.5f);
    tower.colour = { 0.85f, 0.85f, 0.82f };
    m_buildings.push_back(tower);
    m_centre = glm::vec3(w.start.x - 330.0f, ground + 60.0f, w.start.z - w.length * 0.5f);
    if (island.map() == Map::Canyon) {
        addCanyon(island, rings);
        index();
        return;
    }
    if (island.map() == Map::City) {
        addCity(island, rings);
        index();
        // The middle, for the start of a dogfight: over downtown's roofs.
        m_centre.y = std::max(m_centre.y, roof(m_centre.x, m_centre.z, 150.0f) + 60.0f);
        return;
    }

    if (district) {
        // Blocks of 70 m (50 m of buildings, 20 m of street) west of the runway.
        uint32_t s = island.seed() * 2891336453u + 1013904223u;
        constexpr float kBlock = 70.0f, kLot = 50.0f;
        const glm::vec2 downtown(m_centre.x, m_centre.z);
        for (int bx = 0; bx < 8; ++bx)
            for (int bz = 0; bz < 12; ++bz) {
                const float cx = w.start.x - 110.0f - kBlock * (static_cast<float>(bx) + 0.5f);
                const float cz = w.start.z - w.length * 0.5f + kBlock * (static_cast<float>(bz) - 5.5f);
                const float fromMiddle = glm::length(glm::vec2(cx, cz) - downtown);
                const bool towers = fromMiddle < 230.0f;
                if (!towers && unit(s) < 0.25f) continue; // a park, a car park
                // Up to four buildings on the lot: towers one or two, houses four.
                const int count = towers ? 1 + static_cast<int>(unit(s) * 2.0f) : 4;
                for (int i = 0; i < count; ++i) {
                    Building b;
                    glm::vec3 size;
                    if (towers) {
                        const float width = 16.0f + unit(s) * 12.0f, depth = 16.0f + unit(s) * 12.0f;
                        const float tall = 40.0f + (1.0f - fromMiddle / 230.0f) * 70.0f + unit(s) * 25.0f;
                        size = glm::vec3(width, tall, depth);
                        b.tower = true;
                        const float grey = 0.5f + unit(s) * 0.3f;
                        b.colour = glm::vec3(grey, grey * (0.95f + unit(s) * 0.08f), grey * (1.0f + unit(s) * 0.1f));
                    } else if (!houses.empty()) {
                        b.art = static_cast<int>(unit(s) * static_cast<float>(houses.size())) % static_cast<int>(houses.size());
                        b.turn = static_cast<int>(unit(s) * 4.0f) % 4;
                        size = houses[static_cast<size_t>(b.art)];
                        if (b.turn % 2 == 1) std::swap(size.x, size.z);
                    } else {
                        size = glm::vec3(9.0f + unit(s) * 5.0f, 6.0f + unit(s) * 5.0f, 9.0f + unit(s) * 5.0f);
                        const glm::vec3 walls[] = { { 0.93f, 0.88f, 0.78f }, { 0.78f, 0.55f, 0.45f }, { 0.85f, 0.85f, 0.88f }, { 0.62f, 0.72f, 0.8f } };
                        b.colour = walls[static_cast<int>(unit(s) * 4.0f) % 4];
                    }
                    // Where on the lot: towers side by side, houses in its corners.
                    glm::vec2 at(cx, cz);
                    if (count == 2) at.x += (i == 0 ? -1.0f : 1.0f) * kLot * 0.25f;
                    if (count == 4) at += glm::vec2((i % 2 == 0 ? -1.0f : 1.0f) * kLot * 0.27f, (i < 2 ? -1.0f : 1.0f) * kLot * 0.27f);
                    size.x = std::min(size.x, count == 1 ? kLot : kLot * 0.48f);
                    size.z = std::min(size.z, count == 4 ? kLot * 0.48f : kLot);
                    const float hx = size.x * 0.5f, hz = size.z * 0.5f;
                    // Only on fairly flat land, not on the beach or the runway.
                    float lo = 1e9f, hi = -1e9f;
                    for (const glm::vec2 k : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1), glm::vec2(0, 0) }) {
                        const float h = island.terrain(at.x + k.x * hx, at.y + k.y * hz);
                        lo = std::min(lo, h);
                        hi = std::max(hi, h);
                    }
                    if (lo < 2.5f || hi - lo > (towers ? 14.0f : 6.0f) || island.onRunway(at.x, at.y, 45.0f + std::max(hx, hz))) continue;
                    b.lo = glm::vec3(at.x - hx, lo - 1.0f, at.y - hz);
                    b.hi = glm::vec3(at.x + hx, lo + size.y, at.y + hz);
                    m_buildings.push_back(b);
                }
            }
        // The middle, for the start of a dogfight: over the town's roofs.
        m_centre.y = std::max(m_centre.y, roof(m_centre.x, m_centre.z, 200.0f) + 80.0f);
    }
    index();
}

// Rock pillars standing in the gorge's wide stretches and stone bridges
// across it where it runs along X or Z (a building is an upright box),
// high enough that the race goes under them.
void Town::addCanyon(const Island& island, const std::vector<Ring>& rings) {
    uint32_t s = island.seed() * 2891336453u + 1013904223u;
    const glm::vec3 rockColour(0.62f, 0.38f, 0.26f);
    auto nearRing = [&](const glm::vec2& at, float gap) {
        for (const Ring& r : rings)
            if (glm::length(glm::vec2(r.center.x, r.center.z) - at) < gap) return true;
        return false;
    };
    // Pillars, off to one side of the middle line, from the floor to most
    // of the way up.
    int pillars = 0;
    for (int tries = 0; tries < 200 && pillars < 16; ++tries) {
        const float a = unit(s) * glm::two_pi<float>();
        const float hw = island.gorgeHalfWidth(a);
        if (hw < 60.0f) continue; // too narrow: no room to fly past
        const glm::vec3 mid = island.gorgePoint(a), along = island.gorgeTangent(a);
        const glm::vec3 side = glm::normalize(glm::cross(along, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 at = mid + side * ((unit(s) < 0.5f ? -1.0f : 1.0f) * hw * 0.5f);
        if (nearRing(glm::vec2(at.x, at.z), 110.0f)) continue;
        const float half = 6.0f + unit(s) * 4.0f;
        Building b;
        b.plain = true;
        b.colour = rockColour * (0.9f + unit(s) * 0.2f);
        b.lo = glm::vec3(at.x - half, Island::kFloor - 2.0f, at.z - half);
        b.hi = glm::vec3(at.x + half, Island::kFloor + (Island::kPlateau - Island::kFloor) * (0.45f + unit(s) * 0.45f), at.z + half);
        m_buildings.push_back(b);
        ++pillars;
    }
    // Bridges: a slab from rim to rim, 50 m or so below the plateau.
    int bridges = 0;
    for (int k = 0; k < 64 && bridges < 5; ++k) {
        const float a = static_cast<float>(k) / 64.0f * glm::two_pi<float>();
        const glm::vec3 t = island.gorgeTangent(a);
        if (std::abs(t.x) < 0.95f && std::abs(t.z) < 0.95f) continue;
        const glm::vec3 mid = island.gorgePoint(a);
        if (nearRing(glm::vec2(mid.x, mid.z), 70.0f)) continue;
        bool apart = true; // not two side by side
        for (const Building& b : m_buildings)
            if (b.plain && b.hi.y - b.lo.y < 20.0f && glm::length(glm::vec2((b.lo.x + b.hi.x) * 0.5f, (b.lo.z + b.hi.z) * 0.5f) - glm::vec2(mid.x, mid.z)) < 400.0f)
                apart = false;
        if (!apart) continue;
        const float span = island.gorgeHalfWidth(a) + 50.0f, y = Island::kFloor + 115.0f;
        Building b;
        b.plain = true;
        b.colour = rockColour * 0.85f;
        if (std::abs(t.x) >= 0.95f) { // the gorge runs along X: the bridge spans Z
            b.lo = glm::vec3(mid.x - 7.0f, y, mid.z - span);
            b.hi = glm::vec3(mid.x + 7.0f, y + 9.0f, mid.z + span);
        } else {
            b.lo = glm::vec3(mid.x - span, y, mid.z - 7.0f);
            b.hi = glm::vec3(mid.x + span, y + 9.0f, mid.z + 7.0f);
        }
        m_buildings.push_back(b);
        ++bridges;
    }
    // A dogfight circles over the gorge on the far side from the runway.
    const glm::vec3 far = island.gorgePoint(glm::pi<float>());
    m_centre = glm::vec3(far.x, Island::kPlateau + 50.0f, far.z);
}

// Towers on a 100 m grid (70 m lots, 30 m streets): skyscrapers downtown,
// smaller further out, a park here and there, sky bridges between some
// neighbours. Clear of the airport, and of an avenue along the race course
// with a square round each ring, so the course can be flown between them.
void Town::addCity(const Island& island, const std::vector<Ring>& rings) {
    uint32_t s = island.seed() * 2891336453u + 1013904223u;
    constexpr float kPitch = 100.0f, kLot = 70.0f;
    const float da = unit(s) * glm::two_pi<float>();
    const glm::vec2 downtown(std::cos(da) * 750.0f, std::sin(da) * 750.0f);
    m_centre = glm::vec3(downtown.x, Island::kCityGround + 60.0f, downtown.y);
    auto toCourse = [&](const glm::vec2& p) {
        float best = 1e9f;
        for (size_t i = 0; i < rings.size(); ++i) {
            const glm::vec2 a(rings[i].center.x, rings[i].center.z), b(rings[(i + 1) % rings.size()].center.x, rings[(i + 1) % rings.size()].center.z);
            const glm::vec2 ab = b - a;
            const float t = std::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1.0f), 0.0f, 1.0f);
            best = std::min(best, glm::length(p - (a + ab * t)));
            best = std::min(best, glm::length(p - a) - 110.0f); // a square round the ring, room to turn
        }
        return best;
    };
    constexpr int kHalf = 17;
    std::vector<float> tall(static_cast<size_t>((2 * kHalf + 1) * (2 * kHalf + 1)), 0.0f); // each lot's height (0: empty)
    auto lot = [&](int bx, int bz) -> float& { return tall[static_cast<size_t>((bz + kHalf) * (2 * kHalf + 1) + (bx + kHalf))]; };
    for (int bz = -kHalf; bz <= kHalf; ++bz)
        for (int bx = -kHalf; bx <= kHalf; ++bx) {
            const glm::vec2 c(static_cast<float>(bx) * kPitch, static_cast<float>(bz) * kPitch);
            const float u = unit(s), v = unit(s), w = unit(s);
            if (glm::length(c) > 1700.0f || island.onRunway(c.x, c.y, 160.0f)) continue;
            if (!rings.empty() && toCourse(c) < 120.0f) continue; // the avenue
            if (u < 0.08f) continue;                              // a park
            const float d = glm::length(c - downtown);
            const float base = 30.0f + 260.0f * std::exp(-d * d / (560.0f * 560.0f));
            const float height = std::clamp(base * (0.55f + 0.6f * v) + 10.0f * w, 18.0f, 340.0f);
            const int count = height > 110.0f || w < 0.5f ? 1 : 2;
            for (int i = 0; i < count; ++i) {
                Building b;
                b.tower = true;
                const float grey = 0.45f + unit(s) * 0.35f, warm = unit(s);
                b.colour = warm < 0.3f ? glm::vec3(grey * 1.05f, grey, grey * 0.88f) : warm < 0.6f ? glm::vec3(grey * 0.9f, grey * 0.97f, grey * 1.08f) : glm::vec3(grey);
                const float h = i == 0 ? height : height * (0.5f + unit(s) * 0.4f);
                const float wx = count == 1 ? kLot * (0.6f + unit(s) * 0.4f) : kLot * 0.46f, wz = kLot * (0.6f + unit(s) * 0.4f);
                const float cx = c.x + (count == 2 ? (i == 0 ? -1.0f : 1.0f) * kLot * 0.27f : 0.0f);
                b.lo = glm::vec3(cx - wx * 0.5f, Island::kCityGround - 1.0f, c.y - wz * 0.5f);
                b.hi = glm::vec3(cx + wx * 0.5f, Island::kCityGround + h, c.y + wz * 0.5f);
                m_buildings.push_back(b);
            }
            lot(bx, bz) = height;
        }
    // Sky bridges across a street between two tall neighbours.
    for (int bz = -kHalf; bz <= kHalf; ++bz)
        for (int bx = -kHalf; bx < kHalf; ++bx) {
            const float a = lot(bx, bz), b = lot(bx + 1, bz);
            if (a < 80.0f || b < 80.0f || unit(s) > 0.18f) continue;
            const float y = Island::kCityGround + 30.0f + unit(s) * (std::min(a, b) - 50.0f);
            const float x0 = static_cast<float>(bx) * kPitch + kLot * 0.3f, x1 = static_cast<float>(bx + 1) * kPitch - kLot * 0.3f;
            const float z = static_cast<float>(bz) * kPitch;
            Building sky;
            sky.plain = true;
            sky.colour = glm::vec3(0.35f, 0.42f, 0.5f);
            sky.lo = glm::vec3(x0, y, z - 5.0f);
            sky.hi = glm::vec3(x1, y + 6.0f, z + 5.0f);
            m_buildings.push_back(sky);
        }
}

void Town::index() {
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (const Building& b : m_buildings) {
        lo = glm::min(lo, glm::vec2(b.lo.x, b.lo.z));
        hi = glm::max(hi, glm::vec2(b.hi.x, b.hi.z));
    }
    m_cells.clear();
    if (m_buildings.empty()) {
        m_gridW = m_gridH = 0;
        return;
    }
    m_gridLo = lo - glm::vec2(1.0f);
    m_gridW = static_cast<int>((hi.x - m_gridLo.x) / kCell) + 1;
    m_gridH = static_cast<int>((hi.y - m_gridLo.y) / kCell) + 1;
    m_cells.assign(static_cast<size_t>(m_gridW * m_gridH), {});
    for (size_t i = 0; i < m_buildings.size(); ++i) {
        const Building& b = m_buildings[i];
        const int x0 = static_cast<int>((b.lo.x - m_gridLo.x) / kCell), x1 = static_cast<int>((b.hi.x - m_gridLo.x) / kCell);
        const int z0 = static_cast<int>((b.lo.z - m_gridLo.y) / kCell), z1 = static_cast<int>((b.hi.z - m_gridLo.y) / kCell);
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) m_cells[static_cast<size_t>(z * m_gridW + x)].push_back(static_cast<int>(i));
    }
}

std::vector<int> Town::near(float x0, float z0, float x1, float z1) const {
    std::vector<int> out;
    if (m_cells.empty()) return out;
    const int cx0 = std::max(0, static_cast<int>(std::floor((std::min(x0, x1) - m_gridLo.x) / kCell)));
    const int cx1 = std::min(m_gridW - 1, static_cast<int>(std::floor((std::max(x0, x1) - m_gridLo.x) / kCell)));
    const int cz0 = std::max(0, static_cast<int>(std::floor((std::min(z0, z1) - m_gridLo.y) / kCell)));
    const int cz1 = std::min(m_gridH - 1, static_cast<int>(std::floor((std::max(z0, z1) - m_gridLo.y) / kCell)));
    for (int z = cz0; z <= cz1; ++z)
        for (int x = cx0; x <= cx1; ++x)
            for (int i : m_cells[static_cast<size_t>(z * m_gridW + x)])
                if (std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
    return out;
}

float Town::roof(float x, float z, float margin, float under) const {
    float top = -1e9f;
    for (int i : near(x - margin, z - margin, x + margin, z + margin)) {
        const Building& b = m_buildings[static_cast<size_t>(i)];
        if (b.lo.y < under && x >= b.lo.x - margin && x <= b.hi.x + margin && z >= b.lo.z - margin && z <= b.hi.z + margin) top = std::max(top, b.hi.y);
    }
    return top;
}

bool Town::touches(const glm::vec3& center, float radius, glm::vec3& normal, float& depth) const {
    bool hit = false;
    depth = 0.0f;
    for (int i : near(center.x - radius, center.z - radius, center.x + radius, center.z + radius)) {
        const Building& b = m_buildings[static_cast<size_t>(i)];
        const glm::vec3 closest = glm::clamp(center, b.lo, b.hi);
        const glm::vec3 d = center - closest;
        const float dist = glm::length(d);
        if (dist >= radius) continue;
        glm::vec3 n;
        float pen;
        if (dist > 1e-4f) {
            n = d / dist;
            pen = radius - dist;
        } else {
            // The centre is inside: out through the nearest face.
            const glm::vec3 toLo = center - b.lo, toHi = b.hi - center;
            pen = 1e9f;
            n = glm::vec3(0.0f, 1.0f, 0.0f);
            for (int a = 0; a < 3; ++a) {
                if (toLo[a] < pen) {
                    pen = toLo[a];
                    n = glm::vec3(0.0f);
                    n[a] = -1.0f;
                }
                if (toHi[a] < pen) {
                    pen = toHi[a];
                    n = glm::vec3(0.0f);
                    n[a] = 1.0f;
                }
            }
            pen += radius;
        }
        if (pen > depth) {
            depth = pen;
            normal = n;
            hit = true;
        }
    }
    return hit;
}

bool Town::blocks(const glm::vec3& from, const glm::vec3& to, float& t) const {
    float best = 2.0f;
    for (int i : near(from.x, from.z, to.x, to.z)) {
        const Building& b = m_buildings[static_cast<size_t>(i)];
        const float k = segmentBox(from, to, b.lo, b.hi);
        if (k >= 0.0f && k < best) best = k;
    }
    if (best > 1.0f) return false;
    t = best;
    return true;
}

} // namespace flying
