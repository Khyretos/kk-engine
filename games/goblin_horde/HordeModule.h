#pragma once

#include "kke/AnimRig.h"
#include "kke/Animator.h"
#include "kke/CameraRig.h"
#include "kke/Capabilities.h"
#include "kke/Combat.h"
#include "kke/Module.h"
#include "kke/Ragdoll.h"
#include "kke/RigidWorld.h"
#include "kke/ai/AiWorld.h"
#include "kke/ai/NavMesh.h"
#include "kke/modules/ModelModule.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <deque>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace Rml {
class ElementDocument;
}

namespace kke {
class InputModule;
class RigidBodyModule;
class DynamicMeshRenderer;
} // namespace kke

namespace horde {

// Goblin Horde: hold the old watchtower against waves of goblins (Synty
// SIDEKICK Goblin Fighters with the Goblin Locomotion clips). You are the
// king with his sword (POLYGON Fantasy Characters, Quaternius' UAL sword
// clips retargeted onto him).
//
//  - The goblins' minds are the engine AI core (kke::ai::AiWorld): the
//    "goblin" species in data/goblin.yml charges, waits its turn when the
//    king is already surrounded, and breaks and runs when the wave's
//    morale goes; paths go round the ruins on a Recast navmesh built from
//    the arena. Its Attack events are when a goblin swipes.
//  - Hits, blocks, stamina and knockdowns are kke::CombatWorld: goblins
//    are CombatStats::grunt(), the king's heavy swing sweeps.
//  - Waves are data too (data/waves.yml): how many, how many at once, how
//    fast.
//  - Crowd cost: each goblin variant is simplified once with meshoptimizer
//    (kke::simplifyModel, 18k -> ~3k triangles), instances are pooled and
//    reused, and the dead ragdoll (Jolt) up to a cap, oldest first.
//
// Environment: KKE_HORDE_BOT=1 (the king fights by himself),
// KKE_HORDE_QUIT=<s> (quit after that long, a report every 10 s),
// KKE_HORDE_MAX=<n> (goblins alive at once, default 60),
// KKE_HORDE_VARIANTS=<n> (goblin looks loaded, default 5),
// KKE_HORDE_LOD=<ratio> (default 0.2), KKE_HORDE_WAVE=<n> (start wave).
class HordeModule : public kke::Module {
public:
    HordeModule();
    ~HordeModule() override;
    const char* name() const override { return "Horde"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void onEvent(const SDL_Event& e) override;
    void shutdown() override;

private:
    // ---- The king ---------------------------------------------------------
    struct HeroStates {
        int move = -1, slashA = -1, slashB = -1, heavy = -1, block = -1, roll = -1, hit = -1, down = -1, getUp = -1, cheer = -1;
    };
    struct Hero {
        kke::CombatantId id = 0;
        kke::RigidWorld::CharacterId body = 0;
        glm::vec3 facing{ 0.0f, 0.0f, -1.0f };
        glm::vec3 push{ 0.0f };
        kke::ModelModule::InstanceId model = 0, sword = 0;
        std::unique_ptr<kke::Animator> anim;
        int lastState = -1;
        bool secondSlash = false;
        float flinch = 0.0f;
        glm::vec3 flinchDir{ 0.0f };
    };
    void loadHero();
    void spawnHero();
    void updateHero(float dt);
    void heroBot(glm::vec2& move, bool& slash, bool& heavy, bool& block, bool& roll);
    void animateHero(float dt);

    // ---- Goblins ----------------------------------------------------------
    struct GoblinStates {
        int idle = -1, walk = -1, run = -1, sprint = -1, swipe = -1, menace = -1;
    };
    // One goblin look: its merged, simplified model, and its own copy of
    // the skeleton and clips (a variant's parts can add bones).
    struct Variant {
        std::string name;
        kke::ModelModule::ModelId model = 0;
        kke::ModelData rig; // bones + clips, no meshes
        std::unique_ptr<kke::AnimationSet> set;
        GoblinStates states;
        float yaw = 0.0f;   // model yaw so its forward is +z
        int spine = -1;
        std::vector<kke::ModelModule::InstanceId> free; // pooled, hidden
    };
    struct Goblin {
        kke::ai::AgentId agent = 0;
        kke::CombatantId id = 0;
        int variant = 0;
        kke::ModelModule::InstanceId model = 0;
        std::unique_ptr<kke::Animator> anim;
        glm::vec3 position{ 0.0f }, velocity{ 0.0f }, push{ 0.0f };
        float yaw = 0.0f; // degrees, 0 = +z
        float speedScale = 1.0f;
        float scale = 1.0f;
        float waitRadius = 3.4f; // where it hangs back while others fight
        std::string tactic;
        int lastState = -1;
        float flinch = 0.0f;
        glm::vec3 flinchDir{ 0.0f };
        float hurtFlash = 0.0f;
        bool strike = false;
        bool dead = false;
        float deadTime = 0.0f;
        bool frozen = false; // ragdoll handed back: lies still, then sinks
        kke::IRagdollPhysics::RagdollHandle ragdoll = 0;
        kke::RagdollDesc ragdollDesc;
        kke::RagdollSkinBinding binding;
    };
    void loadGoblins();
    void spawnGoblin(const glm::vec3& at);
    void updateGoblins(float dt);
    void animateGoblin(Goblin& g, float dt);
    void killGoblin(Goblin& g, const glm::vec3& push);
    void releaseGoblin(Goblin& g);
    Goblin* goblinByCombatant(kke::CombatantId id);
    Goblin* goblinByAgent(kke::ai::AgentId id);

    // ---- The fight --------------------------------------------------------
    struct Wave {
        int goblins = 8;       // in the wave
        int atOnce = 8;        // alive at the same time (capped by KKE_HORDE_MAX)
        float speed = 1.0f;    // run speed multiplier
        float health = 1.0f;   // health multiplier
    };
    enum class Phase { Intro, Fighting, Cleared, Overrun };
    void loadWaves();
    Wave waveAt(int n) const;
    void startWave(int n);
    void restart();
    void onHit(const kke::HitEvent& e);
    void updateCamera(float dt);
    void buildArena();
    void buildHud();
    void updateHud();
    void setCaptured(bool on);

    kke::Application* m_app = nullptr;
    kke::RigidBodyModule* m_rigid = nullptr;
    kke::InputModule* m_input = nullptr;
    kke::ModelModule* m_models = nullptr;
    kke::IRagdollPhysics* m_ragdolls = nullptr;

    kke::CombatWorld m_combat;
    kke::ai::AiWorld m_ai;
    kke::ai::NavMesh m_nav;
    std::mt19937 m_rng{ 7 };

    // Arena
    std::unique_ptr<kke::DynamicMeshRenderer> m_arena, m_block;
    std::vector<glm::vec3> m_gates;       // where goblins come in from
    std::vector<glm::vec4> m_obstacles;   // x, z, half x, half z: ruins goblins are kept out of

    // King
    Hero m_hero;
    kke::ModelData m_heroRig;
    std::unique_ptr<kke::AnimationSet> m_heroSet;
    kke::ModelModule::ModelId m_heroModel = 0, m_swordModel = 0;
    HeroStates m_hs;
    float m_heroYaw = 0.0f; // model yaw so modelForward points along facing
    int m_handBone = -1;
    glm::mat4 m_grip{ 1.0f };
    std::vector<int> m_heroSpine;
    kke::CameraRig m_rig;
    bool m_captured = false;
    bool m_bot = false;
    float m_mouseSensitivity = 0.12f, m_stickSpeed = 160.0f;
    glm::vec3 m_moveWorld{ 0.0f };

    // Goblins
    std::vector<Variant> m_variants;
    std::vector<std::unique_ptr<Goblin>> m_goblins;
    std::deque<kke::ai::AgentId> m_ragdolled; // oldest first
    kke::ai::AgentId m_nextAgent = 100;
    size_t m_triangles = 0, m_fullTriangles = 0;

    // Waves
    std::vector<Wave> m_waves;
    int m_attackers = 6;
    float m_breather = 4.0f;
    Phase m_phase = Phase::Intro;
    float m_phaseTime = 0.0f;
    int m_wave = 0;
    int m_toSpawn = 0, m_waveSize = 0;
    float m_spawnTimer = 0.0f;
    int m_kills = 0, m_waveKills = 0;
    int m_maxAlive = 60, m_ragdollCap = 12, m_startWave = 1;
    float m_morale = 1.0f;

    // Report / headless
    float m_clock = 0.0f, m_quitAfter = -1.0f, m_reportAt = 10.0f;
    double m_frameMs = 0.0, m_worstMs = 0.0;
    int m_frames = 0;
    double m_aiMs = 0.0, m_goblinMs = 0.0;

    // HUD
    struct Hud {
        Rml::String health = "100%", stamina = "100%";
        Rml::String wave, left, kills, banner, sub, hint;
        bool low = false;
    };
    Hud m_hud;
    Rml::DataModelHandle m_hudModel;
    Rml::ElementDocument* m_hudDoc = nullptr;
};

} // namespace horde
