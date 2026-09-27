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
        // Dusk on the hill: a low orange sun, a cold blue sky fill.
        kke::Lighting& light = app.lighting();
        light.lights[0].enabled = true;
        light.lights[0].direction = glm::normalize(glm::vec3(-0.7f, -0.45f, -0.35f));
        light.lights[0].color = glm::vec3(1.0f, 0.72f, 0.45f);
        light.lights[0].intensity = 2.2f;
        light.lights[1].enabled = true;
        light.lights[1].isDirectional = true;
        light.lights[1].direction = glm::normalize(glm::vec3(0.5f, -0.6f, 0.6f));
        light.lights[1].color = glm::vec3(0.45f, 0.55f, 0.85f);
        light.lights[1].intensity = 0.45f;
        light.ambientColor = glm::vec3(0.2f, 0.19f, 0.22f);

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
