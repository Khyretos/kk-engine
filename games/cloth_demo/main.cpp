#include "kke/Application.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/StatsModule.h"

#include "ClothDemoModule.h"

#include <iostream>

// Cloth: fabrics, a bed, nets and a cape on Jolt soft bodies with the
// engine's clipping protection (kke/Cloth.h, docs/CLOTH.md).
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Cloth Demo", 1280, 720);
        // Soft daylight: fabric reads best in an even light (assets/moods).
        app.setMood("studio");
        app.camera().farPlane = 120.0f;
        app.addModule<kke::OrbitCameraModule>(/*distance=*/8.0f, /*pitch=*/-0.2f, /*yaw=*/0.0f, glm::vec3(0.0f, 1.4f, 0.0f));
        app.addModule<kke_cloth::ClothDemoModule>();
        app.addModule<kke::DebugControlModule>().setUiVisible(false);
        app.addModule<kke::StatsModule>();
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
