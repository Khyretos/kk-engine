#include "ClothDemoModule.h"

#include "kke/Application.h"
#include "kke/BenchRecorder.h"
#include "kke/ClothGpu.h"
#include "kke/Log.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/OrbitCameraModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <utility>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace kke_cloth {

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

const char* kSceneNames[] = { "Fabrics", "Bed", "Nets", "Cape", "Stress", "Hair" };
constexpr int kSceneCount = 6;
constexpr float kTourSeconds = 14.0f;

// The hair catalog's pages: which heads (hairstyleOnHead names).
struct HairPage {
    const char* name;
    const char* id; // KKE_HAIR_SHOW
    std::vector<std::string> styles;
};
const HairPage kHairPages[] = {
    { "Types 3 and 4", "types34", { "3a", "3b", "3c", "4a", "4b", "4c" } },
    { "Hairstyles", "styles", { "afro", "puff", "high-top fade", "twist-out", "bantu knots" } },
    { "Braids and locs", "braids", { "box braids", "cornrows", "locs", "two-strand twists" } },
    { "Types 1 and 2", "types12", { "1a", "1b", "1c", "2a", "2b", "2c" } },
    { "Classic", "classic", { "long", "wavy", "curly", "short" } },
};
constexpr int kHairPageCount = 5;

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
    m_gpu = kke::ClothGpu::create(app.device());
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
    if (const char* e = std::getenv("KKE_HAIR_GUIDES")) m_hairGuides = std::clamp(std::atoi(e), 8, 4000);
    // Phones draw half the hairs (each wider, so it looks as full): the
    // hair page is GPU-bound there (9 fps on an Adreno 730 with all drawn).
    const std::string& target = app.hardwareTarget().name;
    if (target == "android" || target == "ios") m_hairDetail = 0.5f;
    if (const char* e = std::getenv("KKE_HAIR_MOTION")) m_hairMotion = std::clamp(float(std::atof(e)), 0.0f, 1.0f);
    if (const char* e = std::getenv("KKE_HAIR_DETAIL")) m_hairDetail = std::clamp(float(std::atof(e)), 0.05f, 1.0f);
    if (const char* e = std::getenv("KKE_HAIR_PER_GUIDE")) m_hairsPerGuide = std::clamp(std::atoi(e), 0, 256);
    if (const char* e = std::getenv("KKE_HAIR_SHOW")) {
        for (int i = 0; i < kHairPageCount; ++i)
            if (SDL_strcasecmp(e, kHairPages[i].id) == 0) m_hairShow = i;
    }
    m_tour = app.benchmark() != nullptr;
    if (const char* e = std::getenv("KKE_CLOTH_TOUR")) m_tour = *e == '1';
    Scene start = Scene::Fabrics;
    if (const char* e = std::getenv("KKE_CLOTH_SCENE")) {
        for (int i = 0; i < kSceneCount; ++i)
            if (SDL_strcasecmp(e, kSceneNames[i]) == 0) start = Scene(i);
    }
    setScene(start);
    defineInput();
    buildPanel();
}

void ClothDemoModule::clear() {
    // The last scene's meshes and hair may still be in use by frames in
    // flight: the renderer frees them once those frames are done.
    struct Last {
        std::vector<std::unique_ptr<Piece>> cloth;
        std::vector<Solid> solids;
        std::unique_ptr<Runner> runner;
        std::vector<std::unique_ptr<Head>> heads;
    };
    auto last = std::make_shared<Last>();
    last->cloth = std::move(m_cloth);
    last->solids = std::move(m_solids);
    last->runner = std::move(m_runner);
    last->heads = std::move(m_heads);
    m_cloth.clear();
    m_solids.clear();
    m_heads.clear();
    m_balls.clear();
    m_app->renderer().retire(std::shared_ptr<void>(std::move(last)));
    // A new world per scene: nothing of the last one lingers in Jolt.
    m_world = std::make_unique<kke::RigidWorld>();
    m_world->setClothGpu(m_gpu); // Full protection's pair search on the GPU, when it has a queue for it
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
    case Scene::Hair: buildHair(); break;
    }
    const std::string what = std::string("cloth scene ") + kSceneNames[int(s)] + ", protection " + protectionName(m_protection);
    if (s == Scene::Hair) {
        size_t guides = 0, hairs = 0;
        for (auto& h : m_heads) {
            guides += m_world->hairStats(h->hair).guides;
            hairs += h->drawn->hairs();
        }
        kke::log::get(name())->info("hair scene: {} heads, {} guide strands, {} hairs drawn", m_heads.size(), guides, hairs);
    } else {
        kke::log::get(name())->info("{}: {} cloths", what, m_cloth.size());
    }
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

// A page of the hair catalog: heads on shoulders in a row, each with a
// hair type or style, turning and nodding in the wind. The head is a
// sphere only cloth and hair collide with; the shoulders are solid.
void ClothDemoModule::buildHair() {
    // KKE_HAIR_STYLES=4c,afro picks the heads (hairStyleNames(), hairstyleNames()).
    std::vector<std::string> styles = kHairPages[std::clamp(m_hairShow, 0, kHairPageCount - 1)].styles;
    if (const char* e = std::getenv("KKE_HAIR_STYLES")) {
        styles.clear();
        std::string list = e;
        for (size_t at = 0; at <= list.size();) {
            const size_t comma = std::min(list.find(',', at), list.size());
            if (comma > at) styles.push_back(list.substr(at, comma - at));
            at = comma + 1;
        }
        if (styles.empty()) styles.push_back("long");
    }
    // Hair colours (root, tip; sRGB) and skin tones, varied along the row.
    const glm::vec3 colors[][2] = { { { 0.05f, 0.04f, 0.04f }, { 0.12f, 0.09f, 0.08f } },   // black
                                    { { 0.09f, 0.05f, 0.03f }, { 0.2f, 0.12f, 0.07f } },    // dark brown
                                    { { 0.45f, 0.3f, 0.14f }, { 0.78f, 0.6f, 0.36f } },     // blond
                                    { { 0.06f, 0.04f, 0.03f }, { 0.3f, 0.14f, 0.07f } },    // off-black, auburn ends
                                    { { 0.35f, 0.1f, 0.04f }, { 0.6f, 0.22f, 0.08f } },     // red
                                    { { 0.16f, 0.1f, 0.06f }, { 0.36f, 0.24f, 0.14f } } };  // brown
    const glm::vec3 skins[] = { { 0.36f, 0.22f, 0.15f }, { 0.8f, 0.64f, 0.52f }, { 0.55f, 0.36f, 0.24f },
                                { 0.9f, 0.76f, 0.66f }, { 0.44f, 0.28f, 0.19f }, { 0.72f, 0.56f, 0.4f } };
    const glm::vec3 shirt(0.3f, 0.34f, 0.4f);
    const float radius = 0.1f;
    const int count = int(styles.size());
    for (int i = 0; i < count; ++i) {
        auto h = std::make_unique<Head>();
        const float x = (float(i) - float(count - 1) * 0.5f) * 0.8f;
        const glm::vec3 centre(x, 1.62f, 0.0f);
        const glm::vec3 skin = skins[i % 6];
        h->neck = glm::vec3(x, 1.5f, 0.0f);
        h->phase = float(i) * 1.7f;
        h->label = styles[size_t(i)];
        h->bind = glm::mat4(1.0f);
        h->now = h->bind;
        // The hair first: the scalp is painted where it grows.
        kke::HairDesc d;
        const auto& named = kke::hairstyleNames();
        const bool coiled = kke::hairStyle(styles[size_t(i)]).coil > 0.0f || std::find(named.begin(), named.end(), h->label) != named.end();
        const int colour = coiled ? (i % 2 == 0 ? 0 : (i % 4 == 1 ? 3 : 1)) : i % 6;
        d.style.rootColor = colors[colour][0];
        d.style.tipColor = colors[colour][1];
        d.bindPose = h->bind;
        if (!kke::hairstyleOnHead(d, styles[size_t(i)], centre, radius, m_hairGuides)) {
            kke::log::get(name())->warn("hair: no style '{}' (hairStyleNames(), hairstyleNames())", styles[size_t(i)]);
            continue;
        }
        if (m_hairsPerGuide > 0) d.style.hairsPerGuide = m_hairsPerGuide;
        // Shoulders and neck (solid), the head (moves).
        addSolidBox(glm::vec3(x, 1.33f, 0.0f), glm::vec3(0.21f, 0.07f, 0.11f), shirt, 0.8f);
        addSolidBox(glm::vec3(x, 0.63f, 0.0f), glm::vec3(0.05f, 0.63f, 0.05f), glm::vec3(0.25f), 0.6f);
        kke::RigidWorld::BodyDesc hb;
        hb.shape = kke::RigidWorld::Shape::Sphere;
        hb.motion = kke::RigidWorld::Motion::Kinematic;
        hb.clothOnly = true;
        hb.radius = radius;
        hb.position = centre;
        h->collider = m_world->add(hb);
        kke::RigidWorld::BodyDesc nb;
        nb.shape = kke::RigidWorld::Shape::Capsule;
        nb.motion = kke::RigidWorld::Motion::Static;
        nb.clothOnly = true;
        nb.radius = 0.045f;
        nb.halfHeight = 0.06f;
        nb.position = glm::vec3(x, 1.45f, 0.0f);
        m_world->add(nb);
        h->mesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
        {
            std::vector<kke::Vertex> v;
            std::vector<uint32_t> idx;
            addCapsule(v, idx, centre, centre, radius, skin, d.style.plait > 0 ? 112 : 40); // finer where parts and rows are painted
            // The scalp painted the hair's root colour where hair grows, so
            // no skin shows between the hairs (parts between sections do).
            // A fade is painted: short at the cut, down to the skin.
            float spacing = 0.0f;
            if (d.roots.size() > 1) spacing = std::sqrt(4.0f * radius * radius * 2.4f / float(d.roots.size())); // over most of the sphere
            // Braids and locs: only under them (the parts between show), a
            // cornrow all along its row.
            std::vector<std::pair<glm::vec3, glm::vec3>> under; // the braids' pieces lying on the scalp
            if (d.style.plait > 0) {
                const std::vector<glm::vec3> rest = kke::hairRestPose(d);
                const size_t per = size_t(kke::hairStrandVertices(d.style));
                for (size_t k = 0; k + 1 < rest.size(); ++k) {
                    if ((k + 1) % per == 0) continue; // the next guide
                    const float lying = radius + 2.0f * d.style.thickness;
                    if (glm::distance(rest[k], centre) < lying && glm::distance(rest[k + 1], centre) < lying) under.emplace_back(rest[k], rest[k + 1]);
                }
            }
            for (kke::Vertex& vx : v) {
                float nearest = 1e9f;
                float cover = 0.0f;
                if (d.style.plait > 0) {
                    for (const auto& [a, b] : under) {
                        const glm::vec3 ab = b - a;
                        const float t = std::clamp(glm::dot(vx.position - a, ab) / std::max(glm::dot(ab, ab), 1e-12f), 0.0f, 1.0f);
                        nearest = std::min(nearest, glm::distance(vx.position, a + ab * t));
                    }
                    const float edge = 1.5f * d.style.thickness + 0.8f * d.style.plaitRadius;
                    cover = std::clamp((edge - nearest) / 0.002f, 0.0f, 1.0f);
                } else {
                    for (const glm::vec3& r : d.roots) nearest = std::min(nearest, glm::distance(vx.position, r));
                    cover = std::clamp((1.6f * spacing - nearest) / std::max(0.6f * spacing, 1e-4f), 0.0f, 1.0f);
                }
                if (h->label == "high-top fade") {
                    const float below = centre.y + radius * std::cos(0.95f) - vx.position.y; // under the cut
                    const bool face = vx.position.z - centre.z > 0.25f * radius && vx.position.y < centre.y + radius * std::cos(1.0f);
                    if (below > 0.0f && !face) cover = std::max(cover, 0.85f * std::clamp(1.0f - below / 0.07f, 0.0f, 1.0f));
                }
                vx.color = glm::mix(skin, d.style.rootColor, cover * 0.92f);
            }
            for (float side : { -1.0f, 1.0f }) // eyes
                addCapsule(v, idx, centre + glm::vec3(side * 0.035f, 0.015f, radius * 0.9f), centre + glm::vec3(side * 0.035f, 0.015f, radius * 0.9f), 0.012f,
                           glm::vec3(0.1f), 8);
            addCapsule(v, idx, centre + glm::vec3(0.0f, -0.02f, radius * 0.95f), centre + glm::vec3(0.0f, -0.02f, radius * 0.95f), 0.018f, skin, 10); // nose
            addCapsule(v, idx, glm::vec3(x, 1.43f, 0.0f), glm::vec3(x, 1.53f, 0.0f), 0.045f, skin, 14);                                        // neck
            h->mesh->upload(v, idx);
        }
        h->hair = m_world->addHair(d);
        h->drawn = std::make_unique<kke::HairRenderer>(*m_app);
        h->drawn->build(d);
        m_heads.push_back(std::move(h));
    }
    if (!std::getenv("KKE_CLOTH_WIND")) m_windSpeed = 3.0f;
    m_gusts = true;
    applyHairSettings();
    stepHeads(0.0f);
    if (m_camera) m_camera->setView(glm::vec3(0.1f * float(count), 1.5f, 0.0f), 0.8f + 0.55f * float(count), -0.1f, glm::pi<float>() + 0.15f); // the panel is on the right
}

// The heads look around: slow turns and nods, now and then a quick shake.
void ClothDemoModule::stepHeads(float dt) {
    for (auto& hp : m_heads) {
        Head& h = *hp;
        const float t = m_sceneTime + h.phase;
        const float shake = std::pow(std::max(0.0f, std::sin(t * 0.45f)), 12.0f) * std::sin(t * 9.0f) * 0.3f;
        const float yaw = m_headMotion * (0.7f * std::sin(t * 0.8f) + shake);
        const float nod = m_headMotion * (0.22f * std::sin(t * 1.3f + 0.5f));
        const float tilt = m_headMotion * (0.12f * std::sin(t * 0.6f + 2.0f));
        h.now = glm::translate(glm::mat4(1.0f), h.neck) * glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0)) *
                glm::rotate(glm::mat4(1.0f), nod, glm::vec3(1, 0, 0)) * glm::rotate(glm::mat4(1.0f), tilt, glm::vec3(0, 0, 1)) *
                glm::translate(glm::mat4(1.0f), -h.neck) * h.bind;
        const glm::vec3 centre = glm::vec3(h.now * glm::vec4(h.neck + glm::vec3(0.0f, 0.12f, 0.0f), 1.0f));
        const glm::quat rot = glm::quat_cast(glm::mat3(h.now));
        if (dt > 0.0f) m_world->moveKinematic(h.collider, centre, rot, dt);
        else m_world->setTransform(h.collider, centre, rot);
        m_world->setHairJoint(h.hair, h.now);
    }
}

void ClothDemoModule::redrop() {
    for (auto& p : m_cloth) m_world->resetCloth(p->id);
    for (auto& h : m_heads) m_world->resetHair(h->hair);
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
    const bool windy = m_scene == Scene::Fabrics || m_scene == Scene::Cape || m_scene == Scene::Hair;
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
    stepHeads(dt);
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
    readInput();
    const kke::Camera& cam = m_app->camera();
    int w = 0, h = 0;
    m_app->window().getFramebufferSize(w, h);
    m_pixelAngle = 2.0f * std::tan(glm::radians(cam.fovDegrees) * 0.5f) / float(std::max(h, 1));
    updateMeshes(cam.position);
    const auto t0 = Clock::now();
    for (auto& h : m_heads) {
        m_world->hairPositions(h->hair, h->guides);
        h->drawn->update(h->guides, h->now);
    }
    m_hairUploadMs = m_hairUploadMs * 0.95 + msSince(t0) * 0.05;
}

void ClothDemoModule::compute(VkCommandBuffer cmd) {
    for (auto& h : m_heads) h->drawn->compute(cmd); // the hairs' points, before the passes draw them
}

void ClothDemoModule::applyHairSettings() {
    for (auto& h : m_heads) {
        m_world->setHairMotion(h->hair, m_hairMotion);
        h->drawn->setDetail(m_hairDetail);
    }
}

void ClothDemoModule::render(const kke::RenderContext& ctx) {
    m_floor->draw(ctx, glm::mat4(1.0f), 0.0f, 0.95f);
    for (Solid& s : m_solids) s.mesh->draw(ctx, glm::mat4(1.0f), 0.0f, s.roughness);
    for (auto& p : m_cloth) p->mesh3d->drawCloth(ctx, p->fabric);
    if (m_runner) m_runner->body->draw(ctx, glm::mat4(1.0f), 0.0f, 0.7f);
    for (auto& h : m_heads) {
        h->mesh->draw(ctx, h->now * glm::inverse(h->bind), 0.0f, 0.6f);
        h->drawn->draw(ctx);
    }
    m_sphereScratch.clear();
    for (const Ball& b : m_balls) m_sphereScratch.push_back({ m_world->position(b.body), b.radius, b.color, 0.0f, 0.45f });
    if (!m_sphereScratch.empty()) m_spheres->draw(ctx, m_sphereScratch);
}

void ClothDemoModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    for (Solid& s : m_solids) s.mesh->drawShadow(ctx);
    for (auto& p : m_cloth) p->mesh3d->drawShadow(ctx);
    if (m_runner) m_runner->body->drawShadow(ctx);
    const glm::vec3 towardsLight = -m_app->lighting().lights[0].direction;
    for (auto& h : m_heads) {
        h->mesh->drawShadow(ctx, h->now * glm::inverse(h->bind));
        h->drawn->drawShadow(ctx, towardsLight);
    }
}

void ClothDemoModule::defineInput() {
    auto* in = m_app->getModule<kke::InputModule>();
    if (!in) return;
    using IM = kke::InputModule;
    kke::InputMap& m = in->map(0);
    auto action = [&](const char* id, const char* label, SDL_Scancode key, SDL_GamepadButton pad) {
        m.defineAction({ id, label, "Cloth" });
        m.addBinding(IM::bind(id, IM::key(key)));
        if (pad != SDL_GAMEPAD_BUTTON_INVALID) m.addBinding(IM::bind(id, IM::pad(pad)));
    };
    action("cloth.scene", "Next scene", SDL_SCANCODE_TAB, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    action("cloth.scene_back", "Previous scene", SDL_SCANCODE_GRAVE, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    action("cloth.redrop", "Drop again", SDL_SCANCODE_R, SDL_GAMEPAD_BUTTON_SOUTH);
    action("cloth.protection", "Clipping protection: Full, Basic, Off", SDL_SCANCODE_P, SDL_GAMEPAD_BUTTON_WEST);
    action("cloth.gusts", "Gusts on / off", SDL_SCANCODE_G, SDL_GAMEPAD_BUTTON_NORTH);
    // Keys 1-6 pick a scene directly (a controller steps with the shoulders).
    for (int i = 0; i < kSceneCount; ++i) {
        const std::string id = "cloth.scene" + std::to_string(i + 1);
        m.defineAction({ id, kSceneNames[i], "Cloth" });
        m.addBinding(IM::bind(id, IM::key(static_cast<SDL_Scancode>(SDL_SCANCODE_1 + i))));
    }
    in->commitDefaults();
}

void ClothDemoModule::readInput() {
    auto* in = m_app->getModule<kke::InputModule>();
    if (!in) return;
    const kke::InputMap& m = in->map(0);
    for (int i = 0; i < kSceneCount; ++i)
        if (m.pressed("cloth.scene" + std::to_string(i + 1))) setScene(Scene(i));
    if (m.pressed("cloth.scene")) setScene(Scene((int(m_scene) + 1) % kSceneCount));
    if (m.pressed("cloth.scene_back")) setScene(Scene((int(m_scene) + kSceneCount - 1) % kSceneCount));
    if (m.pressed("cloth.redrop")) redrop();
    if (m.pressed("cloth.gusts")) m_gusts = !m_gusts;
    if (m.pressed("cloth.protection") && m_scene != Scene::Hair) {
        m_protection = m_protection == kke::ClothProtection::Full ? kke::ClothProtection::Basic
                       : m_protection == kke::ClothProtection::Basic ? kke::ClothProtection::Off : kke::ClothProtection::Full;
        applyProtection();
    }
}

// The settings (RmlUi, kke::DemoPanelModule): View on a controller or F3
// opens them, the mouse just clicks. Rows for other scenes hide.
void ClothDemoModule::buildPanel() {
    auto* panel = m_app->getModule<kke::DemoPanelModule>();
    if (!panel) return;
    using Panel = kke::DemoPanelModule;
    auto is = [this](Scene a) { return [this, a] { return m_scene == a; }; };
    auto cloth = [this] { return m_scene != Scene::Hair; };

    auto& top = panel->section("Cloth and hair");
    top.choice("Scene", Panel::Ref<int>([this] {
                   m_sceneIndex = int(m_scene);
                   return &m_sceneIndex;
               }),
               std::vector<std::string>(kSceneNames, kSceneNames + kSceneCount), [this] { setScene(Scene(m_sceneIndex)); });
    top.hint("{cloth.scene} next scene  {cloth.redrop} drop again", "{cloth.scene_back} {cloth.scene} scene  {cloth.redrop} drop again");
    top.text([this] {
        switch (m_scene) {
        case Scene::Fabrics: return std::string("Left to right: satin, leather, wool, denim, cotton, silk.");
        case Scene::Bed: return std::string("A wool blanket, then a silk sheet, then a denim throw.");
        case Scene::Nets: return std::string("A hammock catching balls, a tennis net stopping shots.");
        case Scene::Cape: return std::string("A satin cape on a runner, through a linen curtain.");
        case Scene::Stress: return std::string("Sheets of cotton over balls, dropped again every 5 s.");
        case Scene::Hair: {
            std::string names;
            for (const auto& h : m_heads) names += (names.empty() ? "" : ", ") + h->label;
            return "A hair catalog, left to right: " + names + ".";
        }
        }
        return std::string();
    });
    top.choice("Clipping protection", Panel::Ref<int>([this] {
                   m_protectionIndex = int(m_protection);
                   return m_scene == Scene::Hair ? nullptr : &m_protectionIndex;
               }),
               { "Off", "Basic", "Full" }, [this] {
                   m_protection = kke::ClothProtection(m_protectionIndex);
                   applyProtection();
               });
    top.note("{cloth.protection} changes it. Full: cloth can't pass through cloth, not even itself. Basic: through the world, not other cloth. Off: raw Jolt.")
        .showIf(cloth);
    top.button("Drop again", [this] { redrop(); });
    top.toggle("Tour the scenes", &m_tour);

    auto& wind = panel->section("Wind");
    wind.sectionIf([this] { return m_scene == Scene::Fabrics || m_scene == Scene::Cape || m_scene == Scene::Hair; });
    wind.slider("Wind", &m_windSpeed, 0.0f, 15.0f, "%.1f m/s", {}, 0.5f);
    wind.slider("Wind from", &m_windDegrees, -180.0f, 180.0f, "%.0f deg", [this] { m_windYaw = glm::radians(m_windDegrees); }, 5.0f);
    wind.toggle("Gusts", &m_gusts);

    auto& nets = panel->section("Nets");
    nets.sectionIf(is(Scene::Nets));
    nets.toggle("Balls and shots", &m_rain);

    auto& cape = panel->section("Cape");
    cape.sectionIf(is(Scene::Cape));
    cape.slider("Run speed", Panel::Ref<float>([this] { return m_runner ? &m_runner->speed : nullptr; }), 0.0f, 8.0f, "%.1f m/s", {}, 0.5f);

    auto& stress = panel->section("Stress");
    stress.sectionIf(is(Scene::Stress));
    stress.slider("Sheets", &m_stressCount, 1, 100);
    stress.slider("Vertices per side", &m_stressRes, 8, 64);
    stress.button("Rebuild", [this] { setScene(Scene::Stress); });

    auto& hair = panel->section("Hair");
    hair.sectionIf(is(Scene::Hair));
    std::vector<std::string> pages;
    for (const HairPage& p : kHairPages) pages.push_back(p.name);
    hair.choice("Show", &m_hairShow, pages, [this] { setScene(Scene::Hair); });
    hair.slider("Head motion", &m_headMotion, 0.0f, 2.0f, "%.1f", {}, 0.1f);
    hair.slider("Hair physics (0 solid, 1 natural)", &m_hairMotion, 0.0f, 1.0f, "%.2f", [this] { applyHairSettings(); }, 0.05f);
    hair.slider("Hair detail (share drawn)", &m_hairDetail, 0.05f, 1.0f, "%.2f", [this] { applyHairSettings(); }, 0.05f);
    hair.slider("Guide strands per head", &m_hairGuides, 16, 1000);
    hair.slider("Hairs drawn per guide (0: the style's)", &m_hairsPerGuide, 0, 128);
    hair.button("Rebuild", [this] { setScene(Scene::Hair); });
    hair.note("Guides are simulated; the hairs drawn around them are built on the GPU.");

    auto& cost = panel->section("Cost");
    cost.text([this] {
        if (m_scene == Scene::Hair) {
            size_t guides = 0, verts = 0, hairs = 0, drawn = 0;
            for (auto& h : m_heads) {
                const kke::HairStats st = m_world->hairStats(h->hair);
                guides += st.guides;
                verts += st.vertices;
                hairs += h->drawn->hairs();
                drawn += h->drawn->drawnHairs();
            }
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%zu guide strands (%zu vertices), %zu of %zu hairs drawn", guides, verts, drawn, hairs);
            return std::string(buf);
        }
        uint32_t verts = 0, tris = 0, asleep = 0;
        for (auto& p : m_cloth) {
            const kke::ClothStats st = m_world->clothStats(p->id);
            verts += st.vertices;
            tris += st.triangles;
            asleep += st.sleeping ? 1u : 0u;
        }
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%zu cloths (%u asleep), %u vertices, %u triangles", m_cloth.size(), asleep, verts, tris);
        return std::string(buf);
    });
    cost.text([this] {
        char buf[256];
        if (m_scene == Scene::Hair)
            std::snprintf(buf, sizeof(buf), "Physics step %.2f ms (hair and all), guides to the GPU %.2f ms", m_stepMs, m_hairUploadMs);
        else
            std::snprintf(buf, sizeof(buf), "Physics step %.2f ms (protection %.2f ms), meshes %.2f ms", m_stepMs, m_protectMs, m_meshMs);
        return std::string(buf);
    });
    cost.text([this] {
        if (m_scene == Scene::Hair) return std::string();
        uint32_t contacts = 0, undone = 0;
        for (auto& p : m_cloth) {
            const kke::ClothStats st = m_world->clothStats(p->id);
            contacts += st.selfContacts;
            undone += st.crossingsUndone;
        }
        return "Cloth contacts " + std::to_string(contacts) + ", crossings undone " + std::to_string(undone);
    });
    cost.text([this] {
        if (m_scene == Scene::Hair) return std::string();
        if (!m_gpu) return std::string("Pair search on the CPU (this GPU has no compute queue of its own)");
        const kke::ClothGpu::Stats st = m_gpu->stats();
        char buf[200];
        if (st.cpuMs <= 0.0)
            std::snprintf(buf, sizeof(buf), "Pair search on the GPU: %.2f ms a search (%llu searched, %llu left to the CPU)", st.gpuMs,
                          static_cast<unsigned long long>(st.searches), static_cast<unsigned long long>(st.declined));
        else
            std::snprintf(buf, sizeof(buf), "Pair search on the %s, the faster here: GPU %.2f ms, CPU %.2f ms a search", st.onCpu ? "CPU" : "GPU",
                          st.gpuMs, st.cpuMs);
        return std::string(buf);
    });
}

} // namespace kke_cloth
