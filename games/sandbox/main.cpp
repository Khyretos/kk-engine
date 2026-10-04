#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/SoundVisualizerModule.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/DebugDrawModule.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/ThumbnailModule.h"
#include "kke/modules/UiModule.h"
#if KKE_ENABLE_JOLT
#include "kke/modules/RigidBodyModule.h"
#endif

#include "SandboxModule.h"
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsBridgeModule.h"
#include "kke/modules/PhysicsModule.h"
#endif

#include <iostream>
#include <vector>

// The sandbox: finds your asset packs, lets you build with them and break
// what you built. See SandboxModule.h and docs/HISTORY.md "Sandbox".
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Sandbox", 1280, 720);
        // A bright day to build in (assets/moods/clear_day.yaml).
        app.setMood("clear_day");
        app.window().setQuitOnEscape(false); // Esc puts the tool down, then opens the pause menu
        app.camera().farPlane = 300.0f;

        // Walk mode's controls (rebindable, controllers included) and the
        // pause menu's.
        app.addModule<kke::InputModule>("sandbox_input.json");
        auto& camera = app.addModule<kke::OrbitCameraModule>(/*distance=*/14.0f, /*pitch=*/-0.6f, /*yaw=*/2.6f, glm::vec3(0.0f));
        camera.setControls(kke::OrbitCameraModule::Controls::Editor);
        camera.setDistanceLimits(1.0f, 120.0f);
        std::vector<kke::Module*> panels{ &camera };
        app.addModule<kke::ModelModule>();
        app.addModule<kke::ThumbnailModule>(); // pictures in the Assets panel
        // Before SandboxModule: its update() clears last frame's lines,
        // then the sandbox adds this frame's.
        app.addModule<kke::DebugDrawModule>();
#if KKE_ENABLE_JOLT
        // Jolt: ragdolls in every build (the bat in Play mode), landing on
        // a floor and on the placed pieces (SandboxModule gives it those).
        panels.push_back(&app.addModule<kke::RigidBodyModule>());
#endif
        panels.push_back(&app.addModule<kke::AudioModule>()); // bonks, ragdoll thuds, impacts and breaks
        panels.push_back(&app.addModule<kke::SoundVisualizerModule>());
#if KKE_ENABLE_FEMFX
        // Real units, nothing spawned at start; it draws the ground slab.
        auto& physics = app.addModule<kke::PhysicsModule>(/*renderScale=*/1.0f, /*initialObjectCount=*/0);
        physics.setDrawGround(false); // the sandbox draws a grid; placed floor tiles are the visible ground
        panels.push_back(&physics);
        // FEMFX <-> Jolt: breaking props land on ragdolls, rolling props
        // and the walker, and push them (docs/PHYSICS_BRIDGE.md).
        panels.push_back(&app.addModule<kke::PhysicsBridgeModule>());
#endif
        // RmlUi: the node graph editor (Look, in Play mode).
        app.addModule<kke::UiModule>();
        // The menus every game shares (docs/GAME_SHELL.md): a title with
        // the two ways to play, pause, settings and controls. Before the
        // sandbox, so Esc reaches the menu before the sandbox puts the
        // tool down (SandboxModule::escWouldCancel).
        app.addModule<kke::GameShellModule>("Sandbox", "Drag things in, then bonk, shoot, burn and melt them");
        auto& sandbox = app.addModule<kke_sandbox::SandboxModule>();
        panels.push_back(&app.addModule<kke::DebugControlModule>());
        panels.push_back(&app.addModule<kke::StatsModule>());
        sandbox.setEnginePanels(panels);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
