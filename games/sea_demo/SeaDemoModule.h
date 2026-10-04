#pragma once

#include "Ships.h"

#include "kke/CameraRig.h"
#include "kke/Capabilities.h"
#include "kke/FloatingBodies.h"
#include "kke/Module.h"
#include "kke/Ocean.h"
#include "kke/OceanRenderer.h"
#include "kke/ParticleEffects.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/ModelModule.h"

#include <memory>
#include <string>
#include <vector>

namespace kke {
class LobbyModule;
class PhysicsModule;
class NetModule;
namespace net { struct GameEventMsg; }
}

namespace kke_sea {

// What the voyage is for (the start menu's Mode row, m_mode).
inline constexpr int kModeBattle = 0;  // enemy ships fight back
inline constexpr int kModeTargets = 1; // anchored ships to sink
inline constexpr int kModeFree = 2;    // just sailing

// A naval sandbox on an open sea (README.md). Pick a ship from Synty's
// POLYGON Pirate Pack (a rowing boat up to a man-o'-war, each handling its
// own way), set the sails and steer with the wind, aim a broadside and
// watch the balls fly on their arc. Enemy ships sit still as targets or
// fight back; hits dent the hull, throw splinters, snap masts and sink
// ships. Throw things overboard and density decides whether they float.
// A friend can sail alongside online (each gets their own ship).
//
// Built from kke::OceanWaves (the sea), kke::FloatingBodies (buoyancy,
// every hull, mast, splinter and crate), kke::ParticleEffects (smoke,
// spray, wakes, fire), kke::ModelModule (the Synty ships, dented per
// ship), kke::CameraRig (the chase camera) and kke::NetModule.
class SeaDemoModule : public kke::Module, public kke::ISettingsListener {
public:
    SeaDemoModule();
    ~SeaDemoModule() override;
    const char* name() const override { return "SeaDemo"; }
    void init(kke::Application& app) override;
    void fixedUpdate(const kke::FixedUpdateContext& ctx) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderTranslucent(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void shutdown() override;
    void onSettingsChanged(const kke::EngineSettings& settings) override;

    struct Kind { const char* name; float density; glm::vec3 halfExtents; glm::vec3 color; };

    // Teams: who shoots whom.
    enum Team : int { kPlayers = 0, kEnemies = 1 };

    struct Ship {
        int cls = 0;
        size_t body = 0;
        int team = kPlayers;
        bool alive = true;
        bool sinking = false;
        float sinkTime = 0.0f;
        bool local = false;        // sailed from this screen (the player)
        bool remote = false;       // another player's ship, online (drawn from what they send)
        int netId = -1;            // online: the player id that sails it
        std::string name;
        float health = 100.0f, maxHealth = 100.0f;
        int sail = 0;              // 0 furled / anchored, 1 half sail, 2 full sail
        float sailShown = 0.0f;    // 0..1, eased toward the sail setting (drawn)
        float throttle = 0.0f;     // oars: -0.5..1
        float rudder = 0.0f;       // -1..1, eased
        float reload[2] = { 0.0f, 0.0f }; // port, starboard: s until loaded
        std::vector<bool> mastUp;
        std::vector<float> mastHealth;
        std::vector<kke::ModelModule::InstanceId> instances; // one per art part (Synty)
        kke::ModelModule::InstanceId flag = 0;
        // Dents (Synty hull), model space, per hull mesh.
        std::vector<std::vector<glm::vec3>> dented, dentedNormals;
        bool dentsChanged = false;
        std::vector<glm::vec3> fires; // ship space: where it burns
        float fireTimer = 0.0f;
        // Enemy brain.
        float aiThink = 0.0f;
        int aiSide = 0;            // which broadside it means to show
        glm::vec3 home{0.0f};      // a target ship stays near here
        // Drawn between physics ticks.
        glm::vec3 drawPos{0.0f};
        glm::quat drawRot{1.0f, 0.0f, 0.0f, 0.0f};
        int lastHitBy = -1;        // ship index of whoever hit it last
        float baseMass = 1.0f;     // the body's mass, sound (water raises it)
        int skill = 1;             // an enemy's aim: 0 easy .. 3 expert
        int netSlot = -1;          // online: the local player slot it is sent as
        float remoteHealth = -1.0f;
    };

    // A person at this screen (split screen: up to four), with their ship,
    // their controller or keyboard (InputModule player) and their camera.
    struct Captain {
        int seat = 0;              // lobby seat
        int input = 0;             // InputModule player
        int ship = -1;             // index into m_ships
        int cls = 3;               // the ship class they sail
        std::string name;
        kke::CameraRig rig;
        float zoom = 1.0f;
        float lookIdle = 0.0f;     // s since they last turned the camera
        bool aiming = false;
        int aimSideNow = 0;
        float elevation = 4.0f;    // degrees, while aiming
        std::vector<glm::vec3> arc;
        float sailInput = 0.0f;    // last sail axis value (edge detection)
        float respawn = -1.0f;     // s until back after sinking
        int sunk = 0, lost = 0;
        kke::Camera camera;
    };

    struct Ball {
        glm::vec3 pos{0.0f}, vel{0.0f};
        float life = 0.0f;
        int team = kPlayers;
        int owner = -1;            // ship index (local), -1 remote
        float mass = 2.7f;
        bool live = true;          // false: another screen's shot, only drawn (it resolves its own hits)
    };

private:
    // SeaDemoModule.cpp
    void defineInput();
    void buildPanel();
    void reset();
    int spawnShip(int cls, int team, const glm::vec3& at, float heading, bool local);
    void removeShip(size_t index);
    void respawnLocal(int cls);
    void spawnArt(Ship& s);
    void removeArt(Ship& s);
    void sailShip(Ship& s, float dt);
    void throwObject(int kind, bool atMouse);
    void steerCaptain(Captain& c, float dt);
    void updateCamera(Captain& c, float dt);
    void updateViews();
    void startVoyage();
    glm::mat4 shipMatrix(const Ship& s) const;  // ship space -> world, drawn pose
    Captain* captainOf(size_t ship);
    float windFactor(const Ship& s) const;

    // Battle.cpp
    void fire(Ship& s, int side, float elevationDegrees);
    void fireGun(size_t ship, int gun, int side, float elevationDegrees);
    glm::vec3 gunMuzzle(const Ship& s, int gun, int side, glm::vec3* direction, float elevationDegrees) const;
    void predictArc(const Ship& s, int side, float elevationDegrees, std::vector<glm::vec3>& out) const;
    void stepBalls(float dt);
    bool hitShip(Ball& b, const glm::vec3& from, const glm::vec3& to);
    void damageShip(size_t index, const glm::vec3& worldPoint, const glm::vec3& worldDirection, float amount, bool fromNet);
    void dent(Ship& s, const glm::vec3& shipPoint, const glm::vec3& shipDirection, float depth);
    void snapMast(size_t ship, int mast);
    void startSinking(size_t index);
    void thinkEnemy(size_t index, float dt);
    float aimElevation(const Captain& c) const;
    int aimSide(const Captain& c, const Ship& s) const;
    int nearestEnemy(const Ship& s, float maxRange) const;
    static float solveElevation(float range, float muzzle, float height);
    void spawnEnemies();

    // Effects.cpp
    void splash(const glm::vec3& at, float strength);
    void bigSplash(const glm::vec3& at);
    void muzzle(const glm::vec3& at, const glm::vec3& dir, const glm::vec3& shipVel);
    void splinters(const glm::vec3& at, const glm::vec3& dir, int count);
    void wake(const Ship& s, float dt);
    void burn(Ship& s, float dt);
    void stepDebris(float dt);
    glm::vec3 wind() const;

    // World.cpp
    void buildWorld();
    void keepOffIslands();
    void drawWorld(const kke::RenderContext& ctx);
    // The fort (FEMFX builds): wooden walls that splinter along the grain.
    void buildFort();
    bool fortHit(const Ball& b, const glm::vec3& from, const glm::vec3& to);
    void stepFort(float dt);

    // SeaNet.cpp
    void setupNet();
    void sendNet();
    void receiveNet();
    void onNetEvent(const kke::net::GameEventMsg& e);
    void sendShot(const glm::vec3& pos, const glm::vec3& vel, float mass);
    void sendHit(int victimNetId, const glm::vec3& shipPoint, const glm::vec3& shipDir, float amount);

    kke::Application* m_app = nullptr;
    kke::ModelModule* m_models = nullptr;
    kke::NetModule* m_net = nullptr;
    ShipArtLibrary m_library;
    kke::OceanWaves m_waves;
    kke::FloatingBodies m_bodies;
    std::unique_ptr<kke::OceanRenderer> m_ocean;
    std::unique_ptr<kke::SphereImpostorRenderer> m_spheres;
    std::unique_ptr<kke::ParticleEffects> m_fx;
    std::vector<std::unique_ptr<kke::DynamicMeshRenderer>> m_kindMeshes; // unit box per throwable kind
    std::unique_ptr<kke::DynamicMeshRenderer> m_plankMesh, m_mastMesh;
    std::vector<kke::SphereImpostorRenderer::Sphere> m_sphereScratch;

    std::vector<Ship> m_ships;
    std::vector<Ball> m_balls;
    // A broadside rolls down the side, gun after gun.
    struct PendingShot { size_t ship = 0; int gun = 0; int side = 0; float elevation = 0.0f; float delay = 0.0f; };
    std::vector<PendingShot> m_pending;
    // Floating things that aren't ships: thrown objects, splinters, broken masts.
    enum class Bit : int { Thrown, Splinter, Mast };
    struct Floater {
        size_t body = 0;
        Bit bit = Bit::Thrown;
        int kind = 0;              // thrown: kKinds index; mast: ship class
        int mast = -1;             // a broken mast: which one of its class
        float life = 0.0f;         // s left (splinters and masts fade out)
        std::vector<kke::ModelModule::InstanceId> instances; // a broken Synty mast and its sails
        glm::mat4 artOffset{1.0f}; // body space -> ship space of the mast art
    };
    std::vector<Floater> m_floaters;
    // Previous physics pose of every body (draw interpolation).
    std::vector<glm::vec3> m_prevPos;
    std::vector<glm::quat> m_prevRot;
    float m_alpha = 1.0f;

    // The world: islands (circles ships can't sail into), props.
    struct Island { glm::vec3 center{0.0f}; float radius = 10.0f; };
    std::vector<Island> m_islands;
    std::vector<kke::ModelModule::InstanceId> m_worldInstances;
    std::unique_ptr<kke::DynamicMeshRenderer> m_islandMesh; // no pack: a sandy mound
    std::vector<glm::mat4> m_islandBoxes;
    kke::PhysicsModule* m_physics = nullptr; // FEMFX (null in a build without it)
    glm::vec3 m_fortCenter{0.0f};
    float m_fortFacing = 0.0f;               // radians: the walls face the open sea this way
    glm::vec3 m_fortOut{0.0f, 0.0f, 1.0f};   // the palisade's axis-aligned outward normal (buildFort)
    std::vector<uint32_t> m_fortWalls;       // FEMFX handles
    struct FortBall { uint32_t handle = 0; float age = 0.0f; };
    std::vector<FortBall> m_fortBalls;

    // People at this screen.
    std::vector<Captain> m_captains;
    kke::LobbyModule* m_lobby = nullptr;
    bool m_started = false;        // past the lobby
    float m_lookSpeed = 1.0f;
    bool m_invertY = false;

    // Settings (panel and shell).
    float m_time = 0.0f;
    float m_windSpeed = 7.0f, m_windDir = 0.4f, m_chop = 0.6f;
    float m_windDirDeg = 0.0f;
    int m_kind = 1;
    int m_mode = kModeBattle;      // kModeBattle, kModeTargets or kModeFree
    int m_enemyClass = 0;          // 0 = a mix, else class + 1
    bool m_enemiesRespawn = true;
    bool m_windMatters = true;
    bool m_aimHelp = true;
    float m_muzzleSpeed = 80.0f;   // m/s: real 6-24 pounders fire ~400 m/s; slower reads better on screen
    float m_damageScale = 1.0f;
    bool m_friendlyFire = false;   // players' balls hit each other's ships
    std::vector<float> m_enemyRespawn; // per bot slot: s until it comes back
    std::vector<int> m_enemyShips;     // ship index per enemy (-1 = none yet)
    std::vector<int> m_enemySkill;     // lobby difficulty per enemy: 0 easy .. 3 expert
    uint32_t m_rng = 777;
    float random01();

    // Online.
    float m_netSendTimer = 0.0f;
    std::vector<uint8_t> m_lastRoster;
};

} // namespace kke_sea
