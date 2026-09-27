#pragma once

#include "kke/AnimRig.h"
#include "kke/Animator.h"
#include "kke/Capabilities.h"
#include "kke/Combat.h"
#include "kke/Module.h"
#include "kke/Ragdoll.h"
#include "kke/RigidWorld.h"
#include "kke/modules/ModelModule.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <memory>
#include <string>
#include <vector>

namespace kke {
class DynamicMeshRenderer;
class InputModule;
class RigidBodyModule;
} // namespace kke
namespace Rml { class ElementDocument; }

namespace duel {

class SparringBot;

// Duel: a one-on-one fist fight in a ring, best of three rounds. The rules
// are kke::Combatant's (kke/Combat.h): jabs are quick and cheap, the
// uppercut is slow and knocks people down, the knee breaks a guard, a
// block raised just in time parries, stamina runs every one of them. A
// knockdown is a Jolt ragdoll that gets back up; a knockout stays down.
//
// The opponent is a sparring bot (SparringBot.h), or a second player: F2
// (Back on a controller) hands the red corner to the second controller,
// or to the arrow keys and the number pad when there's only one.
//
// Bodies: Quaternius' Universal Animation Library mannequin (CC0), with
// UAL 2's melee clips when it's there (assets/animations/UAL2.fbx, or the
// pack under KKE_ASSETS_DIR); UAL 1's punches otherwise.
//
// Headless / demo switches: KKE_DUEL_BOTS=1 (both corners are bots),
// KKE_DUEL_QUIT=<s> (quit after that long, logging the score), KKE_DUEL_SEED
// (the bots' dice), KKE_DUEL_LEVEL=easy|normal|hard (the bot).
class DuelModule : public kke::Module {
public:
    DuelModule();
    ~DuelModule() override;
    const char* name() const override { return "Duel"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void onEvent(const SDL_Event& event) override;

    // What a fighter wants this frame: from a player's controls or a bot.
    struct Intent {
        glm::vec2 move{0.0f};   // x = right (the fighter's own), y = toward the opponent
        bool light = false, heavy = false, kick = false, dodge = false;
        bool block = false;
    };

    struct Fighter {
        int corner = 0;                 // 0 blue, 1 red
        std::string name;
        glm::vec3 tint{1.0f};
        bool bot = true;
        int player = 0;                 // input map
        std::unique_ptr<SparringBot> brain;
        kke::CombatantId id = 0;
        kke::RigidWorld::CharacterId body = 0;
        glm::vec3 facing{0.0f, 0.0f, -1.0f};
        glm::vec3 push{0.0f};           // knockback still being applied (m/s)
        int wins = 0;
        // Drawing.
        kke::ModelModule::InstanceId model = 0;
        std::unique_ptr<kke::Animator> anim;
        int lastState = -1;
        bool leftHand = false;          // jabs alternate hands
        float guard = 0.0f;             // IK hands up: 0 relaxed .. 1 blocking
        float flinch = 0.0f;            // hit reaction weight (fades)
        glm::vec3 flinchDir{0.0f};
        // Ragdoll (knockdown / knockout).
        kke::IRagdollPhysics::RagdollHandle ragdoll = 0;
        kke::RagdollDesc ragdollDesc;
        kke::RagdollSkinBinding binding;
        float downTime = 0.0f;
        std::vector<glm::mat4> getUpFrom; // the lying pose, blended out of
        float getUpAge = -1.0f;
        Intent intent;
    };

private:
    void loadCharacter();
    void buildArena();
    void spawnFighters();
    void startRound(bool newMatch);
    void setTwoPlayers(bool on);

    Intent readPlayer(Fighter& f);
    void updateFighter(Fighter& f, Fighter& other, float dt);
    void onHit(const kke::HitEvent& e);
    void knockDown(Fighter& f, const glm::vec3& push);
    void getUp(Fighter& f);
    void updateCamera(float dt);

    // Bodies (Body.cpp).
    void setupBody(Fighter& f);
    void animateBody(Fighter& f, const Fighter& other, float dt);

    // HUD (Hud.cpp, ui/duel_hud.rml).
    void buildHud();
    void updateHud();

    Fighter& fighter(kke::CombatantId id) { return m_fighters[0].id == id ? m_fighters[0] : m_fighters[1]; }

    kke::Application* m_app = nullptr;
    kke::RigidBodyModule* m_rigid = nullptr;
    kke::InputModule* m_input = nullptr;
    kke::ModelModule* m_models = nullptr;
    kke::IRagdollPhysics* m_ragdolls = nullptr;

    kke::CombatWorld m_combat;
    Fighter m_fighters[2];
    std::unique_ptr<kke::DynamicMeshRenderer> m_arena, m_block;

    enum class Phase { Intro, Fight, RoundOver, MatchOver };
    Phase m_phase = Phase::Intro;
    float m_phaseTime = 0.0f;
    int m_round = 1;
    std::string m_roundWinner;
    bool m_twoPlayers = false, m_allBots = false;
    float m_quitAfter = -1.0f, m_clock = 0.0f, m_reportAt = 5.0f;
    uint32_t m_seed = 1;
    std::string m_level = "normal";
    struct Tally { int hits = 0, blocks = 0, parries = 0, guardBreaks = 0, knockdowns = 0; } m_tally[2];

    // Camera.
    glm::vec3 m_camMid{0.0f};
    float m_camSide = 1.0f, m_camDist = 5.0f;
    bool m_camInit = false;

    // The mannequin and its clips.
    kke::ModelModule::ModelId m_charModel = 0;
    kke::ModelData m_rigData;
    std::unique_ptr<kke::AnimationSet> m_animSet;
    float m_modelYaw = 0.0f;
    kke::TwoBoneChain m_arm[2];
    bool m_meleeClips = false; // UAL 2 is there
    struct States {
        int idle = -1, fwd = -1, back = -1, left = -1, right = -1;
        int jabL = -1, jabR = -1, heavy = -1, kick = -1, dodge = -1;
        int hitHigh = -1, hitLow = -1, hitHard = -1, getUp = -1, win = -1;
    } m_st;

    // HUD.
    struct Corner { std::string name, health = "100%", stamina = "100%", wins, note; bool low = false; };
    struct Hud { Corner c[2]; std::string banner, sub, hint, round; };
    Hud m_hud;
    Rml::DataModelHandle m_hudModel;
    Rml::ElementDocument* m_hudDoc = nullptr;
};

} // namespace duel
