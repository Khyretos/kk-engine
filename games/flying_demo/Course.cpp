#include "Course.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace flying {

namespace {
// A small hash for the seed's phases (the same on every machine and compiler).
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
float smooth(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
} // namespace

Island::Island(uint32_t seed) : m_seed(seed), m_map(static_cast<Map>(std::min(seed >> 24, 2u))) {
    if (m_map == Map::Canyon) m_runway.start.y = kPlateau; // the airfield is up on the plateau
    uint32_t s = seed * 2654435761u + 17u;
    for (float& p : m_phase) p = unit(s) * glm::two_pi<float>();
    // The mountain: somewhere on a ring around the middle, clear of the runway.
    const float a = unit(s) * glm::two_pi<float>();
    const float r = 650.0f + unit(s) * 250.0f;
    m_peak = glm::vec2(std::cos(a) * r, std::sin(a) * r);
    if (std::abs(m_peak.x) < 300.0f) m_peak.x = m_peak.x < 0.0f ? -300.0f : 300.0f; // not at the runway's ends
    m_peakHeight = 220.0f + unit(s) * 90.0f;
}

float Island::hills(float x, float z) const {
    float h = 55.0f * (0.5f + 0.5f * std::sin(x * 0.0041f + m_phase[0]) * std::cos(z * 0.0047f + m_phase[1]));
    h += 28.0f * std::sin(x * 0.0113f + m_phase[2]) * std::sin(z * 0.0097f + m_phase[3]);
    h += 9.0f * std::sin(x * 0.031f + m_phase[4]) * std::cos(z * 0.027f + m_phase[5]);
    const glm::vec2 d = glm::vec2(x, z) - m_peak;
    h += m_peakHeight * std::exp(-glm::dot(d, d) / (260.0f * 260.0f));
    return h;
}

float Island::landRadius() const { return m_map == Map::Canyon ? 1850.0f : m_map == Map::City ? 1950.0f : kRadius; }

float Island::terrain(float x, float z) const {
    switch (m_map) {
    case Map::Canyon: return canyonTerrain(x, z);
    case Map::City: return cityTerrain(x, z);
    case Map::Island: break;
    }
    return islandTerrain(x, z);
}

float Island::islandTerrain(float x, float z) const {
    const float r = std::sqrt(x * x + z * z) / kRadius;
    // The coast: land fades into a sea floor 30 m down.
    const float land = 1.0f - smooth(0.72f, 1.0f, r);
    const float h = hills(x, z) * land + (land - 1.0f) * 30.0f + 4.0f * land;
    return flattenRunway(x, z, h);
}

// A plateau with a gorge winding round the middle: two steps down each
// side (a ledge halfway), a sandy floor, cliffs into the sea all round.
float Island::canyonTerrain(float x, float z) const {
    const float plateau = kPlateau + 12.0f * std::sin(x * 0.004f + m_phase[3]) * std::cos(z * 0.0037f + m_phase[4]) +
                          5.0f * std::sin(x * 0.013f + m_phase[5]) * std::sin(z * 0.011f + m_phase[0]);
    const float hw = gorgeHalfWidth(std::atan2(z, x));
    const float d = gorgeDistance(x, z);
    const float wall = 0.45f * smooth(hw, hw + 14.0f, d) + 0.55f * smooth(hw + 26.0f, hw + 42.0f, d);
    float h = kFloor + (plateau - kFloor) * wall;
    const float land = 1.0f - smooth(1700.0f, 1850.0f, std::sqrt(x * x + z * z));
    h = h * land + (land - 1.0f) * 30.0f;
    return flattenRunway(x, z, h);
}

// Flat ground for the towers, a beach into the sea far out.
float Island::cityTerrain(float x, float z) const {
    const float land = 1.0f - smooth(1800.0f, 1950.0f, std::sqrt(x * x + z * z));
    return flattenRunway(x, z, kCityGround * land + (land - 1.0f) * 30.0f);
}

float Island::gorgeRadius(float a) const {
    return 1050.0f + 160.0f * std::sin(3.0f * a + m_phase[0]) + 50.0f * std::sin(5.0f * a + m_phase[1]);
}

float Island::gorgeHalfWidth(float a) const { return 68.0f + 24.0f * std::sin(4.0f * a + m_phase[2]); }

float Island::gorgeDistance(float x, float z) const {
    const float a = std::atan2(z, x);
    const float r = std::sqrt(x * x + z * z);
    const float rc = gorgeRadius(a);
    const float slope = (480.0f * std::cos(3.0f * a + m_phase[0]) + 250.0f * std::cos(5.0f * a + m_phase[1])) / rc; // dR/da over R
    // Radially off the line, times how much the line leans away from round.
    return std::abs(r - rc) / std::sqrt(1.0f + slope * slope);
}

glm::vec3 Island::gorgePoint(float a) const {
    const float r = gorgeRadius(a);
    return { std::cos(a) * r, kFloor, std::sin(a) * r };
}

glm::vec3 Island::gorgeTangent(float a) const {
    const float r = gorgeRadius(a);
    const float dr = 480.0f * std::cos(3.0f * a + m_phase[0]) + 250.0f * std::cos(5.0f * a + m_phase[1]);
    return glm::normalize(glm::vec3(dr * std::cos(a) - r * std::sin(a), 0.0f, dr * std::sin(a) + r * std::cos(a)));
}

float Island::flattenRunway(float x, float z, float h) const {
    // The runway and its apron: flattened, with a gentle bank to the land.
    const Runway& w = m_runway;
    const float dx = std::max(0.0f, std::abs(x - w.start.x) - w.width * 0.5f);
    const float along = w.start.z - z; // 0..length along the strip
    const float dz = std::max({ 0.0f, -along, along - w.length });
    const float away = std::sqrt(dx * dx + dz * dz);
    const float flat = 1.0f - smooth(20.0f, 140.0f, away);
    return h + (w.height() - h) * flat;
}

float Island::surface(float x, float z) const { return std::max(terrain(x, z), kSea); }

bool Island::onRunway(float x, float z, float margin) const {
    const Runway& w = m_runway;
    const float along = w.start.z - z;
    return std::abs(x - w.start.x) <= w.width * 0.5f + margin && along >= -margin && along <= w.length + margin;
}

Ground Island::ground() const {
    Ground g;
    g.height = [this](float x, float z) { return surface(x, z); };
    g.landable = [this](float x, float z) { return onRunway(x, z, 12.0f); };
    return g;
}

// Canyon: the rings go down the gorge, low between its walls, each facing
// along it. More of them than on the island: on the bends the straight
// line from one to the next has to stay inside the gorge.
std::vector<Ring> Island::gorgeRings(int count, float radius) const {
    std::vector<Ring> out;
    count = std::max(count, 20);
    uint32_t s = m_seed * 747796405u + 2891336453u;
    const float start = unit(s) * glm::two_pi<float>();
    const float dir = unit(s) < 0.5f ? 1.0f : -1.0f;
    for (int i = 0; i < count; ++i) {
        const float a = start + dir * static_cast<float>(i) / static_cast<float>(count) * glm::two_pi<float>();
        glm::vec3 p = gorgePoint(a);
        p.y = kFloor + 26.0f + radius + 22.0f * (0.5f + 0.5f * std::sin(3.0f * a + m_phase[2]));
        out.push_back({ p, gorgeTangent(a) * dir, radius });
    }
    return out;
}

std::vector<Ring> Island::rings(int count, float radius, float clearance) const {
    if (m_map == Map::Canyon) return gorgeRings(count, radius);
    std::vector<Ring> out;
    count = std::max(count, 3);
    uint32_t s = m_seed * 747796405u + 2891336453u;
    const float start = unit(s) * glm::two_pi<float>();
    const bool clockwise = unit(s) < 0.5f;
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count);
        const float a = start + (clockwise ? -1.0f : 1.0f) * t * glm::two_pi<float>();
        // Wander in and out: over the sea, across the hills, by the mountain.
        const float r = 520.0f + 480.0f * (0.5f + 0.5f * std::sin(a * 2.0f + m_phase[0])) + (unit(s) - 0.5f) * 160.0f;
        glm::vec3 p(std::cos(a) * r, 0.0f, std::sin(a) * r);
        // Every few rings a low one (a valley, a sea skim), the others higher.
        const float low = (i % 4 == 2) ? 0.0f : 1.0f;
        const float ground = surface(p.x, p.z);
        // What's around matters too: the ring has to be reachable, not in a hollow.
        float around = ground;
        for (int k = 0; k < 8; ++k) {
            const float b = static_cast<float>(k) * glm::quarter_pi<float>();
            around = std::max(around, surface(p.x + std::cos(b) * 90.0f, p.z + std::sin(b) * 90.0f));
        }
        p.y = std::max(ground + clearance + radius, around + 20.0f + radius) + low * (30.0f + unit(s) * 110.0f);
        if (m_map == Map::City) p.y = std::min(p.y, kCityGround + 90.0f + radius); // down among the towers
        out.push_back({ p, glm::vec3(0.0f, 0.0f, -1.0f), radius });
    }
    // No ring much higher than the one before it or after it (a climb or
    // dive of more than ~12 degrees between them): the lower one rises.
    constexpr float kSlope = 0.22f;
    for (int pass = 0; pass < 3; ++pass)
        for (int i = 0; i < 2 * count; ++i) {
            Ring& a = out[static_cast<size_t>(i % count)];
            Ring& b = out[static_cast<size_t>((i + 1) % count)];
            const float d = glm::length(glm::vec2(a.center.x - b.center.x, a.center.z - b.center.z));
            a.center.y = std::max(a.center.y, b.center.y - kSlope * d);
            b.center.y = std::max(b.center.y, a.center.y - kSlope * d);
        }
    for (int i = 0; i < count; ++i) {
        const Ring& prev = out[static_cast<size_t>((i + count - 1) % count)];
        const Ring& next = out[static_cast<size_t>((i + 1) % count)];
        // Facing mostly the way you arrive from the last ring, turned a
        // little toward the next: flying the line through them works.
        const glm::vec3 here = out[static_cast<size_t>(i)].center;
        // (The Mega City: straight down the avenue from the last ring, the
        // only way in that isn't through a tower.)
        const float toNext = m_map == Map::City ? 0.0f : 0.3f;
        glm::vec3 n = glm::normalize(here - prev.center) * (1.0f - toNext) + glm::normalize(next.center - here) * toNext;
        n.y *= 0.5f; // mostly level: a ring on a slope, not a wall to climb
        out[static_cast<size_t>(i)].normal = glm::normalize(n);
    }
    return out;
}

bool throughRing(const Ring& ring, const glm::vec3& from, const glm::vec3& to) {
    const float a = glm::dot(from - ring.center, ring.normal), b = glm::dot(to - ring.center, ring.normal);
    if (!(a < 0.0f && b >= 0.0f)) return false; // from the front to the back, this step
    const float t = a / (a - b);
    const glm::vec3 hit = from + (to - from) * t;
    return glm::length(hit - ring.center) <= ring.radius;
}

glm::vec3 RingPilot::aim(const Ring& ring, const glm::vec3& from) {
    constexpr float kSetup = 450.0f; // m in front of the ring to turn back from
    const glm::vec3 rel = from - ring.center;
    const float along = glm::dot(rel, ring.normal); // < 0: in front of it (the side you fly in from)
    const float aside = glm::length(rel - ring.normal * along);
    const glm::vec3 setup = ring.center - ring.normal * kSetup;
    if (m_settingUp && glm::length(from - setup) < 120.0f) m_settingUp = false;
    // (Once on the last stretch, it stays on it until it's through or past.)
    const bool linedUp = (along < -20.0f && aside < -along * 0.45f + ring.radius * 2.0f) || (m_final && along < 5.0f);
    if (!m_settingUp && !linedUp) m_settingUp = true;
    m_final = !m_settingUp && along > -160.0f;
    if (m_settingUp) return setup;
    // Pure pursuit: a point on the ring's axis, a fixed distance ahead of
    // where the plane is along it, pulls it smoothly onto the line.
    return ring.center + ring.normal * (along + std::clamp(aside * 1.5f, 70.0f, 160.0f));
}

} // namespace flying
