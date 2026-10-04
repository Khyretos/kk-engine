// Sea: Synty pirate ships on an open sea. Sail, change ship, fight
// broadsides with arcing cannon fire, splinter a wooden fort, or sail with
// a friend online (see SeaDemoModule.h and README.md).
#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/DebugControlModule.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/NetModule.h"
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsModule.h"
#endif
#include "kke/modules/SettingsModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"

#include "SeaDemoModule.h"

#include <iostream>

int main() {
    try {
        kke::Application app("Kreative Kompas Engine - Sea Demo", 1280, 720);
        // A bright day over open water (assets/moods/clear_day.yaml): the
        // sky and its clouds reflected in every swell.
        app.setMood("clear_day");

        app.addModule<kke::SettingsModule>("settings.json");
        // Controls are actions (keyboard, mouse or a controller, remappable).
        app.addModule<kke::InputModule>("sea_demo_input.json");
#if KKE_ENABLE_FEMFX
        // The island fort's palisade (World.cpp): FEMFX wood that splinters.
        app.addModule<kke::PhysicsModule>(/*renderScale=*/1.0f, /*initialObjectCount=*/0).setDrawGround(false);
#endif
        app.addModule<kke::ModelModule>();
        app.addModule<kke::UiModule>();
        // The shared menus: title, settings, controls, pause (docs/GAME_SHELL.md).
        app.addModule<kke::GameShellModule>("Sea", "Sail, fight and sink pirate ships");
        // Plays the mood's ambience loop (the mood names it; BUG-079).
        app.addModule<kke::AudioModule>().setUiVisible(false);
        app.addModule<kke::LobbyModule>("sea_lobby.json");
        // Online: Host / Join in the start menu (or KKE_NET=host, KKE_NET=join:ADDRESS).
        kke::net::NetConfig net;
        net.gameId = "sea_demo";
        net.maxPlayers = 16; // up to 4 at each screen, and the host's enemy ships
        app.addModule<kke::NetModule>(net);
        app.addModule<kke_sea::SeaDemoModule>();
        // The settings panel (RmlUi, pad/keyboard/mouse): wind, waves, guns, enemies.
        app.addModule<kke::DemoPanelModule>("Sea");
        app.addModule<kke::DebugControlModule>().setUiVisible(false);
        app.addModule<kke::StatsModule>().setUiVisible(false);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
