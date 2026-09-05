#include "kke/Application.h"
#include "kke/modules/UiModule.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/StatsModule.h"
#include "RmlUiShowcaseModule.h"

#include <iostream>

// A dedicated demo for RmlUi -- see RmlUiShowcaseModule.h/.cpp for the
// actual content: real <input>, <select>, <textarea>, <tabset>, and
// <progress> elements, all confirmed to already be part of RmlUi 6.3's
// Core library (no extra linking needed -- verified by checking the
// fetched source directly, not assumed) rather than requiring the
// older, separate "Controls" plugin some RmlUi tutorials still
// reference.
//
// kke::UiModule MUST be added before RmlUiShowcaseModule -- the
// latter's init() looks up the former's Rml::Context via
// app.getModule<kke::UiModule>() and throws if it isn't there yet.
// No 3D backdrop at all here, deliberately -- this demo's subject is
// purely the UI.
int main() {
    try {
        kke::Application app("Kreative Kompas Engine - RmlUi Demo", 1280, 720);

        app.addModule<kke::UiModule>();
        app.addModule<kke_demo::RmlUiShowcaseModule>();
        app.addModule<kke::DebugControlModule>();
        app.addModule<kke::StatsModule>();

        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
