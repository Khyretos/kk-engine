// Flying: stunt planes over a generated island. Race through rings,
// score stunts, or just fly; on one screen, split screen or online, with
// a gamepad, the keyboard and mouse, or a flight stick (see
// FlyingModule.h and README.md).
#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"

#include "FlyingModule.h"

#include <iostream>
#include <string_view>

int main() {
    try {
        kke::Application app("Flying", 1280, 720);
        // A clear day over the sea (assets/moods/clear_day.yaml); the start
        // menu's Sky row picks another.
        app.setMood("clear_day");

        app.addModule<kke::SettingsModule>("settings.json");
        app.addModule<kke::InputModule>("flying_input.json");
        app.addModule<kke::ModelModule>();
        app.addModule<kke::UiModule>();
        // The shared menus: title, settings, controls, pause (docs/GAME_SHELL.md).
        auto& shell = app.addModule<kke::GameShellModule>("Flying", "Races, stunts and dogfights");
        // Flown by the CPU (demos, headless checks) or straight into a flight:
        // no title menu over it.
        const char* lobby = kke::dev::env("KKE_FLY_LOBBY");
        shell.setStartOnTitle(!kke::dev::flag("KKE_FLY_AUTOPILOT") && !(lobby && std::string_view(lobby) == "0"));
        app.addModule<kke::AudioModule>().setUiVisible(false);
        app.addModule<kke::LobbyModule>("flying_lobby.json");
        // Online: Host / Join in the start menu (or KKE_NET=host, KKE_NET=join:ADDRESS).
        kke::net::NetConfig net;
        net.gameId = "flying_demo";
        net.maxPlayers = 16; // up to 4 at each screen, and the host's CPU pilots
        app.addModule<kke::NetModule>(net);
        app.addModule<flying::FlyingModule>();
        app.addModule<kke::StatsModule>().setUiVisible(false);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
