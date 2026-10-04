// kke_demo's open world (Kees, 2026-09-28: "this demo is a complete
// showcase of everything ... a vast world to walk around in and do
// stuff"; 2026-10-04: one open world with the yard in the middle). The
// yard's walls get a gate on each side; outside them the ground rolls on
// for 700 m each way to a ring of hills, with a mountain to the north
// west whose shoulder holds the snow field. Roads run from the gates to
// the zones (layout::kZones): the parkour park and the firing range to
// the north, the airfield to the east, the race track to the south, the
// nature park to the west and, up a trail, the snow field. Each zone
// flies a tall flag in its colour, so you can find it from far away.
//
// The terrain is one mesh (a vertex every 5 m) and one static Jolt mesh
// body; groundHeight() reads the same grid, so whatever later rounds put
// out there (trees, cars, the plane) sits on it. The world map (M, or the
// pause menu's "World map") shows the zones and where you are, and takes
// you to one.

#include "ShowcaseModule.h"
#include "Geometry.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <SDL3/SDL.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace kke_showcase {

using namespace layout;

namespace {

float smooth(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// The lie of the land before anything is levelled: long low swells, the
// mountain to the north west, the ring of hills at the edge.
float naturalHeight(float x, float z) {
    float h = 4.0f * std::sin(x * 0.013f + 0.4f) * std::cos(z * 0.011f) + 2.5f * std::sin(x * 0.031f + 1.3f) * std::sin(z * 0.027f + 0.7f) +
              1.0f * std::sin(x * 0.071f) * std::cos(z * 0.063f + 2.1f);
    const float m = glm::length(glm::vec2(x + 330.0f, z + 330.0f));
    if (m < 300.0f) h += 48.0f * (0.5f + 0.5f * std::cos(glm::pi<float>() * m / 300.0f));
    const float r = std::max(std::abs(x), std::abs(z));
    if (r > 580.0f) h += (r - 580.0f) * (r - 580.0f) / (120.0f * 120.0f) * 70.0f;
    return h;
}

// Level ground for the zones: round or a rectangle, at height h, blending
// into the land over `blend` metres.
struct Pad {
    glm::vec2 centre, half; // half.y < 0: round, radius half.x
    float h, blend;
};
const Pad kPads[] = {
    { { 0.0f, -160.0f }, { 60.0f, -1.0f }, 0.0f, 40.0f },     // parkour park
    { { 170.0f, -150.0f }, { 45.0f, -1.0f }, 0.0f, 40.0f },   // firing range
    { { 380.0f, 40.0f }, { 220.0f, 50.0f }, 0.0f, 50.0f },    // airfield
    { { 0.0f, 255.0f }, { 140.0f, 110.0f }, 0.0f, 50.0f },    // race track
    { { -300.0f, -280.0f }, { 80.0f, -1.0f }, 30.0f, 40.0f }, // snow field, on the mountain's shoulder
};

float padDistance(const Pad& p, float x, float z) {
    const glm::vec2 d(x - p.centre.x, z - p.centre.y);
    if (p.half.y < 0.0f) return glm::length(d) - p.half.x;
    return glm::length(glm::max(glm::abs(d) - p.half, glm::vec2(0.0f)));
}

// The land with the yard and the zones levelled.
float paddedHeight(float x, float z) {
    float h = naturalHeight(x, z);
    for (const Pad& p : kPads) h = glm::mix(h, p.h, 1.0f - smooth(0.0f, p.blend, padDistance(p, x, z)));
    // The yard: under its floor, then level ground out to 60 m past its walls.
    const float yard = std::max(std::abs(x), std::abs(z)) - 30.0f;
    if (yard < -0.01f) return -0.6f;
    return glm::mix(h, 0.0f, 1.0f - smooth(0.0f, 60.0f, yard));
}

struct Road {
    std::vector<glm::vec2> points;
    float width;
    bool paved; // asphalt with a centre line; else a dirt trail
};
const std::vector<Road>& roads() {
    static const std::vector<Road> r = {
        { { kGateNorth, { 0.0f, -100.0f } }, 7.0f, true },                                                 // to the parkour park
        { { { 0.0f, -70.0f }, { 60.0f, -95.0f }, { 128.0f, -126.0f } }, 6.0f, true },                     // to the firing range
        { { kGateEast, { 120.0f, -18.0f }, { 160.0f, 5.0f }, { 172.0f, 22.0f } }, 7.0f, true },           // to the airfield
        { { kGateSouth, { 0.0f, 145.0f } }, 7.0f, true },                                                 // to the race track
        { { kGateWest, { -80.0f, 4.0f }, { -135.0f, 8.0f } }, 7.0f, true },                               // to the nature park
        { { { -135.0f, 8.0f }, { -170.0f, -60.0f }, { -215.0f, -150.0f }, { -252.0f, -218.0f } }, 4.0f, false }, // up to the snow
    };
    return r;
}

// Nearest point on a road's centre line (and how far).
glm::vec2 nearestOnRoad(const Road& road, const glm::vec2& p, float& dist) {
    glm::vec2 best = road.points[0];
    dist = 1e9f;
    for (size_t k = 0; k + 1 < road.points.size(); ++k) {
        const glm::vec2 a = road.points[k], b = road.points[k + 1];
        const float t = std::clamp(glm::dot(p - a, b - a) / std::max(glm::dot(b - a, b - a), 1e-6f), 0.0f, 1.0f);
        const glm::vec2 q = a + (b - a) * t;
        const float d = glm::length(p - q);
        if (d < dist) {
            dist = d;
            best = q;
        }
    }
    return best;
}

// The final ground: roads cut level across (their centre line follows
// the land) so a car sits flat on them.
float worldHeight(float x, float z) {
    float h = paddedHeight(x, z);
    if (std::max(std::abs(x), std::abs(z)) < 29.99f) return h; // inside the yard
    for (const Road& r : roads()) {
        float d = 0.0f;
        const glm::vec2 q = nearestOnRoad(r, { x, z }, d);
        if (d > r.width * 0.5f + 8.0f) continue;
        const float w = 1.0f - smooth(r.width * 0.5f + 1.0f, r.width * 0.5f + 8.0f, d);
        h = glm::mix(h, paddedHeight(q.x, q.y), w);
    }
    return h;
}

// A quad strip along a road, a hand's width above the ground.
void appendRibbon(const std::vector<glm::vec2>& pts, float width, float lift, const glm::vec3& color, const std::function<float(float, float)>& ground,
                  std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    std::vector<glm::vec2> s; // resampled every 4 m
    for (size_t k = 0; k + 1 < pts.size(); ++k) {
        const glm::vec2 a = pts[k], b = pts[k + 1];
        const int n = std::max(1, static_cast<int>(glm::length(b - a) / 4.0f));
        for (int i = 0; i < n; ++i) s.push_back(a + (b - a) * (static_cast<float>(i) / static_cast<float>(n)));
    }
    s.push_back(pts.back());
    const uint32_t base = static_cast<uint32_t>(v.size());
    for (size_t k = 0; k < s.size(); ++k) {
        const glm::vec2 dir = glm::normalize(s[std::min(k + 1, s.size() - 1)] - s[k == 0 ? 0 : k - 1]);
        const glm::vec2 side(-dir.y, dir.x);
        for (int e = -1; e <= 1; e += 2) {
            const glm::vec2 p = s[k] + side * (width * 0.5f * static_cast<float>(e));
            v.push_back({ glm::vec3(p.x, ground(p.x, p.y) + lift, p.y), color, glm::vec3(0, 1, 0), glm::vec2(0.0f) });
        }
    }
    for (uint32_t k = 0; k + 1 < s.size(); ++k) {
        const uint32_t a = base + k * 2;
        // Facing up whichever way the strip runs.
        const glm::vec3 e1 = v[a + 2].position - v[a].position, e2 = v[a + 1].position - v[a].position;
        if (glm::cross(e1, e2).y > 0.0f) idx.insert(idx.end(), { a, a + 2, a + 1, a + 1, a + 2, a + 3 });
        else idx.insert(idx.end(), { a, a + 1, a + 2, a + 1, a + 3, a + 2 });
    }
}

// The map shows x -400..600, z -500..500 (where the zones are) in 420 dp.
constexpr float kMapScale = 0.42f; // dp per metre
constexpr glm::vec2 kMapMin(-400.0f, -500.0f);
glm::vec2 onMap(float x, float z) { return (glm::vec2(x, z) - kMapMin) * kMapScale; }

} // namespace

float ShowcaseModule::groundHeight(float x, float z) const {
    if (m_terrainN < 2) return 0.0f;
    if (std::max(std::abs(x), std::abs(z)) < 30.0f) return 0.0f; // the yard's floor
    const float gx = std::clamp((x + kWorldHalf) / kTerrainStep, 0.0f, static_cast<float>(m_terrainN - 1) - 0.001f);
    const float gz = std::clamp((z + kWorldHalf) / kTerrainStep, 0.0f, static_cast<float>(m_terrainN - 1) - 0.001f);
    const int ix = static_cast<int>(gx), iz = static_cast<int>(gz);
    const float fx = gx - static_cast<float>(ix), fz = gz - static_cast<float>(iz);
    auto at = [&](int i, int k) { return m_terrain[static_cast<size_t>(k * m_terrainN + i)]; };
    // The mesh's triangles split each cell along the same diagonal.
    if (fx + fz <= 1.0f) return at(ix, iz) + (at(ix + 1, iz) - at(ix, iz)) * fx + (at(ix, iz + 1) - at(ix, iz)) * fz;
    return at(ix + 1, iz + 1) + (at(ix, iz + 1) - at(ix + 1, iz + 1)) * (1.0f - fx) + (at(ix + 1, iz) - at(ix + 1, iz + 1)) * (1.0f - fz);
}

int ShowcaseModule::zoneAt(const glm::vec3& p) const {
    for (int k = 1; k < kZoneCount; ++k) // 0 is the yard: its stations say more
        if (glm::length(glm::vec2(p.x - kZones[k].centre.x, p.z - kZones[k].centre.z)) < kZones[k].radius) return k;
    return -1;
}

void ShowcaseModule::buildWorld() {
    m_world = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    const int n = static_cast<int>(2.0f * kWorldHalf / kTerrainStep) + 1;
    m_terrainN = n;
    m_terrain.assign(static_cast<size_t>(n * n), 0.0f);
    for (int k = 0; k < n; ++k)
        for (int i = 0; i < n; ++i)
            m_terrain[static_cast<size_t>(k * n + i)] = worldHeight(-kWorldHalf + kTerrainStep * static_cast<float>(i), -kWorldHalf + kTerrainStep * static_cast<float>(k));

    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    v.reserve(static_cast<size_t>(n * n) + 4096);
    idx.reserve(static_cast<size_t>((n - 1) * (n - 1) * 6) + 8192);
    auto h = [&](int i, int k) { return m_terrain[static_cast<size_t>(std::clamp(k, 0, n - 1) * n + std::clamp(i, 0, n - 1))]; };
    for (int k = 0; k < n; ++k)
        for (int i = 0; i < n; ++i) {
            const float x = -kWorldHalf + kTerrainStep * static_cast<float>(i), z = -kWorldHalf + kTerrainStep * static_cast<float>(k);
            const float y = h(i, k);
            const glm::vec3 normal = glm::normalize(glm::vec3(h(i - 1, k) - h(i + 1, k), 2.0f * kTerrainStep, h(i, k - 1) - h(i, k + 1)));
            // Grass with a little variation, rock where it's steep, snow
            // high on the mountain and on the hills round the edge.
            const float vary = 0.5f + 0.5f * std::sin(x * 0.05f + std::cos(z * 0.043f) * 2.0f) * std::cos(z * 0.061f);
            glm::vec3 c = glm::mix(glm::vec3(0.27f, 0.45f, 0.18f), glm::vec3(0.38f, 0.52f, 0.22f), vary);
            c = glm::mix(c, glm::vec3(0.42f, 0.4f, 0.36f), smooth(0.12f, 0.3f, 1.0f - normal.y));
            const bool northWest = x < -150.0f && z < -120.0f;
            c = glm::mix(c, glm::vec3(0.92f, 0.94f, 0.97f), smooth(northWest ? 22.0f : 48.0f, northWest ? 27.0f : 55.0f, y));
            // The firing range is dirt, the parkour park concrete.
            c = glm::mix(c, glm::vec3(0.52f, 0.43f, 0.3f), 1.0f - smooth(0.0f, 8.0f, padDistance(kPads[1], x, z)));
            c = glm::mix(c, glm::vec3(0.72f, 0.71f, 0.68f), 1.0f - smooth(0.0f, 8.0f, padDistance(kPads[0], x, z)));
            v.push_back({ glm::vec3(x, y, z), c, normal, glm::vec2(0.0f) });
        }
    for (int k = 0; k + 1 < n; ++k)
        for (int i = 0; i + 1 < n; ++i) {
            const uint32_t a = static_cast<uint32_t>(k * n + i), b = a + 1, c = a + static_cast<uint32_t>(n), d = c + 1;
            // Counter-clockwise seen from above (+Y), split a-d... along b-c
            // (groundHeight interpolates the same triangles).
            idx.insert(idx.end(), { a, c, b, b, c, d });
        }
    // The collider: the same triangles.
    {
        kke::RigidWorld::BodyDesc d;
        d.shape = kke::RigidWorld::Shape::Mesh;
        d.motion = kke::RigidWorld::Motion::Static;
        d.points.reserve(v.size());
        for (const kke::Vertex& p : v) d.points.push_back(p.position);
        d.indices = idx;
        d.friction = 0.8f;
        if (m_rigid->world().add(d) == kke::RigidWorld::kNoBody) kke::log::get(name())->warn("world: the terrain collider could not be made");
    }
    // Invisible walls at the very edge, past the ring of hills.
    for (int s = 0; s < 4; ++s) {
        kke::RigidWorld::BodyDesc d;
        d.motion = kke::RigidWorld::Motion::Static;
        const float e = kWorldHalf - 2.0f;
        d.halfExtents = s < 2 ? glm::vec3(kWorldHalf, 150.0f, 1.0f) : glm::vec3(1.0f, 150.0f, kWorldHalf);
        d.position = s == 0 ? glm::vec3(0, 100, -e) : s == 1 ? glm::vec3(0, 100, e) : s == 2 ? glm::vec3(-e, 100, 0) : glm::vec3(e, 100, 0);
        m_rigid->world().add(d);
    }

    // Roads, with a dashed centre line on the paved ones.
    const auto ground = [this](float x, float z) { return groundHeight(x, z); };
    for (const Road& r : roads()) {
        const glm::vec3 surface = r.paved ? glm::vec3(0.38f, 0.38f, 0.4f) : glm::vec3(0.55f, 0.45f, 0.33f);
        appendRibbon(r.points, r.width, 0.06f, surface, ground, v, idx);
        if (!r.paved) continue;
        for (size_t k = 0; k + 1 < r.points.size(); ++k) {
            const glm::vec2 a = r.points[k], b = r.points[k + 1];
            const float len = glm::length(b - a);
            const glm::vec2 dir = (b - a) / len;
            for (float t = 2.0f; t + 3.0f < len; t += 8.0f)
                appendRibbon({ a + dir * t, a + dir * (t + 3.0f) }, 0.18f, 0.08f, glm::vec3(0.9f, 0.88f, 0.75f), ground, v, idx);
        }
    }
    // The runway (the airfield is level: y = 0) with its markings.
    {
        const float z = 40.0f, x0 = 185.0f, x1 = 575.0f;
        appendBox(glm::translate(glm::mat4(1.0f), glm::vec3((x0 + x1) * 0.5f, 0.0f, z)), glm::vec3((x1 - x0) * 0.5f, 0.07f, 15.0f), glm::vec3(0.25f, 0.25f, 0.27f), v, idx);
        for (float x = x0 + 40.0f; x < x1 - 40.0f; x += 25.0f)
            appendBox(glm::translate(glm::mat4(1.0f), glm::vec3(x + 6.0f, 0.075f, z)), glm::vec3(6.0f, 0.005f, 0.4f), glm::vec3(0.92f), v, idx);
        for (float end : { x0 + 8.0f, x1 - 8.0f })
            for (int s = -5; s <= 5; ++s)
                if (s != 0) appendBox(glm::translate(glm::mat4(1.0f), glm::vec3(end, 0.075f, z + static_cast<float>(s) * 2.4f)), glm::vec3(5.0f, 0.005f, 0.6f), glm::vec3(0.92f), v, idx);
    }
    // A flag over each zone, and at each gate a post with a board for
    // every zone down that road, in the zone's colour.
    auto pole = [&](const glm::vec3& at, float height, const glm::vec3& color) {
        appendCylinder(glm::translate(glm::mat4(1.0f), at + glm::vec3(0, height * 0.5f, 0)), 0.12f, height * 0.5f, glm::vec3(0.8f), v, idx);
        appendBox(glm::translate(glm::mat4(1.0f), at + glm::vec3(1.6f, height - 1.1f, 0)), glm::vec3(1.5f, 1.0f, 0.04f), color, v, idx);
        kke::RigidWorld::BodyDesc d;
        d.motion = kke::RigidWorld::Motion::Static;
        d.halfExtents = glm::vec3(0.15f, height * 0.5f, 0.15f);
        d.position = at + glm::vec3(0, height * 0.5f, 0);
        m_rigid->world().add(d);
    };
    for (int k = 1; k < kZoneCount; ++k) {
        const glm::vec3 c = kZones[k].centre;
        pole(glm::vec3(c.x, groundHeight(c.x, c.z), c.z), 14.0f, kZones[k].color);
    }
    struct Sign { glm::vec2 at; std::vector<int> zones; float yaw; };
    const Sign signs[] = {
        { kGateNorth + glm::vec2(5.5f, -4.0f), { 1, 2 }, 0.0f },
        { kGateEast + glm::vec2(4.0f, 5.5f), { 3 }, 90.0f },
        { kGateSouth + glm::vec2(-5.5f, 4.0f), { 4 }, 180.0f },
        { kGateWest + glm::vec2(-4.0f, -5.5f), { 5, 6 }, -90.0f },
    };
    for (const Sign& s : signs) {
        const glm::vec3 base(s.at.x, groundHeight(s.at.x, s.at.y), s.at.y);
        appendBox(glm::translate(glm::mat4(1.0f), base + glm::vec3(0, 1.4f, 0)), glm::vec3(0.08f, 1.4f, 0.08f), glm::vec3(0.45f, 0.33f, 0.22f), v, idx);
        for (size_t b = 0; b < s.zones.size(); ++b) {
            // Each board points down the road (its long side along the way to go).
            const glm::mat4 m = glm::rotate(glm::translate(glm::mat4(1.0f), base + glm::vec3(0, 2.5f - 0.45f * static_cast<float>(b), 0)),
                                            glm::radians(90.0f - s.yaw), glm::vec3(0, 1, 0));
            appendBox(m * glm::translate(glm::mat4(1.0f), glm::vec3(0.55f, 0.0f, 0.0f)), glm::vec3(0.6f, 0.17f, 0.03f), kZones[s.zones[b]].color, v, idx);
        }
        kke::RigidWorld::BodyDesc d;
        d.motion = kke::RigidWorld::Motion::Static;
        d.halfExtents = glm::vec3(0.1f, 1.4f, 0.1f);
        d.position = base + glm::vec3(0, 1.4f, 0);
        m_rigid->world().add(d);
    }
    m_world->upload(v, idx);
    kke::log::get(name())->info("world: {} x {} m of terrain, {} triangles, {} roads, {} zones", 2.0f * kWorldHalf, 2.0f * kWorldHalf, idx.size() / 3,
                                roads().size(), kZoneCount);
}

void ShowcaseModule::travelTo(int zone) {
    if (zone < 0 || zone >= kZoneCount) return;
    const Zone& z = kZones[zone];
    dropHeld();
    openMap(false);
    const glm::vec3 at(z.arrive.x, (zone == 0 ? 0.0f : groundHeight(z.arrive.x, z.arrive.z)) + 0.05f, z.arrive.z);
    m_loco->teleport(at);
    const float yaw = glm::radians(z.arriveYaw);
    m_loco->setFacing(glm::vec3(std::sin(yaw), 0.0f, -std::cos(yaw)));
    m_rig.yaw = z.arriveYaw;
    m_ik.reset();
    std::string name = z.name;
    for (size_t k = 1; k < name.size(); ++k)
        if (name[k - 1] != ' ') name[k] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[k])));
    toast("Welcome to " + name);
    kke::log::get(this->name())->info("world: travelled to {} at {:.1f} {:.1f} {:.1f}", z.name, at.x, at.y, at.z);
}

// --------------------------------------------------------------- the map

void ShowcaseModule::buildMapScreen() {
    kke::InputMap& in = m_input->map(0);
    using IM = kke::InputModule;
    in.defineAction({ "map.open", "World map", "Showcase", "mapclosed" });
    in.addBinding(IM::bind("map.open", IM::key(SDL_SCANCODE_M)));
    in.defineAction({ "map.up", "Map: up", "Showcase", "maplist" });
    in.defineAction({ "map.down", "Map: down", "Showcase", "maplist" });
    in.defineAction({ "map.go", "Map: go there", "Showcase", "maplist" });
    in.defineAction({ "map.close", "Map: close", "Showcase", "maplist" });
    for (SDL_Scancode k : { SDL_SCANCODE_W, SDL_SCANCODE_UP }) in.addBinding(IM::bind("map.up", IM::key(k)));
    for (SDL_Scancode k : { SDL_SCANCODE_S, SDL_SCANCODE_DOWN }) in.addBinding(IM::bind("map.down", IM::key(k)));
    in.addBinding(IM::bind("map.up", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_UP)));
    in.addBinding(IM::bind("map.down", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN)));
    in.addBinding(IM::bind("map.up", IM::padAxis(SDL_GAMEPAD_AXIS_LEFTY, -1)));
    in.addBinding(IM::bind("map.down", IM::padAxis(SDL_GAMEPAD_AXIS_LEFTY, 1)));
    for (SDL_Scancode k : { SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER }) in.addBinding(IM::bind("map.go", IM::key(k)));
    in.addBinding(IM::bind("map.go", IM::pad(SDL_GAMEPAD_BUTTON_SOUTH)));
    for (SDL_Scancode k : { SDL_SCANCODE_M, SDL_SCANCODE_BACKSPACE }) in.addBinding(IM::bind("map.close", IM::key(k)));
    in.addBinding(IM::bind("map.close", IM::pad(SDL_GAMEPAD_BUTTON_EAST)));
    in.setContextEnabled("maplist", false);

    if (!m_ui || !m_ui->context()) return;
    Rml::Context* ctx = m_ui->context();
    Rml::DataModelConstructor c = ctx->CreateDataModel("worldmap");
    if (!c) return;
    if (auto s = c.RegisterStruct<MapZone>()) {
        s.RegisterMember("name", &MapZone::name);
        s.RegisterMember("text", &MapZone::text);
        s.RegisterMember("dot", &MapZone::dot);
        s.RegisterMember("label", &MapZone::label);
        s.RegisterMember("sel", &MapZone::sel);
    }
    c.RegisterArray<std::vector<MapZone>>();
    if (auto s = c.RegisterStruct<MapRoad>()) s.RegisterMember("style", &MapRoad::style);
    c.RegisterArray<std::vector<MapRoad>>();
    c.Bind("open", &m_mapOpen);
    c.Bind("zones", &m_mapZones);
    c.Bind("roads", &m_mapRoads);
    c.Bind("me", &m_mapMe);
    c.Bind("keys", &m_mapKeys);
    c.BindEventCallback("go", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) travelTo(args[0].Get<int>());
    });
    c.BindEventCallback("hover", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList& args) {
        if (args.empty() || args[0].Get<int>() == m_mapSel) return;
        m_mapSel = args[0].Get<int>();
        for (size_t k = 0; k < m_mapZones.size(); ++k) m_mapZones[k].sel = static_cast<int>(k) == m_mapSel;
        h.DirtyVariable("zones");
    });
    c.BindEventCallback("close", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { openMap(false); });
    m_mapModel = c.GetModelHandle();
    // What doesn't move: the zones' circles and the roads.
    for (int k = 0; k < kZoneCount; ++k) {
        const Zone& z = kZones[k];
        MapZone m;
        m.name = z.name;
        m.text = z.text;
        const float r = std::clamp(z.radius, 12.0f, 70.0f) * kMapScale; // the big ones capped: a mark, not the whole area
        char buf[200];
        const glm::vec3 col = z.color * 255.0f;
        const glm::vec2 c = onMap(z.centre.x, z.centre.z);
        std::snprintf(buf, sizeof(buf), "left: %.0fdp; top: %.0fdp; width: %.0fdp; height: %.0fdp; border-radius: %.0fdp; background-color: rgba(%d,%d,%d,110); border-color: rgb(%d,%d,%d);",
                      static_cast<double>(c.x - r), static_cast<double>(c.y - r),
                      static_cast<double>(2.0f * r), static_cast<double>(2.0f * r), static_cast<double>(r), static_cast<int>(col.r), static_cast<int>(col.g),
                      static_cast<int>(col.b), static_cast<int>(col.r), static_cast<int>(col.g), static_cast<int>(col.b));
        m.dot = buf;
        // Under its circle; the parkour park's above (the firing range is next to it).
        std::snprintf(buf, sizeof(buf), "left: %.0fdp; top: %.0fdp;", static_cast<double>(c.x - 60.0f), static_cast<double>(k == 1 ? c.y - r - 16.0f : c.y + r + 2.0f));
        m.label = buf;
        m_mapZones.push_back(std::move(m));
    }
    for (const Road& r : roads())
        for (size_t k = 0; k + 1 < r.points.size(); ++k) {
            const glm::vec2 a = onMap(r.points[k].x, r.points[k].y), b = onMap(r.points[k + 1].x, r.points[k + 1].y);
            char buf[200];
            std::snprintf(buf, sizeof(buf), "left: %.1fdp; top: %.1fdp; width: %.1fdp; transform: rotate(%.1fdeg);", static_cast<double>(a.x),
                          static_cast<double>(a.y), static_cast<double>(glm::length(b - a)),
                          static_cast<double>(glm::degrees(std::atan2(b.y - a.y, b.x - a.x))));
            m_mapRoads.push_back({ buf });
        }
    const char* base = SDL_GetBasePath();
    const std::string root = std::string(base ? base : "") + "ui/";
    m_mapDoc = ctx->LoadDocument(root + "showcase_map.rml");
    if (!m_mapDoc) {
        kke::log::get(name())->warn("world map: could not load {}showcase_map.rml", root);
        return;
    }
    m_mapDoc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
}

void ShowcaseModule::openMap(bool open) {
    if (open == m_mapOpen) return;
    if (open) {
        openInventory(false);
        openSpawnMenu(false);
    }
    m_mapOpen = open;
    kke::InputMap& in = m_input->map(0);
    in.setContextEnabled("maplist", open);
    in.setContextEnabled("mapclosed", !open);
    if (open) {
        m_mapRecapture = m_captured;
        if (m_captured) setCaptured(false);
        // The zone you're in (or the yard) is selected first.
        const int here = zoneAt(m_rigid->world().characterPosition(m_player));
        m_mapSel = here < 0 ? 0 : here;
        for (size_t k = 0; k < m_mapZones.size(); ++k) m_mapZones[k].sel = static_cast<int>(k) == m_mapSel;
        const kke::InputModule* input = m_app->getModule<kke::InputModule>();
        const std::string keys = "{map.up} {map.down} choose   {map.go} go there   {map.close} close";
        m_mapKeys = input ? input->promptText(keys) : keys;
    } else if (m_mapRecapture) {
        setCaptured(true);
        m_swallowFire = true;
    }
    m_mapHeldDir = 0;
    if (m_mapModel) m_mapModel.DirtyAllVariables();
}

void ShowcaseModule::updateMap(float dt) {
    kke::InputMap& in = m_input->map(0);
    if (m_menuOpen) {
        openMap(false);
        in.setContextEnabled("mapclosed", false);
        return;
    }
    if (!m_mapOpen) {
        in.setContextEnabled("mapclosed", !m_spawnOpen && !m_invOpen);
        if (in.pressed("map.open")) openMap(true);
        return;
    }
    if (in.pressed("map.close")) {
        openMap(false);
        return;
    }
    const int dir = in.held("map.down") ? 1 : in.held("map.up") ? -1 : 0;
    int step = 0;
    if (dir != m_mapHeldDir) {
        m_mapHeldDir = dir;
        m_mapRepeat = 0.4f;
        step = dir;
    } else if (dir != 0 && (m_mapRepeat -= dt) <= 0.0f) {
        m_mapRepeat = 0.15f;
        step = dir;
    }
    if (step != 0) {
        m_mapSel = (m_mapSel + step + kZoneCount) % kZoneCount;
        for (size_t k = 0; k < m_mapZones.size(); ++k) m_mapZones[k].sel = static_cast<int>(k) == m_mapSel;
        if (m_mapModel) m_mapModel.DirtyVariable("zones");
    }
    if (in.pressed("map.go")) {
        travelTo(m_mapSel);
        return;
    }
    // You: an arrow the way you face.
    const glm::vec3 p = m_rigid->world().characterPosition(m_player);
    char buf[160];
    const glm::vec2 me = glm::clamp(onMap(p.x, p.z), glm::vec2(0.0f), glm::vec2(420.0f));
    std::snprintf(buf, sizeof(buf), "left: %.0fdp; top: %.0fdp; transform: rotate(%.0fdeg);", static_cast<double>(me.x - 8.0f), static_cast<double>(me.y - 8.0f),
                  static_cast<double>(m_facing));
    if (m_mapMe != buf) {
        m_mapMe = buf;
        if (m_mapModel) m_mapModel.DirtyVariable("me");
    }
}

// KKE_DEMO_WORLD=1: the map for a few seconds, then a visit to every
// zone in turn, logging the ground there (screenshots, checks).
void ShowcaseModule::updateWorldDemo(float dt) {
    const float before = m_demoWorld;
    m_demoWorld += dt;
    const float t = m_demoWorld;
    auto at = [&](float mark) { return before < mark && t >= mark; };
    if (at(0.5f)) openMap(true);
    for (int k = 1; k < kZoneCount; ++k) {
        const float visit = 8.0f + 5.0f * static_cast<float>(k - 1);
        if (at(visit)) {
            travelTo(k);
            m_rig.pitch = -8.0f;
        }
        if (at(visit + 2.0f)) {
            const glm::vec3 p = m_rigid->world().characterPosition(m_player);
            kke::log::get(name())->info("world demo: at {} ({:.1f} {:.1f} {:.1f}), ground {:.2f}, {}, zone here: {}", kZones[k].name, p.x, p.y, p.z,
                                        groundHeight(p.x, p.z), m_rigid->world().characterOnGround(m_player) ? "standing" : "not on the ground",
                                        zoneAt(p) >= 0 ? kZones[zoneAt(p)].name : "none");
        }
    }
    if (at(8.0f + 5.0f * static_cast<float>(kZoneCount - 1))) {
        travelTo(0);
        m_demoWorld = -1.0f;
    }
}

} // namespace kke_showcase
