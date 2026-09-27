#include "ClothDemoModule.h"

#include "kke/Application.h"
#include "kke/BenchRecorder.h"
#include "kke/Log.h"
#include "kke/modules/OrbitCameraModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace kke_cloth {

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

const char* kSceneNames[] = { "Fabrics", "Bed", "Nets", "Cape", "Stress" };
constexpr int kSceneCount = 5;
constexpr float kTourSeconds = 14.0f;

float rand01(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return float(s >> 8) * (1.0f / 16777216.0f);
}

void addBox(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx, const glm::mat4& m, const glm::vec3& half, const glm::vec3& color) {
    static const glm::vec3 n[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (const glm::vec3& f : n) {
        const glm::vec3 u = std::fabs(f.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 w = glm::cross(f, u);
        const uint32_t base = uint32_t(v.size());
        const glm::vec3 nw = glm::normalize(glm::mat3(m) * f);
        for (int k = 0; k < 4; ++k) {
            const float a = (k == 1 || k == 2) ? 1.0f : -1.0f, b = (k >= 2) ? 1.0f : -1.0f;
            const glm::vec3 p = (f + u * a + w * b) * half;
            v.push_back({ glm::vec3(m * glm::vec4(p, 1.0f)), color, nw, glm::vec2(0.0f) });
        }
        idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }
}

// A capsule from a to b (or a sphere when a == b), as a stretched UV sphere.
void addCapsule(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx, const glm::vec3& a, const glm::vec3& b, float r, const glm::vec3& color,
                int seg = 14) {
    glm::vec3 axis = b - a;
    const float len = glm::length(axis);
    axis = len > 1e-6f ? axis / len : glm::vec3(0, 1, 0);
    const glm::vec3 x = glm::normalize(glm::cross(axis, std::fabs(axis.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
    const glm::vec3 z = glm::cross(x, axis);
    const int rings = seg / 2 + 1;
    const uint32_t base = uint32_t(v.size());
    for (int i = 0; i <= rings; ++i) {
        const float th = glm::pi<float>() * float(i) / float(rings);
        const float cy = std::cos(th), sy = std::sin(th);
        const glm::vec3 centre = cy > 0.0f ? b : a; // top half around b, bottom around a
        for (int j = 0; j <= seg; ++j) {
            const float ph = glm::two_pi<float>() * float(j) / float(seg);
            const glm::vec3 nrm = axis * cy + (x * std::cos(ph) + z * std::sin(ph)) * sy;
            v.push_back({ centre + nrm * r, color, nrm, glm::vec2(0.0f) });
        }
    }
    for (int i = 0; i < rings; ++i)
        for (int j = 0; j < seg; ++j) {
            const uint32_t p0 = base + uint32_t(i * (seg + 1) + j), p1 = p0 + 1, p2 = p0 + uint32_t(seg + 1), p3 = p2 + 1;
            idx.insert(idx.end(), { p0, p2, p1, p1, p2, p3 });
        }
}

glm::quat fromTo(const glm::vec3& a, const glm::vec3& b) {
    const float d = glm::dot(a, b);
    if (d < -0.9999f) return glm::angleAxis(glm::pi<float>(), glm::vec3(1, 0, 0));
    const glm::vec3 c = glm::cross(a, b);
    return glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
}

kke::ClothProtection protectionFromName(const char* s) {
    if (std::strcmp(s, "off") == 0) return kke::ClothProtection::Off;
    if (std::strcmp(s, "basic") == 0) return kke::ClothProtection::Basic;
    return kke::ClothProtection::Full;
}

const char* protectionName(kke::ClothProtection p) {
    return p == kke::ClothProtection::Full ? "Full" : p == kke::ClothProtection::Basic ? "Basic" : "Off";
}

} // namespace

void ClothDemoModule::init(kke::Application& app) {
    m_app = &app;
    m_camera = app.getModule<kke::OrbitCameraModule>();
    m_spheres = std::make_unique<kke::SphereImpostorRenderer>(app);
    m_floor = std::make_unique<kke::DynamicMeshRenderer>(app);
    {
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        addBox(v, idx, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.05f, 0.0f)), glm::vec3(30.0f, 0.05f, 30.0f), glm::vec3(0.36f, 0.37f, 0.4f));
        m_floor->upload(v, idx);
    }
    if (const char* e = std::getenv("KKE_CLOTH_PROTECTION")) m_protection = protectionFromName(e);
    if (const char* e = std::getenv("KKE_CLOTH_WIND")) m_windSpeed = float(std::atof(e));
    if (const char* e = std::getenv("KKE_CLOTH_COUNT")) m_stressCount = std::clamp(std::atoi(e), 1, 400);
    if (const char* e = std::getenv("KKE_CLOTH_RES")) m_stressRes = std::clamp(std::atoi(e), 4, 128);
    m_tour = app.benchmark() != nullptr;
    if (const char* e = std::getenv("KKE_CLOTH_TOUR")) m_tour = *e == '1';
    Scene start = Scene::Fabrics;
    if (const char* e = std::getenv("KKE_CLOTH_SCENE")) {
        for (int i = 0; i < kSceneCount; ++i)
            if (SDL_strcasecmp(e, kSceneNames[i]) == 0) start = Scene(i);
    }
    setScene(start);
}

void ClothDemoModule::clear() {
    m_cloth.clear();
    m_solids.clear();
    m_balls.clear();
    m_runner.reset();
    // A new world per scene: nothing of the last one lingers in Jolt.
    m_world = std::make_unique<kke::RigidWorld>();
    kke::RigidWorld::BodyDesc g;
    g.motion = kke::RigidWorld::Motion::Static;
    g.halfExtents = glm::vec3(30.0f, 0.5f, 30.0f);
    g.position = glm::vec3(0.0f, -0.5f, 0.0f);
    m_world->add(g);
}

ClothDemoModule::Piece& ClothDemoModule::addCloth(const kke::ClothDesc& desc, const std::string& label) {
    auto p = std::make_unique<Piece>();
    p->fabric = desc.fabric;
    p->mesh = desc.mesh;
    p->label = label;
    kke::ClothDesc d = desc;
    d.protection = m_protection;
    p->id = m_world->addCloth(d);
    p->mesh3d = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_cloth.push_back(std::move(p));
    return *m_cloth.back();
}

void ClothDemoModule::addSolidBox(const glm::vec3& centre, const glm::vec3& half, const glm::vec3& color, float roughness, bool collide) {
    Solid s;
    s.mesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    s.roughness = roughness;
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    addBox(v, idx, glm::translate(glm::mat4(1.0f), centre), half, color);
    s.mesh->upload(v, idx);
    m_solids.push_back(std::move(s));
    if (!collide) return;
    kke::RigidWorld::BodyDesc b;
    b.motion = kke::RigidWorld::Motion::Static;
    b.halfExtents = half;
    b.position = centre;
    m_world->add(b);
}

void ClothDemoModule::addSolidSphere(const glm::vec3& centre, float radius, const glm::vec3& color, float roughness) {
    Solid s;
    s.mesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    s.roughness = roughness;
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    addCapsule(v, idx, centre, centre, radius, color, 28);
    s.mesh->upload(v, idx);
    m_solids.push_back(std::move(s));
    kke::RigidWorld::BodyDesc b;
    b.shape = kke::RigidWorld::Shape::Sphere;
    b.motion = kke::RigidWorld::Motion::Static;
    b.radius = radius;
    b.position = centre;
    m_world->add(b);
}

void ClothDemoModule::dropBall(const glm::vec3& at, const glm::vec3& velocity, float radius, const glm::vec3& color) {
    kke::RigidWorld::BodyDesc b;
    b.shape = kke::RigidWorld::Shape::Sphere;
    b.radius = radius;
    b.position = at;
    b.velocity = velocity;
    b.density = radius < 0.05f ? 385.0f : 250.0f; // a tennis ball weighs 58 g
    b.restitution = radius < 0.05f ? 0.7f : 0.4f;
    const auto id = m_world->add(b);
    if (id != kke::RigidWorld::kNoBody) m_balls.push_back({ id, radius, color, 0.0f });
}

// ---------------------------------------------------------------------
// Scenes

void ClothDemoModule::setScene(Scene s) {
    m_scene = s;
    m_sceneTime = 0.0f;
    m_ballTimer = 0.0f;
    clear();
    switch (s) {
    case Scene::Fabrics: buildFabrics(); break;
    case Scene::Bed: buildBed(); break;
    case Scene::Nets: buildNets(); break;
    case Scene::Cape: buildCape(); break;
    case Scene::Stress: buildStress(); break;
    }
    const std::string what = std::string("cloth scene ") + kSceneNames[int(s)] + ", protection " + protectionName(m_protection);
    kke::log::get(name())->info("{}: {} cloths", what, m_cloth.size());
    if (kke::BenchRecorder* b = m_app->benchmark()) b->addEvent(what);
}

void ClothDemoModule::buildFabrics() {
    const char* names[] = { "silk", "cotton", "denim", "wool", "leather", "satin" };
    for (int i = 0; i < 6; ++i) {
        const float x = (float(i) - 2.5f) * 1.75f;
        const kke::Fabric f = kke::clothFabric(names[i]);
        // Dropped on a little table: how it falls, then the folds at its corners.
        addSolidBox(glm::vec3(x, 0.5f, 0.4f), glm::vec3(0.04f, 0.5f, 0.04f), glm::vec3(0.25f), 0.5f);
        addSolidBox(glm::vec3(x, 1.0f, 0.4f), glm::vec3(0.3f, 0.025f, 0.3f), glm::vec3(0.55f, 0.55f, 0.58f), 0.4f);
        kke::ClothDesc drape;
        drape.mesh = kke::clothGrid(glm::vec3(x, 1.5f, 0.4f), 1.2f, 1.2f, 28, 28, glm::vec3(1, 0, 0), glm::vec3(0, 0, 1));
        drape.fabric = f;
        drape.wind = 0.1f; // sheltered: the drapes show how each fabric falls, the banners how it flies
        addCloth(drape, f.name);
        // A banner of it in the wind.
        kke::ClothDesc banner;
        banner.mesh = kke::clothGrid(glm::vec3(x, 2.25f, -1.8f), 0.9f, 1.5f, 18, 30, glm::vec3(1, 0, 0), glm::vec3(0, -1, 0));
        banner.fabric = f;
        for (int c = 0; c < banner.mesh.columns; ++c) banner.pinned.push_back(kke::clothGridIndex(banner.mesh, c, 0));
        addCloth(banner, "");
    }
    addSolidBox(glm::vec3(0.0f, 3.02f, -1.8f), glm::vec3(5.4f, 0.03f, 0.03f), glm::vec3(0.35f, 0.25f, 0.15f), 0.6f, false); // the rod
    for (float x : { -5.4f, 5.4f }) addSolidBox(glm::vec3(x, 1.5f, -1.8f), glm::vec3(0.05f, 1.5f, 0.05f), glm::vec3(0.35f, 0.25f, 0.15f), 0.6f);
    m_gusts = true;
    if (m_camera) m_camera->setView(glm::vec3(0.0f, 1.45f, -0.5f), 8.2f, -0.16f, 0.0f);
}

void ClothDemoModule::buildBed() {
    addSolidBox(glm::vec3(0.0f, 0.18f, 0.0f), glm::vec3(1.05f, 0.18f, 0.8f), glm::vec3(0.4f, 0.28f, 0.18f), 0.6f);     // frame
    addSolidBox(glm::vec3(0.0f, 0.46f, 0.0f), glm::vec3(1.0f, 0.1f, 0.76f), glm::vec3(0.92f, 0.92f, 0.9f), 0.9f);      // mattress
    addSolidBox(glm::vec3(-1.1f, 0.6f, 0.0f), glm::vec3(0.05f, 0.6f, 0.82f), glm::vec3(0.4f, 0.28f, 0.18f), 0.6f);      // headboard
    for (float z : { -0.36f, 0.36f }) addSolidBox(glm::vec3(-0.76f, 0.63f, z), glm::vec3(0.17f, 0.07f, 0.3f), glm::vec3(0.95f, 0.95f, 0.97f), 0.9f);
    kke::ClothDesc blanket;
    blanket.mesh = kke::clothGrid(glm::vec3(0.15f, 1.1f, 0.0f), 2.1f, 2.1f, 46, 46, glm::vec3(1, 0, 0), glm::vec3(0, 0, 1));
    blanket.fabric = kke::clothFabric("wool");
    addCloth(blanket, "wool blanket");
    m_gusts = false;
    if (m_camera) m_camera->setView(glm::vec3(0.0f, 0.5f, 0.0f), 4.4f, -0.55f, 0.65f);
}

void ClothDemoModule::buildNets() {
    // A hammock between two posts, catching balls.
    for (float x : { -1.55f, 1.55f }) addSolidBox(glm::vec3(x, 0.7f, 0.0f), glm::vec3(0.06f, 0.7f, 0.6f), glm::vec3(0.35f, 0.25f, 0.15f), 0.6f);
    kke::ClothDesc hammock;
    hammock.mesh = kke::clothNet(glm::vec3(0.0f, 1.2f, 0.0f), 3.0f, 1.1f, 31, 12, glm::vec3(1, 0, 0), glm::vec3(0, 0, 1));
    hammock.fabric = kke::clothFabric("net");
    hammock.fabric.color = glm::vec3(0.85f, 0.7f, 0.45f);
    hammock.contactMass = 8.0f;
    for (int r = 0; r < hammock.mesh.rows; ++r) {
        hammock.pinned.push_back(kke::clothGridIndex(hammock.mesh, 0, r));
        hammock.pinned.push_back(kke::clothGridIndex(hammock.mesh, hammock.mesh.columns - 1, r));
    }
    addCloth(hammock, "hammock");
    // A tennis net (5 cm mesh, held at the top cable and the posts), with shots at it.
    const float netZ = -3.0f;
    kke::ClothDesc net;
    net.mesh = kke::clothNet(glm::vec3(0.0f, 0.52f, netZ), 4.0f, 0.95f, 81, 20, glm::vec3(1, 0, 0), glm::vec3(0, -1, 0));
    net.fabric = kke::clothFabric("net");
    net.fabric.color = glm::vec3(0.12f, 0.12f, 0.12f);
    net.contactMass = 5.0f;
    for (int c = 0; c < net.mesh.columns; ++c) net.pinned.push_back(kke::clothGridIndex(net.mesh, c, 0));
    for (int r = 1; r < net.mesh.rows; ++r) {
        net.pinned.push_back(kke::clothGridIndex(net.mesh, 0, r));
        net.pinned.push_back(kke::clothGridIndex(net.mesh, net.mesh.columns - 1, r));
    }
    addCloth(net, "tennis net");
    addSolidBox(glm::vec3(0.0f, 1.0f, netZ), glm::vec3(2.05f, 0.012f, 0.012f), glm::vec3(0.95f), 0.5f, false); // the white tape
    for (float x : { -2.06f, 2.06f }) addSolidBox(glm::vec3(x, 0.53f, netZ), glm::vec3(0.03f, 0.53f, 0.03f), glm::vec3(0.2f, 0.3f, 0.22f), 0.5f);
    m_gusts = false;
    if (m_camera) m_camera->setView(glm::vec3(0.0f, 0.8f, -1.3f), 7.0f, -0.32f, 0.55f);
}

void ClothDemoModule::buildCape() {
    m_runner = std::make_unique<Runner>();
    Runner& r = *m_runner;
    r.body = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    // Cloth-only capsules: only cloth feels them (like a character's).
    for (int i = 0; i < 5; ++i) {
        kke::RigidWorld::BodyDesc b;
        b.shape = kke::RigidWorld::Shape::Capsule;
        b.motion = kke::RigidWorld::Motion::Kinematic;
        b.clothOnly = true;
        b.radius = i == 0 ? 0.17f : i < 3 ? 0.075f : 0.055f;
        b.halfHeight = i == 0 ? 0.2f : i < 3 ? 0.38f : 0.3f;
        r.proxies.push_back(m_world->add(b));
    }
    stepRunner(0.0f);
    // The cape: 18 x 24, hung from the shoulders behind the back, skinned
    // to the torso so back-stops keep it out of the body.
    const glm::mat4 torso = r.torso;
    const glm::vec3 right = glm::vec3(torso * glm::vec4(-1, 0, 0, 0)); // faces away from the back
    const glm::vec3 topCentre = glm::vec3(torso * glm::vec4(0.0f, 0.4f, -0.2f, 1.0f));
    kke::ClothDesc cape;
    cape.mesh = kke::clothGrid(topCentre + glm::vec3(0.0f, -0.55f, 0.0f), 0.62f, 1.1f, 16, 26, right, glm::vec3(0, -1, 0));
    cape.fabric = kke::clothFabric("satin");
    cape.fabric.color = glm::vec3(0.6f, 0.05f, 0.08f);
    cape.fabric.bend = 60.0f;
    cape.bindPose = { torso };
    cape.skin.resize(cape.mesh.positions.size());
    for (auto& sv : cape.skin) {
        sv.joints = glm::uvec4(0);
        sv.weights = glm::vec4(1, 0, 0, 0);
        sv.maxDistance = 2.0f; // free to fly, but never behind its back-stop (into the back)
    }
    for (int c = 0; c < cape.mesh.columns; ++c) cape.pinned.push_back(kke::clothGridIndex(cape.mesh, c, 0));
    cape.backStop = 0.01f;
    addCloth(cape, "");
    // A curtain across its path to run through.
    const float a = glm::half_pi<float>();
    const glm::vec3 door(std::cos(a) * r.radius, 0.0f, std::sin(a) * r.radius);
    kke::ClothDesc curtain;
    curtain.mesh = kke::clothGrid(door + glm::vec3(0.0f, 1.2f, 0.0f), 1.4f, 2.2f, 24, 36, glm::vec3(1, 0, 0), glm::vec3(0, -1, 0));
    curtain.fabric = kke::clothFabric("linen");
    for (int c = 0; c < curtain.mesh.columns; ++c) curtain.pinned.push_back(kke::clothGridIndex(curtain.mesh, c, 0));
    addCloth(curtain, "curtain");
    addSolidBox(door + glm::vec3(0.0f, 2.32f, 0.0f), glm::vec3(0.85f, 0.03f, 0.03f), glm::vec3(0.35f, 0.25f, 0.15f), 0.6f, false);
    for (float x : { -0.82f, 0.82f }) addSolidBox(door + glm::vec3(x, 1.16f, 0.0f), glm::vec3(0.04f, 1.16f, 0.04f), glm::vec3(0.35f, 0.25f, 0.15f), 0.6f);
    m_gusts = false;
    if (m_camera) m_camera->setView(glm::vec3(0.0f, 0.9f, 0.0f), 8.0f, -0.4f, 0.4f);
}

void ClothDemoModule::buildStress() {
    const int side = int(std::ceil(std::sqrt(float(m_stressCount))));
    for (int i = 0; i < m_stressCount; ++i) {
        const glm::vec3 at((float(i % side) - float(side - 1) * 0.5f) * 2.3f, 0.0f, (float(i / side) - float(side - 1) * 0.5f) * 2.3f);
        addSolidSphere(at + glm::vec3(0.0f, 0.4f, 0.0f), 0.4f, glm::vec3(0.5f), 0.5f);
        kke::ClothDesc d;
        d.mesh = kke::clothGrid(at + glm::vec3(0.0f, 1.3f, 0.0f), 1.6f, 1.6f, m_stressRes, m_stressRes, glm::vec3(1, 0, 0), glm::vec3(0, 0, 1));
        d.fabric = kke::clothFabric(kke::clothFabricNames()[size_t(i) % 6]);
        addCloth(d, "");
    }
    m_gusts = false;
    if (m_camera) m_camera->setView(glm::vec3(0.0f, 0.5f, 0.0f), 3.0f + float(side) * 2.4f, -0.6f, 0.5f);
}

void ClothDemoModule::redrop() {
    for (auto& p : m_cloth) m_world->resetCloth(p->id);
    for (const Ball& b : m_balls) m_world->remove(b.body);
    m_balls.clear();
    m_sceneTime = 0.0f;
}

void ClothDemoModule::applyProtection() {
    setScene(m_scene); // tethers and back-stops are built with the cloth: rebuild
}

// The runner: a mannequin jogging a circle (legs and arms swinging), its
// limbs as cloth-only capsules moved every step.
void ClothDemoModule::stepRunner(float dt) {
    Runner& r = *m_runner;
    r.angle += dt * r.speed / r.radius;
    r.phase += dt * r.speed * 2.2f;
    const glm::vec3 centre(std::cos(r.angle) * r.radius, 0.0f, std::sin(r.angle) * r.radius);
    const glm::vec3 forward(-std::sin(r.angle), 0.0f, std::cos(r.angle));
    const float yaw = std::atan2(forward.x, forward.z);
    const float bob = std::fabs(std::sin(r.phase)) * 0.05f;
    const glm::quat face = glm::angleAxis(yaw, glm::vec3(0, 1, 0)) * glm::angleAxis(0.12f, glm::vec3(1, 0, 0)); // leaning into the run
    r.torso = glm::translate(glm::mat4(1.0f), centre + glm::vec3(0.0f, 1.12f + bob, 0.0f)) * glm::mat4_cast(face);
    const glm::vec3 hip = glm::vec3(r.torso * glm::vec4(0, -0.3f, 0, 1));
    const glm::vec3 shoulder = glm::vec3(r.torso * glm::vec4(0, 0.33f, 0, 1));
    const glm::vec3 side = glm::vec3(r.torso * glm::vec4(1, 0, 0, 0));
    struct Limb { glm::vec3 a, b; };
    Limb limbs[5];
    limbs[0] = { hip, shoulder };
    for (int k = 0; k < 2; ++k) {
        const float s = k == 0 ? 1.0f : -1.0f;
        const float swing = std::sin(r.phase) * 0.7f * s;
        const glm::vec3 legDir = glm::normalize(glm::vec3(0, -1, 0) * std::cos(swing) + forward * std::sin(swing));
        const glm::vec3 hipK = hip + side * (0.11f * s);
        limbs[1 + k] = { hipK, hipK + legDir * 0.82f };
        const glm::vec3 armDir = glm::normalize(glm::vec3(0, -1, 0) * std::cos(-swing * 0.8f) + forward * std::sin(-swing * 0.8f));
        const glm::vec3 sh = shoulder + side * (0.24f * s);
        limbs[3 + k] = { sh, sh + armDir * 0.62f };
    }
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    const glm::vec3 skin(0.8f, 0.72f, 0.62f), shirt(0.2f, 0.32f, 0.5f);
    for (int i = 0; i < 5; ++i) {
        const Limb& l = limbs[i];
        const glm::vec3 mid = (l.a + l.b) * 0.5f;
        const glm::vec3 dir = glm::normalize(l.b - l.a);
        const float radius = i == 0 ? 0.17f : i < 3 ? 0.075f : 0.055f;
        const float half = glm::length(l.b - l.a) * 0.5f;
        if (dt > 0.0f) m_world->moveKinematic(r.proxies[size_t(i)], mid, fromTo(glm::vec3(0, 1, 0), dir), dt);
        else m_world->setTransform(r.proxies[size_t(i)], mid, fromTo(glm::vec3(0, 1, 0), dir));
        addCapsule(v, idx, mid - dir * half, mid + dir * half, radius, i == 0 || i >= 3 ? shirt : glm::vec3(0.2f), 12);
    }
    addCapsule(v, idx, shoulder + glm::vec3(0, 0.22f, 0), shoulder + glm::vec3(0, 0.22f, 0), 0.12f, skin, 16); // head
    r.body->upload(v, idx);
}

// ---------------------------------------------------------------------
// Frame

void ClothDemoModule::fixedUpdate(const kke::FixedUpdateContext& ctx) {
    const float dt = ctx.fixedDt;
    m_time += dt;
    m_sceneTime += dt;
    if (m_tour && m_sceneTime > kTourSeconds) {
        setScene(Scene((int(m_scene) + 1) % kSceneCount));
        return;
    }
    // Wind: steady, or gusting (two slow waves and a flutter).
    float speed = m_windSpeed;
    if (m_gusts) speed *= 0.55f + 0.3f * std::sin(m_time * 0.7f) + 0.15f * std::sin(m_time * 2.3f + 1.0f);
    const bool windy = m_scene == Scene::Fabrics || m_scene == Scene::Cape;
    m_world->setWind(windy ? glm::vec3(std::sin(m_windYaw), 0.0f, std::cos(m_windYaw)) * speed : glm::vec3(0.0f));

    if (m_scene == Scene::Bed) {
        // More layers after the blanket: a silk sheet, then a denim throw.
        const float t0 = m_sceneTime - dt;
        if (t0 < 2.5f && m_sceneTime >= 2.5f) {
            kke::ClothDesc sheet;
            sheet.mesh = kke::clothGrid(glm::vec3(0.25f, 1.3f, 0.05f), 1.5f, 1.5f, 32, 32, glm::normalize(glm::vec3(1, 0, 0.3f)), glm::normalize(glm::vec3(-0.3f, 0, 1)));
            sheet.fabric = kke::clothFabric("silk");
            addCloth(sheet, "silk sheet");
        }
        if (t0 < 5.0f && m_sceneTime >= 5.0f) {
            kke::ClothDesc throwCloth;
            throwCloth.mesh = kke::clothGrid(glm::vec3(0.3f, 1.4f, -0.1f), 1.0f, 1.0f, 22, 22, glm::normalize(glm::vec3(1, 0, -0.5f)), glm::normalize(glm::vec3(0.5f, 0, 1)));
            throwCloth.fabric = kke::clothFabric("denim");
            addCloth(throwCloth, "denim throw");
        }
    }
    if (m_scene == Scene::Nets && m_rain) {
        m_ballTimer += dt;
        if (m_ballTimer > 0.9f) {
            m_ballTimer = 0.0f;
            const glm::vec3 colors[] = { { 0.95f, 0.45f, 0.1f }, { 0.15f, 0.55f, 0.95f }, { 0.2f, 0.8f, 0.35f }, { 0.9f, 0.2f, 0.3f } };
            dropBall(glm::vec3((rand01(m_rng) - 0.5f) * 2.0f, 2.6f, (rand01(m_rng) - 0.5f) * 0.6f), glm::vec3(0.0f), 0.1f + rand01(m_rng) * 0.08f,
                     colors[m_rng % 4]);
            // A shot at the tennis net.
            dropBall(glm::vec3((rand01(m_rng) - 0.5f) * 3.0f, 0.5f, -8.0f), glm::vec3((rand01(m_rng) - 0.5f) * 1.0f, 2.2f, 15.0f + rand01(m_rng) * 6.0f), 0.033f,
                     glm::vec3(0.85f, 0.95f, 0.2f));
        }
    }
    for (size_t i = m_balls.size(); i-- > 0;) {
        m_balls[i].age += dt;
        if (m_balls[i].age > 9.0f) {
            m_world->remove(m_balls[i].body);
            m_balls.erase(m_balls.begin() + long(i));
        }
    }
    if (m_runner) {
        stepRunner(dt);
        m_world->setClothJoints(m_cloth.front()->id, { m_runner->torso });
    }
    if (m_scene == Scene::Stress && m_sceneTime > 5.0f) redrop();
    m_world->step(dt);
    m_stepMs = m_stepMs * 0.95 + m_world->lastStepMs() * 0.05;
    m_protectMs = m_protectMs * 0.95 + m_world->lastClothMs() * 0.05;
}

void ClothDemoModule::updateMeshes(const glm::vec3& cameraPos) {
    const auto t0 = Clock::now();
    for (auto& pp : m_cloth) {
        Piece& p = *pp;
        if (!m_world->clothPositions(p.id, p.pos)) continue;
        p.verts.clear();
        p.idx.clear();
        if (!p.mesh.indices.empty() && p.mesh.lines.empty()) {
            kke::clothNormals(p.pos, p.mesh.indices, p.nrm);
            p.verts.resize(p.pos.size());
            for (size_t i = 0; i < p.pos.size(); ++i)
                p.verts[i] = kke::Vertex{ p.pos[i], p.fabric.color, p.nrm[i], i < p.mesh.uvs.size() ? p.mesh.uvs[i] : glm::vec2(0.0f) };
            p.mesh3d->upload(p.verts, p.mesh.indices);
            continue;
        }
        // Threads (nets): a camera-facing ribbon per thread, at least a
        // pixel wide so a far net doesn't break up into dashes.
        const float thread = 0.0025f;
        for (size_t i = 0; i + 1 < p.mesh.lines.size(); i += 2) {
            const glm::vec3 a = p.pos[p.mesh.lines[i]], b = p.pos[p.mesh.lines[i + 1]];
            const glm::vec3 mid = (a + b) * 0.5f;
            const glm::vec3 toCam = cameraPos - mid;
            const float dist = glm::length(toCam);
            glm::vec3 side = glm::cross(b - a, toCam);
            const float sl = glm::length(side);
            if (sl < 1e-9f) continue;
            side *= std::max(thread, dist * m_pixelAngle * 0.6f) / sl;
            const glm::vec3 n = toCam / std::max(dist, 1e-6f);
            const uint32_t base = uint32_t(p.verts.size());
            p.verts.push_back({ a - side, p.fabric.color, n, glm::vec2(0.0f) });
            p.verts.push_back({ a + side, p.fabric.color, n, glm::vec2(0.0f) });
            p.verts.push_back({ b + side, p.fabric.color, n, glm::vec2(0.0f) });
            p.verts.push_back({ b - side, p.fabric.color, n, glm::vec2(0.0f) });
            p.idx.insert(p.idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        }
        p.mesh3d->upload(p.verts, p.idx);
    }
    m_meshMs = m_meshMs * 0.95 + msSince(t0) * 0.05;
}

void ClothDemoModule::update(const kke::UpdateContext&) {
    const kke::Camera& cam = m_app->camera();
    int w = 0, h = 0;
    m_app->window().getFramebufferSize(w, h);
    m_pixelAngle = 2.0f * std::tan(glm::radians(cam.fovDegrees) * 0.5f) / float(std::max(h, 1));
    updateMeshes(cam.position);
}

void ClothDemoModule::render(const kke::RenderContext& ctx) {
    m_viewProj = ctx.proj * ctx.view;
    m_floor->draw(ctx, glm::mat4(1.0f), 0.0f, 0.95f);
    for (Solid& s : m_solids) s.mesh->draw(ctx, glm::mat4(1.0f), 0.0f, s.roughness);
    for (auto& p : m_cloth) p->mesh3d->drawCloth(ctx, p->fabric);
    if (m_runner) m_runner->body->draw(ctx, glm::mat4(1.0f), 0.0f, 0.7f);
    m_sphereScratch.clear();
    for (const Ball& b : m_balls) m_sphereScratch.push_back({ m_world->position(b.body), b.radius, b.color, 0.0f, 0.45f });
    if (!m_sphereScratch.empty()) m_spheres->draw(ctx, m_sphereScratch);
}

void ClothDemoModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    for (Solid& s : m_solids) s.mesh->drawShadow(ctx);
    for (auto& p : m_cloth) p->mesh3d->drawShadow(ctx);
    if (m_runner) m_runner->body->drawShadow(ctx);
}

void ClothDemoModule::onEvent(const SDL_Event& event) {
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat || ImGui::GetIO().WantTextInput) return;
    switch (event.key.key) {
    case SDLK_TAB: setScene(Scene((int(m_scene) + 1) % kSceneCount)); break;
    case SDLK_1: setScene(Scene::Fabrics); break;
    case SDLK_2: setScene(Scene::Bed); break;
    case SDLK_3: setScene(Scene::Nets); break;
    case SDLK_4: setScene(Scene::Cape); break;
    case SDLK_5: setScene(Scene::Stress); break;
    case SDLK_R: redrop(); break;
    case SDLK_P:
        m_protection = m_protection == kke::ClothProtection::Full ? kke::ClothProtection::Basic
                       : m_protection == kke::ClothProtection::Basic ? kke::ClothProtection::Off : kke::ClothProtection::Full;
        applyProtection();
        break;
    case SDLK_G: m_gusts = !m_gusts; break;
    default: break;
    }
}

void ClothDemoModule::renderUi() {
    const float s = ImGui::GetFontSize() / 13.0f;
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 370 * s, 10 * s), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360 * s, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Cloth");
    int scene = int(m_scene);
    if (ImGui::Combo("Scene (1-5, Tab)", &scene, kSceneNames, kSceneCount)) setScene(Scene(scene));
    ImGui::Checkbox("Tour the scenes", &m_tour);
    int prot = int(m_protection);
    const char* prots[] = { "Off", "Basic", "Full" };
    if (ImGui::Combo("Clipping protection (P)", &prot, prots, 3)) {
        m_protection = kke::ClothProtection(prot);
        applyProtection();
    }
    ImGui::TextDisabled(m_protection == kke::ClothProtection::Full    ? "Cloth can't pass through cloth, not even itself."
                        : m_protection == kke::ClothProtection::Basic ? "Collides with the world; cloth can pass through cloth."
                                                                      : "Raw Jolt: no thickness, no tethers, no back-stops.");
    if (ImGui::Button("Drop again (R)")) redrop();
    if (m_scene == Scene::Fabrics || m_scene == Scene::Cape) {
        ImGui::SliderFloat("Wind m/s", &m_windSpeed, 0.0f, 15.0f, "%.1f");
        ImGui::SliderAngle("Wind from", &m_windYaw, -180.0f, 180.0f);
        ImGui::Checkbox("Gusts (G)", &m_gusts);
    }
    if (m_scene == Scene::Nets) ImGui::Checkbox("Balls and shots", &m_rain);
    if (m_scene == Scene::Cape && m_runner) ImGui::SliderFloat("Run speed m/s", &m_runner->speed, 0.0f, 8.0f, "%.1f");
    if (m_scene == Scene::Stress) {
        bool rebuild = ImGui::SliderInt("Sheets", &m_stressCount, 1, 100);
        rebuild |= ImGui::SliderInt("Vertices per side", &m_stressRes, 8, 64);
        if (rebuild && ImGui::IsItemDeactivatedAfterEdit()) setScene(Scene::Stress);
        if (ImGui::Button("Rebuild")) setScene(Scene::Stress);
    }
    ImGui::SeparatorText("Cost");
    uint32_t verts = 0, tris = 0, contacts = 0, undone = 0, asleep = 0;
    for (auto& p : m_cloth) {
        const kke::ClothStats st = m_world->clothStats(p->id);
        verts += st.vertices;
        tris += st.triangles;
        contacts += st.selfContacts;
        undone += st.crossingsUndone;
        asleep += st.sleeping ? 1u : 0u;
    }
    ImGui::Text("%zu cloths (%u asleep), %u vertices, %u triangles", m_cloth.size(), asleep, verts, tris);
    ImGui::Text("Physics step: %.2f ms (cloth protection %.2f ms)", m_stepMs, m_protectMs);
    ImGui::Text("Mesh rebuild: %.2f ms", m_meshMs);
    ImGui::Text("Cloth contacts %u, crossings undone %u", contacts, undone);
    ImGui::End();

    // Fabric names under the drapes and hammocks.
    const glm::mat4 vp = m_viewProj;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    for (auto& p : m_cloth) {
        if (p->label.empty() || p->pos.empty()) continue;
        glm::vec3 lo(1e9f);
        glm::vec3 centre(0.0f);
        for (const glm::vec3& v : p->pos) {
            lo = glm::min(lo, v);
            centre += v;
        }
        centre /= float(p->pos.size());
        const glm::vec4 c = vp * glm::vec4(centre.x, lo.y - 0.12f, centre.z, 1.0f);
        if (c.w <= 0.0f) continue;
        const ImVec2 at((c.x / c.w * 0.5f + 0.5f) * size.x, (c.y / c.w * 0.5f + 0.5f) * size.y);
        const ImVec2 ts = ImGui::CalcTextSize(p->label.c_str());
        dl->AddText(ImVec2(at.x - ts.x * 0.5f + 1, at.y + 1), IM_COL32(0, 0, 0, 200), p->label.c_str());
        dl->AddText(ImVec2(at.x - ts.x * 0.5f, at.y), IM_COL32(255, 255, 255, 255), p->label.c_str());
    }
}

} // namespace kke_cloth
