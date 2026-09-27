// Procedural animation demo: creatures walking without clips, looking at
// you and reacting to hits (see ProceduralDemoModule.h and README.md).
#include "kke/Application.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"

#include "ProceduralDemoModule.h"

#include <iostream>

int main() {
    try {
        kke::Application app("Procedural Animation", 1280, 720);
        // A fresh morning for the creatures to walk in (assets/moods/morning.yaml).
        app.setMood("morning");

        app.addModule<kke::SettingsModule>("settings.json");
        app.addModule<kke::RigidBodyModule>();
        // Controls are actions (mouse, keyboard or controller, rebindable);
        // the panel is RmlUi (kke::DemoPanelModule). A controller aims with
        // the crosshair: the left stick moves the view, the right one turns it.
        app.addModule<kke::InputModule>("procedural_demo_input.json");
        app.addModule<kke::UiModule>();
        auto& camera = app.addModule<kke::OrbitCameraModule>(6.5f, -0.4f, -0.6f, glm::vec3(0.5f, 0.3f, 0.0f));
        camera.setPadControls(true);
        camera.setPadPan(true);
        app.addModule<procedural_demo::ProceduralDemoModule>();
        app.addModule<kke::DemoPanelModule>("Procedural animation").setPadCrosshair(true);
        app.addModule<kke::StatsModule>().setUiVisible(false);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
