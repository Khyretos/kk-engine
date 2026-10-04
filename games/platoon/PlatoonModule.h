#pragma once

#include "CommandHud.h"
#include "CommandInput.h"
#include "Humanoid.h"
#include "Scenery.h"

#include "kke/Module.h"
#include "kke/OrderBridge.h"
#include "kke/OrderScript.h"
#include "kke/Orders.h"
#include "kke/ai/AiWorld.h"

#include <memory>
#include <string>
#include <vector>

namespace kke {
class DynamicMeshRenderer;
class InputModule;
class ModelModule;
class RigidBodyModule;
} // namespace kke

namespace platoon {

// Platoon (README.md): a squad of six on a training ground, five enemies
// dug in across it. Select soldiers (click, drag a box, groups, a finger,
// a controller's reticle) and order them: go there in a formation, hold
// position, take cover, attack a target, focus fire, regroup; everyone or
// one soldier at a time.
//
// Orders are the engine's (kke/Orders.h, docs/COMMANDS.md); every soldier,
// friend or foe, is an agent of the AI core (kke/ai/AiWorld.h) and the
// orders reach it through kke::AiOrderBridge. Formations and cover are this
// layer's: slots come from kke::formationSlots, cover spots from the
// crates and walls on the field. Shots are the AI's Attack events; this
// game rolls the hit (cover and line of sight count) and deals the damage.
//
// Headless: KKE_PLATOON_DEMO=1 plays a whole assault by itself and logs
// it; KKE_PLATOON_QUIT=<s> quits after that long.
class PlatoonModule : public kke::Module, public kke::IOrderHost {
public:
    PlatoonModule();
    ~PlatoonModule() override;
    const char* name() const override { return "Platoon"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void onEvent(const SDL_Event& event) override;
    void shutdown() override;

    // kke::IOrderHost: Lua orders go to whoever is selected.
    uint32_t player() const override { return 0; }
    std::vector<uint32_t> selected() const override { return m_selection.ids(); }

    static constexpr uint32_t kFirstSoldier = 1, kFirstEnemy = 100;

private:
    struct Soldier {
        uint32_t id = 0;
        std::string name;
        bool enemy = false;
        float hp = 100.0f, maxHp = 100.0f;
        bool dead = false;
        float deadFor = 0.0f;
        glm::vec3 pos{0.0f};
        float yaw = 0.0f;          // AiWorld's convention: 0 = +Z, 90 = +X
        uint32_t aimAt = 0;        // who it last shot at
        float sinceShot = 1e9f;
        bool covering = false;     // told to hold a cover spot:
        glm::vec3 coverPos{0.0f};  //   where to crouch
        glm::vec3 coverFacing{0.0f}; // toward the cover (and the danger past it)
        std::unique_ptr<command_kit::Humanoid> body;
    };
    // Something to hide behind: a crate, a barrier (a box on the ground).
    struct CoverObject {
        glm::vec3 pos{0.0f};
        glm::vec3 half{0.0f};      // its own half extents (x across, z deep)
        float yaw = 0.0f;          // degrees about +Y
    };
    struct CoverSpot {
        glm::vec3 pos{0.0f};       // where to crouch
        glm::vec3 facing{0.0f};    // toward the cover (and the danger past it)
        int object = -1;
    };
    struct Tracer { glm::vec3 from{0.0f}, to{0.0f}; float left = 0.0f; bool hit = false; };

    void buildField();
    void addCover(const char* model, const glm::vec3& pos, float yaw, const glm::vec3& half);
    void addSoldier(uint32_t id, const std::string& name, const glm::vec3& pos, bool enemy);
    Soldier* soldier(uint32_t id);
    const Soldier* soldier(uint32_t id) const;
    std::vector<uint32_t> living(bool enemy) const;

    // Orders.
    void give(kke::OrderKind kind, uint32_t target = 0, const glm::vec3* point = nullptr, bool queue = false);
    void takeCover(const glm::vec3* near = nullptr);
    // Cover spots on the side of each obstacle away from `threat`.
    std::vector<CoverSpot> coverSpots(const glm::vec3& threat) const;
    glm::vec3 threatFor(bool enemy) const;           // where the other side is (its middle)
    int coverObjectNear(const glm::vec3& p, float within) const; // -1: none that close
    bool inCover(const Soldier& s) const;
    void cycleFormation();
    void padSelect(kke::InputMap& in, command_kit::CommandInput::Frame& f, float dt);
    void buildPanel();
    void selectAll();
    void cycleSelection(int step);
    kke::PointerTarget pick(const glm::vec2& pointer) const;
    std::vector<kke::ScreenUnit> screenUnits() const;
    void click(const command_kit::CommandInput::Frame& f);
    void giveContext(const glm::vec2& pointer, bool force, bool queue);
    void giveWheel(int item, const glm::vec2& pointer);
    void pressButton(int button);

    // The fight.
    void handleEvents(const std::vector<kke::ai::AiEvent>& events);
    void shoot(Soldier& from, Soldier& at);
    bool clearShot(const glm::vec3& from, const glm::vec3& to) const;
    void kill(Soldier& s);

    void updateCamera(const command_kit::CommandInput::Frame& f, float dt);
    void centerOnSelection();
    void updateBodies(float dt);
    void updateHud(float dt);
    void runDemo(float dt);

    kke::Application* m_app = nullptr;
    kke::RigidBodyModule* m_rigid = nullptr;
    kke::InputModule* m_input = nullptr;
    kke::ModelModule* m_models = nullptr;
    std::unique_ptr<command_kit::Scenery> m_scenery;
    std::unique_ptr<command_kit::HumanoidKit> m_kit;
    std::unique_ptr<command_kit::CommandHud> m_hud;
    command_kit::CommandInput m_cmd;
    float m_padSelectHeld = -1.0f; // how long the controller's select is held, -1 = not
    int m_formationIndex = 0;      // the panel's copy of m_formation
    std::unique_ptr<kke::DynamicMeshRenderer> m_ring, m_enemyRing, m_hpBack, m_hpFront, m_tracer, m_marker;

    kke::ai::AiWorld m_ai{ 3 };
    kke::OrderBoard m_board;
    std::unique_ptr<kke::AiOrderBridge> m_bridge;
    std::unique_ptr<kke::OrderScript> m_script;
    kke::Selection m_selection;
    kke::Formation m_formation = kke::Formation::Wedge;
    kke::OrderKind m_armed = kke::OrderKind::None; // a button waiting for a target (touch: Attack, Focus)

    std::vector<Soldier> m_soldiers;
    std::vector<CoverObject> m_coverObjects;
    std::vector<Tracer> m_tracers;
    glm::vec3 m_marker3{0.0f};
    float m_markerLeft = 0.0f;
    uint32_t m_nextRoll = 1;

    // The camera (XCOM-like): over the field, looking down at `m_focus`.
    // Every control moves a goal; the view glides to it.
    glm::vec3 m_focus{0.0f, 0.0f, 12.0f}, m_focusGoal{0.0f, 0.0f, 12.0f};
    float m_camYaw = 0.0f, m_camPitch = 50.0f, m_camDistance = 19.0f;
    float m_yawGoal = 0.0f, m_pitchGoal = 50.0f, m_distanceGoal = 19.0f;
    bool m_edgePan = false; // settings: the mouse at the window's edge moves the view
    float m_lastClick = -1.0f; uint32_t m_lastClicked = 0; // a double click on a soldier centres on it

    float m_clock = 0.0f, m_quitAfter = -1.0f;
    bool m_demo = false, m_announcedClear = false;
    int m_demoStep = 0;
    float m_demoTimer = 0.0f;
    uint32_t m_stepTarget = 0;
};

} // namespace platoon
