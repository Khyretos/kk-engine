// The wildlife farm: you are a dog, and the animals around you live their
// own lives on the AI core (see FarmModule.h, README.md, docs/AI.md).
#include "kke/Application.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/StatsModule.h"

#include "FarmModule.h"

#include <iostream>

int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Farm", 1280, 720);
        app.camera().farPlane = 300.0f;
        app.camera().fovDegrees = 55.0f;
        app.renderer().setClearColor(glm::vec3(0.58f, 0.74f, 0.9f));
        kke::Lighting& light = app.lighting();
        light.lights[1].enabled = true;
        light.lights[1].isDirectional = true;
        light.lights[1].direction = glm::normalize(glm::vec3(0.5f, -0.3f, 0.6f));
        light.lights[1].color = glm::vec3(0.55f, 0.65f, 0.85f);
        light.lights[1].intensity = 0.3f;
        app.addModule<kke::InputModule>("farm_input.json");
        app.addModule<kke::ModelModule>();
        app.addModule<farm::FarmModule>();
        app.addModule<kke::StatsModule>().setUiVisible(false);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
