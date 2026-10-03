// Pet Companion: you and a dog in a garden; tell it what to do with the
// mouse, a controller or a finger (README.md, docs/COMMANDS.md).
#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/ScriptModule.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"

#include "PetModule.h"

#include <filesystem>
#include <iostream>

int main() {
    try {
        kke::Application app("Pet Companion", 1280, 720);
        // A sunny day out with the pet (assets/moods/clear_day.yaml).
        app.setMood("clear_day");

        app.addModule<kke::SettingsModule>("settings.json");
        app.addModule<kke::InputModule>("pet_companion_input.json");
        app.addModule<kke::RigidBodyModule>();
        app.addModule<kke::ModelModule>();
        app.addModule<kke::UiModule>();
        // The shared menus: title, settings, controls, pause (docs/GAME_SHELL.md).
        app.addModule<kke::GameShellModule>("Pet Companion", "Train your dog");
        app.addModule<kke::AudioModule>().setUiVisible(false);
        // scripts/*.lua (order.* and the Ordered / OrderDone events),
        // hot-reloaded from the source folder while you develop.
        std::error_code ec;
        const bool fromSource = std::filesystem::is_directory(GAME_SCRIPTS_SOURCE, ec);
        app.addModule<kke::ScriptModule>(fromSource ? GAME_SCRIPTS_SOURCE : GAME_SCRIPTS_INSTALLED);
        app.addModule<pet_companion::PetModule>();
        app.addModule<kke::StatsModule>().setUiVisible(false);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
