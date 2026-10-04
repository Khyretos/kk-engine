#pragma once

#include "kke/AnimRig.h"
#include "kke/Animator.h"
#include "kke/AssetCatalog.h"
#include "kke/SceneLoader.h"
#include "kke/CameraRig.h"
#include "kke/CharacterIk.h"
#include "kke/FrameStats.h"
#include "kke/Footsteps.h"
#include "kke/ResourceGovernor.h"
#include "kke/Buoyancy.h"
#include "kke/Locomotion.h"
#include "LavaStation.h"
#include "Layout.h"
#include "kke/Ocean.h"
#include "kke/Ragdoll.h"
#include "kke/OceanRenderer.h"
#include "kke/Module.h"
#include "kke/RigidWorld.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/ModelModule.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <map>
#include <string>
#include <memory>
#include <vector>

namespace kke { class RigidBodyModule; class PhysicsModule; class InputModule; class NetModule; class UiModule; class GameShellModule; }
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
    void updateAnimation(float dt);
    // What the animation state machine reads: from kke::Locomotion for
    // our player, from the network for everyone else's.
    struct MotionInfo {
        kke::Locomotion::State state = kke::Locomotion::State::Ground;
        float speed = 0.0f, progress = 0.0f, stateTime = 0.0f, fallHeight = 0.0f;
        float obstacleHeight = 0.0f; // vault / climb: the top above the feet
        float wallSide = 0.0f;       // wall run: +1 wall on the right, -1 left
        bool crouch = false, landed = false, jumped = false;
    };
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
        std::string move, speed, station, stationText, stationLive, menuHint;
        bool trick = false, panels = false, online = false;
        int players = 1;
    };
    HudState m_hud;
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
