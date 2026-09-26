#pragma once

#include "kke/Animator.h"
#include "kke/CameraRig.h"
#include "kke/Module.h"
#include "kke/RigidWorld.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/ModelModule.h"

#include <memory>
#include <vector>

namespace kke { class RigidBodyModule; class PhysicsModule; }

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
    void updateAnimation(float dt, float speed, bool grounded);
    void shoot();
    void forcePush();
    void setCaptured(bool on);

    kke::Application* m_app = nullptr;
    kke::RigidBodyModule* m_rigid = nullptr;
    kke::PhysicsModule* m_femfx = nullptr;
    kke::ModelModule* m_models = nullptr;
    std::unique_ptr<kke::DynamicMeshRenderer> m_level, m_capsule;
    // Unit cubes, one per colour (the renderer colours by vertex).
    std::vector<std::unique_ptr<kke::DynamicMeshRenderer>> m_cubes;

    // Crates and the moving platform (Jolt).
    struct Crate { kke::RigidWorld::BodyId body; glm::vec3 half; int cube; };
    std::vector<Crate> m_crates;
    kke::RigidWorld::BodyId m_platform = kke::RigidWorld::kNoBody;
    glm::vec3 m_platformHalf{1.5f, 0.15f, 1.5f};
    float m_platformTime = 0.0f;

    // Player.
    kke::RigidWorld::CharacterId m_player = 0;
    kke::CameraRig m_rig;
    float m_facing = 0.0f;          // degrees, the body's yaw
    bool m_captured = false;
    bool m_crouch = false, m_walk = false, m_sprint = false;
    bool m_jumpQueued = false;
    glm::vec3 m_spawn{0.0f, 0.05f, 6.0f};
    kke::ModelModule::ModelId m_charModel = 0;
    kke::ModelModule::InstanceId m_charInstance = 0;
    std::unique_ptr<kke::AnimationSet> m_animSet;
    std::unique_ptr<kke::Animator> m_anim;
    int m_stMove = -1, m_stCrouch = -1, m_stJump = -1, m_stFall = -1, m_stLand = -1;
    float m_airTime = 0.0f;

    // Lighting panel.
    float m_sunAzimuth = 35.0f, m_sunElevation = 50.0f, m_sunIntensity = 1.0f, m_ambient = 0.25f;
    glm::vec3 m_sunColor{1.0f, 0.95f, 0.85f};

    std::vector<kke::Module*> m_panels;
    bool m_showPanels = false;
    float m_fps = 0.0f;
    std::string m_status;
};

} // namespace kke_showcase
