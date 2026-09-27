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
        // A bright cartoon sky for bouncy things (assets/moods/playful.yaml).
        app.setMood("playful");
        app.camera().farPlane = 100.0f;
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
