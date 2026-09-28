// The start menu (kke::LobbyModule): players join with A and pick a name
// and a colour; player 1 sets the CPU players, the match length, the
// teams and the helping hand. Start plays on court 1 of the sport center.

#include "TennisModule.h"

#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"

#include <cstring>

namespace tennis {

namespace {

struct Colour {
    const char* name;
    glm::vec3 rgb;
};
const Colour kColours[] = { { "Sky", { 0.35f, 0.65f, 1.0f } },  { "Clay", { 1.0f, 0.45f, 0.2f } }, { "Grass", { 0.45f, 0.85f, 0.35f } },
                            { "Plum", { 0.72f, 0.45f, 1.0f } }, { "Ball", { 0.85f, 0.95f, 0.25f } }, { "Coral", { 1.0f, 0.5f, 0.6f } },
                            { "Ice", { 0.75f, 0.95f, 1.0f } },  { "Stone", { 0.62f, 0.62f, 0.66f } } };
const char* const kNames[] = { "Ace", "Juno", "Rafa", "Venus", "Nova", "Serena", "Bjorn", "Steffi", "Arthur", "Billie" };

} // namespace

void TennisModule::setupLobby() {
    if (!m_lobby) return;
    kke::Lobby& l = m_lobby->lobby();
    kke::Lobby::LookField names{ "name", "Name", {}, {} };
    for (const char* n : kNames) names.choices.push_back(n);
    l.addLookField(std::move(names));
    kke::Lobby::LookField colours{ "colour", "Colour", {}, {} };
    for (const Colour& c : kColours) {
        colours.choices.push_back(c.name);
        colours.swatches.push_back(c.rgb);
    }
    l.addLookField(std::move(colours));
    l.setMaxCpus(3);
    l.setCpuCount(1);
    l.addOption({ "length", "Match", { "Short set (to 4)", "Set (to 6)", "Best of three short sets", "Quick (to 2)" }, 0, true, {},
                  [this](int v) { m_length = v; } });
    l.addOption({ "teams", "Teams", { "Across the net", "Players together" }, 0, true, {}, [this](int v) { m_teams = v; } });
    l.addOption({ "assist", "Helping hand", { "On", "Off" }, 0, true, {}, [this](int v) { m_assist = v == 0; } });
    m_lobby->load();
    if (const kke::Lobby::Option* o = l.option("length")) m_length = o->value;
    if (const kke::Lobby::Option* o = l.option("teams")) m_teams = o->value;
    if (const kke::Lobby::Option* o = l.option("assist")) m_assist = o->value == 0;
    m_lobby->setTitle("TENNIS", "Two to four players, or you against the CPU. Another controller? Press {a} on it to join.");
    // Straight into a match: demos, tests.
    const char* lobbyVar = kke::dev::env("KKE_TENNIS_LOBBY");
    if (m_allBots || (lobbyVar && std::strcmp(lobbyVar, "0") == 0)) {
        m_lobby->close();
        m_inMenu = false;
        return;
    }
    m_inMenu = true;
    m_lobby->open();
}

void TennisModule::updateLobby(float) {
    if (!m_lobby) {
        m_inMenu = false;
        startLocalMatch();
        return;
    }
    kke::Lobby& l = m_lobby->lobby();
    if (!l.takeStart()) return;
    m_lobby->close();
    m_lobby->save();
    m_lobby->applyInput();
    m_inMenu = false;
    startLocalMatch();
}

} // namespace tennis
