#include "kke/Application.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/StatsModule.h"

#include "JiggleDemoModule.h"

#include <iostream>

// Jiggle physics: a jelly with balls bouncing on it (kke::JellyBody) and a
// character with soft-tissue bones (kke::JiggleRig, kke::JiggleSkin).
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Jiggle Demo", 1280, 720);
        app.camera().farPlane = 100.0f;
        app.lighting().lights[0].direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.35f));
        app.lighting().lights[0].intensity = 1.8f;
        app.lighting().lights[1].enabled = true;
        app.lighting().lights[1].isDirectional = true;
        app.lighting().lights[1].direction = glm::normalize(glm::vec3(0.6f, -0.3f, 0.5f));
        app.lighting().lights[1].color = glm::vec3(0.55f, 0.65f, 0.85f);
        app.lighting().lights[1].intensity = 0.35f;
        app.lighting().ambientColor = glm::vec3(0.24f);
        app.addModule<kke::OrbitCameraModule>(/*distance=*/2.6f, /*pitch=*/-0.45f, /*yaw=*/0.5f, glm::vec3(0.0f, 0.3f, 0.0f));
        app.addModule<kke::ModelModule>();
        app.addModule<kke_jiggle::JiggleDemoModule>();
        app.addModule<kke::DebugControlModule>().setUiVisible(false);
        app.addModule<kke::StatsModule>();
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
