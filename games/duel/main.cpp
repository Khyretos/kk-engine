// Duel: a one-on-one fist fight in a ring against a sparring bot or a
// second player (see DuelModule.h and README.md).
#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"

#include "DuelModule.h"

#include <iostream>

int main() {
    try {
        kke::Application app("Duel", 1280, 720);
        // A gym fight under the lights at night (assets/moods/arena_night.yaml).
        app.setMood("arena_night");

        app.addModule<kke::SettingsModule>("settings.json");
        app.addModule<kke::InputModule>("duel_input.json");
        app.addModule<kke::RigidBodyModule>();
        app.addModule<kke::ModelModule>();
        app.addModule<kke::UiModule>();
        // The shared menus: title, settings, controls, pause (docs/GAME_SHELL.md).
        app.addModule<kke::GameShellModule>("Duel", "A one-on-one fist fight");
        app.addModule<kke::AudioModule>().setUiVisible(false);
        app.addModule<duel::DuelModule>();
        app.addModule<kke::StatsModule>().setUiVisible(false);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
