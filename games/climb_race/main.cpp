// Climb Race: race a rival up a generated mountain face, choosing every
// hold yourself (see ClimbRaceModule.h and README.md).
#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"

#include "ClimbRaceModule.h"

#include <iostream>

int main() {
    try {
        kke::Application app("Climb Race", 1280, 720);
        // Late afternoon sun raking across the rock face (assets/moods/golden_hour.yaml).
        app.setMood("golden_hour");

        app.addModule<kke::SettingsModule>("settings.json");
        app.addModule<kke::InputModule>("climb_race_input.json");
        app.addModule<kke::RigidBodyModule>();
        app.addModule<kke::ModelModule>();
        app.addModule<kke::UiModule>();
        app.addModule<kke::AudioModule>().setUiVisible(false);
        app.addModule<climb_race::ClimbRaceModule>();
        app.addModule<kke::StatsModule>().setUiVisible(false);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
