// The start menu (kke::LobbyModule): each player picks a name, a car, its
// body kit and its paint; player 1 picks the track, how many cars, laps,
// the CPU drivers' skill and the damage (and Online, Net.cpp). Behind the
// menu the grid waits on the chosen track, every car as it'll race.

#include "RacingModule.h"

#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"


#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace racing {

namespace {

const char* const kNames[] = { "Blaze", "Nitro", "Drift", "Rocket", "Viper", "Comet", "Turbo", "Piston", "Nova", "Sparky" };
const char* const kCarCounts[] = { "2", "4", "6", "8", "10", "12", "16", "20", "24" };
const char* const kLapCounts[] = { "1", "2", "3", "5", "8", "10", "15", "20" };

} // namespace

void RacingModule::setupLobby() {
    if (!m_lobby) return;
    kke::Lobby& l = m_lobby->lobby();
    kke::Lobby::LookField names{ "name", "Name", {}, {} };
    for (const char* n : kNames) names.choices.push_back(n);
    l.addLookField(std::move(names));
    kke::Lobby::LookField cars{ "car", "Car", {}, {} };
    for (const CarType& t : carTypes()) cars.choices.push_back(t.name);
    l.addLookField(std::move(cars));
    kke::Lobby::LookField kits{ "kit", "Body kit", {}, {} };
    for (int k = 0; k < kKits; ++k) kits.choices.push_back("Kit " + std::to_string(k + 1));
    l.addLookField(std::move(kits));
    kke::Lobby::LookField paintJobs{ "paint", "Paint", {}, {} };
    for (const Paint& p : paints()) {
        paintJobs.choices.push_back(p.name);
        paintJobs.swatches.push_back(p.color);
    }
    l.addLookField(std::move(paintJobs));
    // The field is ours (up to 24 cars), not the lobby's CPU rows (5 at most).
    l.setMaxCpus(0);
    kke::Lobby::Option track{ "track", "Track", {}, 0, true, {}, {} };
    for (const TrackDesc& t : m_tracks) track.choices.push_back(t.name + " (" + eventName(t.event) + ")");
    track.onChange = [this](int) {
        // The track's own number of laps.
        const TrackDesc& t = m_tracks[static_cast<size_t>(chosenTrack())];
        if (kke::Lobby::Option* laps = m_lobby->lobby().option("laps")) {
            int best = 0;
            for (int i = 0; i < static_cast<int>(laps->choices.size()); ++i)
                if (std::atoi(laps->choices[static_cast<size_t>(i)].c_str()) <= t.laps) best = i;
            laps->value = best;
            laps->visible = t.event != Event::Drag && t.event != Event::Derby && t.event != Event::Rally; // one run: no laps
        }
    };
    l.addOption(std::move(track));
    kke::Lobby::Option cars2{ "cars", "Cars", {}, 5, true, {}, {} };
    for (const char* n : kCarCounts) cars2.choices.push_back(n);
    for (size_t i = 0; i < cars2.choices.size(); ++i)
        if (std::atoi(cars2.choices[i].c_str()) <= m_defaultCars) cars2.value = static_cast<int>(i);
    l.addOption(std::move(cars2));
    kke::Lobby::Option laps{ "laps", "Laps", {}, 3, true, {}, {} };
    for (const char* n : kLapCounts) laps.choices.push_back(n);
    l.addOption(std::move(laps));
    l.addOption({ "skill", "CPU drivers", { "Easy", "Normal", "Hard", "Expert" }, 2, true, {}, {} });
    l.addOption({ "damage", "Damage", { "Off", "Normal", "Brutal" }, 1, true, {}, {} });
    m_lobby->load();
    if (m_forceTrack >= 0)
        if (kke::Lobby::Option* o = l.option("track")) o->value = m_forceTrack;
    if (kke::Lobby::Option* o = l.option("track")) {
        o->value = std::clamp(o->value, 0, static_cast<int>(o->choices.size()) - 1);
        if (kke::Lobby::Option* lp = l.option("laps")) {
            const Event e = m_tracks[static_cast<size_t>(o->value)].event;
            lp->visible = e != Event::Drag && e != Event::Derby && e != Event::Rally; // one run: no laps
        }
    }
    if (const char* cars = kke::dev::env("KKE_RACE_CARS")) {
        kke::Lobby::Option* o = l.option("cars");
        for (size_t i = 0; o && i < o->choices.size(); ++i)
            if (std::atoi(o->choices[i].c_str()) <= std::atoi(cars)) o->value = static_cast<int>(i);
    }
    m_lobby->setTitle("RACING", "Pick your car. Another controller? Press {a} on it to join.");
    l.onJoin = [this](int seat) {
        if (m_phase == Phase::Lobby) return;
        m_rosterChanged = true;
        m_lobby->lobby().toast(m_lobby->lobby().seatName(seat) + " joins the next race: " + m_input->promptText("{race.again}") + " to race again now", 6.0f);
    };
    // Straight into a race: demos, tests.
    const char* lobbyVar = kke::dev::env("KKE_RACE_LOBBY");
    if (m_autopilot || (lobbyVar && std::strcmp(lobbyVar, "0") == 0)) m_lobby->close();
}

void RacingModule::updateLobby(float) {
    // Another track, or the line-up changed: the grid again.
    const TrackDesc& want = m_tracks[static_cast<size_t>(chosenTrack())];
    const std::vector<Entry> roster = wantedRoster();
    bool same = want.id == m_builtTrack && roster.size() == m_cars.size();
    for (size_t i = 0; same && i < roster.size(); ++i)
        same = roster[i].seat == m_cars[i].seat && roster[i].type == m_cars[i].type && roster[i].kit == m_cars[i].kit &&
               roster[i].paint == m_cars[i].paint;
    if (!same) {
        if (want.id != m_builtTrack) buildTrack(want);
        buildRace(roster);
        resetRace();
        m_phase = Phase::Lobby;
    }
    for (size_t i = 0; i < roster.size() && i < m_cars.size(); ++i) m_cars[i].name = roster[i].name;

    // The camera: in front of the grid, a little to the side, looking back
    // down the rows at the players' cars.
    m_app->views().clear();
    if (!m_cars.empty()) {
        const Car& pole = m_cars[0];
        // Along the way the pole car faces (in an arena that's into the pen).
        glm::vec3 ahead = carForward(pole);
        ahead = glm::normalize(glm::vec3(ahead.x, 0.0f, ahead.z) + glm::vec3(0.0f, 0.0f, 1e-4f));
        const glm::vec3 leftFlat(ahead.z, 0.0f, -ahead.x);
        const glm::vec3 at = carPosition(pole);
        kke::Camera& cam = m_app->camera();
        cam.position = at + ahead * 9.0f - leftFlat * 4.0f + glm::vec3(0.0f, 2.2f, 0.0f);
        cam.target = at - ahead * 6.0f + glm::vec3(0.0f, 0.6f, 0.0f);
        cam.fovDegrees = 60.0f;
    }
    if (m_lobby->lobby().takeStart()) startFromLobby();
}

void RacingModule::startFromLobby() {
    if (netClient()) {
        // In someone else's game: they start it (applySetup).
        if (m_lobby) m_lobby->lobby().toast("The host starts the race", 3.0f);
        return;
    }
    if (m_lobby && m_lobby->isOpen()) m_lobby->save();
    if (m_lobby) m_lobby->close();
    if (netHost()) syncNetPlayers();
    const std::vector<Entry> roster = netHost() ? onlineRoster() : wantedRoster();
    const TrackDesc& want = m_tracks[static_cast<size_t>(chosenTrack())];
    if (want.id != m_builtTrack) buildTrack(want);
    buildRace(roster);
    if (m_lobby) {
        m_lobby->applyInput();
        for (Car& c : m_cars)
            if (c.seat >= 0) c.player = std::max(0, m_lobby->playerOf(c.seat));
    } else {
        m_input->setPlayers(1);
    }
    resetRace();
    if (m_howtoFirst) showHowTo(true);
    m_howtoFirst = false;
}

void RacingModule::backToLobby() {
    showHowTo(false);
    m_phase = Phase::Lobby;
    m_rosterChanged = false;
    m_app->views().clear();
    resetRace();
    m_phase = Phase::Lobby;
    m_lobby->open();
}

} // namespace racing
