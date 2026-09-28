// Tennis: a FEMFX rubber ball, a CPU opponent or friends on one screen,
// and a sport center with ten courts to walk around online, challenge
// people and watch their matches (see TennisModule.h and README.md).
#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/PhysicsModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"

#include "TennisModule.h"

#include <iostream>

int main() {
    try {
        kke::Application app("Tennis", 1280, 720);
        // An open-air sport center on a bright day (assets/moods/clear_day.yaml).
        app.setMood("clear_day");

        app.addModule<kke::SettingsModule>("settings.json");
        app.addModule<kke::InputModule>("tennis_input.json");
        app.addModule<kke::RigidBodyModule>();                 // people, fences, the stands (Jolt)
        // The balls: FEMFX rubber, one per court. No ground slab of its own
        // (the courts are drawn by the game) and no starting objects.
        app.addModule<kke::PhysicsModule>(/*renderScale=*/1.0f, /*initialObjectCount=*/0).setDrawGround(false);
        app.addModule<kke::ModelModule>();
        app.addModule<kke::UiModule>();
        app.addModule<kke::AudioModule>().setUiVisible(false);
        app.addModule<kke::LobbyModule>("tennis_lobby.json");
        // Online: the sport center. Everyone in it is a player; a host's CPU
        // players are its guests.
        kke::net::NetConfig net;
        net.gameId = "tennis";
        net.maxPlayers = 128;
        app.addModule<kke::NetModule>(net);
        app.addModule<kke::DemoPanelModule>();
        app.addModule<tennis::TennisModule>();
        app.addModule<kke::StatsModule>().setUiVisible(false);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
