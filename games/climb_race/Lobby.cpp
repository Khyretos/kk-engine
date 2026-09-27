// The start menu (kke::LobbyModule): each player picks a name and a
// colour, player 1 sets the CPU climbers, and the climbers stand in a row
// in front of the mountain, each one above its player's card. Start makes
// the race's roster from it: players first, then the CPU climbers, one
// face of the mountain each.

#include "ClimbRaceModule.h"

#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace climb_race {

namespace {

struct Colour {
    const char* name;
    glm::vec3 rgb;
};
const Colour kColours[] = { { "Sky", { 0.35f, 0.65f, 1.0f } },  { "Ember", { 1.0f, 0.45f, 0.2f } }, { "Moss", { 0.45f, 0.85f, 0.35f } },
                            { "Plum", { 0.72f, 0.45f, 1.0f } }, { "Sun", { 1.0f, 0.84f, 0.3f } },   { "Coral", { 1.0f, 0.5f, 0.6f } },
                            { "Ice", { 0.75f, 0.95f, 1.0f } },  { "Stone", { 0.62f, 0.62f, 0.66f } } };
const char* const kNames[] = { "Pip", "Juno", "Rook", "Kestrel", "Nova", "Flint", "Wren", "Ziggy", "Scout", "Bram" };
constexpr int kColourCount = static_cast<int>(sizeof(kColours) / sizeof(kColours[0]));
constexpr int kNameCount = static_cast<int>(sizeof(kNames) / sizeof(kNames[0]));

// Where each card's climber stands, as a point on the screen (0..1 from
// the top left): above the middle of the card (LobbyModule's layout: four
// cards 22.5% wide, 25.8% apart, in 94% of the width).
glm::vec2 cardSpot(int seat) { return { 0.03f + 0.94f * (static_cast<float>(seat) * 0.258f + 0.1125f), 0.64f }; }
// The CPU climbers stand a few steps further back, between the players.
glm::vec2 cpuSpot(int i, int count) {
    const float t = count <= 1 ? 0.5f : static_cast<float>(i) / static_cast<float>(count - 1);
    return { 0.22f + 0.56f * t, 0.64f };
}
constexpr float kCpuBack = 3.5f;

} // namespace

void ClimbRaceModule::setupLobby() {
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
    l.setCpuCount(1); // a rival, unless last time said otherwise
    m_lobby->load();
    m_lobby->setTitle("CLIMB RACE", "Pick your climber. Another controller? Press {a} on it to join.");
    l.onJoin = [this](int seat) {
        if (m_phase == Phase::Lobby) return;
        m_rosterChanged = true;
        m_lobby->lobby().toast(m_lobby->lobby().seatName(seat) + " joins the next race: " + m_input->promptText("{race.again}") + " to race again now", 6.0f);
    };
    // Straight into a race: demos, tests, and the old switches.
    const char* lobbyVar = kke::dev::env("KKE_CLIMB_LOBBY");
    if (m_autopilot || m_rockfall >= 0.0f || (lobbyVar && std::strcmp(lobbyVar, "0") == 0)) {
        l.setCpuCount(m_defaultCpus);
        for (int i = 0; i < kke::Lobby::kMaxCpus; ++i) l.setCpuDifficulty(i, 2); // Hard, the rival it always was
        m_lobby->close();
    }
}

std::vector<ClimbRaceModule::Entry> ClimbRaceModule::wantedRoster() const {
    std::vector<Entry> out;
    std::vector<bool> nameUsed(kNameCount, false), colourUsed(kColourCount, false);
    int cpus = m_defaultCpus;
    std::vector<int> difficulty(static_cast<size_t>(kke::Lobby::kMaxCpus), 1);
    if (m_lobby) {
        const kke::Lobby& l = m_lobby->lobby();
        for (int seat : l.joinedSeats()) {
            const kke::Lobby::Seat& s = l.seat(seat);
            const int n = std::clamp(s.look[0], 0, kNameCount - 1), c = std::clamp(s.look[1], 0, kColourCount - 1);
            nameUsed[static_cast<size_t>(n)] = colourUsed[static_cast<size_t>(c)] = true;
            out.push_back({ seat, 2, kNames[n], kColours[c].rgb });
        }
        cpus = l.cpuCount();
        for (int i = 0; i < cpus; ++i) difficulty[static_cast<size_t>(i)] = l.cpuDifficulty(i);
    } else {
        nameUsed[0] = colourUsed[0] = true;
        out.push_back({ 0, 2, "You", kColours[0].rgb });
    }
    if (m_autopilot) out[0].name += " (autopilot)";
    // The CPU climbers take the names and colours nobody picked.
    for (int i = 0; i < cpus; ++i) {
        const auto n = std::find(nameUsed.begin(), nameUsed.end(), false), c = std::find(colourUsed.begin(), colourUsed.end(), false);
        const size_t ni = n == nameUsed.end() ? static_cast<size_t>(i) % kNameCount : static_cast<size_t>(n - nameUsed.begin());
        const size_t ci = c == colourUsed.end() ? static_cast<size_t>(i) % kColourCount : static_cast<size_t>(c - colourUsed.begin());
        nameUsed[ni] = colourUsed[ci] = true;
        out.push_back({ -1, difficulty[static_cast<size_t>(i)], std::string(kNames[ni]) + " (CPU)", kColours[ci].rgb });
    }
    return out;
}

void ClimbRaceModule::removeRacer(Racer& r) {
    if (r.model) m_models->remove(r.model);
    r.model = 0;
    if (r.id) m_rigid->world().removeCharacter(r.id);
    r.id = 0;
}

void ClimbRaceModule::buildRacers(const std::vector<Entry>& roster) {
    for (Racer& r : m_racers) removeRacer(r);
    m_racers.clear();
    kke::RigidWorld& w = m_rigid->world();
    const kke::Lobby* l = m_lobby ? &m_lobby->lobby() : nullptr;
    for (size_t i = 0; i < roster.size(); ++i) {
        const Entry& e = roster[i];
        Racer r;
        r.lane = std::min(static_cast<int>(i), static_cast<int>(m_lanes.size()) - 1);
        r.seat = e.seat;
        r.bot = e.seat < 0 || m_autopilot;
        r.mouse = e.seat >= 0 && (!l ? e.seat == 0 : l->seat(e.seat).device != kke::Lobby::Device::Pad);
        r.difficulty = e.difficulty;
        r.name = e.name;
        r.tint = e.tint;
        kke::RigidWorld::CharacterDesc cd;
        cd.position = glm::vec3(static_cast<float>(i), 0.0f, 12.0f);
        r.id = w.addCharacter(cd);
        r.loco = std::make_unique<kke::Locomotion>(w, r.id);
        r.climber = std::make_unique<kke::Climber>(*m_lanes[static_cast<size_t>(r.lane)]->wall);
        r.rig.mode = kke::CameraRig::Mode::ThirdPerson;
        r.rig.settings.armLength = 4.2f;
        r.rig.pitch = -5.0f;
        m_racers.push_back(std::move(r));
    }
    for (Racer& r : m_racers) {
        makeBrain(r);
        setupBody(r);
    }
}

void ClimbRaceModule::applyLooks() {
    const std::vector<Entry> roster = wantedRoster();
    for (size_t i = 0; i < m_racers.size() && i < roster.size(); ++i) {
        Racer& r = m_racers[i];
        r.name = roster[i].name;
        if (r.difficulty != roster[i].difficulty) {
            r.difficulty = roster[i].difficulty;
            makeBrain(r);
        }
        if (r.tint != roster[i].tint) {
            r.tint = roster[i].tint;
            if (r.model) m_models->setTint(r.model, r.tint);
        }
    }
}

int ClimbRaceModule::humans() const {
    return static_cast<int>(std::count_if(m_racers.begin(), m_racers.end(), [](const Racer& r) { return r.seat >= 0; }));
}

kke::Camera& ClimbRaceModule::cameraOf(Racer& r) {
    return r.seat >= 0 && r.player == 0 ? m_app->camera() : r.camera;
}

void ClimbRaceModule::updateLobby(float dt) {
    // Someone joined or left, or the CPU count changed: a new line-up.
    const std::vector<Entry> roster = wantedRoster();
    bool same = roster.size() == m_racers.size();
    for (size_t i = 0; same && i < roster.size(); ++i) same = roster[i].seat == m_racers[i].seat;
    if (!same) buildRacers(roster);
    applyLooks();

    // The camera in front of the mountain, looking at the line-up.
    m_app->views().clear();
    kke::Camera& cam = m_app->camera();
    const glm::vec3 stage(0.0f, 0.0f, 12.0f);
    cam.position = stage + glm::vec3(0.0f, 1.6f, 9.0f);
    cam.target = cam.position + glm::vec3(0.0f, -std::tan(glm::radians(5.0f)) * 10.0f, -10.0f); // 5 degrees down
    const VkExtent2D ext = m_app->renderer().extent();
    const float aspect = ext.height > 0 ? static_cast<float>(ext.width) / static_cast<float>(ext.height) : 16.0f / 9.0f;
    const glm::vec3 fwd = glm::normalize(cam.target - cam.position);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    const glm::vec3 up = glm::cross(right, fwd);
    const float t = std::tan(glm::radians(cam.fovDegrees) * 0.5f);
    // Where a point on the screen meets the meadow.
    auto onGround = [&](glm::vec2 screen) {
        const glm::vec3 dir = glm::normalize(fwd + right * ((screen.x * 2.0f - 1.0f) * t * aspect) + up * ((1.0f - screen.y * 2.0f) * t));
        const float k = dir.y < -0.02f ? -cam.position.y / dir.y : 12.0f;
        return cam.position + dir * std::min(k, 20.0f);
    };
    kke::RigidWorld& w = m_rigid->world();
    int cpu = 0;
    const int cpus = static_cast<int>(m_racers.size()) - humans();
    for (Racer& r : m_racers) {
        glm::vec3 feet = r.seat >= 0 ? onGround(cardSpot(r.seat)) : onGround(cpuSpot(cpu++, cpus)) - glm::vec3(0.0f, 0.0f, kCpuBack);
        feet.y = 0.05f;
        glm::vec3 face = cam.position - feet;
        face.y = 0.0f;
        if (glm::length(w.characterPosition(r.id) - feet) > 0.05f) r.loco->teleport(feet);
        r.loco->setFacing(glm::normalize(face));
        r.loco->update(kke::Locomotion::Input{}, dt);
        r.climber->recover(30.0f, dt); // fresh for the start
    }
    for (Racer& r : m_racers) animateBody(r, dt);

    if (m_lobby->lobby().takeStart()) startFromLobby();
}

void ClimbRaceModule::startFromLobby() {
    const bool fromMenu = m_lobby && m_lobby->isOpen();
    if (fromMenu) m_lobby->save();
    if (m_lobby) m_lobby->close();
    const std::vector<Entry> roster = wantedRoster();
    bool same = roster.size() == m_racers.size();
    for (size_t i = 0; same && i < roster.size(); ++i) same = roster[i].seat == m_racers[i].seat;
    if (!same) buildRacers(roster);
    applyLooks();
    // One face per climber.
    if (m_lanes.size() != m_racers.size()) buildMountain(m_seed, static_cast<int>(m_racers.size()));
    if (m_lobby) {
        m_lobby->applyInput();
        for (Racer& r : m_racers)
            if (r.seat >= 0) r.player = std::max(0, m_lobby->playerOf(r.seat));
    } else {
        m_input->setPlayers(1);
    }
    m_rosterChanged = false;
    resetRace();
    if (m_howtoFirst) showHowTo(true);
    m_howtoFirst = false;
    kke::log::get(name())->info("race: {} climbers: {} playing, {} CPU, on mountain {}", m_racers.size(), humans(),
                                static_cast<int>(m_racers.size()) - humans(), m_seed);
}

void ClimbRaceModule::backToLobby() {
    showHowTo(false);
    m_captured = false;
    SDL_SetWindowRelativeMouseMode(m_app->window().handle(), false);
    resetRace();
    m_phase = Phase::Lobby;
    m_rosterChanged = false;
    m_app->views().clear();
    m_lobby->open();
}

} // namespace climb_race
