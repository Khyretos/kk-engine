#include "kke/Application.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/StatsModule.h"

#include "MeltDemoModule.h"

#include <iostream>

// Lava poured onto a meltable block: kke::ParticleFluid + kke::MeltVolume.
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Melt Demo", 1280, 720);
        app.lighting().lights[0].direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
        app.lighting().lights[0].intensity = 2.0f;
        app.lighting().ambientColor = glm::vec3(0.18f);
        app.addModule<kke::OrbitCameraModule>(/*distance=*/2.8f, /*pitch=*/-0.35f, /*yaw=*/0.6f, glm::vec3(0.0f, 0.45f, 0.0f));
        app.addModule<kke_melt::MeltDemoModule>();
        app.addModule<kke::DebugControlModule>().setUiVisible(false);
        app.addModule<kke::StatsModule>();
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
