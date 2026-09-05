#include "kke/Application.h"
#include "kke/modules/GridModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/StatsModule.h"
#include "ImGuiShowcaseModule.h"

#include <iostream>

// A dedicated demo for Dear ImGui specifically -- see
// ImGuiShowcaseModule.h for why this wraps ImGui's own built-in
// ShowDemoWindow() rather than hand-curating a widget list: it's
// already the canonical, comprehensive answer to "show me everything
// this UI library can do," maintained by ImGui itself.
//
// A grid + orbit camera provide minimal 3D backdrop/context so the UI
// isn't floating over a flat void -- this demo's actual subject is the
// UI, not the 3D scene.
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - ImGui Demo", 1280, 720);

        app.addModule<kke::GridModule>();
        app.addModule<kke::OrbitCameraModule>();
        app.addModule<kke_demo::ImGuiShowcaseModule>();
        app.addModule<kke::DebugControlModule>();
        app.addModule<kke::StatsModule>();

        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
