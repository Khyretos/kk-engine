#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"

#include "AnimDemoModule.h"

#include <iostream>

// The animation tools side by side: a 2D blend space, rest matching and
// CCD against FABRIK (ENG-G19).
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Animation Demo", 1280, 720);
        app.setMood("playful");
        app.addModule<kke::InputModule>("anim_demo_input.json");
        app.addModule<kke::UiModule>();
        app.addModule<kke::AudioModule>().setUiVisible(false);
        app.addModule<kke::OrbitCameraModule>(/*distance=*/7.5f, /*pitch=*/-0.3f, /*yaw=*/0.6f, glm::vec3(0.0f, 1.0f, 0.0f))
            .setPadControls(true); // right stick turns, d-pad zooms
        app.addModule<kke::ModelModule>();
        app.addModule<kke_anim::AnimDemoModule>();
        app.addModule<kke::DemoPanelModule>("Animation", kke::DemoPanelModule::Side::Right);
        app.addModule<kke::DebugControlModule>().setUiVisible(false);
        app.addModule<kke::StatsModule>();
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
