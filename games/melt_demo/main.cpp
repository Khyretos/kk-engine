#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/UiModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/StatsModule.h"

#include "MeltDemoModule.h"

#include <iostream>

// Lava poured onto a meltable block: kke::ParticleFluid + kke::MeltVolume.
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Melt Demo", 1280, 720);
        // Blue hour: the hot, glowing melt reads best against a darker sky (assets/moods/dusk.yaml).
        app.setMood("dusk");
        // Controls are actions (keyboard or controller, rebindable); the
        // settings panel is RmlUi (kke::DemoPanelModule).
        app.addModule<kke::InputModule>("melt_demo_input.json");
        app.addModule<kke::UiModule>();
        // The shared menus: title, settings, controls, pause (docs/GAME_SHELL.md).
        app.addModule<kke::GameShellModule>("Melt", "Heat things until they flow");
        // Plays the mood's ambience loop (the mood names it; BUG-079).
        app.addModule<kke::AudioModule>().setUiVisible(false);
        app.addModule<kke::OrbitCameraModule>(/*distance=*/2.8f, /*pitch=*/-0.35f, /*yaw=*/0.6f, glm::vec3(0.0f, 0.45f, 0.0f))
            .setPadControls(true); // right stick turns, d-pad zooms
        app.addModule<kke_melt::MeltDemoModule>();
        app.addModule<kke::DemoPanelModule>("Melt");
        app.addModule<kke::DebugControlModule>().setUiVisible(false);
        app.addModule<kke::StatsModule>();
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
