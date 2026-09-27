// Goblin Horde: hold the old fort against waves of goblins (see
// HordeModule.h and README.md).
#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"

#include "HordeModule.h"

#include <iostream>

int main() {
    try {
        kke::Application app("Goblin Horde", 1280, 720);
        // Sundown on the hill: a low orange sun, long shadows (assets/moods/sunset.yaml).
        app.setMood("sunset");

        app.addModule<kke::SettingsModule>("settings.json");
        app.addModule<kke::InputModule>("horde_input.json");
        app.addModule<kke::RigidBodyModule>();
        app.addModule<kke::ModelModule>();
        app.addModule<kke::UiModule>();
        app.addModule<kke::AudioModule>().setUiVisible(false);
        app.addModule<horde::HordeModule>();
        app.addModule<kke::StatsModule>().setUiVisible(false);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
