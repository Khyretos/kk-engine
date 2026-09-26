#pragma once

#include "kke/AnimRig.h"
#include "kke/Animator.h"
#include "kke/AssetCatalog.h"
#include "kke/SceneLoader.h"
#include "kke/CameraRig.h"
#include "kke/FrameStats.h"
#include "kke/ResourceGovernor.h"
#include "kke/Locomotion.h"
#include "kke/Module.h"
#include "kke/RigidWorld.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/ModelModule.h"

#include <memory>
#include <vector>

namespace kke { class RigidBodyModule; class PhysicsModule; class InputModule; }

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
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void renderUi() override;
    void onEvent(const SDL_Event& event) override;

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
    // Synty scenes (scenes/*.scene.json), each loaded on first visit at
    // its own spot far from the course.
    void findScenes();
    void visitScene(size_t index);
    void scanCatalog();
    // Who you play: "" = the UAL mannequin, else a Synty SK_ character
    // (by asset name) wearing the UAL clips, retargeted.
    void useCharacter(const std::string& asset);
    void buildAnimator();
    // Feet on the ground, hands on the edge (after the Animator).
    void applyIk(float dt);
    void shoot();
    void forcePush();
    void setCaptured(bool on);
    void readActions(float dt);

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
    glm::vec3 m_demoAway{0.0f};
    glm::vec3 m_spawn{0.0f, 0.05f, 6.0f};
    kke::ModelModule::ModelId m_charModel = 0;
    kke::ModelModule::InstanceId m_charInstance = 0;
    kke::ModelModule::ModelId m_ualModel = 0;
    std::string m_character;               // "" = mannequin
    std::vector<std::string> m_characters; // SK_ assets in the catalog
    kke::ModelData m_rigData;              // the character's bones + its clips
    kke::FootPlacer m_feet;
    kke::TwoBoneChain m_armL, m_armR;
    bool m_footIk = true, m_handIk = true;
    float m_footWeight = 0.0f, m_handWeight = 0.0f;
    float m_modelYaw = 0.0f; // turns the model to face -Z
    std::unique_ptr<kke::AnimationSet> m_animSet;
    std::unique_ptr<kke::Animator> m_anim;
    int m_stMove = -1, m_stCrouch = -1, m_stJump = -1, m_stFall = -1, m_stLand = -1;
    int m_stVault = -1, m_stClimbUp = -1, m_stClimbOver = -1, m_stHang = -1;

    struct SceneEntry {
        std::string path;
        kke::SceneFile file;
        kke::LoadedScene loaded;
        bool isLoaded = false;
        glm::vec3 origin{0.0f};
        std::unique_ptr<kke::DynamicMeshRenderer> ground;
    };
    std::vector<SceneEntry> m_scenes;
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
