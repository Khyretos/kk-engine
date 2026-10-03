// Party: a show of minigames for friends on one screen and online (see
// PartyModule.h and README.md).
#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"
#include "kke/modules/VoiceHudModule.h"
#include "kke/modules/VoiceModule.h"

#include "PartyModule.h"

#include <iostream>

int main() {
    try {
        kke::Application app("Party", 1280, 720);
        // Each minigame sets its own mood (Minigame::mood); the menu is at sunset.
        app.setMood("sunset");

        app.addModule<kke::SettingsModule>("settings.json");
        app.addModule<kke::InputModule>("party_input.json");
        app.addModule<kke::RigidBodyModule>();
        app.addModule<kke::ModelModule>(); // people (People.h), when the Synty City pack is there
        app.addModule<kke::UiModule>();
        // The shared menus: title, settings, controls, pause (docs/GAME_SHELL.md).
        app.addModule<kke::GameShellModule>("Party", "Minigames with friends");
        app.addModule<kke::AudioModule>().setUiVisible(false);
        app.addModule<kke::LobbyModule>("party_lobby.json");
        // Online: Host / Join in the start menu (or KKE_NET=host, KKE_NET=join:ADDRESS).
        kke::net::NetConfig net;
        net.gameId = "party";
        net.maxPlayers = 12; // everyone at every screen, and the host's CPU beans (NetParty.cpp: a Round holds 12 seats)
        app.addModule<kke::NetModule>(net);
        // Proximity voice chat: beans near you are heard from where they
        // stand, and fade out across the arena; push to talk by default
        // (docs/NETWORKING.md "Voice").
        kke::VoiceModule::Settings voice;
        voice.channel = kke::net::VoiceChannel::Proximity;
        voice.hearingRange = 30.0f;
        voice.mouthHeight = 1.2f; // a bean's face (people are taller, but close enough)
        app.addModule<kke::VoiceModule>(voice);
        // The pause menu: Esc, or Back on a controller (Pause.cpp).
        app.addModule<kke::DemoPanelModule>("Menu", kke::DemoPanelModule::Side::Right).setWidth(360.0f);
        app.addModule<party::PartyModule>();
        // Who is talking and from where, and a mute for each nearby slot (pause menu).
        app.addModule<kke::VoiceHudModule>();
        app.addModule<kke::StatsModule>().setUiVisible(false);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
