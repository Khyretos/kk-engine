// Pet Companion: you and a dog in a garden; tell it what to do with the
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

#include "PetModule.h"

#include <filesystem>
#include <iostream>

int main() {
    try {
        kke::Application app("Pet Companion", 1280, 720);
        // A warm afternoon.
        kke::Lighting& light = app.lighting();
        light.lights[0].enabled = true;
        light.lights[0].direction = glm::normalize(glm::vec3(-0.45f, -0.9f, -0.35f));
        light.lights[0].color = glm::vec3(1.0f, 0.94f, 0.84f);
        light.lights[0].intensity = 2.1f;
        light.lights[1].enabled = true;
        light.lights[1].direction = glm::normalize(glm::vec3(0.6f, -0.3f, 0.5f));
        light.lights[1].color = glm::vec3(0.55f, 0.65f, 0.85f);
        light.lights[1].intensity = 0.3f;
        light.ambientColor = glm::vec3(0.22f);
        light.toneMapper = kke::ToneMapper::AgX;

        app.addModule<kke::SettingsModule>("settings.json");
        app.addModule<kke::InputModule>("pet_companion_input.json");
        app.addModule<kke::RigidBodyModule>();
        app.addModule<kke::ModelModule>();
        app.addModule<kke::UiModule>();
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
