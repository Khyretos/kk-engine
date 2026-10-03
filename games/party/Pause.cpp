// The pause menu is the shared one (kke::GameShellModule: Esc, Start or
// Select): its Main menu takes the party back to the start menu. Its
// "Demo settings" row opens this panel (kke::DemoPanelModule): voice chat
// off or on for this screen, and a mute for each player online, so nobody
// has to listen to anyone they'd rather not.

#include "PartyModule.h"

#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/VoiceModule.h"

#include <algorithm>

namespace party {

std::vector<PartyModule::VoicePeer> PartyModule::voicePeers() const {
    std::vector<VoicePeer> out;
    if (!m_net || !m_net->connected()) return out;
    for (const kke::net::RemotePlayer& p : m_net->remotePlayers()) {
        // CPU beans (the host's local players) have no microphone.
        const bool cpu = p.character == "cpu";
        if (!cpu) out.push_back({ p.id, p.name });
    }
    return out;
}

void PartyModule::setupPause() {
    if (auto* shell = m_app->getModule<kke::GameShellModule>())
        shell->onMainMenu = [this] {
            if (m_lobby && m_phase != Phase::Lobby && !netClient()) backToLobby();
        };
    m_panel = m_app->getModule<kke::DemoPanelModule>();
    if (!m_panel) return;
    m_panel->setTitle("Party and voice");
    m_panel->setState(kke::DemoPanelModule::State::Collapsed); // a tab in the corner until it's wanted
    kke::DemoPanelModule::Section& party = m_panel->section("Party");
    party.button("Back to the start menu", [this] {
        m_panel->setState(kke::DemoPanelModule::State::Collapsed);
        if (m_phase != Phase::Lobby && !netClient()) backToLobby();
    });
    party.showIf([this] { return m_lobby && m_phase != Phase::Lobby && !netClient(); });
    party.text([this] { return netClient() ? std::string("The host picks the games") : std::string(); });
    party.showIf([this] { return netClient(); });

    if (!m_voice) return;
    kke::DemoPanelModule::Section& voice = m_panel->section("Voice chat");
    voice.toggle("Voice chat", &m_voiceOn, [this] {
        m_voice->setEnabled(m_voiceOn);
        if (kke::Lobby::Option* o = m_lobby ? m_lobby->lobby().option("voice") : nullptr) {
            o->value = m_voiceOn ? 0 : 1;
            m_lobby->save();
        }
    });
    voice.note("Off: you don't hear anyone and nobody hears you.");
    // One player at a time: pick them, then mute or unmute.
    voice.choice(
        "Player", [this]() -> int* { return &m_mutePick; },
        [this] {
            std::vector<std::string> names;
            for (const VoicePeer& p : voicePeers()) names.push_back(p.name + (m_voice->muted(p.id) ? " (muted)" : ""));
            return names;
        });
    voice.showIf([this] { return m_voiceOn && !voicePeers().empty(); });
    voice.toggle(
        "Muted",
        [this]() -> bool* {
            const std::vector<VoicePeer> peers = voicePeers();
            if (peers.empty()) return nullptr;
            m_mutePick = std::clamp(m_mutePick, 0, static_cast<int>(peers.size()) - 1);
            m_muteShown = m_voice->muted(peers[static_cast<size_t>(m_mutePick)].id);
            return &m_muteShown;
        },
        [this] {
            const std::vector<VoicePeer> peers = voicePeers();
            if (m_mutePick >= 0 && m_mutePick < static_cast<int>(peers.size())) m_voice->mute(peers[static_cast<size_t>(m_mutePick)].id, m_muteShown);
        });
    voice.showIf([this] { return m_voiceOn; });
    voice.button("Mute everyone", [this] {
        const std::vector<VoicePeer> peers = voicePeers();
        const bool all = std::all_of(peers.begin(), peers.end(), [this](const VoicePeer& p) { return m_voice->muted(p.id); });
        for (const VoicePeer& p : peers) m_voice->mute(p.id, !all);
    });
    voice.showIf([this] { return m_voiceOn && !voicePeers().empty(); });
    voice.text([this] {
        const std::vector<VoicePeer> peers = voicePeers();
        if (peers.empty()) return std::string("Nobody else is online.");
        std::string talking;
        for (const VoicePeer& p : peers)
            if (m_voice->speaking(p.id)) talking += (talking.empty() ? "" : ", ") + p.name;
        return talking.empty() ? std::string("Hold {voice.talk} to talk.") : "Talking: " + talking;
    });
    voice.showIf([this] { return m_voiceOn; });
}

void PartyModule::updatePause() {
    if (!m_panel) return;
    // Not over the start menu (it has its own Back, and its Voice row).
    const bool inMenu = m_lobby && m_lobby->isOpen();
    m_panel->setVisible(!inMenu);
    if (inMenu && m_panel->state() != kke::DemoPanelModule::State::Collapsed) m_panel->setState(kke::DemoPanelModule::State::Collapsed);
    if (m_voice) {
        m_voiceOn = m_voice->enabled();
        // Proximity chat in the arena (heard from where each bean is); between
        // games (menu, results, vote, podium) everyone is together, like a call.
        const bool arena = m_phase == Phase::Intro || m_phase == Phase::Countdown || m_phase == Phase::Play || m_phase == Phase::RoundOver;
        m_voice->settings.channel = arena ? kke::net::VoiceChannel::Proximity : kke::net::VoiceChannel::All;
    }
}

} // namespace party
