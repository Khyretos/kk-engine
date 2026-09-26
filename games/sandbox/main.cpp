#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/SoundVisualizerModule.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/DebugDrawModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/StatsModule.h"

#include "SandboxModule.h"
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsModule.h"
#endif

#include <iostream>
#include <vector>

// The sandbox: finds your asset packs, lets you build with them and break
// what you built. See SandboxModule.h and README "Sandbox".
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Sandbox", 1280, 720);
        app.window().setQuitOnEscape(false); // Esc cancels placing / deselects
        app.camera().farPlane = 300.0f;
        app.lighting().lights[0].direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
        app.lighting().lights[1].enabled = true;
        app.lighting().lights[1].isDirectional = true;
        app.lighting().lights[1].direction = glm::normalize(glm::vec3(0.6f, -0.3f, 0.5f));
        app.lighting().lights[1].color = glm::vec3(0.55f, 0.65f, 0.85f);
        app.lighting().lights[1].intensity = 0.35f;
        app.lighting().ambientColor = glm::vec3(0.25f);

        auto& camera = app.addModule<kke::OrbitCameraModule>(/*distance=*/14.0f, /*pitch=*/-0.6f, /*yaw=*/2.6f, glm::vec3(0.0f));
        camera.setControls(kke::OrbitCameraModule::Controls::Editor);
        camera.setDistanceLimits(1.0f, 120.0f);
        std::vector<kke::Module*> panels{ &camera };
        app.addModule<kke::ModelModule>();
        // Before SandboxModule: its update() clears last frame's lines,
        // then the sandbox adds this frame's.
        app.addModule<kke::DebugDrawModule>();
#if KKE_ENABLE_FEMFX
        // Real units, nothing spawned at start; it draws the ground slab.
        auto& physics = app.addModule<kke::PhysicsModule>(/*renderScale=*/1.0f, /*initialObjectCount=*/0);
        app.addModule<kke::AudioModule>(); // impacts and breaks make sound
        app.addModule<kke::SoundVisualizerModule>();
        physics.setDrawGround(false); // the sandbox draws a grid; placed floor tiles are the visible ground
        panels.push_back(&physics);
#endif
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
