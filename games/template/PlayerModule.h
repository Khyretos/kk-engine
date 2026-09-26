#pragma once

#include "kke/CameraRig.h"
#include "kke/Locomotion.h"
#include "kke/Module.h"
#include "kke/RigidWorld.h"

#include <memory>

namespace kke {
class DynamicMeshRenderer;
class InputModule;
class RigidBodyModule;
} // namespace kke

namespace starter {

// The player: a character you walk, run, jump, vault and climb with, and
// a third-person camera that follows it. Movement is kke::Locomotion on
// Jolt's character controller (docs/MOVEMENT.md), the camera is
// kke::CameraRig, the controls are InputModule's standard character
// actions (rebindable, controllers included).
//
// It also gives Lua scripts a `player` table (docs/tutorials/), which is
// how you add your own bindings: player.position(), player.teleport(pos),
// player.facing().
//
// The body is a coloured block. kke_demo (games/showcase) shows how to put
// an animated character model on the same capsule.
class PlayerModule : public kke::Module {
public:
    PlayerModule();
    ~PlayerModule() override;
    const char* name() const override { return "Player"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void onEvent(const SDL_Event& event) override;

    glm::vec3 spawn{0.0f, 0.1f, 6.0f};

private:
    void registerLua();
    void setCaptured(bool on);

    kke::Application* m_app = nullptr;
    kke::RigidBodyModule* m_rigid = nullptr;
    kke::InputModule* m_input = nullptr;
    kke::RigidWorld::CharacterId m_player = 0;
    std::unique_ptr<kke::Locomotion> m_loco;
    kke::CameraRig m_rig;
    std::unique_ptr<kke::DynamicMeshRenderer> m_body;
    bool m_captured = false;
    bool m_jumpQueued = false;
    float m_mouseSensitivity = 0.12f; // degrees per pixel
    float m_stickSpeed = 200.0f;      // degrees per second at full stick
};

} // namespace starter
