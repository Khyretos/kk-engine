// Party: a show of minigames for friends on one screen and online (see
// PartyModule.h and README.md).
#include "kke/Application.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/StatsModule.h"
#include "kke/modules/UiModule.h"
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
        app.addModule<kke::AudioModule>().setUiVisible(false);
        app.addModule<kke::LobbyModule>("party_lobby.json");
        // Online: Host / Join in the start menu (or KKE_NET=host, KKE_NET=join:ADDRESS).
        kke::net::NetConfig net;
        net.gameId = "party";
        net.maxPlayers = 12; // everyone at every screen, and the host's CPU beans (NetParty.cpp: a Round holds 12 seats)
        app.addModule<kke::NetModule>(net);
        // Voice chat with the whole party (everyone hears everyone, like a
        // call); push to talk by default (docs/NETWORKING.md "Voice").
        kke::VoiceModule::Settings voice;
        voice.channel = kke::net::VoiceChannel::All;
        app.addModule<kke::VoiceModule>(voice);
        app.addModule<party::PartyModule>();
        app.addModule<kke::StatsModule>().setUiVisible(false);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
