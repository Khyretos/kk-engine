#include "PhysicsDemoModule.h"

#include "kke/Application.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/MaterialGridModule.h"
#include "kke/modules/PhysicsModule.h"

#include <cstdio>
#include <string>
#include <typeindex>
#include <vector>

namespace kke_physics_demo {

namespace {

using Scene = kke::PhysicsModule::Scene;
// The scenes worth a button (LavaMelt is kept for the scripted benchmark
// only; real melting lives in games/melt_demo).
const struct { Scene scene; const char* label; } kScenes[] = {
    { Scene::BreakTest, "Break test" },   { Scene::GlassSheet, "Glass sheet" },          { Scene::Brick, "Brick" },
    { Scene::RubberBall, "Rubber ball" }, { Scene::CarCrash, "Car crash" },              { Scene::FracturableCube, "Fracturable cube" },
    { Scene::PlasticCube, "Plastic cube" },
};
constexpr int kSceneCount = static_cast<int>(sizeof(kScenes) / sizeof(kScenes[0]));

} // namespace

std::vector<kke::ModuleDependency> PhysicsDemoModule::dependencies() const {
    return { { std::type_index(typeid(kke::PhysicsModule)), true, "the soft bodies it spawns" },
             { std::type_index(typeid(kke::InputModule)), true, "controller and keyboard controls" } };
}

void PhysicsDemoModule::init(kke::Application& app) {
    m_app = &app;
    auto* physics = app.getModule<kke::PhysicsModule>();
    m_debris = static_cast<int>(physics->debrisBudget());
    applyMaterial();

    auto* in = app.getModule<kke::InputModule>();
    using IM = kke::InputModule;
    kke::InputMap& m = in->map(0);
    auto action = [&](const char* id, const char* label, SDL_Scancode key, SDL_GamepadButton pad) {
        m.defineAction({ id, label, "Physics" });
        m.addBinding(IM::bind(id, IM::key(key)));
        m.addBinding(IM::bind(id, IM::pad(pad)));
    };
    action("phys.spawn", "Spawn a tetrahedron", SDL_SCANCODE_SPACE, SDL_GAMEPAD_BUTTON_SOUTH);
    action("phys.scene", "Spawn the chosen scene", SDL_SCANCODE_RETURN, SDL_GAMEPAD_BUTTON_WEST);
    action("phys.material", "Next material", SDL_SCANCODE_LEFTBRACKET, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    action("phys.next", "Next scene", SDL_SCANCODE_RIGHTBRACKET, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    action("phys.clear", "Clear everything", SDL_SCANCODE_C, SDL_GAMEPAD_BUTTON_NORTH);
    in->commitDefaults();

    auto* panel = app.getModule<kke::DemoPanelModule>();
    if (!panel) return;
    auto& s = panel->section("Physics");
    s.text("{phys.spawn} spawn  {phys.scene} scene  {phys.material} material  {phys.next} next scene  {phys.clear} clear");
    std::vector<std::string> materials;
    for (int i = 0; i < kke::MaterialGridModule::presetCount(); ++i) materials.push_back(kke::MaterialGridModule::presetLabel(i));
    s.choice("Material", &m_material, materials, [this] { applyMaterial(); });
    s.button("Spawn tetrahedron", [physics] { physics->spawnTetrahedronHere(); });
    std::vector<std::string> scenes;
    for (const auto& sc : kScenes) scenes.push_back(sc.label);
    s.choice("Scene", &m_scene, scenes);
    s.button("Spawn scene", [this] { spawnChosenScene(); });
    s.button("Clear all", [physics] { physics->clearAll(); });
    s.slider("Debris budget", &m_debris, 0, 500, [this, physics] { physics->setDebrisBudget(static_cast<uint32_t>(m_debris)); });
    // The world's fracture seed (was only in the ImGui Physics window,
    // BUG-082): the same seed breaks the same way again (HW-014).
    // The slider covers 1-999; a bigger seed (typed into the ImGui
    // Physics window) shows as text instead.
    s.slider("Fracture seed", kke::DemoPanelModule::Ref<int>([this, physics] {
                 m_seed = static_cast<int>(physics->fractureWorldSeed());
                 return m_seed <= 999 ? &m_seed : nullptr;
             }),
             1, 999, [this, physics] { physics->setFractureWorldSeed(static_cast<uint32_t>(m_seed)); });
    s.text([physics] { return "Fracture seed " + std::to_string(physics->fractureWorldSeed()); })
        .showIf([physics] { return physics->fractureWorldSeed() > 999; });
    s.button("New fracture seed", [physics] { physics->setFractureWorldSeed(1u + static_cast<uint32_t>(SDL_GetPerformanceCounter() % 999u)); });
    s.note("Most broken pieces kept at once (0 = no limit). Past it the oldest sleeping piece goes. Each awake piece costs about "
           "0.2 ms per step on one core.");
    s.text([physics] {
        const kke::PhysicsModule::PanelStats st = physics->panelStats();
        char buf[260];
        std::snprintf(buf, sizeof(buf), "Objects %zu. Step %.2f ms avg, %.2f ms max (%.0f ticks/s). Pieces %u (%u awake), %u faces drawn. %.0f fps",
                      physics->objectCount(), st.stepMsAvg, st.stepMsMax, static_cast<double>(st.ticksPerSecond), st.pieces, st.awakePieces,
                      st.facesDrawn, static_cast<double>(st.framesPerSecond));
        return std::string(buf);
    });
    s.text("Simulation can't keep up: running in slow motion").showIf([physics, this] {
        return m_app->fixedStepsLastFrame() >= m_app->maxFixedStepsPerFrame() && physics->panelStats().ticksPerSecond < 55.0f;
    });
    s.note("Lava and melting: run melt_demo.");
}

void PhysicsDemoModule::applyMaterial() {
    m_app->getModule<kke::PhysicsModule>()->selectedMaterial() = kke::MaterialGridModule::presetMaterial(m_material);
}

void PhysicsDemoModule::spawnChosenScene() {
    m_app->getModule<kke::PhysicsModule>()->spawnScene(kScenes[m_scene].scene);
}

void PhysicsDemoModule::update(const kke::UpdateContext&) {
    const kke::InputMap& m = m_app->getModule<kke::InputModule>()->map(0);
    auto* physics = m_app->getModule<kke::PhysicsModule>();
    if (m.pressed("phys.spawn")) physics->spawnTetrahedronHere();
    if (m.pressed("phys.scene")) spawnChosenScene();
    if (m.pressed("phys.material")) {
        m_material = (m_material + 1) % kke::MaterialGridModule::presetCount();
        applyMaterial();
    }
    if (m.pressed("phys.next")) m_scene = (m_scene + 1) % kSceneCount;
    if (m.pressed("phys.clear")) physics->clearAll();
}

} // namespace kke_physics_demo
