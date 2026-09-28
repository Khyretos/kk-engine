// Racing: oval races, drifting and drag on Jolt's vehicle physics, with
// damage you can see (see RacingModule.h and README.md).
#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/NetModule.h"
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsModule.h"
#endif
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"

#include "RacingModule.h"

#include <iostream>

int main() {
    try {
        kke::Application app("Racing", 1280, 720);
        // Each track sets its own mood (tracks/*.yaml, "mood:").
        app.setMood("golden_hour");

        app.addModule<kke::SettingsModule>("settings.json");
        app.addModule<kke::InputModule>("racing_input.json");
        app.addModule<kke::RigidBodyModule>();
#if KKE_ENABLE_FEMFX
        // The car bodies' crumpling (Crumple.cpp): FEMFX solids, nothing of its own drawn.
        app.addModule<kke::PhysicsModule>(/*renderScale=*/1.0f, /*initialObjectCount=*/0).setDrawGround(false);
#endif
        app.addModule<kke::ModelModule>();
        app.addModule<kke::UiModule>();
        app.addModule<kke::AudioModule>().setUiVisible(false);
        app.addModule<kke::LobbyModule>("racing_lobby.json");
        // Online: Host / Join in the start menu (or KKE_NET=host, KKE_NET=join:ADDRESS).
        kke::net::NetConfig net;
        net.gameId = "racing";
        net.maxPlayers = 24; // every car a person drives: up to 4 at each screen
        app.addModule<kke::NetModule>(net);
        app.addModule<racing::RacingModule>();
        app.addModule<kke::StatsModule>().setUiVisible(false);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
