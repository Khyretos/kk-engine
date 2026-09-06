#pragma once

#include "kke/Module.h"

// This entire module only exists when KKE_ENABLE_FEMFX is on — same
// guard, same reasoning, as kke::PhysicsModule itself (see
// PhysicsModule.h). Found as a real, previously-latent bug: this
// header always included/used PhysicsModule unconditionally, which
// compiled fine every time it happened to be tested (always with
// FEMFX on), but kke_engine itself builds once per CMake
// configuration regardless of which specific demo enables FEMFX —
// a build with KKE_ENABLE_FEMFX=0 failed immediately once this file
// was actually compiled as part of one. Guarding the whole module
// this way is also the honest reflection of reality: MaterialGridModule
// has nothing meaningful to do without a PhysicsModule to select
// materials for.
#if KKE_ENABLE_FEMFX

namespace Rml {
class ElementDocument;
class Event;
} // namespace Rml

namespace kke {

// A real, extraction-shooter-style loot/inventory grid, built specifically
// for this — not a generic idea grafted on. Each card is a real material
// preset (see kke::Material); clicking one genuinely changes
// PhysicsModule::selectedMaterial(), which the next "Spawn tetrahedron"
// click actually uses — the same "real state, not decoration" standard
// this whole engine's UI work has followed since kke::LightingControlsModule
// (see that class's own header comment for the precedent this follows).
//
// Requires both kke::UiModule and kke::PhysicsModule to already be added
// and initialised first — same requirement, same reasoning, as every
// other RmlUi-content module in this engine.
class MaterialGridModule : public Module {
public:
    const char* name() const override { return "MaterialGrid"; }
    ~MaterialGridModule();
    // Same reasoning, same defaults-preserve-existing-behavior
    // approach, as kke::LightingControlsModule's own left/top
    // parameters — added for the exact same reason: this module and
    // LightingControlsModule originally hardcoded the identical
    // screen position, which only became a real problem once a
    // single demo (kke_demo, see README "Complete kke_demo showcase")
    // needed both at once.
    explicit MaterialGridModule(float left = 40.0f, float top = 500.0f);
    void init(Application& app) override;
    void shutdown() override;

private:
    class CardClickListener;

    Application* m_app = nullptr;
    float m_left;
    float m_top;
    Rml::ElementDocument* m_document = nullptr;
    CardClickListener* m_listener = nullptr; // raw pointer, not unique_ptr -- see
                                              // LightingControlsModule.h's own comment
                                              // for the real, confirmed reason why
};

} // namespace kke

#endif // KKE_ENABLE_FEMFX
