#pragma once

#include "CommandHud.h"
#include "CommandInput.h"
#include "Humanoid.h"
#include "Scenery.h"

#include "kke/Animator.h"
#include "kke/CameraRig.h"
#include "kke/Locomotion.h"
#include "kke/Module.h"
#include "kke/OrderBridge.h"
#include "kke/OrderScript.h"
#include "kke/ProceduralAnim.h"
#include "kke/Orders.h"
#include "kke/RigidWorld.h"
#include "kke/ai/AiWorld.h"
#include "kke/modules/ModelModule.h"

#include <memory>
#include <string>
#include <vector>

namespace kke {
class DynamicMeshRenderer;
class InputModule;
class RigidBodyModule;
} // namespace kke

namespace pet_companion {

// Pet Companion (README.md): you and a dog in a garden. Tell it to come,
// sit, stay, fetch, drop it or go somewhere, pet it, or throw its ball;
// with a mouse and keyboard, a controller (the radial wheel on LB) or a
// finger (the big buttons, tap the world, hold for the wheel).
//
// The orders are the engine's (kke/Orders.h, docs/COMMANDS.md): the same
// order board, the same order.* Lua and nodes. The dog's brain is the AI
// core (kke/ai/AiWorld.h, docs/AI.md), the built-in "dog" species: the
// orders reach it through kke::AiOrderBridge, and with no order it lives
// its own life (sniffs about, rests, watches you). Its legs are
// kke::ProceduralGait on the pug's skeleton.
//
// What makes a companion good, on top: it reads what you are doing
// (kke::IntentReader turned into soft AI orders: it trots along when you
// walk, comes to look at what you look at, waits for a pat when you walk
// up to it, fetches a ball you throw without being told), it never stands
// in your way (the core's Follow keeps it off your path), and it shows
// how it feels (it jumps for joy after a good fetch or a pat).
//
// Headless: KKE_PET_DEMO=1 plays through every order by itself and logs
// what the dog did; KKE_PET_QUIT=<s> quits after that long.
class PetModule : public kke::Module, public kke::IOrderHost {
public:
    PetModule();
    ~PetModule() override;
    const char* name() const override { return "Pet"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void onEvent(const SDL_Event& event) override;
    void shutdown() override;

    // kke::IOrderHost: Lua orders come from the player and go to the dog.
    uint32_t player() const override { return kPlayer; }
    std::vector<uint32_t> selected() const override { return { kDog }; }

    static constexpr uint32_t kPlayer = 1, kDog = 2, kFirstBall = 10;

private:
    struct Ball {
        uint32_t id = 0;
        kke::RigidWorld::BodyId body = kke::RigidWorld::kNoBody; // none while carried
        kke::ModelModule::InstanceId model = 0;
        glm::vec3 pos{0.0f};
        enum class Held { No, Dog, Player } held = Held::No;
        float thrownAgo = 1e9f; // seconds since the player threw it
    };

    void buildGarden();
    void loadDog();
    void addBall(const glm::vec3& pos);
    Ball* ball(uint32_t id);
    glm::vec3 positionOf(uint32_t thing) const;
    bool exists(uint32_t thing) const;

    // Orders from the player.
    void give(kke::OrderKind kind, uint32_t target = 0, const glm::vec3* point = nullptr);
    void giveContext(const glm::vec2& pointer, bool force);
    void giveWheel(int item, const glm::vec2& pointer);
    kke::PointerTarget pick(const glm::vec2& pointer) const;
    void pressButton(int button);
    void throwBall();
    void pet();

    // The dog: the AI core decides, this moves and shows it.
    void setUpAi();
    void updateDog(float dt);
    std::string doing() const;
    void grab(Ball& b);
    void release(Ball& b, const glm::vec3& velocity);
    void animateDog(float dt);
    void updatePlayer(float dt);
    void updateBalls(float dt);
    void updateHud(float dt);
    void runDemo(float dt);
    void playerSay(const std::string& text);

    kke::Application* m_app = nullptr;
    kke::RigidBodyModule* m_rigid = nullptr;
    kke::InputModule* m_input = nullptr;
    kke::ModelModule* m_models = nullptr;
    std::unique_ptr<command_kit::Scenery> m_scenery;
    std::unique_ptr<command_kit::HumanoidKit> m_kit;
    std::unique_ptr<command_kit::CommandHud> m_hud;
    command_kit::CommandInput m_cmd;

    // The player.
    kke::RigidWorld::CharacterId m_playerChar = 0;
    std::unique_ptr<kke::Locomotion> m_loco;
    std::unique_ptr<command_kit::Humanoid> m_body;
    kke::CameraRig m_rig;
    bool m_captured = false, m_jumpQueued = false;
    float m_kneel = 0.0f; // seconds left petting

    // The dog.
    kke::RigidWorld::CharacterId m_dogChar = 0;
    kke::ModelModule::InstanceId m_dogModel = 0;
    kke::ModelData m_dogRig;
    std::unique_ptr<kke::AnimationSet> m_dogSet;
    std::unique_ptr<kke::Animator> m_dogAnim;
    int m_dogIdle = -1, m_dogJump = -1;
    std::vector<kke::TwoBoneChain> m_legChains;
    kke::ProceduralGait m_gait;
    std::vector<int> m_legs, m_knees; // FL, FR, BL, BR upper and lower legs (the sit pose)
    int m_hips = -1;
    float m_dogScale = 0.19f;
    std::unique_ptr<kke::DynamicMeshRenderer> m_dogBlock, m_ballBlock;
    float m_dogYaw = 0.0f; // AiWorld's convention: 0 = +Z, 90 = +X
    float m_turnRate = 0.0f, m_sit = 0.0f, m_joyTime = 0.0f, m_petTime = 0.0f;
    bool m_gaitReady = false;
    kke::LookAt m_look;
    glm::vec3 m_lookTarget{0.0f};
    float m_happy = 0.6f, m_excited = 0.3f;
    // The soft order the dog is following from what you're doing (not a command).
    struct Reflex { kke::Intent::Kind kind = kke::Intent::Kind::Idle; uint32_t thing = 0; bool on = false; } m_reflex;

    std::vector<Ball> m_balls;
    kke::ai::AiWorld m_ai{ 7 };
    kke::OrderBoard m_board;
    std::unique_ptr<kke::AiOrderBridge> m_bridge;
    kke::IntentReader m_intent;
    std::unique_ptr<kke::OrderScript> m_script;

    float m_clock = 0.0f, m_quitAfter = -1.0f;
    bool m_demo = false;
    int m_demoStep = 0, m_demoBlocked = 0;
    float m_demoTimer = 0.0f;
    glm::vec3 m_demoWant{0.0f};
};

} // namespace pet_companion
