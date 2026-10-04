#pragma once

#include "kke/AnimRig.h"
#include "kke/Animator.h"
#include "kke/AssetCatalog.h"
#include "kke/SceneLoader.h"
#include "kke/CameraRig.h"
#include "kke/CharacterIk.h"
#include "kke/FrameStats.h"
#include "kke/Footsteps.h"
#include "kke/Equipment.h"
#include "kke/Inventory.h"
#include "kke/ResourceGovernor.h"
#include "kke/Buoyancy.h"
#include "kke/Locomotion.h"
#include "Flight.h" // games/flying_demo: the plane's flight model
#include "LavaStation.h"
#include "Layout.h"
#include "kke/Ocean.h"
#include "kke/Ragdoll.h"
#include "kke/OceanRenderer.h"
#include "kke/Module.h"
#include "kke/AudioMixer.h"
#include "kke/EngineSound.h"
#include "kke/ParticleEffects.h"
#include "kke/ParticleLibrary.h"
#include "kke/RigidWorld.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/ModelModule.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <map>
#include <string>
#include <memory>
#include <vector>

namespace kke { class RigidBodyModule; class PhysicsModule; class InputModule; class InputMap; class NetModule; class UiModule; class GameShellModule; }
namespace Rml { class ElementDocument; }

namespace kke_showcase {

// The walkable showcase (ACTION_PLAN.md P1): a character you control in a
// small level that touches every system — Jolt collision and rigid
// bodies (walk, climb, push crates, ride a platform), the camera rig
// (first/third person spring arm), the animator (idle/walk/jog/sprint
// blend, jump, crouch), FEMFX breakables you can shoot (glass, stone,
// wood), and lighting you can change. Grows as systems land.
class ShowcaseModule : public kke::Module {
public:
    const char* name() const override { return "Showcase"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void fixedUpdate(const kke::FixedUpdateContext& ctx) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void prepass(const kke::PrepassContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void renderUi() override;
    void onEvent(const SDL_Event& event) override;
    void frameStart(const kke::UpdateContext& ctx) override; // the pause menu works while paused

    // Modules whose panels F1 shows/hides.
    void setEnginePanels(std::vector<kke::Module*> panels) { m_panels = std::move(panels); }

private:
    struct Box { glm::vec3 center, half, color; float yaw = 0.0f; };
    void buildLevel();
    void addStaticBox(const Box& b, std::vector<kke::Vertex>& v, std::vector<uint32_t>& i);
    void spawnCrates();
    void spawnBreakables();
    void setupPlayer();
    void buildParkourLane(std::vector<kke::Vertex>& v, std::vector<uint32_t>& i);
    // The parkour park (Parkour.cpp): its sections, the HUD's name for the
    // one you're in, and KKE_DEMO_PARKOUR (step -1 = off).
    void buildParkourPark(std::vector<kke::Vertex>& v, std::vector<uint32_t>& i);
    bool parkourStation(const glm::vec3& p, std::string& station, std::string& text) const;
    void updateParkourDemo(float dt, kke::Locomotion::Input& in);
    int m_demoParkourStep = -1, m_demoParkourLast = 0, m_demoParkourRoof = 0;
    float m_demoParkourT = 0.0f;
    // KKE_DEMO_PARKOUR_FREEZE=wallclimb|hangvault|shimmy: stop there, mid-move (screenshots).
    std::string m_demoParkourFreeze;
    bool m_demoParkourFrozen = false;
    kke::Locomotion::State m_demoParkourState = kke::Locomotion::State::Ground;
    void updateAnimation(float dt);
    // What the animation state machine reads: from kke::Locomotion for
    // our player, from the network for everyone else's.
    struct MotionInfo {
        kke::Locomotion::State state = kke::Locomotion::State::Ground;
        float speed = 0.0f, progress = 0.0f, stateTime = 0.0f, fallHeight = 0.0f;
        float obstacleHeight = 0.0f; // vault / climb: the top above the feet
        float wallSide = 0.0f;       // wall run: +1 wall on the right, -1 left
        bool crouch = false, landed = false, jumped = false;
        bool wallClimb = false, hangVault = false; // a Leap up a wall from the ground; a Vault from a hang
    };
    // Shimmy hands and feet: how far along the edge (signed, m) and which
    // way the last move went; the wall climb's footholds.
    float m_shimmyDist = 0.0f, m_shimmyDir = 1.0f;
    glm::vec3 m_climbStep[2]{};
    bool m_climbStepSet[2]{};
    void addAnimatorStates(kke::Animator& a);
    void animate(kke::Animator& a, const MotionInfo& m, float dt);
    // Synty scenes (scenes/*.scene.json), each loaded on first visit at
    // its own spot far from the course.
    void findScenes();
    void visitScene(size_t index);
    void scanCatalog();
    // Synty art around the stations (course_art.scene.json, loaded on the
    // course itself): only when its packs are installed, else the course
    // stays plain boxes. KKE_COURSE_ART=0 leaves it out.
    void dressCourse();
    // Who you play: "" = the UAL mannequin, else a Synty SK_ character
    // (by asset name) wearing the UAL clips, retargeted.
    void useCharacter(const std::string& asset);
    void buildAnimator();
    // Feet on the ground, hands on the edge (after the Animator).
    void applyIk(float dt);
    void shoot(const kke::Camera& cam);
    void forcePush(const kke::Camera& cam);
    void setCaptured(bool on);
    void readActions(float dt);
    void applyLighting();
    void resetCourse(); // crates back (asks the host when we're a client)

    // Multiplayer (kke::NetModule, docs/NETWORKING.md): our player's state out,
    // everyone else's drawn with our character model and their own
    // animator; crates and the platform are the host's. Shots are events.
    void replicateBodies();
    bool stepNetPlayer(const kke::Locomotion::Input& in, float dt);
    void sendNetState(const glm::vec3& feet);
    void updateAvatars(float dt);
    void onNetEvent(uint16_t kind, uint8_t from, const std::vector<uint8_t>& payload);
    void spawnBall(const glm::vec3& from, const glm::vec3& dir);
    struct Avatar {
        kke::ModelModule::InstanceId instance = 0;
        std::unique_ptr<kke::Animator> anim;
        int lastState = -1;
        float stateTime = 0.0f;
    };
    kke::NetModule* m_net = nullptr;
    std::map<uint8_t, Avatar> m_avatars;

    // Split screen (ACTION_PLAN.md 1.3, SplitScreen.cpp): players 2-4 on
    // this machine, each with a controller, a character, a camera and a
    // part of the window. Player 1 keeps the keyboard and mouse (and any
    // controller nobody else has). A player without a controller runs
    // the parkour lane on its own, so split screen can be tried alone.
    struct LocalPlayer {
        kke::RigidWorld::CharacterId id = 0;
        std::unique_ptr<kke::Locomotion> loco;
        kke::CameraRig rig;
        kke::Camera camera;
        kke::ModelModule::InstanceId instance = 0;
        std::unique_ptr<kke::Animator> anim;
        uint32_t pad = 0; // device ref; 0 = none (runs the lane)
        bool crouch = false, jumpQueued = false;
        float fireCooldown = 0.0f;
        float runTime = 0.0f; // on the lane: seconds since the start (< 0 = waiting)
    };
    // Stations (Stations.cpp). The pool: a walled basin of water (vault
    // over the wall to wade in) where crates, planks and a raft float,
    // bob on small waves and right themselves; a steel block sinks.
    void buildPool(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx);
    void spawnPoolFloaters(int& n);
    void floatBodies(float dt);
    bool inPool(const glm::vec3& p) const;
    void drawPool(const kke::RenderContext& ctx);
    kke::OceanWaves m_poolWaves;
    std::unique_ptr<kke::OceanRenderer> m_poolWater;
    std::vector<kke::RigidWorld::BodyBox> m_poolBodies;
    std::vector<kke::BuoyancyPoint> m_buoyancy;
    float m_poolTime = 0.0f;
    // The lava station (LavaStation.cpp): a basin where lava melts a block.
    void buildLava(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx);
    bool lavaWatched() const;
    std::unique_ptr<LavaStation> m_lava;

    void setLocalPlayers(int count);
    void assignControllers();
    void updateLocalPlayers(float dt);
    std::vector<LocalPlayer> m_locals;
    bool m_splitSideBySide = true;
    bool m_overhead = false; // picture-in-picture: player 1 seen from above
    std::vector<glm::mat4> m_avatarCapsules; // no character model: boxes
    glm::vec3 m_lastFeet{0.0f};

    // End-user stress test (ACTION_PLAN.md 1.7, StressTest.cpp): a fixed
    // script (walk, crate rain, breaking) at an uncapped frame rate, then
    // one report: benchmark/stress_<time>_<host>.txt (+ .json). Panel
    // button, or KKE_STRESS_TEST=1 (runs at start, quits when done).
    void startStressTest();
    void updateStressTest(float dt);
    void finishStressTest();
    void stressShoot(const glm::vec3& from, const glm::vec3& target);
    bool m_stressActive = false, m_stressQuitAtEnd = false;
    float m_stressTime = 0.0f, m_stressSpawn = 0.0f, m_stressShot = 0.0f;
    int m_stressPhase = -1, m_stressShots = 0, m_stressRained = 0;
    uint32_t m_stressRandom = 1;
    size_t m_stressFirstCrate = 0; // crates from here on are the rain's
    double m_stressLastTick = 0.0;
    kke::FrameStats m_stressStats;
    kke::ResourceBudget m_stressSavedBudget;
    bool m_stressSavedVsync = false;
    std::string m_stressReport; // last report written (path without extension)

    kke::Application* m_app = nullptr;
    kke::RigidBodyModule* m_rigid = nullptr;
    kke::PhysicsModule* m_femfx = nullptr;
    kke::ModelModule* m_models = nullptr;
    kke::InputModule* m_input = nullptr;
    glm::vec2 m_moveInput{0.0f};
    bool m_swallowFire = false;
    float m_fireCooldown = 0.0f;
    float m_mouseSensitivity = 0.12f; // degrees per pixel
    float m_stickSpeed = 200.0f;      // degrees per second at full stick
    std::unique_ptr<kke::DynamicMeshRenderer> m_level, m_capsule;
    // Unit cubes, one per colour (the renderer colours by vertex).
    std::vector<std::unique_ptr<kke::DynamicMeshRenderer>> m_cubes;
    std::vector<glm::vec3> m_cubeColors;
    // Every crate in one mesh, rebuilt each frame from the bodies: one
    // draw (and one shadow draw) instead of two per crate.
    std::unique_ptr<kke::DynamicMeshRenderer> m_crateBatch;
    size_t m_crateBatchIndices = 0;
    void batchCrates();

    // Crates and the moving platform (Jolt).
    struct Crate { kke::RigidWorld::BodyId body; glm::vec3 half; int cube; };
    std::vector<Crate> m_crates;
    kke::RigidWorld::BodyId m_platform = kke::RigidWorld::kNoBody;
    glm::vec3 m_platformHalf{1.5f, 0.15f, 1.5f};
    float m_platformTime = 0.0f;

    // Things the player spawns from the spawn menu (Spawner.cpp): RB or G
    // opens it over the game; pick a thing and it lands in front of you.
    // "Clear what I spawned" and "Reset the world" are its last rows.
    enum class PropShape : uint8_t { Box, Sphere, Barrel };
    struct Prop {
        kke::RigidWorld::BodyId body = kke::RigidWorld::kNoBody;
        PropShape shape = PropShape::Box;
        glm::vec3 half{0.3f}; // box half extents; sphere: x = radius; barrel: x = radius, y = half height
        glm::vec3 color{0.6f};
        float metallic = 0.0f;
    };
    struct SpawnDummy { // a ragdoll dummy: the mannequin on Jolt bodies
        kke::RigidWorld::RagdollId ragdoll = 0;
        kke::ModelModule::InstanceId instance = 0;
        kke::RagdollSkinBinding binding;
    };
    struct SpawnRow { std::string label, hint; };
    void buildSpawnMenu();
    void openSpawnMenu(bool open);
    void updateSpawnMenu(float dt);
    void spawnRow(int row);
    void spawnProp(PropShape shape, const glm::vec3& half, float density, const glm::vec3& color, uint32_t material, const glm::vec3& at,
                   float metallic = 0.0f, float restitution = 0.1f);
    void spawnDummy(const glm::vec3& at);
    void clearSpawned();
    void resetWorld();
    void updateDummies();
    void batchProps();
    glm::vec3 spawnSpot(float distance) const; // on the ground in front of the player
    std::vector<Prop> m_props;
    std::vector<SpawnDummy> m_dummies;
    std::vector<SpawnRow> m_spawnRows;
    std::unique_ptr<kke::DynamicMeshRenderer> m_propBatch, m_propMetal;
    size_t m_propBatchIndices = 0, m_propMetalIndices = 0;
    Rml::ElementDocument* m_spawnDoc = nullptr;
    Rml::DataModelHandle m_spawnModel;
    bool m_spawnOpen = false, m_spawnRecapture = false;
    int m_spawnSel = 0;
    float m_spawnRepeat = 0.0f; // stick / key repeat while held
    int m_spawnHeldDir = 0;
    std::string m_spawnNote; // "Spawned a crate" under the list

    // Picking things up (Carry.cpp): X or F lifts the crate, barrel or
    // ball in front of you (up to kMaxLift kg); it rides in front of the
    // chest held in both hands (CharacterIk), X or F puts it down, RT or
    // a click throws it. Heavier things you push (Y or E).
    void togglePickUp();
    void carryStep(float dt);  // physics rate: the held body follows the hands
    void throwHeld(const kke::Camera& cam);
    void dropHeld();
    void holdHands();          // IK contacts for the hands on the held body
    glm::vec3 holdPoint() const;
    struct Held {
        kke::RigidWorld::BodyId body = kke::RigidWorld::kNoBody;
        glm::vec3 half{0.3f};
        float mass = 0.0f;
        float yawOffset = 0.0f; // its yaw against the facing when lifted, snapped to the nearest face
        float age = 0.0f;       // s since it was lifted
    };
    Held m_held;
    float m_demoCarry = -1.0f; // KKE_DEMO_CARRY: seconds into the script, -1 = off

    // Items (Items.cpp): things lying about (the supply table by the
    // start, later the zones) that pickup puts in your bag, a grid
    // inventory (kke::Inventory, data/items.json), and what you equip
    // shown on the character (kke::Equipment: in a hand, on the back, a
    // hip, the head). The bag's weight slows you down.
    struct ItemLook {               // one kind's mesh, item space (grip at the origin)
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        glm::vec3 lo{0.0f}, hi{0.0f}; // bounds
        float metallic = 0.0f;
        kke::Equippable equip;       // held by Equipment: m_looks never moves an entry
    };
    struct WorldItem {
        std::string id;
        int count = 1;
        kke::RigidWorld::BodyId body = kke::RigidWorld::kNoBody;
    };
    void loadItems();
    void buildLooks();
    void placeItems();              // the supply table's things, back where they started
    void clearWorldItems();
    void dropItem(const std::string& id, int count, const glm::vec3& at);
    int itemInReach() const;        // index into m_worldItems, -1 = none
    void takeItem(int index);
    void syncEquipment();           // m_inv's equipped things -> m_equipment
    void drawEquipped(kke::Pose& pose, const glm::mat4& toWorld);
    void batchItems();
    void toast(std::string text);
    float loadFactor() const;       // 1 = light, less with a heavy bag
    kke::ItemCatalog m_items;
    kke::Inventory m_inv{10, 6, 30.0f};
    std::map<std::string, ItemLook> m_looks;
    std::vector<WorldItem> m_worldItems;
    kke::Equipment m_equipment;
    bool m_equipmentBuilt = false;
    std::unique_ptr<kke::DynamicMeshRenderer> m_itemBatch, m_equipBatch;
    size_t m_itemBatchIndices = 0, m_equipBatchIndices = 0;
    float m_demoItems = -1.0f;      // KKE_DEMO_ITEMS: seconds into the script, -1 = off
    bool m_demoItemsKeepOpen = false; // KKE_DEMO_ITEMS=2: the bag stays open
    void updateItemsDemo(float dt, glm::vec3& move);

    // The open world round the yard (World.cpp): terrain 1.4 km across,
    // roads from the yard's gates to the zones (layout::kZones), a flag
    // over each; the world map (M, or the pause menu) takes you to one.
    void buildWorld();
    float groundHeight(float x, float z) const; // the terrain's height under (x, z)
    float roadDistance(float x, float z) const; // m from the nearest road's edge (negative: on it)
    int zoneAt(const glm::vec3& p) const;       // index into layout::kZones (1..), -1 = none (or the yard)
    void travelTo(int zone);
    void buildMapScreen();
    void openMap(bool open);
    void updateMap(float dt);
    std::vector<float> m_terrain; // heights, m_terrainN x m_terrainN, row = z
    int m_terrainN = 0;
    std::unique_ptr<kke::DynamicMeshRenderer> m_world;
    struct MapZone { std::string name, text, dot, label; bool sel = false; };
    struct MapRoad { std::string style; };
    std::vector<MapZone> m_mapZones;
    std::vector<MapRoad> m_mapRoads;
    std::string m_mapMe, m_mapKeys;
    bool m_mapOpen = false, m_mapRecapture = false;
    int m_mapSel = 0, m_mapHeldDir = 0;
    float m_mapRepeat = 0.0f;
    Rml::ElementDocument* m_mapDoc = nullptr;
    Rml::DataModelHandle m_mapModel;
    float m_demoWorld = -1.0f; // KKE_DEMO_WORLD: seconds into the tour, -1 = off
    void updateWorldDemo(float dt);

    // Guns, grenades and explosions (Guns.cpp). A rifle or pistol in the
    // right hand fires (RT, click) where the crosshair is; aim (LT, right
    // click) brings it up to the eye and the camera over the shoulder.
    // Rounds come from the bag (it reloads by itself). A grenade in the
    // right hand is thrown instead. Bullets knock Jolt bodies and break
    // FEMFX ones; red barrels and grenades blow up (kke::physicsBlast).
    struct GunDef {
        const char* id;
        const char* ammo;  // item id of its rounds
        int magazine;
        float interval;    // s between shots
        bool automatic;    // held trigger keeps firing
        float reload;      // s
        float push;        // N s on what it hits
        glm::vec3 muzzle;  // item space
    };
    struct Grenade { kke::RigidWorld::BodyId body = kke::RigidWorld::kNoBody; float fuse = 0.0f; };
    struct Slug { uint32_t handle = 0; float age = 0.0f; }; // a FEMFX bullet, gone after a moment
    void buildGuns();
    const GunDef* gunInHand() const; // nullptr: no gun in the right hand
    bool grenadeInHand() const;
    bool armed() const { return gunInHand() || grenadeInHand(); }
    void updateGuns(float dt, bool fireHeld, bool firePressed, bool aimHeld);
    void fireGun(const GunDef& gun);
    void bulletHit(const glm::vec3& from, const glm::vec3& dir, const GunDef& gun);
    void throwGrenade();
    void explode(const glm::vec3& at, float power);
    void aimHands(const kke::Pose& pose, const glm::mat4& toWorld); // IK targets for the aim pose (before m_ik.apply)
    glm::mat4 aimedGun(const glm::mat4& inHand) const;              // the gun's frame while aiming
    void updateEffects(float dt);
    void tickFuses(float dt); // fixedUpdate
    void playSound(const std::shared_ptr<const kke::SoundBuffer>& sound, const glm::vec3& at, float gain, float range);
    void renderTranslucent(const kke::RenderContext& ctx) override;
    std::unique_ptr<kke::ParticleEffects> m_fx;
    std::unique_ptr<kke::ParticleLibrary> m_fxLib;
    std::shared_ptr<const kke::SoundBuffer> m_sndRifle, m_sndPistol, m_sndBoom, m_sndClick, m_sndReload;
    std::map<std::string, int> m_loaded;   // rounds in each gun's magazine
    float m_gunCooldown = 0.0f, m_reloadLeft = 0.0f;
    float m_sinceShot = 10.0f;             // s since the last shot (the gun stays up a moment)
    float m_aimBlend = 0.0f;               // 0 = relaxed, 1 = aiming
    float m_armBase = 3.5f, m_fovBase = 60.0f;
    bool m_aimWanted = false;
    glm::vec3 m_aimPoint{0.0f};            // where the crosshair ray lands
    glm::vec3 m_muzzle{0.0f};              // world, last drawn
    bool m_muzzleValid = false;
    float m_flash = 0.0f, m_flashStrength = 0.0f;
    glm::vec3 m_flashAt{0.0f}, m_flashColor{1.0f};
    float m_shake = 0.0f;                  // camera shake from a blast, 0..1
    std::vector<Grenade> m_grenades;
    std::vector<Slug> m_slugs;
    uint32_t m_shotSeed = 1;
    // The firing range (Range.cpp), in its zone: a bench with guns,
    // rounds and grenades; steel plates that fall when hit; glass, a plank
    // and stone walls that break (FEMFX); red barrels by a crate pile;
    // two dummies. Reset the world puts it all back.
    enum class RangeKind : uint8_t { Plate, Barrel, Crate };
    struct RangeThing {
        kke::RigidWorld::BodyId body = kke::RigidWorld::kNoBody;
        RangeKind kind = RangeKind::Crate;
        glm::vec3 half{0.3f}; // barrel: x = radius, y = half height
        glm::vec3 color{0.6f};
        float fuse = -1.0f;   // barrel: s to the bang, -1 = not lit
    };
    void buildRange(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx);
    void spawnRange();
    void clearRange();
    void batchRange();
    int platesDown() const;
    int plateCount() const;
    std::vector<RangeThing> m_range;
    std::vector<uint32_t> m_rangeBreakables; // FEMFX handles
    std::unique_ptr<kke::DynamicMeshRenderer> m_rangeStatic, m_rangeBatch, m_rangeMetal;
    size_t m_rangeBatchIndices = 0, m_rangeMetalIndices = 0;
    float m_demoGuns = -1.0f; // KKE_DEMO_GUNS: seconds into the script, -1 = off
    bool m_demoAim = false;   // the script aims (m_aimPoint), not the camera
    void updateGunsDemo(float dt, bool& fire, bool& aim);

    // Cars and the plane (Vehicles.cpp). Cars on Jolt's vehicle physics
    // (kke/Vehicle.h) at the race track: an oval with a lap timer, cones
    // and a jump. A stunt plane on the airfield's runway, flown by the
    // Flying demo's flight model (games/flying_demo/Flight.h) on a
    // kinematic body. Walk up and press pickup (F / X) to get in or out.
    enum class Ride : uint8_t { None, Car, Plane };
    struct Car {
        kke::RigidWorld::VehicleId id = 0;
        glm::vec3 half{0.85f, 0.35f, 2.0f}; // the chassis
        glm::vec3 color{0.7f};
        glm::vec3 home{0.0f};
        float homeYaw = 0.0f;               // degrees, 0 = facing -Z (like the rig)
        const char* name = "";
        kke::VehicleState state;
    };
    struct Plane {
        flying::PlaneState s;
        flying::Controls c;
        kke::RigidWorld::BodyId body = kke::RigidWorld::kNoBody;
        glm::vec3 home{0.0f};
        float homeYaw = 0.0f;  // flying::headingOf: 0 = -Z, clockwise
        float prop = 0.0f;     // propeller angle (radians)
        float wreck = -1.0f;   // s since it crashed, -1 = whole
        glm::vec3 drawPos{0.0f};
        glm::quat drawRot{1.0f, 0.0f, 0.0f, 0.0f};
    };
    void defineRideActions();             // drive.gas, drive.brake (init, before the bindings are committed)
    void buildVehicles(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx); // the oval, the car park, the ramp, the hangar
    void spawnVehicles();
    void clearVehicles();
    void batchVehicles();
    bool useVehicle();                    // pickup pressed: in or out (false: nothing to do here)
    int carInReach() const;               // index into m_cars, -1 = none
    bool planeInReach() const;
    void enterVehicle(Ride ride, int car);
    void exitVehicle(bool force = false); // force: even moving (travel, reset, a crash)
    void flipCar();                       // back on its wheels (reset while driving)
    void readRide(kke::InputMap& in, float dt);
    void stepVehicles(float dt);          // fixedUpdate: inputs to the car, the plane's flight
    void updateRide(float dt);            // the camera, the character in the seat, laps
    void updateEngineSound(float dt);
    flying::Ground flightGround() const;
    glm::vec3 ridePosition() const;
    kke::RigidWorld::BodyId rideBody() const;
    std::string rideHud() const;          // speed, gear, altitude, lap
    std::vector<Car> m_cars;
    Plane m_plane;
    flying::FlightSettings m_flight;
    Ride m_ride = Ride::None;
    int m_rideCar = -1;
    kke::VehicleInput m_carIn;
    std::vector<RangeThing> m_cones;      // the track's cones (kind Crate, drawn orange)
    float m_lookIdle = 10.0f;             // s since the player turned the camera (then it swings in behind)
    float m_rideArm = 7.0f, m_walkArm = 3.5f;
    float m_lapAngle = 0.0f, m_lapTime = -1.0f, m_lastLap = -1.0f, m_bestLap = -1.0f, m_lapPrev = 0.0f;
    int m_laps = 0;
    std::unique_ptr<kke::DynamicMeshRenderer> m_vehStatic, m_vehBatch;
    size_t m_vehBatchIndices = 0;
    kke::EngineSound m_engine;
    std::shared_ptr<kke::AudioStream> m_engineStream;
    uint32_t m_engineVoice = 0;
    std::vector<float> m_engineScratch;
    float m_demoDrive = -1.0f; // KKE_DEMO_DRIVE: seconds into the script, -1 = off
    int m_demoWaypoint = 0;
    float m_demoClosest = 1e9f; // m: the plane's nearest pass at the waypoint so far
    void updateDriveDemo(float dt, glm::vec2& move, float& gas, float& brake, bool& handBrake);

    // The nature park and the snow field (Nature.cpp). A forest round a
    // meadow; trees and grass sway in the wind and bend round you; the axe
    // fells a tree in four hits (it falls away from you, then lies as logs
    // to carry or split into firewood); flowers in the meadow go in the
    // bag. With POLYGON Nature the trees and plants are Synty's, else
    // they're built from shapes. Up the mountain, snow that keeps every
    // footprint and tyre track, with snow falling.
    enum class TreeState : uint8_t { Standing, Falling, Down, Gone };
    struct Tree {
        glm::vec3 base{0.0f};
        float yaw = 0.0f, scale = 1.0f, phase = 0.0f;
        int kind = 0;
        float height = 8.0f, trunk = 0.25f; // m: the whole tree, the trunk's radius
        kke::RigidWorld::BodyId body = kke::RigidWorld::kNoBody; // the trunk (static)
        kke::ModelModule::InstanceId instance = 0, stump = 0;     // Synty
        int hits = 0;
        TreeState state = TreeState::Standing;
        glm::vec3 fallDir{1.0f, 0.0f, 0.0f};
        float fall = 0.0f, fallSpeed = 0.0f, downTime = 0.0f, shake = 0.0f;
    };
    struct Flower { glm::vec3 at{0.0f}; int kind = 0; float phase = 0.0f; bool picked = false; };
    struct Plant { glm::vec3 at{0.0f}; float yaw = 0.0f, scale = 1.0f, phase = 0.0f; kke::ModelModule::InstanceId instance = 0; };
    void buildNature(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx); // the chopping block, the snow field's ground
    void spawnNature();
    void clearNature();
    void updateNature(float dt);
    void batchNature();               // trees (shapes), stumps, grass, flowers: every frame (they sway)
    bool axeInHand() const;
    void startChop();
    void chopHit();
    void chopHands(const kke::Pose& pose, const glm::mat4& toWorld);
    int treeInReach(float reach) const;
    int flowerInReach() const;
    std::string flowerName(int index) const;
    void pickFlower(int index);
    glm::mat4 swayOf(const glm::vec3& base, float phase, float amount) const; // a rotation about the base
    float windGust(const glm::vec3& at) const;
    void stampSnow(const glm::vec2& at, const glm::vec2& half, float yaw, float depth);
    void rebuildSnow();
    bool inSnow(const glm::vec3& p) const;
    std::vector<Tree> m_trees;
    std::vector<Flower> m_flowers;
    std::vector<Plant> m_plants;
    std::vector<kke::ModelModule::ModelId> m_treeModels, m_stumpModels, m_plantModels;
    bool m_natureSynty = false, m_natureTried = false;
    float m_windTime = 0.0f;
    float m_chop = -1.0f;                 // s into a swing, -1 = not swinging
    int m_chopTree = -1;
    glm::vec3 m_chopAt{0.0f};             // where the blade lands
    int m_felled = 0;
    float m_pickReach = -1.0f;            // s into reaching down for a flower
    glm::vec3 m_pickAt{0.0f};
    std::shared_ptr<const kke::SoundBuffer> m_sndChop, m_sndFall, m_sndPick;
    std::unique_ptr<kke::DynamicMeshRenderer> m_natureStatic, m_natureBatch, m_grassBatch;
    size_t m_natureBatchIndices = 0, m_grassBatchIndices = 0;
    // The snow: a grid of how far it's pressed down (0 fresh .. 1 to the
    // ground), drawn in chunks, each rebuilt when something presses it.
    std::vector<float> m_snowPress, m_snowGround;
    std::vector<std::unique_ptr<kke::DynamicMeshRenderer>> m_snowChunks;
    std::vector<uint8_t> m_snowDirty;
    std::vector<size_t> m_snowIndices;
    float m_stride = 0.0f;                // m walked since the last footprint
    bool m_leftFoot = false;
    glm::vec3 m_lastSnowFeet{0.0f};
    uint32_t m_snowEmitter = 0, m_leafEmitter = 0;
    float m_demoNature = -1.0f; // KKE_DEMO_NATURE: seconds into the script, -1 = off
    void updateNatureDemo(float dt, bool& chop);

    // The bag (InventoryScreen.cpp, ui/showcase_inventory.rml): Tab, I or
    // View opens it over the game. Move with the arrows, the
    // d-pad or the mouse; take and place with A, Space or a click; turn
    // with R or Y; equip with E or X; drop with Q or RB; sort with T.
    struct InvCell { bool cursor = false, ok = false, bad = false; };
    struct InvTile { std::string style, label, count; bool moving = false; };
    struct InvSlot { std::string name, item, style; bool cursor = false, fits = false; };
    void buildInventoryScreen();
    void openInventory(bool open);
    void updateInventory(float dt);
    void refreshInventory();
    void invPress();                // take or place at the cursor
    void invEquipToggle();
    void invDrop();
    const kke::InventoryItem* invUnderCursor() const;
    bool m_invOpen = false, m_invRecapture = false, m_invDirty = true;
    int m_invX = 0, m_invY = 0;     // the cursor: a cell, or x = -1 for the equipment column (y = slot)
    uint32_t m_invMoving = 0;       // the item being moved (0 = none)
    bool m_invMovingRotated = false;
    float m_invRepeat = 0.0f;
    glm::ivec2 m_invHeldDir{0};
    std::vector<InvCell> m_invCells;
    std::vector<InvTile> m_invTiles;
    std::vector<InvSlot> m_invSlots;
    struct InvDetails { std::string name, category, description, weight, size, equip; bool any = false; };
    InvDetails m_invInfo;
    std::string m_invWeight, m_invWeightBar, m_invNote, m_invKeys;
    bool m_invHeavy = false;
    Rml::ElementDocument* m_invDoc = nullptr;
    Rml::DataModelHandle m_invModel;

    // Player.
    kke::RigidWorld::CharacterId m_player = 0;
    std::unique_ptr<kke::Locomotion> m_loco;      // vault/climb, turning, air control
    kke::CameraRig m_rig;
    float m_facing = 0.0f;          // degrees, the body's yaw
    bool m_captured = false;
    bool m_crouch = false, m_wantCrouch = false, m_walk = false, m_sprint = false;
    bool m_jumpQueued = false;
    // KKE_DEMO_AUTOPILOT=1: runs the parkour lane by itself (screenshots,
    // checking the vault/climb feel without touching the keyboard).
    bool m_autopilot = false;
    float m_autopilotTime = 0.0f;
    glm::vec3 m_autopilotStart{0.0f};
    float m_autopilotEndZ = 0.0f;
    float m_demoHang = -1.0f; // KKE_DEMO_HANG: seconds into the script, -1 = off
    // RmlUi HUD (station hints, what the character is doing) and pause
    // menu (Hud.cpp, ui/showcase_*.rml).
    void buildHud();
    void updateHud();
    void buildPauseRows();
    struct HudState {
        std::string move, speed, station, stationText, stationLive, menuHint, prompt, toast, ammo;
        bool trick = false, panels = false, online = false, crosshair = false, reloading = false;
        int players = 1;
    };
    HudState m_hud;
    float m_toastTime = 0.0f; // s the toast stays
    kke::UiModule* m_ui = nullptr;
    Rml::ElementDocument* m_hudDoc = nullptr;
    Rml::DataModelHandle m_hudModel;
    bool m_menuOpen = false;        // the shared pause menu is up (kke::GameShellModule)
    kke::GameShellModule* m_shell = nullptr;
    int m_playersChoice = 0;        // the pause menu's Players row (0 = one player)
    float m_shoulder = 0.45f;   // camera shoulder offset (m, + = right); moves off a wall being run along
    float m_demoTricks = -1.0f; // KKE_DEMO_TRICKS: wall run, ledge leaps (same)
    void buildTrickCourse(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx);
    // KKE_DEMO_BRIDGE: an iron ball dropped on the yard's glass; logs how
    // far the shards knocked the crates under it (FEMFX <-> Jolt bridge).
    float m_demoBridge = -1.0f;
    size_t m_yardCratesFirst = 0; // the crates under the glass: m_crates[first..first+3)
    std::vector<glm::vec3> m_yardCratesStart;
    void updateBridgeDemo(float dt);
    glm::vec3 m_demoAway{0.0f};
    glm::vec3 m_spawn{0.0f, 0.05f, 6.0f};
    kke::ModelModule::ModelId m_charModel = 0;
    kke::ModelModule::InstanceId m_charInstance = 0;
    kke::ModelModule::ModelId m_ualModel = 0;
    // UAL volume 2 (CC0, optional): the traversal clips (vault, climb,
    // wall run), CPU only; its mesh is the same mannequin.
    std::unique_ptr<kke::ModelData> m_ual2;
    std::string m_character;               // "" = mannequin
    std::vector<std::string> m_characters; // SK_ assets in the catalog
    kke::ModelData m_rigData;              // the character's bones + its clips
    // On top of the clips: feet on the ground or planted, hands on edges
    // (human arms that keep out of the body), the lean into speeding up.
    kke::CharacterIk m_ik;
    kke::CharacterFootsteps m_steps;       // step sounds from the feet (docs/AUDIO.md)
    bool m_footIk = true, m_handIk = true;
    glm::vec3 m_leanVelocity{0.0f};        // the body's velocity, held through scripted moves
    int m_footBone[2] = { -1, -1 };        // foot_l, foot_r (hanging: braced on the wall)
    float m_modelYaw = 0.0f; // turns the model to face -Z
    std::unique_ptr<kke::AnimationSet> m_animSet;
    std::unique_ptr<kke::Animator> m_anim;
    int m_stMove = -1, m_stCrouch = -1, m_stJump = -1, m_stFall = -1, m_stLand = -1;
    int m_stVault = -1, m_stClimbUp = -1, m_stClimbOver = -1, m_stHang = -1;
    // Real clips (UAL2), posed by the move's progress; -1 = stand-ins.
    int m_stVaultClip = -1, m_stClimbLow = -1, m_stClimbHigh = -1;
    int m_stWallRunL = -1, m_stWallRunR = -1;

    struct SceneEntry {
        std::string path;
        kke::SceneFile file;
        kke::LoadedScene loaded;
        bool isLoaded = false;
        glm::vec3 origin{0.0f};
        std::unique_ptr<kke::DynamicMeshRenderer> ground;
    };
    std::vector<SceneEntry> m_scenes;
    kke::LoadedScene m_courseArt;
    kke::AssetCatalog m_catalog;
    bool m_catalogScanned = false;
    std::string m_assetDir;

    // Lighting panel.
    float m_sunAzimuth = 35.0f, m_sunElevation = 50.0f, m_sunIntensity = 1.0f, m_ambient = 0.25f;
    glm::vec3 m_sunColor{1.0f, 0.95f, 0.85f};

    std::vector<kke::Module*> m_panels;
    bool m_showPanels = false;
    float m_fps = 0.0f;
    std::string m_status;
};

} // namespace kke_showcase
