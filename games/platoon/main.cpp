// Platoon: a squad on a training ground; select soldiers and order them with the
// mouse, a controller or a finger (README.md, docs/COMMANDS.md).
#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/ScriptModule.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"

#include "PlatoonModule.h"

#include <filesystem>
#include <iostream>

int main() {
    try {
        kke::Application app("Platoon", 1280, 720);
        // A clear, cool morning in the field (assets/moods/morning.yaml).
        app.setMood("morning");

        app.addModule<kke::SettingsModule>("settings.json");
        app.addModule<kke::InputModule>("platoon_input.json");
        app.addModule<kke::RigidBodyModule>();
        app.addModule<kke::ModelModule>();
        app.addModule<kke::UiModule>();
        app.addModule<kke::AudioModule>().setUiVisible(false);
        // scripts/*.lua (order.* and the Ordered / OrderDone events),
        // hot-reloaded from the source folder while you develop.
        std::error_code ec;
        const bool fromSource = std::filesystem::is_directory(GAME_SCRIPTS_SOURCE, ec);
        app.addModule<kke::ScriptModule>(fromSource ? GAME_SCRIPTS_SOURCE : GAME_SCRIPTS_INSTALLED);
        app.addModule<platoon::PlatoonModule>();
        app.addModule<kke::StatsModule>().setUiVisible(false);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
