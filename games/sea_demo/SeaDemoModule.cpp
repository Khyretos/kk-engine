// The sea demo: ships, captains, the camera, the menus (README.md).
// Battle.cpp has the guns and the damage, Effects.cpp the spray, smoke and
// wakes, World.cpp the islands, SeaNet.cpp sailing online.

#include "SeaDemoModule.h"

#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/EngineSettings.h"
#include "kke/Log.h"
#include "kke/Mesh.h"
#include "kke/Picking.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/PhysicsModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace kke_sea {

namespace {

const SeaDemoModule::Kind kKinds[] = {
    { "Foam block (100 kg/m3)", 100.0f, { 0.35f, 0.25f, 0.35f }, { 0.95f, 0.93f, 0.85f } },
    { "Wooden crate (500)", 500.0f, { 0.4f, 0.4f, 0.4f }, { 0.62f, 0.42f, 0.22f } },
    { "Sealed barrel (650)", 650.0f, { 0.3f, 0.45f, 0.3f }, { 0.25f, 0.45f, 0.3f } },
    { "Ice block (917)", 917.0f, { 0.5f, 0.35f, 0.5f }, { 0.8f, 0.92f, 1.0f } },
    { "Iron block (7800)", 7800.0f, { 0.25f, 0.25f, 0.25f }, { 0.35f, 0.36f, 0.4f } },
};
constexpr int kKindCount = static_cast<int>(sizeof(kKinds) / sizeof(kKinds[0]));
constexpr size_t kMaxThrown = 40; // budget: the oldest thrown thing goes first

struct ModeInfo { const char* label; const char* lobby; };
const ModeInfo kModes[] = {
    { "Battle", "Enemy ships hunt you and fire back" },
    { "Target practice", "Enemy ships lie at anchor: sink them" },
    { "Free sail", "No enemies: sail, throw things, watch the sea" },
};
// The lobby lists the ships from the all-rounder down (its first choice is
// the default): brig, schooner, man-o'-war, longboat, rowing boat.
const int kLobbyShips[] = { 3, 2, 4, 1, 0 };
constexpr float kRespawnTime = 6.0f;
constexpr float kEnemyRespawnTime = 9.0f;

void appendUnitBox(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx, const glm::vec3& color) {
    const glm::vec3 normals[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (const glm::vec3& n : normals) {
        const glm::vec3 u = std::abs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 w = glm::cross(n, u);
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (glm::vec2 k : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1) })
            v.push_back({ (n + u * k.x + w * k.y) * 0.5f, color, n, glm::vec2(0.0f) });
        if (glm::dot(glm::cross(u, w), n) >= 0.0f) idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        else idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    }
}

float wrapPi(float a) {
    while (a > glm::pi<float>()) a -= glm::two_pi<float>();
    while (a < -glm::pi<float>()) a += glm::two_pi<float>();
    return a;
}

} // namespace

SeaDemoModule::SeaDemoModule() = default;
SeaDemoModule::~SeaDemoModule() = default;

float SeaDemoModule::random01() {
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return static_cast<float>(m_rng >> 8) * (1.0f / 16777216.0f);
}

void SeaDemoModule::init(kke::Application& app) {
    m_app = &app;
    m_models = app.getModule<kke::ModelModule>();
    m_lobby = app.getModule<kke::LobbyModule>();
    m_net = app.getModule<kke::NetModule>();
    kke::Camera& cam = app.camera();
    cam.nearPlane = 0.3f;
    cam.farPlane = 2500.0f; // the sea to the horizon
    cam.fovDegrees = 62.0f;
    defineInput();
    m_library.load(app, m_models);
    // 256 x 256 cells: 0.8 m ones over the middle 100 m (every wave the
    // ships float on), growing out to a 2.4 km horizon.
    m_ocean = std::make_unique<kke::OceanRenderer>(app, 256, 200.0f, 2400.0f);
    m_spheres = std::make_unique<kke::SphereImpostorRenderer>(app);
    m_fx = std::make_unique<kke::ParticleEffects>(app, 9000);
    for (const Kind& k : kKinds) {
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        appendUnitBox(v, idx, k.color);
        auto mesh = std::make_unique<kke::DynamicMeshRenderer>(app);
        mesh->upload(v, idx);
        m_kindMeshes.push_back(std::move(mesh));
    }
    {
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        appendUnitBox(v, idx, { 0.5f, 0.34f, 0.2f });
        m_plankMesh = std::make_unique<kke::DynamicMeshRenderer>(app);
        m_plankMesh->upload(v, idx);
    }
    m_bodies.seaFloorY = -40.0f; // deep water: a sunk man-o'-war goes all the way down
    buildWorld();
    buildPanel();
    setupNet();
    if (auto* shell = app.getModule<kke::GameShellModule>()) {
        shell->onMainMenu = [this] {
            m_started = false;
            if (m_net) m_net->leave();
            if (m_lobby) m_lobby->open();
            m_app->views().clear();
        };
        shell->addPauseItem("Change ship", [this] {
            if (m_captains.empty()) return;
            Captain& c = m_captains.front();
            c.cls = (c.cls + 1) % static_cast<int>(shipClasses().size());
            respawnLocal(0);
        });
        shell->addPauseItem("Restart", [this] { startVoyage(); });
        auto& s = shell->settings("Sea");
        s.toggle("Wind fills the sails", &m_windMatters);
        s.toggle("Aim help (the guns find the nearest ship)", &m_aimHelp);
        s.toggle("Players' shots hit each other", &m_friendlyFire);
    }
    if (m_lobby) {
        kke::Lobby& l = m_lobby->lobby();
        std::vector<std::string> ships;
        for (int c : kLobbyShips) ships.push_back(shipClasses()[static_cast<size_t>(c)].name);
        l.addLookField({ "ship", "Ship", ships, {} });
        std::vector<std::string> modes;
        for (const ModeInfo& m : kModes) modes.push_back(m.label);
        l.addOption({ "mode", "Mode", modes, 0, true, {}, {} });
        std::vector<std::string> enemyShips = { "A mix" };
        for (const ShipClass& c : shipClasses()) enemyShips.push_back(c.name);
        l.addOption({ "enemy", "Enemy ships", enemyShips, 0, true, {}, {} });
        l.setCpuCount(2);
        m_lobby->load();
        m_lobby->setTitle("SEA", "Pick a ship. Another controller? Press {a} on it to sail along (split screen).");
    }
    reset();
    // Behind the menu: a ship already sailing.
    if (!m_lobby || !m_lobby->isOpen()) startVoyage();
    else {
        Captain c;
        c.cls = 3;
        c.name = "Captain";
        m_captains.push_back(c);
        m_captains.back().ship = spawnShip(3, kPlayers, { 0.0f, 0.5f, 0.0f }, 0.6f, false);
    }
}

void SeaDemoModule::shutdown() {
    for (Ship& s : m_ships) removeArt(s);
    for (Floater& f : m_floaters)
        for (kke::ModelModule::InstanceId id : f.instances)
            if (m_models) m_models->remove(id);
    for (kke::ModelModule::InstanceId id : m_worldInstances)
        if (m_models) m_models->remove(id);
    m_ships.clear();
    m_floaters.clear();
    m_worldInstances.clear();
    m_fx.reset();
}

void SeaDemoModule::onSettingsChanged(const kke::EngineSettings& settings) {
    m_lookSpeed = settings.controls.mouseSensitivity;
    m_invertY = settings.controls.invertY;
}

// ---- the voyage

void SeaDemoModule::reset() {
    for (Ship& s : m_ships) removeArt(s);
    for (Floater& f : m_floaters)
        for (kke::ModelModule::InstanceId id : f.instances) m_models->remove(id);
    m_ships.clear();
    m_floaters.clear();
    m_balls.clear();
    m_captains.clear();
    m_enemyShips.clear();
    m_enemyRespawn.clear();
    m_enemySkill.clear();
    m_bodies.clear();
    if (m_fx) m_fx->clear();
    m_waves.setWind(m_windSpeed, m_windDir, m_chop);
}

void SeaDemoModule::startVoyage() {
    reset();
    m_started = true;
    int cpus = 2;
    std::vector<int> seats = { 0 };
    std::vector<int> classes = { 3 };
    if (m_lobby) {
        kke::Lobby& l = m_lobby->lobby();
        if (m_lobby->isOpen()) {
            m_lobby->save();
            m_lobby->close();
        }
        m_lobby->applyInput();
        if (const kke::Lobby::Option* o = l.option("mode")) m_mode = o->value;
        if (const kke::Lobby::Option* o = l.option("enemy")) m_enemyClass = o->value;
        cpus = l.cpuCount();
        seats = l.joinedSeats();
        if (seats.empty()) seats = { 0 };
        classes.clear();
        for (int seat : seats) {
            const kke::Lobby::Seat& st = l.seat(seat);
            classes.push_back(kLobbyShips[st.look.empty() ? 0 : std::clamp(st.look[0], 0, 4)]);
        }
        for (int i = 0; i < cpus; ++i) m_enemySkill.push_back(l.cpuDifficulty(i));
    } else {
        m_enemySkill = { 1, 1 };
    }
    if (const char* m = kke::dev::env("KKE_SEA_MODE")) m_mode = std::clamp(std::atoi(m), 0, 2);
    if (const char* e = kke::dev::env("KKE_SEA_ENEMIES")) {
        cpus = std::clamp(std::atoi(e), 0, 6);
        m_enemySkill.assign(static_cast<size_t>(cpus), 1);
    }
    if (const char* sh = kke::dev::env("KKE_SEA_SHIP")) classes[0] = std::clamp(std::atoi(sh), 0, static_cast<int>(shipClasses().size()) - 1);
    for (size_t i = 0; i < seats.size(); ++i) {
        Captain c;
        c.seat = seats[i];
        c.input = m_lobby ? std::max(0, m_lobby->playerOf(seats[i])) : 0;
        c.cls = classes[i];
        c.name = "Captain " + std::to_string(i + 1);
        const float heading = 0.6f;
        const glm::vec3 at = glm::vec3(static_cast<float>(i) * 30.0f, 0.5f, static_cast<float>(i) * -12.0f);
        c.ship = spawnShip(c.cls, kPlayers, at, heading, true);
        c.rig.yaw = glm::degrees(heading) + 180.0f;
        m_captains.push_back(c);
    }
    if (m_mode == kModeFree) cpus = 0;
    m_enemyShips.assign(static_cast<size_t>(cpus), -1);
    m_enemyRespawn.assign(static_cast<size_t>(cpus), 0.0f);
    m_enemySkill.resize(static_cast<size_t>(cpus), 1);
    if (!m_net || m_net->authority()) spawnEnemies();
    // A few things bobbing about to begin with.
    for (int i = 0; i < kKindCount; ++i) {
        Floater f;
        f.bit = Bit::Thrown;
        f.kind = i;
        f.body = m_bodies.add(kKinds[i].halfExtents, kKinds[i].density, { 14.0f + static_cast<float>(i) * 2.2f, 1.5f, 10.0f });
        m_floaters.push_back(std::move(f));
    }
    updateViews();
    kke::log::get(name())->info("{}: {} captain(s), {} enemy ship(s)", kModes[m_mode].label, m_captains.size(), m_enemyShips.size());
}

int SeaDemoModule::spawnShip(int cls, int team, const glm::vec3& at, float heading, bool local) {
    const ShipClass& c = shipClasses()[static_cast<size_t>(cls)];
    const ShipArt& art = m_library.art(cls);
    // Reuse a sunk ship's slot (indices stay valid for captains and enemies).
    size_t index = m_ships.size();
    for (size_t i = 0; i < m_ships.size(); ++i)
        if (!m_ships[i].alive) {
            index = i;
            break;
        }
    if (index == m_ships.size()) m_ships.emplace_back();
    Ship& s = m_ships[index];
    s = Ship{};
    s.cls = cls;
    s.team = team;
    s.local = local;
    s.health = s.maxHealth = c.health;
    s.sail = c.oars ? 0 : 1;
    s.sailShown = c.oars ? 0.0f : 0.5f;
    s.mastUp.assign(art.masts.size(), true);
    s.mastHealth.assign(art.masts.size(), c.health * 0.35f);
    s.home = at;
    s.aiSide = random01() < 0.5f ? 0 : 1;
    // The box floats where the hull is: its centre, the hull's centre.
    // Density: a wooden hull with ballast, guns and air in it floats with
    // ~45% of the box under water; the ballast sits low.
    const glm::quat rot = glm::angleAxis(heading, glm::vec3(0, 1, 0));
    const glm::vec3 centre = at + rot * art.hullCenter;
    s.body = m_bodies.add(art.hullHalf, 470.0f, centre, rot);
    kke::FloatingBody& b = m_bodies.bodies()[s.body];
    b.linearDrag = 0.35f;
    b.heaveDrag = 4.0f;
    b.angularDrag = 2.5f;
    b.centerOfMassOffset = glm::vec3(0.0f, -art.hullHalf.y * 0.85f, 0.0f);
    s.baseMass = b.mass;
    s.drawPos = b.position;
    s.drawRot = b.orientation;
    spawnArt(s);
    return static_cast<int>(index);
}

void SeaDemoModule::removeShip(size_t index) {
    if (index >= m_ships.size()) return;
    Ship& s = m_ships[index];
    removeArt(s);
    if (s.alive) m_bodies.remove(s.body);
    s.alive = false;
}

void SeaDemoModule::respawnLocal(int captain) {
    if (captain < 0 || captain >= static_cast<int>(m_captains.size())) return;
    Captain& c = m_captains[static_cast<size_t>(captain)];
    glm::vec3 at(static_cast<float>(captain) * 30.0f, 0.5f, static_cast<float>(captain) * -12.0f);
    float heading = 0.6f;
    if (c.ship >= 0 && m_ships[static_cast<size_t>(c.ship)].alive && !m_ships[static_cast<size_t>(c.ship)].sinking) {
        // Changing ship: the new one takes the old one's place.
        const kke::FloatingBody& b = m_bodies.bodies()[m_ships[static_cast<size_t>(c.ship)].body];
        at = glm::vec3(b.position.x, 0.5f, b.position.z);
        const glm::vec3 fwd = glm::mat3_cast(b.orientation) * glm::vec3(0, 0, 1);
        heading = std::atan2(fwd.x, fwd.z);
    }
    if (c.ship >= 0) removeShip(static_cast<size_t>(c.ship));
    c.ship = spawnShip(c.cls, kPlayers, at, heading, true);
    m_ships[static_cast<size_t>(c.ship)].name = c.name;
    c.respawn = -1.0f;
}

SeaDemoModule::Captain* SeaDemoModule::captainOf(size_t ship) {
    for (Captain& c : m_captains)
        if (c.ship == static_cast<int>(ship)) return &c;
    return nullptr;
}

// ---- art

void SeaDemoModule::spawnArt(Ship& s) {
    removeArt(s);
    const ShipArt& art = m_library.art(s.cls);
    if (!art.loaded || !m_models) return;
    for (const ShipArt::Part& p : art.parts) s.instances.push_back(m_models->spawn(p.model, glm::mat4(1.0f)));
    // A flag at the top of the main mast: pirate black for players, the
    // navies' colours for the enemies.
    if (!art.masts.empty()) {
        const char* flag = s.team == kPlayers ? "SM_Flag_Pirate_01" : (s.cls % 2 == 0 ? "SM_Flag_British_01" : "SM_Flag_Spanish_01");
        if (kke::ModelModule::ModelId id = m_library.prop(flag)) s.flag = m_models->spawn(id, glm::mat4(1.0f));
    }
}

void SeaDemoModule::removeArt(Ship& s) {
    if (!m_models) return;
    for (kke::ModelModule::InstanceId id : s.instances) m_models->remove(id);
    s.instances.clear();
    if (s.flag) m_models->remove(s.flag);
    s.flag = 0;
}

glm::mat4 SeaDemoModule::shipMatrix(const Ship& s) const {
    const ShipArt& art = m_library.art(s.cls);
    // Body space -> ship space: the body sits at the hull's centre.
    return glm::translate(glm::mat4(1.0f), s.drawPos) * glm::mat4_cast(s.drawRot) * glm::translate(glm::mat4(1.0f), -art.hullCenter);
}

// ---- sailing

float SeaDemoModule::windFactor(const Ship& s) const {
    if (!m_windMatters) return 1.0f;
    const kke::FloatingBody& b = m_bodies.bodies()[s.body];
    const glm::vec3 fwd = glm::mat3_cast(b.orientation) * glm::vec3(0, 0, 1);
    const glm::vec2 f = glm::length(glm::vec2(fwd.x, fwd.z)) > 1e-4f ? glm::normalize(glm::vec2(fwd.x, fwd.z)) : glm::vec2(0, 1);
    const glm::vec2 to(std::cos(m_windDir), std::sin(m_windDir)); // the wind blows toward (as the waves travel)
    // Square-riggers sail best with the wind on the quarter and can't
    // point much closer than 60 degrees to it: in irons they barely move.
    const float c = glm::dot(f, to);
    const float point = std::max(0.18f, 0.6f + 0.42f * c);
    return point * std::clamp(m_windSpeed / 7.0f, 0.35f, 1.4f);
}

void SeaDemoModule::sailShip(Ship& s, float dt) {
    if (!s.alive || s.remote) return;
    const ShipClass& c = shipClasses()[static_cast<size_t>(s.cls)];
    const ShipArt& art = m_library.art(s.cls);
    kke::FloatingBody& b = m_bodies.bodies()[s.body];
    const glm::mat3 R = glm::mat3_cast(b.orientation);
    const glm::vec3 fwd = R * glm::vec3(0, 0, 1), side = R * glm::vec3(1, 0, 0), up = R * glm::vec3(0, 1, 0);
    s.sailShown += (static_cast<float>(s.sail) * 0.5f - s.sailShown) * std::min(1.0f, dt * 0.8f); // hands on the yards: a few seconds
    // Water in the hull: a hurt ship sits lower (and a sinking one goes).
    const float hurt = 1.0f - std::clamp(s.health / s.maxHealth, 0.0f, 1.0f);
    float wantMass = s.baseMass * (1.0f + 0.35f * hurt);
    if (s.sinking) wantMass = s.baseMass * (1.6f + s.sinkTime * 0.12f);
    if (std::abs(b.mass - wantMass) > 1e-3f * s.baseMass) {
        const float k = wantMass / b.mass;
        b.mass = wantMass;
        b.inertiaBody *= k;
    }
    if (b.submerged < 0.05f || s.sinking) return;
    float drive = 0.0f;
    if (c.oars) {
        drive = s.throttle;
    } else {
        size_t up_ = 0;
        for (bool m : s.mastUp) up_ += m ? 1u : 0u;
        const float rig = s.mastUp.empty() ? 1.0f : static_cast<float>(up_) / static_cast<float>(s.mastUp.size());
        drive = s.sailShown * windFactor(s) * rig;
    }
    const float vFwd = glm::dot(b.velocity, fwd);
    const float vSide = glm::dot(b.velocity, side);
    // Thrust and a quadratic hull drag that balance at the top speed.
    const float dragK = c.acceleration / (c.topSpeed * c.topSpeed);
    const float along = b.mass * (c.acceleration * drive - dragK * vFwd * std::abs(vFwd) - (vFwd > 0.0f ? 0.0f : 0.0f));
    m_bodies.applyForce(s.body, fwd * along, b.position);
    // The keel: water resists sideways motion far more than forward. Below
    // the centre, so a ship heels outward in a turn like a real one.
    m_bodies.applyForce(s.body, -side * (vSide * b.mass * 1.6f), b.position - up * (art.hullHalf.y * 0.9f));
    // The rudder turns the ship only when water flows past it; rowed boats
    // can pull one side and turn on the spot.
    float flow = std::clamp(std::abs(vFwd) / (0.35f * c.topSpeed), 0.0f, 1.0f);
    if (c.oars) flow = std::max(flow, 0.35f + 0.4f * std::abs(s.throttle));
    else flow = std::max(flow, 0.08f);
    const float targetYaw = -s.rudder * glm::radians(c.turnRate) * flow * (vFwd < -0.3f ? -1.0f : 1.0f);
    const float yawRate = glm::dot(b.angularVelocity, up);
    const float torque = b.inertiaBody.y * (targetYaw - yawRate) * 1.8f;
    // A couple: equal and opposite side forces at the bow and the stern.
    const float L = std::max(art.hullHalf.z, 1.0f);
    const glm::vec3 F = side * (torque / (2.0f * L));
    m_bodies.applyForce(s.body, F, b.position + fwd * L);
    m_bodies.applyForce(s.body, -F, b.position - fwd * L);
}

void SeaDemoModule::steerCaptain(Captain& c, float dt) {
    if (c.ship < 0) return;
    Ship& s = m_ships[static_cast<size_t>(c.ship)];
    auto* in = m_app->getModule<kke::InputModule>();
    if (!in || c.input >= in->players() || !s.alive) return;
    const kke::InputMap& m = in->map(c.input);
    const ShipClass& cls = shipClasses()[static_cast<size_t>(s.cls)];
    const float steer = m.axis("sea.steer");
    s.rudder += (steer - s.rudder) * std::min(1.0f, dt * 3.0f);
    const float sails = m.axis("sea.sails");
    if (cls.oars) {
        const float t = sails < 0.0f ? sails * 0.5f : sails;
        s.throttle += (t - s.throttle) * std::min(1.0f, dt * 2.0f);
    } else {
        // Push up for more sail, down for less (one step a push), like
        // Black Flag: furled, half sail, full sail.
        if (sails > 0.6f && c.sailInput <= 0.6f) s.sail = std::min(2, s.sail + 1);
        if (sails < -0.6f && c.sailInput >= -0.6f) s.sail = std::max(0, s.sail - 1);
    }
    c.sailInput = sails;
    if (m.pressed("sea.ship")) {
        c.cls = (c.cls + 1) % static_cast<int>(shipClasses().size());
        respawnLocal(static_cast<int>(&c - m_captains.data()));
        return;
    }
    // Guns: hold to aim (the arc shows where the balls will fall), fire.
    c.aiming = m.held("sea.aim");
    if (c.input == 0 && !m_app->uiCapturesMouse() && !ImGui::GetIO().WantCaptureMouse && m_app->window().mouseState().rightButtonDown)
        c.aiming = true;
    c.aimSideNow = aimSide(c, s);
    c.elevation = aimElevation(c);
    if (c.aiming) predictArc(s, c.aimSideNow, c.elevation, c.arc);
    else c.arc.clear();
    if (m.pressed("sea.fire")) fire(s, c.aimSideNow, c.elevation);
    if (m.pressed("sea.throw") && c.input == 0) throwObject(m_kind, false);
    if (m.pressed("sea.next")) m_kind = (m_kind + 1) % kKindCount;
    for (int k = 0; k < kKindCount; ++k)
        if (m.pressed("sea.kind" + std::to_string(k + 1))) m_kind = k;
}

// ---- the camera: a boom behind the ship (kke::CameraRig), turned with
// the right stick or the mouse (right or middle button held), the wheel
// or the d-pad for distance. Left alone it swings back behind the ship.

void SeaDemoModule::updateCamera(Captain& c, float dt) {
    if (c.ship < 0) return;
    const Ship& s = m_ships[static_cast<size_t>(c.ship)];
    const ShipClass& cls = shipClasses()[static_cast<size_t>(s.cls)];
    const ShipArt& art = m_library.art(s.cls);
    auto* in = m_app->getModule<kke::InputModule>();
    glm::vec2 look(0.0f);
    float zoom = 0.0f;
    if (in && c.input < in->players()) {
        const kke::InputMap& m = in->map(c.input);
        look = m.axis2("sea.look") * glm::vec2(140.0f, 90.0f) * dt * m_lookSpeed;
        zoom = m.axis("sea.zoom");
    }
    if (c.input == 0 && !m_app->uiCapturesMouse() && !ImGui::GetIO().WantCaptureMouse) {
        const auto& mouse = m_app->window().mouseState();
        if (mouse.rightButtonDown || mouse.middleButtonDown) look += glm::vec2(mouse.deltaX, mouse.deltaY) * 0.25f * m_lookSpeed;
        zoom += mouse.scrollDelta * 4.0f;
    }
    if (m_invertY) look.y = -look.y;
    c.rig.addLook(-look.x, -look.y);
    c.zoom = std::clamp(c.zoom * (1.0f - zoom * dt * 1.2f), 0.35f, 3.0f);
    const kke::FloatingBody& b = m_bodies.bodies()[s.body];
    const glm::vec3 fwd = glm::mat3_cast(s.drawRot) * glm::vec3(0, 0, 1);
    if (glm::length(look) > 0.01f || c.aiming) c.lookIdle = 0.0f;
    else c.lookIdle += dt;
    // Left alone for a while, the camera swings back behind the ship.
    if (c.lookIdle > 2.5f && glm::length(b.velocity) > 1.0f) {
        const float behind = glm::degrees(std::atan2(-fwd.x, -fwd.z)); // yaw 0 looks along -Z
        const float d = glm::degrees(wrapPi(glm::radians(behind - c.rig.yaw)));
        c.rig.yaw += d * std::min(1.0f, dt * 0.6f);
    }
    c.rig.settings.pivotHeight = art.deckY + 2.0f + cls.length * 0.12f;
    c.rig.settings.armLength = (6.0f + cls.length * 1.15f) * c.zoom;
    c.rig.settings.shoulderOffset = 0.0f;
    c.rig.settings.positionLag = 6.0f;
    c.rig.settings.pitchMin = -60.0f;
    c.rig.settings.pitchMax = 25.0f;
    c.rig.settings.probeRadius = 0.5f;
    c.rig.settings.fovDegrees = 62.0f;
    // The camera stays above the waves.
    const float t = m_time;
    auto ray = [this, t](const glm::vec3& from, const glm::vec3& dir, float maxDistance) {
        for (int i = 1; i <= 16; ++i) {
            const float d = maxDistance * static_cast<float>(i) / 16.0f;
            const glm::vec3 p = from + dir * d;
            if (p.y < m_waves.height({ p.x, p.z }, t) + 1.2f) return std::max(0.0f, d - maxDistance / 16.0f);
        }
        return maxDistance;
    };
    const glm::vec3 focus = s.drawPos - glm::mat3_cast(s.drawRot) * glm::vec3(0.0f, art.hullCenter.y, 0.0f);
    c.camera.farPlane = 2500.0f;
    c.camera.nearPlane = 0.3f;
    c.rig.update(dt, glm::vec3(focus.x, focus.y, focus.z), ray, c.camera);
}

void SeaDemoModule::updateViews() {
    std::vector<kke::Application::View>& views = m_app->views();
    if (m_captains.size() <= 1) {
        views.clear();
        if (!m_captains.empty()) {
            kke::Camera& cam = m_app->camera();
            const kke::Camera& c = m_captains.front().camera;
            cam.position = c.position;
            cam.target = c.target;
            cam.up = c.up;
            cam.fovDegrees = c.fovDegrees;
        }
        return;
    }
    const std::vector<kke::ViewRect> rects = kke::splitScreen(static_cast<int>(m_captains.size()));
    views.resize(m_captains.size());
    for (size_t i = 0; i < m_captains.size(); ++i) {
        views[i].camera = m_captains[i].camera;
        views[i].rect = rects[std::min(i, rects.size() - 1)];
    }
    m_app->camera() = m_captains.front().camera;
}

// ---- the frame

void SeaDemoModule::fixedUpdate(const kke::FixedUpdateContext& ctx) {
    const float dt = ctx.fixedDt;
    m_time += dt;
    for (Ship& s : m_ships) sailShip(s, dt);
    // Previous poses: ships and floaters are drawn between the last two ticks.
    m_prevPos.resize(m_bodies.bodies().size());
    m_prevRot.resize(m_bodies.bodies().size());
    for (size_t i = 0; i < m_bodies.bodies().size(); ++i) {
        m_prevPos[i] = m_bodies.bodies()[i].position;
        m_prevRot[i] = m_bodies.bodies()[i].orientation;
    }
    m_bodies.step(dt, m_waves, m_time);
    keepOffIslands();
    for (size_t i = 0; i < m_ships.size(); ++i) {
        Ship& s = m_ships[i];
        if (!s.alive) continue;
        if (s.sinking) {
            s.sinkTime += dt;
            const kke::FloatingBody& b = m_bodies.bodies()[s.body];
            if (s.sinkTime > 30.0f || b.position.y < m_bodies.seaFloorY + 2.0f) removeShip(i);
        }
    }
    stepBalls(dt);
    stepDebris(dt);
    stepFort(dt);
    for (const kke::FloatingBody& b : m_bodies.bodies())
        if (b.alive && b.impactSpeed > 1.5f) splash(b.position, b.impactSpeed);
}

void SeaDemoModule::update(const kke::UpdateContext& ctx) {
    const float dt = std::min(ctx.dt, 0.1f);
    m_alpha = ctx.alpha;
    receiveNet();
    // KKE_SEA_START=1 (developer switch): sail at once with the lobby's
    // saved choices, for headless checks (tools/check_game).
    if (!m_started && m_lobby && m_lobby->isOpen() && kke::dev::flag("KKE_SEA_START")) m_lobby->close();
    if (m_lobby && m_lobby->isOpen()) {
        if (m_lobby->lobby().takeStart()) startVoyage();
    } else if (!m_started) {
        startVoyage();
    }
    // The settings panel stays out of the start menu's way (it covered
    // player 1's card and its rows), folded to its tab for after.
    if (auto* panel = m_app->getModule<kke::DemoPanelModule>()) {
        const bool inMenu = m_lobby && m_lobby->isOpen();
        panel->setVisible(!inMenu);
        if (inMenu && panel->state() != kke::DemoPanelModule::State::Collapsed) panel->setState(kke::DemoPanelModule::State::Collapsed);
    }
    // Drawn poses, between the last two physics ticks.
    for (Ship& s : m_ships) {
        if (!s.alive || s.remote) continue;
        const kke::FloatingBody& b = m_bodies.bodies()[s.body];
        if (s.body < m_prevPos.size()) {
            s.drawPos = glm::mix(m_prevPos[s.body], b.position, m_alpha);
            s.drawRot = glm::slerp(m_prevRot[s.body], b.orientation, m_alpha);
        } else {
            s.drawPos = b.position;
            s.drawRot = b.orientation;
        }
    }
    for (Captain& c : m_captains) {
        if (m_started) steerCaptain(c, dt);
        if (c.respawn > 0.0f) {
            c.respawn -= dt;
            if (c.respawn <= 0.0f) respawnLocal(static_cast<int>(&c - m_captains.data()));
        }
        if (c.ship >= 0 && !m_ships[static_cast<size_t>(c.ship)].alive && c.respawn <= 0.0f) c.respawn = kRespawnTime;
        updateCamera(c, dt);
    }
    if (m_started && (!m_net || m_net->authority())) {
        for (size_t i = 0; i < m_ships.size(); ++i)
            if (m_ships[i].alive && !m_ships[i].local && !m_ships[i].remote && m_ships[i].team == kEnemies) thinkEnemy(i, dt);
        // Enemies come back after a while.
        for (size_t e = 0; e < m_enemyShips.size(); ++e) {
            const int si = m_enemyShips[e];
            if (si >= 0 && m_ships[static_cast<size_t>(si)].alive) continue;
            if (!m_enemiesRespawn && si >= 0) continue;
            if (m_enemyRespawn[e] <= 0.0f) m_enemyRespawn[e] = kEnemyRespawnTime;
            m_enemyRespawn[e] -= dt;
            if (m_enemyRespawn[e] <= 0.0f) {
                m_enemyShips[e] = -1;
                spawnEnemies();
            }
        }
    }
    // The ships' art follows their bodies; sails show how much is set.
    for (Ship& s : m_ships) {
        if (!s.alive) continue;
        wake(s, dt);
        burn(s, dt);
        const ShipArt& art = m_library.art(s.cls);
        if (!art.loaded || s.instances.size() != art.parts.size()) continue;
        const glm::mat4 world = shipMatrix(s) * art.modelToShip;
        const glm::mat4 shipWorld = shipMatrix(s);
        for (size_t p = 0; p < art.parts.size(); ++p) {
            const ShipArt::Part& part = art.parts[p];
            const bool mastGone = part.mast >= 0 && part.mast < static_cast<int>(s.mastUp.size()) && !s.mastUp[static_cast<size_t>(part.mast)];
            if (part.role == ShipArt::Role::Sail) {
                const float shown = std::clamp(s.sailShown * 2.0f, 0.0f, 1.0f);
                m_models->setVisible(s.instances[p], !mastGone && shown > 0.06f);
                // Reefed: the sail gathered up toward its yard.
                const float k = 0.25f + 0.75f * shown;
                const glm::vec3 top(0.0f, part.hi.y, 0.0f);
                const glm::mat4 reef = glm::translate(glm::mat4(1.0f), top) * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, k, 1.0f)) *
                                       glm::translate(glm::mat4(1.0f), -top);
                m_models->setTransform(s.instances[p], shipWorld * reef * art.modelToShip);
            } else {
                m_models->setVisible(s.instances[p], !mastGone);
                m_models->setTransform(s.instances[p], world);
            }
        }
        if (s.dentsChanged && !s.dented.empty()) {
            m_models->setDeformedVertices(s.instances[0], s.dented, s.dentedNormals, true);
            s.dentsChanged = false;
        }
        if (s.flag && !art.masts.empty()) {
            // The main mast: the tallest.
            size_t main = 0;
            for (size_t m = 1; m < art.masts.size(); ++m)
                if (art.masts[m].height > art.masts[main].height) main = m;
            const bool up = s.mastUp.empty() || s.mastUp[main];
            m_models->setVisible(s.flag, up);
            const glm::vec3 top = glm::vec3(shipWorld * glm::vec4(art.masts[main].base + glm::vec3(0.0f, art.masts[main].height + 0.4f, 0.0f), 1.0f));
            // The flag streams downwind (its cloth runs along -Z), flapping a little.
            const glm::vec3 w = wind();
            const float yaw = std::atan2(-w.x, -w.z) + 0.12f * std::sin(m_time * 6.0f);
            const float scale = std::clamp(shipClasses()[static_cast<size_t>(s.cls)].length / 14.0f, 0.8f, 2.2f);
            m_models->setTransform(s.flag, glm::translate(glm::mat4(1.0f), top) * glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0)) *
                                               glm::scale(glm::mat4(1.0f), glm::vec3(scale)));
        }
    }
    // Floaters' art (broken masts).
    for (Floater& f : m_floaters) {
        if (f.instances.empty()) continue;
        const kke::FloatingBody& b = m_bodies.bodies()[f.body];
        const glm::mat4 world = b.transform() * f.artOffset;
        const ShipArt& art = m_library.art(f.kind);
        for (kke::ModelModule::InstanceId id : f.instances) m_models->setTransform(id, world * art.modelToShip);
    }
    if (m_fx) m_fx->update(dt, wind());
    sendNet();
    updateViews();
}

void SeaDemoModule::throwObject(int kind, bool atMouse) {
    const kke::Camera& cam = m_app->camera();
    const auto& mouse = m_app->window().mouseState();
    int w = 1, h = 1;
    SDL_GetWindowSize(m_app->window().handle(), &w, &h);
    glm::mat4 view = glm::lookAt(cam.position, cam.target, cam.up);
    glm::mat4 proj = kke::engineProjection(cam.fovDegrees, float(w) / float(std::max(h, 1)), cam.nearPlane, cam.farPlane);
    // The mouse throws where it points; a controller throws at the middle
    // of the screen (where the camera looks).
    const glm::vec2 at = atMouse ? glm::vec2(mouse.x, mouse.y) : glm::vec2(float(w) * 0.5f, float(h) * 0.5f);
    kke::Ray ray = kke::screenToRay(at, { float(w), float(h) }, view, proj);
    // Budget: drop the oldest thrown thing.
    size_t thrown = 0;
    for (const Floater& f : m_floaters) thrown += f.bit == Bit::Thrown ? 1u : 0u;
    if (thrown >= kMaxThrown)
        for (size_t i = 0; i < m_floaters.size(); ++i)
            if (m_floaters[i].bit == Bit::Thrown) {
                m_bodies.remove(m_floaters[i].body);
                m_floaters.erase(m_floaters.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
    const Kind& k = kKinds[kind];
    // From a little in front of the camera, so it clears the ship.
    Floater f;
    f.bit = Bit::Thrown;
    f.kind = kind;
    f.body = m_bodies.add(k.halfExtents, k.density, ray.origin + ray.direction * 6.0f,
                          glm::angleAxis(random01() * 6.28f, glm::normalize(glm::vec3(random01(), 1.0f, random01()))));
    m_bodies.bodies()[f.body].velocity = ray.direction * 18.0f + glm::vec3(0, 3.0f, 0);
    m_bodies.bodies()[f.body].angularVelocity = glm::vec3(random01() - 0.5f, random01() - 0.5f, random01() - 0.5f) * 4.0f;
    m_floaters.push_back(std::move(f));
}

void SeaDemoModule::render(const kke::RenderContext& ctx) {
    m_ocean->drawOcean(ctx, m_waves, m_time, ctx.cameraPos);
    // Box ships (no pack).
    for (const Ship& s : m_ships) {
        if (!s.alive) continue;
        const ShipArt& art = m_library.art(s.cls);
        if (art.loaded) continue;
        const glm::mat4 world = shipMatrix(s);
        art.boxHull->draw(ctx, world, 0.0f, 0.7f);
        for (size_t m = 0; m < art.masts.size() && art.boxMast; ++m) {
            if (m < s.mastUp.size() && !s.mastUp[m]) continue;
            const ShipArt::Mast& mast = art.masts[m];
            // The unit mast (1 m tall, yards 1 m wide) stretched to this one.
            art.boxMast->draw(ctx, world * glm::translate(glm::mat4(1.0f), mast.base) *
                                       glm::scale(glm::mat4(1.0f), glm::vec3(mast.halfWidth * 2.0f, mast.height, 1.0f)),
                              0.0f, 0.8f);
            const float shown = std::clamp(s.sailShown * 2.0f, 0.0f, 1.0f);
            if (shown > 0.06f && art.boxSail) {
                const glm::vec3 top = mast.base + glm::vec3(0.0f, mast.height * 0.92f, 0.0f);
                const glm::mat4 reef = glm::translate(glm::mat4(1.0f), top) * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 0.25f + 0.75f * shown, 1.0f)) *
                                       glm::translate(glm::mat4(1.0f), -top);
                art.boxSail->draw(ctx, world * reef * glm::translate(glm::mat4(1.0f), mast.base) *
                                           glm::scale(glm::mat4(1.0f), glm::vec3(mast.halfWidth * 2.0f, mast.height, 1.0f)),
                                  0.0f, 0.9f);
            }
        }
    }
    // Floating bits.
    for (const Floater& f : m_floaters) {
        const kke::FloatingBody& b = m_bodies.bodies()[f.body];
        if (!b.alive) continue;
        glm::vec3 pos = b.position;
        glm::quat rot = b.orientation;
        if (f.body < m_prevPos.size()) {
            pos = glm::mix(m_prevPos[f.body], b.position, m_alpha);
            rot = glm::slerp(m_prevRot[f.body], b.orientation, m_alpha);
        }
        const glm::mat4 m = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot) * glm::scale(glm::mat4(1.0f), b.halfExtents * 2.0f);
        if (f.bit == Bit::Thrown) m_kindMeshes[static_cast<size_t>(f.kind)]->draw(ctx, m, f.kind == 4 ? 0.8f : 0.0f, f.kind == 3 ? 0.15f : 0.6f);
        else if (f.bit == Bit::Splinter || (f.bit == Bit::Mast && f.instances.empty())) m_plankMesh->draw(ctx, m, 0.0f, 0.85f);
    }
    drawWorld(ctx);
    // Cannonballs and the aim arcs.
    m_sphereScratch.clear();
    for (const Ball& b : m_balls)
        m_sphereScratch.push_back({ b.pos, 0.09f + 0.012f * b.mass, glm::vec3(0.08f, 0.08f, 0.09f), 0.6f, 0.4f });
    for (const Captain& c : m_captains) {
        for (size_t i = 0; i < c.arc.size(); i += 2)
            m_sphereScratch.push_back({ c.arc[i], 0.16f, glm::vec3(1.0f, 0.85f, 0.35f), 0.0f, 0.3f });
        // Where it lands: a ring of markers on the water.
        if (!c.arc.empty()) {
            const glm::vec3 land = c.arc.back();
            for (int k = 0; k < 16; ++k) {
                const float a = static_cast<float>(k) / 16.0f * 6.2831853f;
                m_sphereScratch.push_back({ land + glm::vec3(std::cos(a), 0.2f, std::sin(a)) * 3.0f, 0.22f, glm::vec3(1.0f, 0.4f, 0.2f), 0.0f, 0.3f });
            }
        }
    }
    if (!m_sphereScratch.empty()) m_spheres->draw(ctx, m_sphereScratch);
}

void SeaDemoModule::renderTranslucent(const kke::RenderContext& ctx) {
    if (m_fx) m_fx->draw(ctx);
}

void SeaDemoModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    for (const Ship& s : m_ships) {
        if (!s.alive) continue;
        const ShipArt& art = m_library.art(s.cls);
        if (!art.loaded && art.boxHull) art.boxHull->drawShadow(ctx, shipMatrix(s));
    }
    for (const Floater& f : m_floaters) {
        const kke::FloatingBody& b = m_bodies.bodies()[f.body];
        if (!b.alive || f.bit != Bit::Thrown) continue;
        m_kindMeshes[static_cast<size_t>(f.kind)]->drawShadow(ctx, b.transform() * glm::scale(glm::mat4(1.0f), b.halfExtents * 2.0f));
    }
}

void SeaDemoModule::defineInput() {
    auto* in = m_app->getModule<kke::InputModule>();
    if (!in) return;
    using IM = kke::InputModule;
    for (int p = 0; p < kke::Lobby::kMaxSeats; ++p) {
        in->setPlayers(p + 1);
        kke::InputMap& m = in->map(p);
        m.defineAction({ "sea.steer", "Steer (right +)", "Ship", "game", kke::ActionType::Axis1D });
        m.defineAction({ "sea.sails", "Sails up / down (oars: row)", "Ship", "game", kke::ActionType::Axis1D });
        m.defineAction({ "sea.aim", "Hold: aim the guns", "Guns" });
        m.defineAction({ "sea.fire", "Fire a broadside", "Guns" });
        m.defineAction({ "sea.look", "Turn the camera", "Camera", "game", kke::ActionType::Axis2D });
        m.defineAction({ "sea.zoom", "Camera closer / further", "Camera", "game", kke::ActionType::Axis1D });
        m.defineAction({ "sea.ship", "Next ship", "Ship" });
        m.defineAction({ "sea.throw", "Throw something overboard", "Sea" });
        m.defineAction({ "sea.next", "Next thing to throw", "Sea" });
        if (p > 0) continue; // the bindings: player 1's are copied to the others
        auto axis = [&](const char* action, kke::InputSource src, float scale) {
            kke::Binding b = IM::bind(action, src, kke::Trigger::Continuous);
            b.scale = scale;
            b.deadzone = src.kind == kke::SourceKind::GamepadAxis ? 0.2f : 0.0f;
            m.addBinding(b);
        };
        axis("sea.steer", IM::key(SDL_SCANCODE_D), 1.0f);
        axis("sea.steer", IM::key(SDL_SCANCODE_A), -1.0f);
        axis("sea.steer", IM::key(SDL_SCANCODE_RIGHT), 1.0f);
        axis("sea.steer", IM::key(SDL_SCANCODE_LEFT), -1.0f);
        axis("sea.steer", IM::padAxis(SDL_GAMEPAD_AXIS_LEFTX), 1.0f);
        axis("sea.sails", IM::key(SDL_SCANCODE_W), 1.0f);
        axis("sea.sails", IM::key(SDL_SCANCODE_S), -1.0f);
        axis("sea.sails", IM::key(SDL_SCANCODE_UP), 1.0f);
        axis("sea.sails", IM::key(SDL_SCANCODE_DOWN), -1.0f);
        axis("sea.sails", IM::padAxis(SDL_GAMEPAD_AXIS_LEFTY), -1.0f);
        m.addBinding(IM::bind("sea.aim", IM::padAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), kke::Trigger::Continuous));
        m.addBinding(IM::bind("sea.fire", IM::padAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)));
        m.addBinding(IM::bind("sea.fire", IM::mouse(SDL_BUTTON_LEFT)));
        m.addBinding(IM::bind("sea.fire", IM::key(SDL_SCANCODE_SPACE)));
        {
            kke::Binding b = IM::bind("sea.look", IM::padAxis(SDL_GAMEPAD_AXIS_RIGHTX), kke::Trigger::Continuous);
            b.sourceY = IM::padAxis(SDL_GAMEPAD_AXIS_RIGHTY);
            b.deadzone = 0.15f;
            m.addBinding(b);
        }
        axis("sea.look", IM::key(SDL_SCANCODE_E), 1.0f);
        axis("sea.look", IM::key(SDL_SCANCODE_Q), -1.0f);
        axis("sea.zoom", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_UP), 1.0f);
        axis("sea.zoom", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN), -1.0f);
        m.addBinding(IM::bind("sea.ship", IM::key(SDL_SCANCODE_TAB)));
        m.addBinding(IM::bind("sea.ship", IM::pad(SDL_GAMEPAD_BUTTON_NORTH)));
        m.addBinding(IM::bind("sea.throw", IM::key(SDL_SCANCODE_T)));
        m.addBinding(IM::bind("sea.throw", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)));
        m.addBinding(IM::bind("sea.next", IM::key(SDL_SCANCODE_G)));
        m.addBinding(IM::bind("sea.next", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_LEFT)));
        // Keys 1-5 pick what to throw (keyboard shortcuts, rebindable).
        for (int k = 0; k < kKindCount; ++k) {
            const std::string id = "sea.kind" + std::to_string(k + 1);
            m.defineAction({ id, kKinds[k].name, "Sea" });
            m.addBinding(IM::bind(id, IM::key(static_cast<SDL_Scancode>(SDL_SCANCODE_1 + k))));
        }
    }
    in->setPlayers(1);
    in->commitDefaults();
}

// The settings (RmlUi, kke::DemoPanelModule): the same rows work with a
// controller, the keyboard (F3) and the mouse.
void SeaDemoModule::buildPanel() {
    auto* panel = m_app->getModule<kke::DemoPanelModule>();
    if (!panel) return;
    auto& s = panel->section("Sea");
    s.hint("{sea.sails} sails  {sea.steer} steer  {mouse:right} aim  {sea.fire} fire  {sea.ship} ship  {sea.throw} throw  {sea.look} look",
           "{sea.sails} sails  {sea.steer} steer  {sea.aim} aim  {sea.fire} fire  {sea.ship} ship  {sea.look} look  {sea.zoom} zoom  {sea.throw} throw");
    s.text([this] {
        if (m_captains.empty() || m_captains.front().ship < 0) return std::string("Waiting for the ship");
        const Ship& sh = m_ships[static_cast<size_t>(m_captains.front().ship)];
        if (!sh.alive) return std::string("Sunk! A new ship in a moment...");
        const ShipClass& c = shipClasses()[static_cast<size_t>(sh.cls)];
        const kke::FloatingBody& b = m_bodies.bodies()[sh.body];
        const char* sails[] = { "furled", "half sail", "full sail" };
        char buf[220];
        std::snprintf(buf, sizeof(buf), "%s: %.1f knots, %s, hull %.0f%%, guns %s / %s", c.name, static_cast<double>(glm::length(b.velocity) / 0.5144f),
                      c.oars ? "oars" : sails[sh.sail], static_cast<double>(100.0f * sh.health / sh.maxHealth), sh.reload[0] <= 0.0f ? "ready" : "loading",
                      sh.reload[1] <= 0.0f ? "ready" : "loading");
        return std::string(buf);
    });
    s.text([this] {
        int alive = 0;
        for (int e : m_enemyShips) alive += (e >= 0 && m_ships[static_cast<size_t>(e)].alive && !m_ships[static_cast<size_t>(e)].sinking) ? 1 : 0;
        int sunk = 0, lost = 0;
        for (const Captain& c : m_captains) {
            sunk += c.sunk;
            lost += c.lost;
        }
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s. Enemy ships afloat: %d. Sunk: %d, lost: %d", kModes[m_mode].label, alive, sunk, lost);
        return std::string(buf);
    });
    auto wind = [this] { m_waves.setWind(m_windSpeed, m_windDir, m_chop); };
    s.heading("Wind and waves");
    s.slider("Wind", &m_windSpeed, 0.0f, 16.0f, "%.1f m/s", wind, 0.5f);
    m_windDirDeg = glm::degrees(m_windDir);
    s.slider("Wind direction", &m_windDirDeg, -180.0f, 180.0f, "%.0f deg", [this, wind] { m_windDir = glm::radians(m_windDirDeg); wind(); }, 5.0f);
    s.slider("Choppiness", &m_chop, 0.0f, 0.95f, "%.2f", wind, 0.05f);
    s.toggle("Wind fills the sails", &m_windMatters);
    s.heading("Guns");
    s.slider("Ball speed", &m_muzzleSpeed, 50.0f, 160.0f, "%.0f m/s", {}, 5.0f);
    s.slider("Damage", &m_damageScale, 0.25f, 4.0f, "x%.2f", {}, 0.25f);
    s.toggle("Aim help", &m_aimHelp);
    s.heading("Enemies");
    std::vector<std::string> modes;
    for (const ModeInfo& m : kModes) modes.push_back(m.label);
    s.choice("Mode", &m_mode, modes, [this] {
        if (m_mode == kModeFree) {
            for (int& e : m_enemyShips)
                if (e >= 0) startSinking(static_cast<size_t>(e));
        } else if (m_enemyShips.empty()) {
            m_enemyShips.assign(2, -1);
            m_enemyRespawn.assign(2, 0.0f);
            m_enemySkill.assign(2, 1);
            spawnEnemies();
        }
    });
    s.button("Add an enemy ship", [this] {
        if (m_enemyShips.size() >= 6) return;
        m_enemyShips.push_back(-1);
        m_enemyRespawn.push_back(0.0f);
        m_enemySkill.push_back(1);
        if (m_mode == kModeFree) m_mode = kModeTargets;
        spawnEnemies();
    });
    s.toggle("Sunk enemies come back", &m_enemiesRespawn);
    s.heading("The fort");
    s.button("Rebuild the fort", [this] { buildFort(); });
#if KKE_ENABLE_FEMFX
    s.text([this] {
        if (!m_physics || m_fortWalls.empty()) return std::string("Its walls splinter in builds with FEMFX (KKE_ENABLE_FEMFX); this one has none.");
        char buf[160];
        const kke::PhysicsModule::PanelStats st = m_physics->panelStats();
        std::snprintf(buf, sizeof(buf), "FEMFX palisade: %u pieces (%u moving), %.2f ms a step", st.pieces, st.awakePieces, st.stepMsAvg);
        return std::string(buf);
    });
#else
    s.text([] { return std::string("Its walls splinter in builds with FEMFX (KKE_ENABLE_FEMFX); this one has none."); });
#endif
    s.heading("Throw");
    std::vector<std::string> kinds;
    for (const Kind& k : kKinds) kinds.push_back(k.name);
    s.choice("What", &m_kind, kinds);
    s.button("Restart", [this] { startVoyage(); });
    s.text([this] {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%zu floating bodies, %zu particles, %zu balls in the air", m_bodies.aliveCount(), m_fx ? m_fx->count() : size_t(0),
                      m_balls.size());
        return std::string(buf);
    });
    s.note(m_library.status());
    s.note("Water is 1025 kg/m3: lighter things float, heavier ones sink. Waves are Gerstner swell; every hull, mast and splinter floats on the "
           "same waves you see.");
}

} // namespace kke_sea
