#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/ModelModule.h"
#if KKE_ENABLE_NET
#include "kke/modules/NetModule.h"
#endif
#include "kke/modules/RigidBodyModule.h"
#if KKE_ENABLE_LUA
#include "kke/modules/ScriptModule.h"
#endif
#include "kke/modules/SettingsModule.h"
#include "kke/modules/SoundVisualizerModule.h"
#include "kke/modules/StatsModule.h"
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsModule.h"
#endif

#include "ShowcaseModule.h"

#include <iostream>

// kke_demo: the walkable showcase (see ShowcaseModule.h, ACTION_PLAN.md P1).
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Showcase", 1280, 720);
        app.camera().farPlane = 200.0f;
        app.lighting().lights[1].enabled = true;
        app.lighting().lights[1].isDirectional = true;
        app.lighting().lights[1].direction = glm::normalize(glm::vec3(0.6f, -0.3f, 0.5f));
        app.lighting().lights[1].color = glm::vec3(0.55f, 0.65f, 0.85f);
        app.lighting().lights[1].intensity = 0.3f;

        // First, so the resource governor's budget (threads, frame caps)
        // is set before physics starts its workers.
        app.addModule<kke::SettingsModule>("settings.json");
        app.addModule<kke::InputModule>("input.json");
        std::vector<kke::Module*> panels;
        panels.push_back(&app.addModule<kke::RigidBodyModule>());
#if KKE_ENABLE_NET
        // Host / join / LAN list (F1), or KKE_NET=host | join:ADDRESS.
        // Before the showcase, so a join is under way when the crates spawn.
        {
            kke::net::NetConfig net;
            net.gameId = "kke_demo";
            panels.push_back(&app.addModule<kke::NetModule>(net));
        }
#endif
        app.addModule<kke::ModelModule>();
        panels.push_back(&app.addModule<kke::AudioModule>());
        // Overlay stays on when panels are hidden; on by default here so the
        // demo shows it (a saved accessibility.json wins).
        app.addModule<kke::SoundVisualizerModule>().settings.enabled = true;
#if KKE_ENABLE_FEMFX
        auto& physics = app.addModule<kke::PhysicsModule>(/*renderScale=*/1.0f, /*initialObjectCount=*/0);
        physics.setDrawGround(false);
        panels.push_back(&physics);
#endif
        auto& showcase = app.addModule<kke_showcase::ShowcaseModule>();
#if KKE_ENABLE_LUA
        panels.push_back(&app.addModule<kke::ScriptModule>("scripts")); // scripts/*.lua, hot-reloaded
#endif
        panels.push_back(&app.addModule<kke::DebugControlModule>());
        panels.push_back(&app.addModule<kke::StatsModule>());
        for (kke::Module* p : panels) p->setUiVisible(false);
        showcase.setEnginePanels(panels);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
