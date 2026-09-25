#include "kke/Application.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"

#include "BackdropModule.h"
#include "ShowcaseModule.h"

#include <iostream>

// The RmlUi showcase: main menu, settings (that really change the
// engine), inventory with drag & drop, HUD, dialogue/chat and a loading
// screen, over a small lit 3D scene. See ShowcaseModule.h and ui/*.rml.
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - UI Showcase", 1280, 720);
        app.window().setQuitOnEscape(false); // Esc = back to the main menu (see ShowcaseModule)

        auto& camera = app.addModule<kke::OrbitCameraModule>(/*distance=*/9.0f, /*pitch=*/-0.35f, /*yaw=*/-0.6f, glm::vec3(0.0f, 0.6f, 0.0f));
        camera.setAutoOrbit(true, 6.0f);
        app.addModule<kke_demo::BackdropModule>();
        app.addModule<kke::UiModule>();
        app.addModule<kke::SettingsModule>("settings.json");
        app.addModule<kke_demo::ShowcaseModule>();
        app.addModule<kke::DebugControlModule>();
        app.addModule<kke::StatsModule>();

        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
