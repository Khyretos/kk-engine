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
        app.camera().farPlane = 400.0f;
        // A bright day over open water (assets/moods/clear_day.yaml): the
        // sky and its clouds reflected in every swell.
        app.setMood("clear_day");
        // Sea module first: its update() moves the camera target before the
        // orbit camera positions itself.
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
