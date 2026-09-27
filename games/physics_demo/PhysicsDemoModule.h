#pragma once

#include "kke/Module.h"

namespace kke_physics_demo {

// The physics demo's controls and panel: spawn soft and breakable things
// into kke::PhysicsModule (FEMFX) from a controller, the keyboard or the
// mouse. The panel is RmlUi (kke::DemoPanelModule); PhysicsModule's own
// ImGui "Physics" window stays an F1 developer panel.
//
//   A / Space      spawn a tetrahedron of the chosen material
//   X / Enter      spawn the chosen scene
//   LB RB / [ ]    choose the material / the scene
//   Y / C          clear everything
class PhysicsDemoModule : public kke::Module {
public:
    const char* name() const override { return "PhysicsDemo"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;

private:
    void applyMaterial();
    void spawnChosenScene();
    kke::Application* m_app = nullptr;
    int m_material = 0;
    int m_scene = 0;
    int m_debris = 200;
};

} // namespace kke_physics_demo
