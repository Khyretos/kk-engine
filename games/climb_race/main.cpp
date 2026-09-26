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
        // Late afternoon sun on the face, sky light from behind the climbers.
        kke::Lighting& light = app.lighting();
        light.lights[0].enabled = true;
        light.lights[0].direction = glm::normalize(glm::vec3(-0.35f, -0.8f, -0.55f));
        light.lights[0].color = glm::vec3(1.0f, 0.93f, 0.82f);
        light.lights[0].intensity = 2.2f;
        light.lights[1].enabled = true;
        light.lights[1].isDirectional = true;
        light.lights[1].direction = glm::normalize(glm::vec3(0.5f, -0.3f, 0.6f));
        light.lights[1].color = glm::vec3(0.55f, 0.65f, 0.85f);
        light.lights[1].intensity = 0.35f;
        light.ambientColor = glm::vec3(0.22f);

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
