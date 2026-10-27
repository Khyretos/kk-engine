#include "kke/Application.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"

#include "WaterDemoModule.h"

#include <iostream>

// A tank of water with crates in it: kke::MpmWater + kke::WaterBodyLink +
// kke::FluidSurfaceRenderer's clear water. See WaterDemoModule.h.
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Water Demo", 1280, 720);
        // Bright and clear, so the water's colour and what's under it read (assets/moods/clear_day.yaml).
        app.setMood("clear_day");
        app.addModule<kke::InputModule>("water_demo_input.json");
        app.addModule<kke::UiModule>();
        // The shared menus: title, settings, controls, pause (docs/GAME_SHELL.md).
        app.addModule<kke::GameShellModule>("Water", "Break a dam, float crates, make waves");
        app.addModule<kke::OrbitCameraModule>(/*distance=*/4.6f, /*pitch=*/-0.5f, /*yaw=*/3.5f, glm::vec3(0.0f, 0.4f, 0.0f))
            .setPadControls(true); // right stick turns, d-pad zooms
        app.addModule<kke::RigidBodyModule>();
        app.addModule<kke_water::WaterDemoModule>();
        app.addModule<kke::DemoPanelModule>("Water");
        app.addModule<kke::DebugControlModule>().setUiVisible(false);
        app.addModule<kke::StatsModule>();
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
