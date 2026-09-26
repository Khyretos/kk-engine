#include "kke/Application.h"
#include "kke/Renderer.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/StatsModule.h"

#include "SeaDemoModule.h"

#include <iostream>

// A boat, the open sea, and things that float or sink.
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Sea Demo", 1280, 720);
        app.renderer().setClearColor(glm::vec3(0.55f, 0.7f, 0.85f));
        app.camera().farPlane = 400.0f;
        app.lighting().lights[0].direction = glm::normalize(glm::vec3(-0.5f, -0.45f, -0.3f));
        app.lighting().lights[0].color = glm::vec3(1.0f, 0.95f, 0.85f);
        app.lighting().lights[0].intensity = 2.2f;
        app.lighting().ambientColor = glm::vec3(0.3f, 0.35f, 0.42f);
        // Sea module first: its update() moves the camera target before the
        // orbit camera positions itself, and it draws the sky first.
        app.addModule<kke_sea::SeaDemoModule>();
        auto& camera = app.addModule<kke::OrbitCameraModule>(/*distance=*/12.0f, /*pitch=*/-0.3f, /*yaw=*/2.2f, glm::vec3(0.0f, 1.0f, 0.0f));
        camera.setDistanceLimits(2.0f, 80.0f);
        camera.setControls(kke::OrbitCameraModule::Controls::Editor); // left click throws
        app.addModule<kke::DebugControlModule>().setUiVisible(false);
        app.addModule<kke::StatsModule>();
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
