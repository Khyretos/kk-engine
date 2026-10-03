#include "kke/Application.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/UiModule.h"
#include "kke/modules/StatsModule.h"

#include "ClothDemoModule.h"

#include <iostream>

// Cloth and hair: fabrics, a bed, nets, a cape and heads of hair on Jolt
// soft bodies, with the engine's clipping protection (kke/Cloth.h,
// kke/Hair.h, docs/CLOTH.md, docs/HAIR.md).
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Cloth Demo", 1280, 720);
        // Soft daylight: fabric reads best in an even light (assets/moods).
        app.setMood("studio");
        app.camera().farPlane = 120.0f;
        // Controls are actions (keyboard or controller, rebindable); the
        // settings panel is RmlUi (kke::DemoPanelModule, on the right).
        app.addModule<kke::InputModule>("cloth_demo_input.json");
        app.addModule<kke::UiModule>();
        // The shared menus: title, settings, controls, pause (docs/GAME_SHELL.md).
        app.addModule<kke::GameShellModule>("Cloth and Hair", "Capes, sheets, flags and every hair type");
        app.addModule<kke::OrbitCameraModule>(/*distance=*/8.0f, /*pitch=*/-0.2f, /*yaw=*/0.0f, glm::vec3(0.0f, 1.4f, 0.0f))
            .setPadControls(true); // right stick turns, d-pad zooms
        app.addModule<kke_cloth::ClothDemoModule>();
        app.addModule<kke::DemoPanelModule>("Cloth and hair", kke::DemoPanelModule::Side::Right);
        app.addModule<kke::DebugControlModule>().setUiVisible(false);
        app.addModule<kke::StatsModule>();
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
