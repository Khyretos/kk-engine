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
        // Morning on the farm (assets/moods/morning.yaml).
        app.setMood("clear_day");
        app.camera().farPlane = 300.0f;
        app.camera().fovDegrees = 55.0f;
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
