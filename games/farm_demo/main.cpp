// The wildlife farm: you are a dog, and the animals around you live their
// own lives on the AI core (see FarmModule.h, README.md, docs/AI.md).
#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"

#include "FarmModule.h"

#include <iostream>

int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Farm", 1280, 720);
        // Morning on the farm (assets/moods/morning.yaml).
        app.setMood("clear_day");
        app.camera().farPlane = 300.0f;
        app.camera().fovDegrees = 55.0f;
        app.addModule<kke::InputModule>("farm_input.json");
        app.addModule<kke::UiModule>();
        // The shared menus: title, settings, controls, pause (docs/GAME_SHELL.md).
        app.addModule<kke::GameShellModule>("Farm", "Animals that learn and a navigation mesh");
        // Plays the mood's ambience loop (BUG-086).
        app.addModule<kke::AudioModule>().setUiVisible(false);
        app.addModule<kke::ModelModule>();
        // The HUD and settings (RmlUi). F1 is the farm's "what they think".
        app.addModule<kke::DemoPanelModule>("Farm").setDeveloperPanelsKey(false);
        app.addModule<farm::FarmModule>();
        app.addModule<kke::StatsModule>().setUiVisible(false);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
