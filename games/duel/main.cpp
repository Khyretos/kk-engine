// Duel: a one-on-one fist fight in a ring against a sparring bot or a
// second player (see DuelModule.h and README.md).
#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
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
        // A gym at night: a warm key light over the ring, cool fill from the side.
        kke::Lighting& light = app.lighting();
        light.lights[0].enabled = true;
        light.lights[0].direction = glm::normalize(glm::vec3(-0.25f, -1.0f, -0.35f));
        light.lights[0].color = glm::vec3(1.0f, 0.94f, 0.84f);
        light.lights[0].intensity = 2.0f;
        light.lights[1].enabled = true;
        light.lights[1].isDirectional = true;
        light.lights[1].direction = glm::normalize(glm::vec3(0.6f, -0.35f, 0.5f));
        light.lights[1].color = glm::vec3(0.5f, 0.6f, 0.9f);
        light.lights[1].intensity = 0.4f;
        light.ambientColor = glm::vec3(0.2f);

        app.addModule<kke::SettingsModule>("settings.json");
        app.addModule<kke::InputModule>("duel_input.json");
        app.addModule<kke::RigidBodyModule>();
        app.addModule<kke::ModelModule>();
        app.addModule<kke::UiModule>();
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
