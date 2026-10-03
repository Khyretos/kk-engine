#pragma once

#include "kke/CameraRig.h"
#include "kke/Locomotion.h"
#include "kke/Module.h"
#include "kke/RigidWorld.h"

#include <memory>
#include <string>

namespace kke {
class DynamicMeshRenderer;
class InputModule;
class RigidBodyModule;
} // namespace kke

namespace starter {
class PlayerBody;
} // namespace starter

namespace cookbook {

// The cookbook's player: the starter template's character (games/template)
// with every common game camera, switchable while you play. Each camera is
// a few lines in update(); the docs quote them (docs/cookbook/cameras.md).
//
//   first      eyes at the head (shooters, horror)
//   third      over the shoulder, never through walls (action, adventure)
//   orbit      circles the character; the mouse turns it (inspecting, editors)
//   topdown    straight down from above (twin-stick shooters, puzzles)
//   iso        high and at 45 degrees (strategy, Diablo-likes)
//   side       from the side, moving along one line (platformers)
//   fixed      a camera on the wall that turns to watch (Resident Evil)
//   cinematic  a smooth path through keyframes (intros, cutscenes)
//
// Lua gets `view.mode(name)`, `view.shake(amount)`, `view.path(keys, loop)`
// and the starter's `player.position()` / `player.teleport(pos)`.
class CookbookPlayer : public kke::Module {
public:
    enum class View { First, Third, Orbit, TopDown, Iso, Side, Fixed, Cinematic, Count };
    static const char* viewName(View v);
    static bool viewFromName(const std::string& name, View& out);

    CookbookPlayer();
    ~CookbookPlayer() override;
    const char* name() const override { return "CookbookPlayer"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void onEvent(const SDL_Event& event) override;

    void setView(View v);
    View view() const { return m_view; }
    // Screen shake: 0..1 "trauma" that fades by itself (explosions, hits).
    void shake(float amount);

    glm::vec3 spawn{ 0.0f, 0.1f, 6.0f };
    glm::vec3 fixedCameraAt{ 9.0f, 5.0f, 9.0f }; // where the "fixed" camera hangs

private:
    void registerLua();
    void setCaptured(bool on);
    // Which way "forward" on the stick means for this camera (flat, unit).
    glm::vec3 moveForward() const;

    kke::Application* m_app = nullptr;
    kke::RigidBodyModule* m_rigid = nullptr;
    kke::InputModule* m_input = nullptr;
    kke::RigidWorld::CharacterId m_player = 0;
    std::unique_ptr<kke::Locomotion> m_loco;
    kke::CameraRig m_rig;
    std::unique_ptr<starter::PlayerBody> m_mannequin;
    std::unique_ptr<kke::DynamicMeshRenderer> m_body; // the block, without the mannequin
    View m_view = View::Third;
    float m_trauma = 0.0f;
    float m_time = 0.0f;
    bool m_captured = false;
    bool m_jumpQueued = false;
    float m_mouseSensitivity = 0.12f; // degrees per pixel
    float m_stickSpeed = 200.0f;      // degrees per second at full stick
};

} // namespace cookbook
