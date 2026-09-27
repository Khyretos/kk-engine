// Procedural animation demo: creatures walking without clips, looking at
// you and reacting to hits (see ProceduralDemoModule.h and README.md).
#include "kke/Application.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/StatsModule.h"

#include "ProceduralDemoModule.h"

#include <iostream>

int main() {
    try {
        kke::Application app("Procedural Animation", 1280, 720);
        // Morning sun over a meadow.
        kke::Lighting& light = app.lighting();
        light.lights[0].enabled = true;
        light.lights[0].direction = glm::normalize(glm::vec3(-0.45f, -0.75f, -0.4f));
        light.lights[0].color = glm::vec3(1.0f, 0.95f, 0.86f);
        light.lights[0].intensity = 2.4f;
        light.lights[1].enabled = true;
        light.lights[1].isDirectional = true;
        light.lights[1].direction = glm::normalize(glm::vec3(0.5f, -0.4f, 0.6f));
        light.lights[1].color = glm::vec3(0.6f, 0.7f, 0.9f);
        light.lights[1].intensity = 0.4f;
        light.ambientColor = glm::vec3(0.25f);

        app.addModule<kke::SettingsModule>("settings.json");
        app.addModule<kke::RigidBodyModule>();
        app.addModule<kke::OrbitCameraModule>(6.5f, -0.4f, -0.6f, glm::vec3(0.5f, 0.3f, 0.0f));
        app.addModule<procedural_demo::ProceduralDemoModule>();
        app.addModule<kke::StatsModule>().setUiVisible(false);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
