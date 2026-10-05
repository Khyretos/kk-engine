#pragma once

#include "Agility.h"
#include "Care.h"
#include "CommandHud.h"
#include "CommandInput.h"
#include "DogBody.h"
#include "Humanoid.h"
#include "Scenery.h"

#include "kke/CameraRig.h"
#include "kke/Locomotion.h"
#include "kke/Module.h"
#include "kke/OrderBridge.h"
#include "kke/OrderScript.h"
#include "kke/Orders.h"
#include "kke/RigidWorld.h"
#include "kke/ai/AiWorld.h"
#include "kke/ai/NavMesh.h"
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

// Pet Companion (README.md): you and your dog in a garden. Pick the pet
// (any POLYGON Dogs breed, or the pug), look after it (food, water, a
// pat, cleaning up after it: hunger, thirst, health and moods, kke
// Tamagotchi style), play with its toys, tell it to come, sit, stay,
// fetch, drop it or go somewhere, and run the agility course next to the
// garden together; with a mouse and keyboard, a controller (the radial
// wheel on LB) or a finger (the big buttons, tap the world, hold for the
// wheel).
//
// The orders are the engine's (kke/Orders.h, docs/COMMANDS.md): the same
// order board, the same order.* Lua and nodes. The dog's brain is the AI
// core (kke/ai/AiWorld.h, docs/AI.md), the built-in "dog" species, on a
// navmesh of the garden and the field (it goes round the fence through
// the gate): the orders reach it through kke::AiOrderBridge, and with no
// order it lives its own life (sniffs about, eats, drinks, naps, does its
// business). Its body is DogBody: the pack's clips (walk, run, sit,
// sleep, eat, ...) with its feet kept on the ground.
//
// What makes a companion good, on top: it reads what you are doing
// (kke::IntentReader turned into soft AI orders), it never stands in your
// way (the core's Follow keeps it off your path), and it shows how it
// feels. Your hands do what you do (kke::CharacterIk on the mannequin):
// a toy sits in your palm with your fingers round it, you crouch and
// reach for it on the grass, and a pat is your palm on its head.
//
// Headless: KKE_PET_DEMO=1 plays through everything by itself and logs
// what the dog did; KKE_PET_QUIT=<s> quits after that long;
// KKE_PET_BREED=<id> / KKE_PET_COAT=<0..2> pick the pet.
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

    static constexpr uint32_t kPlayer = 1, kDog = 2, kFirstToy = 10;

private:
    // A toy on the grass, in your hand or in its mouth.
    struct Toy {
        enum class Kind { Ball, Football, Frisbee, Bone, Duck, Stick };
        enum class Held { No, Dog, Player };
        uint32_t id = 0;
        Kind kind = Kind::Ball;
        std::string label;
        kke::RigidWorld::BodyId body = kke::RigidWorld::kNoBody; // none while carried
        kke::ModelModule::InstanceId model = 0;
        float modelScale = 1.0f;
        glm::vec3 modelCentre{0.0f}; // the model's bounds centre (model space)
        float radius = 0.05f;        // a ball's, or how big it is in a palm
        glm::vec3 half{0.05f};       // a box toy's half extents
        glm::vec3 color{1.0f};       // the block, without art
        glm::vec3 pos{0.0f};
        glm::quat rot{1.0f, 0.0f, 0.0f, 0.0f};
        Held held = Held::No;
        float thrownAgo = 1e9f; // seconds since you threw it
    };
    struct Bowl {
        bool water = false;
        glm::vec3 pos{0.0f};
        float level = 1.0f;     // 0 empty .. 1 full
        uint32_t place = 0;     // the AI's food / water place while there is something in it
        kke::ModelModule::InstanceId bowl = 0, content = 0;
    };
    struct Poop {
        glm::vec3 pos{0.0f};
        float age = 0.0f;
        kke::ModelModule::InstanceId model = 0;
    };
    // What your hands are busy with.
    enum class Busy { None, PickUp, Throw, Pet, Fill, Scoop };

    void buildGarden();
    void buildNavMesh();
    void addToy(Toy::Kind kind, const glm::vec3& pos);
    void addBowl(bool water, const glm::vec3& pos);
    Toy* toy(uint32_t id);
    glm::vec3 positionOf(uint32_t thing) const;
    bool exists(uint32_t thing) const;
    bool groundAt(const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) const;

    // The pet.
    void choosePet(int breed, int coat);
    void loadSave();
    void writeSave() const;

    // Orders from the player.
    void give(kke::OrderKind kind, uint32_t target = 0, const glm::vec3* point = nullptr);
    void giveContext(const glm::vec2& pointer, bool force);
    void giveWheel(int item, const glm::vec2& pointer);
    kke::PointerTarget pick(const glm::vec2& pointer, int* obstacle = nullptr) const;
    void pressButton(int button);
    void throwToy();
    void interact();
    std::string interactLabel() const;
    void pet();
    void adopt(); // after it died: a new pet, cared for from the start

    // The dog: the AI core decides, this moves and shows it.
    void setUpAi();
    void updateSpecies();
    void updateDog(float dt);
    void updateCare(float dt);
    void updateToilet(float dt);
    void updateAgility(float dt);
    void sendOver(int obstacle);
    DogAct chooseAct() const;
    std::string doing() const;
    void grab(Toy& t);
    void release(Toy& t, const glm::vec3& velocity);
    void animateDog(float dt);
    void updatePlayer(float dt);
    void updateHands(float dt);
    void updateToys(float dt);
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
    Busy m_busy = Busy::None;
    float m_busyTime = 0.0f;
    uint32_t m_busyToy = 0;   // PickUp, Throw
    int m_busyIndex = -1;     // Fill: the bowl; Scoop: the poop
    glm::vec3 m_busyAt{0.0f}; // where the hand goes

    // The dog.
    kke::RigidWorld::CharacterId m_dogChar = 0;
    std::unique_ptr<DogBody> m_dog;
    int m_breed = 0, m_coat = 0;
    std::vector<std::string> m_breedNames; // the picker's list (breeds on this machine)
    std::vector<int> m_breedIds;
    int m_breedChoice = 0;
    float m_dogYaw = 0.0f; // AiWorld's convention: 0 = +Z, 90 = +X
    float m_turnRate = 0.0f, m_joyTime = 0.0f, m_petTime = 0.0f;
    glm::vec3 m_lookTarget{0.0f};
    bool m_hasLook = false;
    Care m_care;
    std::vector<Bowl> m_bowls;
    std::vector<Poop> m_poops;
    glm::vec3 m_bed{0.0f};
    uint32_t m_bedPlace = 0;
    // Going to the toilet: walk to a quiet spot, then do it there.
    struct Toilet { enum class Phase { None, Going, Doing } phase = Phase::None; bool poop = true; glm::vec3 spot{0.0f}; float time = 0.0f; } m_toilet;
    bool m_toBed = false;
    bool m_mourned = false; // its death was announced
    float m_lastHunger = 0.0f, m_lastThirst = 0.0f;
    float m_saveTimer = 0.0f;
    // The soft order the dog is following from what you're doing (not a command).
    struct Reflex { kke::Intent::Kind kind = kke::Intent::Kind::Idle; uint32_t thing = 0; bool on = false; } m_reflex;

    // Agility: going to an obstacle, then over it.
    AgilityCourse m_course;
    struct Over { int obstacle = -1; bool crossing = false; float t = 0.0f; float giveUp = 0.0f; } m_over;
    ObstaclePose m_overPose;

    std::vector<Toy> m_toys;
    kke::ai::AiWorld m_ai{ 7 };
    kke::ai::NavMesh m_nav;
    kke::OrderBoard m_board;
    std::unique_ptr<kke::AiOrderBridge> m_bridge;
    kke::IntentReader m_intent;
    std::unique_ptr<kke::OrderScript> m_script;
    std::unique_ptr<kke::DynamicMeshRenderer> m_toyBlock, m_poopBlock;

    float m_clock = 0.0f, m_quitAfter = -1.0f;
    bool m_demo = false;
    int m_demoStep = 0, m_demoBlocked = 0;
    float m_demoTimer = 0.0f;
    glm::vec3 m_demoWant{0.0f};
};

} // namespace pet_companion
