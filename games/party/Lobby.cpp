// The start menu (kke::LobbyModule): each player dresses their bean
// (name, colour, pattern, face, hat, body), player 1 sets the CPU beans,
// the mode (a party, or one game), the rounds, how the next game is
// picked (at random, or by vote) and voice chat, and the beans stand on the stage, each one above
// its player's card.

#include "PartyModule.h"

#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/VoiceModule.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace party {

namespace {

const char* const kNames[] = { "Wobbles", "Sprinkles", "Gumdrop", "Noodle", "Pickle", "Waffles",
                               "Bubbles", "Taco", "Muffin", "Biscuit", "Doodle", "Jellybean" };
constexpr int kNameCount = static_cast<int>(sizeof(kNames) / sizeof(kNames[0]));

// Where each card's bean stands, as a point on the screen (0..1 from the
// top left): above the middle of the card (LobbyModule's layout: four
// cards 22.5% wide, 25.8% apart, in 94% of the width).
glm::vec2 cardSpot(int seat) { return { 0.03f + 0.94f * (static_cast<float>(seat) * 0.258f + 0.1125f), 0.62f }; }
glm::vec2 cpuSpot(int i, int count) {
    const float t = count <= 1 ? 0.5f : static_cast<float>(i) / static_cast<float>(count - 1);
    return { 0.18f + 0.64f * t, 0.6f };
}
constexpr float kCpuBack = 3.0f;
// The other screens' players, a row behind: between the CPU players' spots
// (the middle one would hide right behind a lone CPU player).
glm::vec2 onlineSpot(int i, int count, int cpus) {
    glm::vec2 at = cpuSpot(i, count);
    if (cpus > 0 && cpus % 2 == count % 2) at.x += 0.12f;
    return at;
}
constexpr float kOnlineBack = 5.5f; // the other screens' players, behind the CPU beans
const glm::vec3 kStage(0.0f, 0.0f, 400.0f);

} // namespace

void PartyModule::setupLobby() {
    if (!m_lobby) return;
    kke::Lobby& l = m_lobby->lobby();
    kke::Lobby::LookField names{ "name", "Name", {}, {} };
    for (const char* n : kNames) names.choices.push_back(n);
    l.addLookField(std::move(names));
    kke::Lobby::LookField colours{ "colour", "Colour", {}, {} };
    for (const NamedColour& c : beanColours()) {
        colours.choices.push_back(c.name);
        colours.swatches.push_back(c.rgb);
    }
    l.addLookField(std::move(colours));
    l.addLookField({ "pattern", "Pattern", beanPatterns(), {} });
    l.addLookField({ "face", "Face", beanFaces(), {} });
    l.addLookField({ "hat", "Hat", beanHats(), {} });
    // Bean, or a person when the Synty City pack is there (People.h).
    m_bodyChoices = m_people.available();
    if (m_people.any()) {
        kke::Lobby::LookField bodies{ "body", "Body", { "Bean" }, {} };
        for (size_t i = 1; i < m_bodyChoices.size(); ++i) bodies.choices.push_back(People::all()[static_cast<size_t>(m_bodyChoices[i] - 1)]);
        l.addLookField(std::move(bodies));
    }
    l.setCpuCount(5); // a full stage, unless last time said otherwise
    // Mode: a party of rounds (points, a podium), or one minigame to play
    // on its own (practice, or trying one out).
    kke::Lobby::Option mode{ "mode", "Mode", { "Party", "One game" }, 0, true, {}, {} };
    mode.onChange = [this](int) { updateModeRows(); };
    l.addOption(std::move(mode));
    kke::Lobby::Option game{ "game", "Game", {}, 0, false, {}, {} };
    for (const auto& g : m_games) game.choices.push_back(g->title());
    l.addOption(std::move(game));
    l.addOption({ "rounds", "Rounds", { "3", "5", "8" }, 1, true, {}, {} });
    l.addOption({ "pick", "Next game", { "Random", "Vote" }, 0, true, {}, {} });
    // Voice chat (online) is never forced on anyone: Off here, or in the
    // pause menu, where each player can also be muted.
    if (m_voice) {
        kke::Lobby::Option voice{ "voice", "Voice chat", { "On", "Off" }, 0, true, {}, {} };
        voice.onChange = [this](int) { applyVoiceOption(); };
        l.addOption(std::move(voice));
    }
    m_lobby->load();
    updateModeRows();
    applyVoiceOption();
    m_lobby->setTitle("PARTY", "Dress your bean. Another controller? Press {a} on it to join.");
    l.onJoin = [this](int seat) {
        if (m_phase == Phase::Lobby) return;
        m_rosterChanged = true;
        m_lobby->lobby().toast(m_lobby->lobby().seatName(seat) + " joins the next party", 5.0f);
    };
    // Straight into the show: demos, tests.
    const char* lobbyVar = kke::dev::env("KKE_PARTY_LOBBY");
    if (m_autopilot || !m_onlyGame.empty() || (lobbyVar && std::strcmp(lobbyVar, "0") == 0)) {
        l.setCpuCount(m_defaultCpus);
        for (int i = 0; i < kke::Lobby::kMaxCpus; ++i) l.setCpuDifficulty(i, 1 + i % 3);
        m_lobby->close();
    }
}

void PartyModule::updateModeRows() {
    kke::Lobby& l = m_lobby->lobby();
    const kke::Lobby::Option* mode = l.option("mode");
    const bool one = mode && mode->value == 1;
    if (kke::Lobby::Option* o = l.option("game")) o->visible = one;
    if (kke::Lobby::Option* o = l.option("rounds")) o->visible = !one;
    if (kke::Lobby::Option* o = l.option("pick")) o->visible = !one;
}

void PartyModule::applyVoiceOption() {
    if (!m_voice || !m_lobby) return;
    const kke::Lobby::Option* o = m_lobby->lobby().option("voice");
    const bool on = !o || o->value == 0;
    if (m_voice->enabled() != on) m_voice->setEnabled(on);
}

std::vector<PartyModule::Entry> PartyModule::wantedRoster() const {
    std::vector<Entry> out;
    const int colours = static_cast<int>(beanColours().size());
    std::vector<bool> nameUsed(kNameCount, false), colourUsed(static_cast<size_t>(colours), false);
    int cpus = m_defaultCpus;
    std::vector<int> difficulty(static_cast<size_t>(kke::Lobby::kMaxCpus), 1);
    if (m_lobby) {
        const kke::Lobby& l = m_lobby->lobby();
        for (int seat : l.joinedSeats()) {
            const kke::Lobby::Seat& s = l.seat(seat);
            auto look = [&s](size_t f) { return f < s.look.size() ? s.look[f] : 0; };
            const int n = std::clamp(look(0), 0, kNameCount - 1);
            Entry e;
            e.seat = seat;
            e.name = kNames[n];
            e.look = BeanLook{ std::clamp(look(1), 0, colours - 1), look(2), look(3), look(4) };
            const int body = look(5);
            e.look.body = body > 0 && body < static_cast<int>(m_bodyChoices.size()) ? m_bodyChoices[static_cast<size_t>(body)] : 0;
            nameUsed[static_cast<size_t>(n)] = true;
            colourUsed[static_cast<size_t>(e.look.colour)] = true;
            out.push_back(std::move(e));
        }
        cpus = l.cpuCount();
        for (int i = 0; i < cpus; ++i) difficulty[static_cast<size_t>(i)] = l.cpuDifficulty(i);
    } else {
        Entry e;
        e.seat = 0;
        e.name = "You";
        nameUsed[0] = colourUsed[0] = true;
        out.push_back(std::move(e));
    }
    if (!m_netName.empty() && !out.empty()) out[0].name = m_netName;
    if (m_autopilot && !out.empty()) out[0].name += " (autopilot)";
    // The CPU beans: the names and colours nobody picked, and clothes.
    for (int i = 0; i < cpus; ++i) {
        const auto n = std::find(nameUsed.begin(), nameUsed.end(), false);
        const auto c = std::find(colourUsed.begin(), colourUsed.end(), false);
        const size_t ni = n == nameUsed.end() ? static_cast<size_t>(i) % kNameCount : static_cast<size_t>(n - nameUsed.begin());
        const size_t ci = c == colourUsed.end() ? static_cast<size_t>(i) % static_cast<size_t>(colours) : static_cast<size_t>(c - colourUsed.begin());
        nameUsed[ni] = colourUsed[ci] = true;
        Entry e;
        e.difficulty = difficulty[static_cast<size_t>(i)];
        e.name = std::string(kNames[ni]) + " (CPU)";
        e.look = BeanLook{ static_cast<int>(ci), static_cast<int>((ni + 1) % beanPatterns().size()), static_cast<int>(ni % beanFaces().size()),
                           1 + static_cast<int>((ni * 5 + 3) % (beanHats().size() - 1)) };
        // With the people there, every other CPU is a person.
        if (m_bodyChoices.size() > 1 && i % 2 == 1) e.look.body = m_bodyChoices[1 + (ni * 7) % (m_bodyChoices.size() - 1)];
        out.push_back(std::move(e));
    }
    return out;
}

void PartyModule::removeBean(Bean& b) {
    m_people.remove(b);
    if (b.id && m_rigid) m_rigid->world().removeCharacter(b.id);
    b.id = 0;
    if (b.body) m_app->renderer().retire(std::shared_ptr<void>(std::move(b.body)));
    if (b.limb) m_app->renderer().retire(std::shared_ptr<void>(std::move(b.limb)));
    b.meshBuilt = false;
}

void PartyModule::buildBeans(const std::vector<Entry>& roster) {
    for (Bean& b : m_beans) removeBean(b);
    m_beans.clear();
    kke::RigidWorld& w = world();
    for (size_t i = 0; i < roster.size(); ++i) {
        const Entry& e = roster[i];
        Bean b;
        b.index = static_cast<int>(i);
        b.seat = e.seat;
        b.remote = e.remote;
        b.netId = e.netId;
        b.bot = !e.remote && (e.seat < 0 || (m_autopilot && e.seat == 0));
        b.difficulty = e.difficulty;
        b.name = e.name;
        b.look = e.look;
        kke::RigidWorld::CharacterDesc cd;
        cd.radius = kBeanRadius;
        cd.height = kBeanHeight;
        cd.stepUp = 0.3f;
        cd.maxSlopeDegrees = 55.0f;
        cd.mass = 60.0f;
        cd.pushStrength = 250.0f;
        cd.position = kStage + glm::vec3(static_cast<float>(i) - static_cast<float>(roster.size()) * 0.5f, 0.1f, 2.0f);
        b.id = w.addCharacter(cd);
        if (b.remote) w.setCharacterKinematic(b.id, true); // placed where its machine says
        b.drawFeet = cd.position;
        b.rig.mode = kke::CameraRig::Mode::ThirdPerson;
        b.rig.pitch = -16.0f;
        m_beans.push_back(std::move(b));
    }
    for (Bean& b : m_beans) ensureMesh(b);
}

void PartyModule::applyLooks() {
    const std::vector<Entry> roster = wantedRoster();
    for (size_t i = 0; i < m_beans.size() && i < roster.size(); ++i) {
        Bean& b = m_beans[i];
        if (b.remote) continue;
        b.name = roster[i].name;
        b.look = roster[i].look;
        b.difficulty = roster[i].difficulty;
    }
}

const std::vector<std::string>& PartyModule::lobbyNames() {
    static const std::vector<std::string> names(std::begin(kNames), std::end(kNames));
    return names;
}

void PartyModule::updateLobby(float dt) {
    // Online, everyone else's beans stand in the line-up too (at the back),
    // so the host sees who joined and a joiner sees who's in.
    std::vector<Entry> roster = m_net && m_net->role() != kke::NetModule::Role::Offline ? onlineRoster() : wantedRoster();
    if (netClient()) std::erase_if(roster, [](const Entry& e) { return e.seat < 0 && !e.remote; }); // our CPU beans stay home
    bool same = roster.size() == m_beans.size();
    for (size_t i = 0; same && i < roster.size(); ++i) same = roster[i].seat == m_beans[i].seat && roster[i].netId == m_beans[i].netId;
    if (!same) buildBeans(roster);
    applyLooks();

    // The camera in front of the stage, looking at the line-up.
    m_app->views().clear();
    kke::Camera& cam = m_app->camera();
    cam.position = kStage + glm::vec3(0.0f, 1.6f, 9.0f);
    cam.target = cam.position + glm::vec3(0.0f, -std::tan(glm::radians(6.0f)) * 10.0f, -10.0f);
    const VkExtent2D ext = m_app->renderer().extent();
    const float aspect = ext.height > 0 ? static_cast<float>(ext.width) / static_cast<float>(ext.height) : 16.0f / 9.0f;
    const glm::vec3 fwd = glm::normalize(cam.target - cam.position);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    const glm::vec3 up = glm::cross(right, fwd);
    const float t = std::tan(glm::radians(cam.fovDegrees) * 0.5f);
    auto onGround = [&](glm::vec2 screen) {
        const glm::vec3 dir = glm::normalize(fwd + right * ((screen.x * 2.0f - 1.0f) * t * aspect) + up * ((1.0f - screen.y * 2.0f) * t));
        const float k = dir.y < -0.02f ? (kStage.y - cam.position.y) / dir.y : 12.0f;
        return cam.position + dir * std::min(k, 20.0f);
    };
    kke::RigidWorld& w = world();
    int cpu = 0, other = 0;
    const int others = static_cast<int>(std::count_if(m_beans.begin(), m_beans.end(), [](const Bean& b) { return b.remote; }));
    const int cpus = static_cast<int>(std::count_if(m_beans.begin(), m_beans.end(), [](const Bean& b) { return b.seat < 0 && !b.remote; }));
    for (Bean& b : m_beans) {
        b.active = true;
        b.hidden = false;
        glm::vec3 feet = b.seat >= 0 ? onGround(cardSpot(b.seat))
                         : b.remote  ? onGround(onlineSpot(other++, others, cpus)) - glm::vec3(0.0f, 0.0f, kOnlineBack)
                                     : onGround(cpuSpot(cpu++, cpus)) - glm::vec3(0.0f, 0.0f, kCpuBack);
        feet.y = kStage.y + 0.02f;
        glm::vec3 face = cam.position - feet;
        b.yaw = glm::degrees(std::atan2(face.x, -face.z));
        if (b.remote) {
            // Another screen's player: their machine sends no pose in the menu.
            b.drawFeet = feet;
            b.velocity = glm::vec3(0.0f);
            b.grounded = true;
            w.moveCharacter(b.id, feet);
            continue;
        }
        const glm::vec3 at = w.characterPosition(b.id);
        if (glm::length(glm::vec2(at.x - feet.x, at.z - feet.z)) > 0.05f) place(b, feet, 0.0f);
        // A little hop now and then: they can't wait.
        b.input = BeanInput{};
        b.botTimer -= dt;
        if (b.botTimer <= 0.0f) {
            b.botTimer = m_botRng.range(2.0f, 6.0f);
            b.input.jump = true;
        }
        moveBean(b, dt);
    }
    for (Bean& b : m_beans) animateBean(b, dt);
    // KKE_PARTY_START=<s>: press Start for player 1 after that long (tests of a
    // full menu, with KKE_LOBBY_JOIN, going into split screen).
    const bool autoStart = m_startAfter > 0.0f && m_clock >= m_startAfter;
    if (m_lobby->lobby().takeStart() || autoStart) {
        m_startAfter = -1.0f;
        startParty();
    }
}

} // namespace party
