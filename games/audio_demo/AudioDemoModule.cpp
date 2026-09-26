#include "AudioDemoModule.h"

#include "kke/Application.h"
#include "kke/ImpactSynth.h"
#include "kke/Log.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <typeindex>

namespace kke_audio_demo {

using Mat = kke::AudioMaterialTable;

namespace {
constexpr float kEar = 1.6f;

bool envOn(const char* name) {
    const char* v = std::getenv(name);
    return v && *v && *v != '0';
}

glm::vec3 colourOf(uint32_t material) {
    switch (material) {
        case Mat::Stone: return {0.56f, 0.55f, 0.52f};
        case Mat::Wood: return {0.58f, 0.40f, 0.22f};
        case Mat::Metal: return {0.70f, 0.73f, 0.78f};
        case Mat::Glass: return {0.55f, 0.80f, 0.92f};
        case Mat::Rubber: return {0.16f, 0.16f, 0.19f};
        case Mat::Dirt: return {0.36f, 0.31f, 0.22f};
        case Mat::Plastic: return {0.85f, 0.32f, 0.30f};
        default: return {0.7f, 0.7f, 0.7f};
    }
}

// A unit cube (-0.5..0.5) with flat normals, one colour.
kke::ModelData cubeData(uint32_t material) {
    kke::ModelData d;
    kke::ModelMaterial m;
    m.name = "audio material " + std::to_string(material);
    m.baseColor = colourOf(material);
    m.metallic = material == Mat::Metal ? 0.7f : 0.0f;
    m.roughness = material == Mat::Glass || material == Mat::Metal ? 0.3f : 0.85f;
    d.materials.push_back(m);
    kke::ModelMesh mesh;
    mesh.name = "cube";
    const glm::vec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const glm::vec3& n : normals) {
        const glm::vec3 u = std::fabs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 v = glm::cross(n, u);
        const uint32_t base = uint32_t(mesh.vertices.size());
        const glm::vec2 corners[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (const glm::vec2& c : corners) {
            kke::ModelVertex vert;
            vert.position = (n + u * c.x + v * c.y) * 0.5f;
            vert.normal = n;
            vert.uv = c * 0.5f + 0.5f;
            mesh.vertices.push_back(vert);
        }
        // Counter-clockwise seen from outside: u x v = n.
        for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(base + i);
    }
    d.meshes.push_back(std::move(mesh));
    d.boundsMin = glm::vec3(-0.5f);
    d.boundsMax = glm::vec3(0.5f);
    return d;
}
} // namespace

std::vector<kke::ModuleDependency> AudioDemoModule::dependencies() const {
    return {
        {std::type_index(typeid(kke::AudioModule)), true, "it is the audio demo"},
        {std::type_index(typeid(kke::RigidBodyModule)), true, "walls, floors and crates are Jolt bodies: the sound's rays hit them"},
        {std::type_index(typeid(kke::ModelModule)), true, "draws the stations"},
        {std::type_index(typeid(kke::OrbitCameraModule)), true, "turning the camera turns your ears"},
    };
}

kke::ModelModule::ModelId AudioDemoModule::cubeModel(uint32_t material) {
    auto it = m_cubes.find(material);
    if (it != m_cubes.end()) return it->second;
    const ModelId id = m_models->add(cubeData(material), "audio_demo/cube/" + std::to_string(material));
    m_cubes[material] = id;
    return id;
}

void AudioDemoModule::box(const glm::vec3& min, const glm::vec3& max, uint32_t material, bool visible) {
    const glm::vec3 lo = glm::min(min, max), hi = glm::max(min, max);
    const glm::vec3 centre = (lo + hi) * 0.5f, size = hi - lo;
    kke::RigidWorld::BodyDesc d;
    d.shape = kke::RigidWorld::Shape::Box;
    d.motion = kke::RigidWorld::Motion::Static;
    d.halfExtents = size * 0.5f;
    d.position = centre;
    d.material = material;
    m_bodies->world().add(d);
    if (visible) m_models->spawn(cubeModel(material), glm::scale(glm::translate(glm::mat4(1.0f), centre), size));
}

void AudioDemoModule::room(const glm::vec3& c, const glm::vec3& inner, float t, uint32_t material, std::vector<Door> doors, bool ceiling) {
    const glm::vec3 h = inner * 0.5f;
    // One wall per side, cut around its doors: pieces between the doors,
    // and a lintel over each.
    for (int side = 0; side < 4; ++side) {
        const bool alongX = side < 2;                 // the +z / -z walls run along x
        const float len = (alongX ? h.x : h.z) + t;   // half length, corners included
        const float sign = (side % 2 == 0) ? 1.0f : -1.0f;
        const float at = sign * ((alongX ? h.z : h.x) + t * 0.5f);
        std::vector<std::pair<float, float>> gaps;
        for (const Door& d : doors)
            if (d.side == side) gaps.push_back({d.offset - d.width * 0.5f, d.offset + d.width * 0.5f});
        std::sort(gaps.begin(), gaps.end());
        auto piece = [&](float a, float b, float y0, float y1) {
            if (b - a < 1e-3f || y1 - y0 < 1e-3f) return;
            glm::vec3 lo, hi;
            if (alongX) {
                lo = {c.x + a, c.y + y0, c.z + at - t * 0.5f};
                hi = {c.x + b, c.y + y1, c.z + at + t * 0.5f};
            } else {
                lo = {c.x + at - t * 0.5f, c.y + y0, c.z + a};
                hi = {c.x + at + t * 0.5f, c.y + y1, c.z + b};
            }
            box(lo, hi, material);
        };
        float from = -len;
        for (size_t i = 0; i < gaps.size(); ++i) {
            piece(from, gaps[i].first, 0.0f, inner.y);
            const Door* d = nullptr;
            for (const Door& dd : doors)
                if (dd.side == side && std::fabs(dd.offset - dd.width * 0.5f - gaps[i].first) < 1e-4f) d = &dd;
            piece(gaps[i].first, gaps[i].second, d ? d->height : inner.y, inner.y);
            from = gaps[i].second;
        }
        piece(from, len, 0.0f, inner.y);
    }
    // The ceiling blocks sound but isn't drawn: the camera looks in from above.
    if (ceiling) box({c.x - h.x - t, c.y + inner.y, c.z - h.z - t}, {c.x + h.x + t, c.y + inner.y + t, c.z + h.z + t}, material, false);
}

void AudioDemoModule::marker(const glm::vec3& p, uint32_t material, float size) {
    m_models->spawn(cubeModel(material), glm::scale(glm::translate(glm::mat4(1.0f), p), glm::vec3(size)));
}

void AudioDemoModule::buildWorld() {
    // The field everything stands on.
    box({-30.0f, -0.5f, -40.0f}, {350.0f, 0.0f, 40.0f}, Mat::Dirt);

    auto station = [&](Kind k, const char* title, const char* listen, glm::vec3 ears, float dist, float yaw) -> Station& {
        Station s;
        s.kind = k;
        s.title = title;
        s.listenFor = listen;
        s.ears = ears;
        s.cameraDistance = dist;
        s.cameraYaw = yaw;
        m_stations.push_back(s);
        return m_stations.back();
    };

    {
        Station& s = station(Kind::Field, "Open field",
                             "Dry: no echo at all, nothing for the sound to bounce off. The far knock is quieter and a little later.",
                             {0.0f, kEar, 0.0f}, 9.0f, 0.4f);
        s.emitters = {{{0.0f, 1.0f, 6.0f}, Mat::Wood, 1.6f, 0.2f, 0.7f, "wood, 6 m ahead"},
                      {{9.0f, 1.0f, 18.0f}, Mat::Wood, 1.6f, 1.0f, 0.7f, "wood, 20 m away"}};
    }
    {
        room({35.0f, 0.0f, 0.0f}, {5.0f, 3.0f, 5.0f}, 0.3f, Mat::Stone, {{0, 0.0f, 1.2f, 2.2f}});
        Station& s = station(Kind::StoneRoom, "Small stone room",
                             "A short, bright ring after every knock: hard walls close by.", {34.0f, kEar, -1.0f}, 9.0f, 0.8f);
        s.emitters = {{{36.2f, 1.0f, 0.8f}, Mat::Wood, 1.4f, 0.2f, 0.7f, "wood"}};
    }
    {
        room({75.0f, 0.0f, 0.0f}, {24.0f, 12.0f, 24.0f}, 0.6f, Mat::Stone, {{0, 0.0f, 2.0f, 3.0f}});
        Station& s = station(Kind::Hall, "Great hall",
                             "A long echo tail that lingers after the hit (seconds), and a gap before it: far walls.",
                             {70.0f, kEar, -4.0f}, 26.0f, 0.6f);
        s.emitters = {{{80.0f, 1.2f, 4.0f}, Mat::Metal, 2.8f, 0.2f, 0.8f, "metal"}};
        s.tourSeconds = 9.0f;
    }
    {
        room({115.0f, 0.0f, 0.0f}, {5.0f, 3.0f, 5.0f}, 0.3f, Mat::Rubber, {{0, 0.0f, 1.2f, 2.2f}});
        Station& s = station(Kind::Padded, "Padded room",
                             "The same knock as the stone room, but dead dry: soft walls soak the sound up.", {114.0f, kEar, -1.0f},
                             9.0f, 0.8f);
        s.emitters = {{{116.2f, 1.0f, 0.8f}, Mat::Wood, 1.4f, 0.2f, 0.7f, "wood"}};
    }
    {
        // A closed room whose front wall is three panels: wood, glass, stone.
        const float x = 150.0f;
        room({x, 0.0f, 0.0f}, {8.0f, 3.0f, 4.0f}, 0.3f, Mat::Stone, {{0, 0.0f, 8.6f, 3.0f}}); // front left open ...
        const float z0 = 2.0f, z1 = 2.3f;
        box({x - 4.3f, 0.0f, z0}, {x - 1.33f, 3.0f, z1}, Mat::Wood); // ... and filled with the panels
        box({x - 1.33f, 0.0f, z0}, {x + 1.33f, 3.0f, z1}, Mat::Glass);
        box({x + 1.33f, 0.0f, z0}, {x + 4.3f, 3.0f, z1}, Mat::Stone);
        Station& s = station(Kind::Walls, "Through walls",
                             "The same knock behind wood (muffled), glass (clearer, louder) and stone (barely there). "
                             "Left to right.",
                             {x, kEar, -0.8f}, 11.0f, 0.0f);
        s.emitters = {{{x - 2.8f, 1.2f, 4.5f}, Mat::Metal, 3.0f, 0.2f, 0.8f, "behind wood"},
                      {{x, 1.2f, 4.5f}, Mat::Metal, 3.0f, 1.2f, 0.8f, "behind glass"},
                      {{x + 2.8f, 1.2f, 4.5f}, Mat::Metal, 3.0f, 2.2f, 0.8f, "behind stone"}};
    }
    {
        const float x = 185.0f;
        room({x, 0.0f, 0.0f}, {6.0f, 3.0f, 6.0f}, 0.3f, Mat::Stone, {{2, 1.5f, 2.0f, 2.4f}});
        Station& s = station(Kind::Door, "Round through a door",
                             "The sound is outside, behind the wall to your right. It reaches you through the door: "
                             "from the door's direction, clearer than through stone. Untick Openings to hear it through the wall instead.",
                             {x - 2.0f, kEar, -1.0f}, 11.0f, 1.2f);
        s.emitters = {{{x + 5.0f, 1.2f, -2.5f}, Mat::Metal, 1.6f, 0.2f, 0.8f, "outside"}};
    }
    {
        const float x = 220.0f;
        box({x - 3.0f, 0.0f, -3.0f}, {x + 3.0f, 0.1f, 3.0f}, Mat::Stone);
        Station& s = station(Kind::Crates, "Falling crates",
                             "Crates of wood, metal, plastic, rubber, glass and stone dropped on stone: every contact is "
                             "synthesized from both materials. Bounces get quieter.",
                             {x, kEar, -4.0f}, 9.0f, 0.0f);
        s.tourSeconds = 12.0f;
    }
    {
        const float x = 255.0f;
        const uint32_t grounds[6] = {Mat::Stone, Mat::Wood, Mat::Metal, Mat::Dirt, Mat::Glass, Mat::Rubber};
        for (int i = 0; i < 6; ++i)
            box({x - 6.0f + 2.0f * float(i), 0.0f, -0.6f}, {x - 4.0f + 2.0f * float(i), 0.03f, 0.6f}, grounds[i]);
        station(Kind::Footsteps, "Footsteps",
                "Someone walking over stone, wood, metal, dirt, glass and rubber: each ground sounds different.",
                {x, kEar, -3.5f}, 9.0f, 0.0f)
            .tourSeconds = 10.0f;
    }
    station(Kind::Circle, "Around your head",
            "A tick circling you. Headphone sound is switched on here (Binaural, or Steam Audio's HRTF when this build has it): "
            "it moves behind you, not just left and right.",
            {290.0f, kEar, 0.0f}, 8.0f, 0.0f);
    {
        const float x = 325.0f;
        box({x - 1.3f, 0.0f, -8.3f}, {x - 1.0f, 3.0f, 6.0f}, Mat::Stone);  // left wall
        box({x + 1.0f, 0.0f, -8.3f}, {x + 1.3f, 3.0f, -1.0f}, Mat::Stone); // right wall, with a gap
        box({x + 1.0f, 0.0f, 1.0f}, {x + 1.3f, 3.0f, 6.0f}, Mat::Stone);
        box({x - 1.3f, 0.0f, -8.3f}, {x + 1.3f, 3.0f, -8.0f}, Mat::Stone); // dead end behind you
        box({x - 1.3f, 3.0f, -8.3f}, {x + 1.3f, 3.3f, 6.0f}, Mat::Stone, false);
        station(Kind::Pings, "Navigation pings",
                "Q (or the button) pings around you, clockwise from ahead: close walls ping high, open ways sound open. "
                "Here: walls left and right, a dead end behind, a way out ahead and a side opening to the right.",
                {x, kEar, -5.0f}, 9.0f, 0.0f);
    }
    // Inside a room the camera looks down steeply, over the walls.
    for (Station& s : m_stations)
        if (s.kind == Kind::StoneRoom || s.kind == Kind::Hall || s.kind == Kind::Padded || s.kind == Kind::Walls || s.kind == Kind::Door ||
            s.kind == Kind::Pings)
            s.cameraPitch = -1.0f;
    for (const Station& s : m_stations)
        for (const Emitter& e : s.emitters) marker(e.position, e.material);
    // The listener, the walker and the circling tick: moved every frame.
    m_earsMarker = m_models->spawn(cubeModel(Mat::Plastic), glm::mat4(1.0f));
    m_walker = m_models->spawn(cubeModel(Mat::Default), glm::mat4(1.0f));
    m_circler = m_models->spawn(cubeModel(Mat::Plastic), glm::mat4(1.0f));
}

void AudioDemoModule::init(kke::Application& app) {
    m_app = &app;
    m_audio = app.getModule<kke::AudioModule>();
    m_bodies = app.getModule<kke::RigidBodyModule>();
    m_models = app.getModule<kke::ModelModule>();
    m_camera = app.getModule<kke::OrbitCameraModule>();
    m_camera->setDistanceLimits(2.0f, 60.0f);
    m_audio->listenerOverride = [this] { return listener(); };
    buildWorld();
    m_tour = envOn("KKE_AUDIO_DEMO_TOUR");
    m_exitAfterTour = envOn("KKE_AUDIO_DEMO_EXIT");
    kke::log::get(name())->info("Audio demo: {} stations{}", m_stations.size(), m_tour ? ", touring" : "");
    enter(0);
}

kke::Listener AudioDemoModule::listener() const {
    kke::Listener l;
    if (m_current < 0) return l;
    l.position = m_stations[size_t(m_current)].ears;
    // Where the camera looks, level: turning the view turns your head.
    const kke::Camera& cam = m_app->camera();
    glm::vec3 f = cam.target - cam.position;
    f.y = 0.0f;
    l.forward = glm::length(f) > 1e-4f ? glm::normalize(f) : glm::vec3(0, 0, 1);
    l.up = {0.0f, 1.0f, 0.0f};
    return l;
}

void AudioDemoModule::enter(int index) {
    if (m_current >= 0 && m_tour) logMeasured(m_stations[size_t(m_current)], m_measured);
    if (m_forcedBinaural) {
        m_audio->setSpatialMode(m_modeBefore);
        m_forcedBinaural = false;
    }
    for (const Crate& c : m_crates) {
        m_bodies->world().remove(c.body);
        m_models->remove(c.instance);
    }
    m_crates.clear();
    m_audio->mixer().stopAll();

    m_current = (index % int(m_stations.size()) + int(m_stations.size())) % int(m_stations.size());
    const Station& s = m_stations[size_t(m_current)];
    m_time = 0.0f;
    m_nextHit.clear();
    for (const Emitter& e : s.emitters) m_nextHit.push_back(e.phase);
    m_seen.clear();
    m_emitterOf.clear();
    m_measured = {};
    m_measured.emitterThrough.assign(s.emitters.size(), 1.0f);
    m_measured.emitterVia.assign(s.emitters.size(), false);
    m_nextCrate = 0.3f;
    m_nextStep = m_nextTick = 0.0f;
    m_nextPing = 0.8f;
    m_tourLeft = s.tourSeconds;
    if (s.kind == Kind::Circle) {
        m_modeBefore = m_audio->spatialMode();
        if (m_modeBefore == kke::SpatialMode::Stereo) {
            m_audio->setSpatialMode(kke::AudioModule::hrtfAvailable() ? kke::SpatialMode::Hrtf : kke::SpatialMode::Binaural);
            m_forcedBinaural = true;
        }
    }
    m_camera->setView(s.ears, s.cameraDistance, s.cameraPitch, s.cameraYaw);
    m_models->setTransform(m_earsMarker, glm::scale(glm::translate(glm::mat4(1.0f), s.ears), glm::vec3(0.25f, 0.3f, 0.25f)));
    m_models->setVisible(m_walker, s.kind == Kind::Footsteps);
    m_models->setVisible(m_circler, s.kind == Kind::Circle);
    kke::log::get(name())->info("Station {}/{}: {}", m_current + 1, m_stations.size(), s.title);
}

void AudioDemoModule::tickStation(float dt) {
    const Station& s = m_stations[size_t(m_current)];
    for (size_t i = 0; i < s.emitters.size(); ++i) {
        if (m_time < m_nextHit[i]) continue;
        const Emitter& e = s.emitters[i];
        if (const uint32_t id = m_audio->playImpact(e.position, e.material, e.intensity)) m_emitterOf[id] = int(i);
        m_nextHit[i] += e.period;
    }
    kke::RigidWorld& world = m_bodies->world();
    switch (s.kind) {
        case Kind::Crates: {
            static const uint32_t mats[6] = {Mat::Wood, Mat::Metal, Mat::Plastic, Mat::Rubber, Mat::Glass, Mat::Stone};
            if (m_time >= m_nextCrate && m_crates.size() < 6) {
                m_nextCrate = m_time + 1.8f;
                const uint32_t mat = mats[m_crateCount % 6];
                const float side = (m_crateCount % 2 == 0) ? -0.8f : 0.8f;
                ++m_crateCount;
                Crate c;
                c.size = 0.5f;
                kke::RigidWorld::BodyDesc d;
                d.shape = kke::RigidWorld::Shape::Box;
                d.motion = kke::RigidWorld::Motion::Dynamic;
                d.halfExtents = glm::vec3(c.size * 0.5f);
                d.position = s.ears + glm::vec3(side, 3.2f - kEar, 4.0f);
                d.angularVelocity = glm::vec3(1.5f, 0.4f, -1.0f);
                d.density = mat == Mat::Metal ? 3000.0f : (mat == Mat::Rubber ? 900.0f : 700.0f);
                d.restitution = mat == Mat::Rubber ? 0.6f : 0.2f;
                d.material = mat;
                c.body = world.add(d);
                c.instance = m_models->spawn(cubeModel(mat), glm::mat4(1.0f));
                m_crates.push_back(c);
            }
            for (auto it = m_crates.begin(); it != m_crates.end();) {
                it->age += dt;
                if (it->age > 5.0f) {
                    world.remove(it->body);
                    m_models->remove(it->instance);
                    it = m_crates.erase(it);
                    continue;
                }
                m_models->setTransform(it->instance, world.transform(it->body) * glm::scale(glm::mat4(1.0f), glm::vec3(it->size)));
                ++it;
            }
            break;
        }
        case Kind::Footsteps: {
            // Back and forth over the six strips at a walk.
            const float x0 = s.ears.x - 5.6f, span = 11.2f, speed = 1.4f;
            const float d = std::fmod(m_time * speed, 2.0f * span);
            const float x = x0 + (d < span ? d : 2.0f * span - d);
            const glm::vec3 feet(x, 0.03f, s.ears.z + 3.5f);
            m_models->setTransform(m_walker, glm::scale(glm::translate(glm::mat4(1.0f), feet + glm::vec3(0, 0.9f, 0)), glm::vec3(0.4f, 1.8f, 0.3f)));
            if (m_time >= m_nextStep) {
                m_nextStep = m_time + 0.55f;
                const kke::RigidWorld::RayHit hit = world.raycast(feet + glm::vec3(0, 0.5f, 0), glm::vec3(0, -1, 0), 1.0f);
                const uint32_t ground = hit.hit ? hit.material : uint32_t(Mat::Dirt);
                const float sideStep = (m_stepCount++ % 2 == 0) ? -0.12f : 0.12f;
                m_audio->playFootstep(feet + glm::vec3(0, 0, sideStep), ground, 0.6f);
            }
            break;
        }
        case Kind::Circle: {
            const float a = m_time * 0.9f;
            const glm::vec3 p = s.ears + glm::vec3(std::sin(a), 0.0f, std::cos(a)) * 3.0f;
            m_models->setTransform(m_circler, glm::scale(glm::translate(glm::mat4(1.0f), p), glm::vec3(0.25f)));
            if (m_time >= m_nextTick) {
                m_nextTick = m_time + 0.35f;
                m_audio->playImpact(p, Mat::Plastic, 0.5f);
            }
            break;
        }
        case Kind::Pings:
            if (m_time >= m_nextPing) {
                m_nextPing = m_time + 2.5f;
                m_audio->ping();
            }
            break;
        default: break;
    }
}

void AudioDemoModule::measure() {
    // Everything playing is this station's: entering one stops the rest.
    for (const kke::ActiveSound& a : m_audio->mixer().activeSounds()) {
        const bool first = m_seen.insert(a.id).second;
        if (first) {
            ++m_measured.sounds;
            if (a.viaOpening) ++m_measured.throughDoor;
        }
        m_measured.minTransmission = std::min(m_measured.minTransmission, a.transmission);
        m_measured.peak = std::max(m_measured.peak, a.loudness);
        auto it = m_emitterOf.find(a.id);
        if (it != m_emitterOf.end()) {
            float& t = m_measured.emitterThrough[size_t(it->second)];
            t = std::min(t, a.transmission);
            if (a.viaOpening) m_measured.emitterVia[size_t(it->second)] = true;
        }
    }
    const kke::RoomAcoustics& r = m_audio->room();
    m_measured.rt60 = r.rt60;
    m_measured.wet = r.wet;
    m_measured.openness = r.openness;
    m_measured.openings = r.openings.size();
    m_measured.echoes = m_audio->roomTracker().echoes(kke::AudioMixer::kMaxEchoes, m_audio->materials()).size();
    m_measured.maxRays = std::max(m_measured.maxRays, m_audio->raysLastFrame());
}

void AudioDemoModule::logMeasured(const Station& s, const Measured& m) const {
    std::string emitters;
    for (size_t i = 0; i < s.emitters.size() && i < m.emitterThrough.size(); ++i)
        emitters += fmt::format("{}{} {:.2f}{}", emitters.empty() ? "; " : ", ", s.emitters[i].label, m.emitterThrough[i],
                                m.emitterVia[i] ? " (via opening)" : "");
    kke::log::get(name())->info("  {}: {} sounds, room RT60 {:.2f} s wet {:.2f} open {:.2f} ({} openings), {} echoes, "
                                "least through walls {:.2f}, {} through an opening, peak {:.2f}, max rays/frame {}{}",
                                s.title, m.sounds, m.rt60, m.wet, m.openness, m.openings, m.echoes, m.minTransmission, m.throughDoor,
                                m.peak, m.maxRays, emitters);
}

void AudioDemoModule::update(const kke::UpdateContext& ctx) {
    if (m_current < 0 || m_stations.empty()) return;
    m_time += ctx.dt;
    tickStation(ctx.dt);
    measure();
    if (m_tour && (m_tourLeft -= ctx.dt) <= 0.0f) {
        if (m_current + 1 < int(m_stations.size())) {
            enter(m_current + 1);
        } else {
            logMeasured(m_stations[size_t(m_current)], m_measured);
            m_tour = false;
            kke::log::get(name())->info("Tour done");
            if (m_exitAfterTour) m_app->window().requestClose();
        }
    }
}

void AudioDemoModule::renderUi() {
    if (m_current < 0) return;
    const float s = ImGui::GetFontSize() / 13.0f;
    if (++m_uiFrames == 2) {
        // The engine's own panels start folded (once every window exists,
        // on the second frame): this one is the demo.
        const char* panels[] = {"Performance", "Audio", "Rigid bodies (Jolt)", "Camera"};
        for (int i = 0; i < 4; ++i) {
            ImGui::SetWindowCollapsed(panels[i], true, ImGuiCond_Always);
            ImGui::SetWindowPos(panels[i], ImVec2(10 * s, (10 + 26 * float(i)) * s), ImGuiCond_Always);
        }
    }
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 370 * s, 10 * s), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360 * s, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Audio demo")) {
        ImGui::End();
        return;
    }
    const Station& st = m_stations[size_t(m_current)];
    if (ImGui::Button("< Prev")) enter(m_current - 1);
    ImGui::SameLine();
    if (ImGui::Button("Again")) enter(m_current);
    ImGui::SameLine();
    if (ImGui::Button("Next >")) enter(m_current + 1);
    ImGui::SameLine();
    ImGui::Checkbox("Tour", &m_tour);
    ImGui::SeparatorText(st.title.c_str());
    ImGui::TextWrapped("Listen for: %s", st.listenFor.c_str());
    if (st.kind == Kind::Pings && ImGui::Button("Ping now (Q)")) m_audio->ping();
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Q, false)) m_audio->ping();
    for (const Emitter& e : st.emitters) ImGui::BulletText("%s (%s)", e.label, m_audio->materials().get(e.material).name.c_str());

    ImGui::SeparatorText("What the engine hears here");
    const kke::RoomAcoustics& r = m_audio->room();
    ImGui::Text("Room: RT60 %.2f s, reverb %.0f%%, open %.0f%%", double(r.rt60), double(r.wet * 100.0f), double(r.openness * 100.0f));
    ImGui::Text("Sounds %d, least through walls %.0f%%, via a door %d", m_measured.sounds, double(m_measured.minTransmission * 100.0f),
                m_measured.throughDoor);

    ImGui::SeparatorText("Switches");
    kke::AudioModule::Settings& set = m_audio->settings;
    ImGui::Checkbox("Reverb", &set.reverb);
    ImGui::SameLine();
    ImGui::Checkbox("Walls muffle", &set.occlusion);
    ImGui::SameLine();
    ImGui::Checkbox("Openings", &set.openings);
    // Speakers, the built-in headphone model, or Steam Audio's measured HRTF.
    int mode = int(m_audio->spatialMode());
    bool changed = ImGui::RadioButton("Speakers", &mode, int(kke::SpatialMode::Stereo));
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Binaural", &mode, int(kke::SpatialMode::Binaural));
    if (kke::AudioModule::hrtfAvailable()) {
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Steam Audio HRTF", &mode, int(kke::SpatialMode::Hrtf));
    }
    if (changed) {
        m_audio->setSpatialMode(kke::SpatialMode(mode));
        m_forcedBinaural = false; // your choice now; leaving the station keeps it
    }

    ImGui::SeparatorText("Stations");
    for (int i = 0; i < int(m_stations.size()); ++i) {
        ImGui::PushID(i);
        if (ImGui::Selectable(m_stations[size_t(i)].title.c_str(), i == m_current)) enter(i);
        ImGui::PopID();
    }
    ImGui::TextDisabled("Left-drag turns you (and your ears); scroll zooms.");
    ImGui::End();
}

void AudioDemoModule::shutdown() {
    if (m_audio) m_audio->listenerOverride = nullptr;
}

} // namespace kke_audio_demo
