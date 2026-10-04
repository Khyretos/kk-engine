// The start menu (kke::LobbyModule): each player picks a name and a
// colour, player 1 sets the CPU climbers, and the climbers stand in a row
// in front of the mountain, each one above its player's card. Start makes
// the race's roster from it: players first, then the CPU climbers, one
// face of the mountain each.

#include "ClimbRaceModule.h"

#include "kke/Application.h"
#include "kke/DataFile.h"
#include "kke/DevTools.h"
#include "kke/ImpactSynth.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

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
// The other screens' players, a row behind: between the CPU players' spots
// (the middle one would hide right behind a lone CPU player).
glm::vec2 onlineSpot(int i, int count, int cpus) {
    glm::vec2 at = cpuSpot(i, count);
    if (cpus > 0 && cpus % 2 == count % 2) at.x += 0.12f;
    return at;
}
constexpr float kOnlineBack = 6.5f; // the other screens' players, behind the CPU climbers

} // namespace

void ClimbRaceModule::setupLobby() {
    if (!m_lobby) {
        pickMountainFromEnv();
        setupModes();
        return;
    }
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
    // The clothes (docs/OUTFITS.md): the colour above is the top.
    kke::Lobby::LookField skin{ "skin", "Skin", {}, {} }, legs{ "trousers", "Trousers", {}, {} }, shoes{ "shoes", "Shoes", {}, {} };
    for (const kke::NamedColour& c : kke::skinTones()) {
        skin.choices.push_back(c.name);
        skin.swatches.push_back(c.rgb);
    }
    for (const kke::NamedColour& c : kke::clothColours()) {
        legs.choices.push_back(c.name);
        legs.swatches.push_back(c.rgb);
        shoes.choices.push_back(c.name);
        shoes.swatches.push_back(c.rgb);
    }
    l.addLookField(std::move(skin));
    l.addLookField(std::move(legs));
    l.addLookField(std::move(shoes));
    // A Synty person instead of the mannequin, when the pack is here.
    if (m_people.any()) {
        kke::Lobby::LookField body{ "body", "Body", { "Mannequin" }, {} };
        for (const std::string& n : kke::PeopleLibrary::names()) body.choices.push_back(n);
        l.addLookField(std::move(body));
    }
    // Before last time's looks load: every seat starts as someone else.
    for (int seat = 0; seat < kke::Lobby::kMaxSeats; ++seat) {
        l.setLook(seat, 2, 2 + seat * 2);
        l.setLook(seat, 3, 7 + seat);
        l.setLook(seat, 4, 6);
    }
    l.setCpuCount(1); // a rival, unless last time said otherwise
    // Which mountain: the open ones of the tour, in order, then Random
    // (a new one each time). Finishing one opens the next.
    l.addOption({ "mountain", "Mountain", { "Random" }, 0, true, {}, {} });
    refreshMountainRow(m_mountainPick);
    setupModes(); // the Mode row: Race, Rockfall, Elimination
    m_lobby->load();
    m_lobby->setTitle("CLIMB RACE", "Pick your climber. Another controller? Press {a} on it to join.");
    l.onJoin = [this](int seat) {
        if (m_phase == Phase::Lobby) return;
        m_rosterChanged = true;
        m_lobby->lobby().toast(m_lobby->lobby().seatName(seat) + " joins the next race: " + m_input->promptText("{race.again}") + " to race again now", 6.0f);
    };
    pickMountainFromEnv();
    // Straight into a race: demos, tests, and the old switches.
    const char* lobbyVar = kke::dev::env("KKE_CLIMB_LOBBY");
    if (m_autopilot || m_rockfall >= 0.0f || (lobbyVar && std::strcmp(lobbyVar, "0") == 0)) {
        l.setCpuCount(m_defaultCpus);
        for (int i = 0; i < kke::Lobby::kMaxCpus; ++i) l.setCpuDifficulty(i, 2); // Hard, the rival it always was
        m_lobby->close();
    }
}

// KKE_CLIMB_MOUNTAIN=<file name or name> picks a mountain; KKE_CLIMB_SEED
// alone picks Random on that seed (as the game always did).
void ClimbRaceModule::pickMountainFromEnv() {
    int pick = -1;
    if (const char* want = kke::dev::env("KKE_CLIMB_MOUNTAIN")) {
        for (size_t i = 0; i < m_mountains.size(); ++i)
            if (m_mountains[i].id == want || m_mountains[i].name == want) pick = static_cast<int>(i);
        if (std::strcmp(want, "random") == 0) pick = static_cast<int>(m_mountains.size());
        if (pick < 0) kke::log::get(name())->warn("KKE_CLIMB_MOUNTAIN: no mountain called '{}'", want);
    } else if (kke::dev::env("KKE_CLIMB_SEED")) {
        pick = static_cast<int>(m_mountains.size());
    }
    if (pick >= 0) setMountainPick(pick);
}

// Same key, same rock: the id and every knob of the generator.
std::string ClimbRaceModule::keyOf(const Mountain& m) {
    const kke::ClimbWallDesc& d = m.desc;
    return fmt::format("{}#{}#{}#{}#{}#{}#{}#{}#{}#{}#{}", m.id, d.seed, d.ledges, d.height, d.maxOverhang, d.maxSlab, d.density, d.jugBias,
                       d.crimpBias, d.looseChance, d.routeStep);
}

void ClimbRaceModule::setMountainPick(int pick) {
    const int count = static_cast<int>(m_mountains.size());
    pick = std::clamp(pick, 0, count);
    if (pick < count && !m_progress.isOpen(m_mountains, static_cast<size_t>(pick))) m_forcedMountain = pick; // asked for by name: in the row too
    refreshMountainRow(pick);
}

int ClimbRaceModule::mountainPick() const {
    const kke::Lobby::Option* o = m_lobby ? m_lobby->lobby().option("mountain") : nullptr;
    if (o && o->value >= 0 && o->value < static_cast<int>(m_menuMountains.size())) return m_menuMountains[static_cast<size_t>(o->value)];
    return m_mountainPick;
}

void ClimbRaceModule::refreshMountainRow(int keep) {
    const int count = static_cast<int>(m_mountains.size());
    m_menuMountains.clear();
    for (int i = 0; i < count; ++i)
        if (m_progress.isOpen(m_mountains, static_cast<size_t>(i)) || i == m_forcedMountain) m_menuMountains.push_back(i);
    m_menuMountains.push_back(count); // Random
    const auto at = std::find(m_menuMountains.begin(), m_menuMountains.end(), keep);
    const size_t row = at == m_menuMountains.end() ? 0 : static_cast<size_t>(at - m_menuMountains.begin());
    m_mountainPick = m_menuMountains[row];
    kke::Lobby::Option* o = m_lobby ? m_lobby->lobby().option("mountain") : nullptr;
    if (!o) return;
    o->choices.clear();
    for (int i : m_menuMountains) o->choices.push_back(i < count ? m_mountains[static_cast<size_t>(i)].name : std::string("Random"));
    o->value = static_cast<int>(row);
}

Mountain ClimbRaceModule::chosenMountain() const {
    const int pick = mountainPick();
    if (pick >= 0 && pick < static_cast<int>(m_mountains.size())) return m_mountains[static_cast<size_t>(pick)];
    return randomMountain(m_randomSeed);
}

// After a race (Y / N): the next open mountain of the tour, round again
// after the last; on Random, another random one.
void ClimbRaceModule::nextMountain() {
    const int pick = mountainPick(), count = static_cast<int>(m_mountains.size());
    std::vector<int> tour;
    for (int i : m_menuMountains)
        if (i < count) tour.push_back(i);
    if (pick >= count || tour.empty()) {
        ++m_randomSeed;
    } else {
        const auto at = std::find(tour.begin(), tour.end(), pick);
        const size_t next = at == tour.end() ? 0 : (static_cast<size_t>(at - tour.begin()) + 1) % tour.size();
        setMountainPick(tour[next]);
    }
    useMountain(chosenMountain());
}

void ClimbRaceModule::loadProgress() {
    // Next to the game, wherever it was started from (KKE_CLIMB_PROGRESS for tests).
    if (const char* path = kke::dev::env("KKE_CLIMB_PROGRESS")) m_progressPath = path;
    else if (const char* base = SDL_GetBasePath()) m_progressPath = (std::filesystem::path(base) / "climb_race_progress.json").string();
    m_progress.openAll = kke::dev::flag("KKE_CLIMB_ALL");
    nlohmann::json j;
    std::string error;
    bool exists = false;
    if (kke::datafile::loadPath(m_progressPath, j, &error, &exists)) m_progress.load(j);
    else if (exists) kke::log::get(name())->warn("could not read {} (starting the tour again): {}", m_progressPath, error);
}

void ClimbRaceModule::saveProgress() {
    std::string error;
    if (!kke::datafile::saveFile(kke::datafile::saveTarget(std::filesystem::path(m_progressPath)), m_progress.save(), &error))
        kke::log::get(name())->warn("could not save {}: {}", m_progressPath, error);
}

std::string ClimbRaceModule::recordText(const Mountain& m) const {
    const Progress::Record* r = m_progress.record(m.id);
    const float best = m.id == "random" ? m_best : r ? r->best : 0.0f;
    if (best <= 0.0f) return "";
    std::string text = "best " + clockText(best);
    if (r && r->medal >= 0) text += std::string(", ") + medalName(r->medal);
    return text;
}

// A player on this screen topped out: their time counts for the tour.
void ClimbRaceModule::recordFinish(Racer& r) {
    if (r.seat < 0) return; // CPU climbers and other screens' players earn nothing here
    if (m_mountain.id == "random") {
        r.newBest = m_best <= 0.0f || r.time < m_best;
        if (r.newBest) m_best = r.time;
        return;
    }
    const Progress::Result res = m_progress.finish(m_mountains, m_mountain, r.time);
    r.medal = res.medal;
    r.newBest = res.newBest;
    saveProgress();
    // The best run yet: the ghost to race next time (Time trial).
    if (res.newBest && !r.run.empty()) {
        r.run.setTime(r.time);
        r.run.name = r.name;
        std::string error;
        if (!r.run.save(ghostFile(m_mountain), &error)) kke::log::get(name())->warn("time trial: could not save the ghost: {}", error);
    }
    kke::log::get(name())->info("{}: {} on {}{}{}", r.name, clockText(r.time), m_mountain.name, res.medal >= 0 ? std::string(", ") + medalName(res.medal) : std::string(),
                                res.newBest ? ", a new best" : "");
    if (!res.opened.empty()) {
        for (const Mountain& m : m_mountains)
            if (m.id == res.opened) m_opened = m.name;
        refreshMountainRow(mountainPick());
        kke::log::get(name())->info("{} is open", m_opened);
        tone(static_cast<int>(kke::Earcon::Activate), 0.8f);
    }
}

kke::Outfit ClimbRaceModule::outfitOf(const std::vector<int>& look, const glm::vec3& tint) const {
    auto pick = [&look](size_t field, const std::vector<kke::NamedColour>& from, int fallback) {
        const int c = field < look.size() ? look[field] : fallback;
        return from[static_cast<size_t>(std::clamp(c, 0, static_cast<int>(from.size()) - 1))].rgb;
    };
    kke::Outfit o;
    o.top = tint;
    o.skin = pick(2, kke::skinTones(), 4);
    o.bottom = pick(3, kke::clothColours(), 7);
    o.shoes = pick(4, kke::clothColours(), 6);
    return o;
}

int ClimbRaceModule::personOf(const std::vector<int>& look) const {
    if (!m_lobby) return 0;
    const auto& fields = m_lobby->lobby().lookFields();
    for (size_t f = 0; f < fields.size() && f < look.size(); ++f)
        if (fields[f].id == "body") return std::max(0, look[f]);
    return 0;
}

int ClimbRaceModule::personOfCharacter(const std::string& character) const {
    return m_lobby ? personOf(m_lobby->lobby().lookFromText(character)) : 0;
}

kke::Outfit ClimbRaceModule::outfitOfCharacter(const std::string& character, const glm::vec3& tint) const {
    const std::vector<int> look = m_lobby ? m_lobby->lobby().lookFromText(character) : std::vector<int>{};
    return outfitOf(look.empty() ? std::vector<int>{ 0, 0, 4, 7, 6 } : look, tint);
}

std::vector<ClimbRaceModule::Entry> ClimbRaceModule::wantedRoster() const {
    std::vector<Entry> out;
    std::vector<bool> nameUsed(kNameCount, false), colourUsed(kColourCount, false);
    int cpus = m_defaultCpus;
    std::vector<int> difficulty(static_cast<size_t>(kke::Lobby::kMaxCpus), 1);
    for (const std::string& taken : onlineNames())
        for (int i = 0; i < kNameCount; ++i)
            if (taken == kNames[i]) nameUsed[static_cast<size_t>(i)] = true;
    if (m_lobby) {
        const kke::Lobby& l = m_lobby->lobby();
        for (int seat : l.joinedSeats()) {
            const kke::Lobby::Seat& s = l.seat(seat);
            const int n = std::clamp(s.look[0], 0, kNameCount - 1), c = std::clamp(s.look[1], 0, kColourCount - 1);
            nameUsed[static_cast<size_t>(n)] = colourUsed[static_cast<size_t>(c)] = true;
            out.push_back({ seat, 2, kNames[n], kColours[c].rgb, outfitOf(s.look, kColours[c].rgb), personOf(s.look), s.look });
        }
        cpus = l.cpuCount();
        for (int i = 0; i < cpus; ++i) difficulty[static_cast<size_t>(i)] = l.cpuDifficulty(i);
    } else {
        nameUsed[0] = colourUsed[0] = true;
        out.push_back({ 0, 2, "You", kColours[0].rgb, outfitOf({ 0, 0, 4, 7, 6 }, kColours[0].rgb), 0, { 0, 0, 4, 7, 6 } });
    }
    if (!m_netName.empty()) out[0].name = m_netName; // the name it joins with, on every screen
    if (m_autopilot) out[0].name += " (autopilot)";
    // The CPU climbers take the names and colours nobody picked.
    for (int i = 0; i < cpus; ++i) {
        const auto n = std::find(nameUsed.begin(), nameUsed.end(), false), c = std::find(colourUsed.begin(), colourUsed.end(), false);
        const size_t ni = n == nameUsed.end() ? static_cast<size_t>(i) % kNameCount : static_cast<size_t>(n - nameUsed.begin());
        const size_t ci = c == colourUsed.end() ? static_cast<size_t>(i) % kColourCount : static_cast<size_t>(c - colourUsed.begin());
        nameUsed[ni] = colourUsed[ci] = true;
        // Clothes of their own, the same on every screen (from their name).
        const std::vector<int> look = { static_cast<int>(ni), static_cast<int>(ci), static_cast<int>((ni * 4 + 1) % kke::skinTones().size()),
                                        static_cast<int>((ni * 5 + 7) % kke::clothColours().size()), static_cast<int>((ni + 6) % kke::clothColours().size()) };
        out.push_back({ -1, difficulty[static_cast<size_t>(i)], std::string(kNames[ni]) + " (CPU)", kColours[ci].rgb, outfitOf(look, kColours[ci].rgb), 0, look });
    }
    return out;
}

void ClimbRaceModule::removeRacer(Racer& r) {
    if (r.model) m_models->remove(r.model);
    r.model = 0;
    if (r.personModel) m_models->remove(r.personModel);
    r.personModel = 0;
    r.personShown = 0;
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
        r.lane = e.ghost ? 0 : std::min(static_cast<int>(i), static_cast<int>(m_lanes.size()) - 1); // the ghost climbs through player 1
        r.seat = e.seat;
        r.bot = !e.remote && !e.ghost && (e.seat < 0 || m_autopilot);
        r.remote = e.remote || e.ghost; // posed from elsewhere: another machine, or the ghost
        r.ghost = e.ghost;
        r.netId = e.netId;
        r.mouse = e.seat >= 0 && (!l ? e.seat == 0 : l->seat(e.seat).device != kke::Lobby::Device::Pad);
        r.difficulty = e.difficulty;
        r.name = e.name;
        r.tint = e.tint;
        r.outfit = e.outfit;
        r.person = e.person;
        kke::RigidWorld::CharacterDesc cd;
        cd.position = glm::vec3(static_cast<float>(i), 0.0f, 12.0f);
        r.id = w.addCharacter(cd);
        if (r.remote) w.setCharacterKinematic(r.id, true); // placed where its machine says
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

void ClimbRaceModule::applyLooks(const std::vector<Entry>& roster) {
    for (size_t i = 0; i < m_racers.size() && i < roster.size(); ++i) {
        Racer& r = m_racers[i];
        if (r.remote != roster[i].remote || r.netId != roster[i].netId) continue; // not the same climber
        r.name = roster[i].name;
        if (r.difficulty != roster[i].difficulty) {
            r.difficulty = roster[i].difficulty;
            makeBrain(r);
        }
        if (r.tint != roster[i].tint) {
            r.tint = roster[i].tint;
            if (r.model && r.ghost) m_models->setTint(r.model, r.tint);
        }
        r.outfit = roster[i].outfit;
        r.person = roster[i].person;
        dress(r);
    }
}

// One face per climber; the ghost climbs on player 1's (its body passes through).
int ClimbRaceModule::faces() const {
    return std::max(1, static_cast<int>(std::count_if(m_racers.begin(), m_racers.end(), [](const Racer& r) { return !r.ghost; })));
}

int ClimbRaceModule::humans() const {
    return static_cast<int>(std::count_if(m_racers.begin(), m_racers.end(), [](const Racer& r) { return r.seat >= 0; }));
}

kke::Camera& ClimbRaceModule::cameraOf(Racer& r) {
    return r.seat >= 0 && r.player == 0 ? m_app->camera() : r.camera;
}

// The menu's line-up: this screen's climbers, and online the other
// screens' players too (in their own colours, standing at the back), so
// the host sees who joined and a joiner sees who's already in.
std::vector<ClimbRaceModule::Entry> ClimbRaceModule::lobbyRoster() const {
    std::vector<Entry> out = wantedRoster();
    if (!m_net || m_net->role() == kke::NetModule::Role::Offline) return out;
    if (netClient()) std::erase_if(out, [](const Entry& e) { return e.seat < 0; }); // a joiner's CPU climbers stay home
    for (const kke::net::RemotePlayer& p : m_net->remotePlayers()) out.push_back(remoteEntry(p));
    return out;
}

void ClimbRaceModule::updateLobby(float dt) {
    // Someone joined or left, or the CPU count changed: a new line-up.
    const std::vector<Entry> roster = lobbyRoster();
    bool same = roster.size() == m_racers.size();
    for (size_t i = 0; same && i < roster.size(); ++i)
        same = roster[i].seat == m_racers[i].seat && roster[i].remote == m_racers[i].remote && roster[i].netId == m_racers[i].netId;
    if (!same) buildRacers(roster);
    applyLooks(roster);
    // Another mountain picked: it rises behind the line-up.
    if (const Mountain m = chosenMountain(); keyOf(m) != m_builtKey) {
        useMountain(m);
        buildMountain(faces()); // every climber on the new rock
    }

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
    int cpu = 0, other = 0;
    const int others = static_cast<int>(std::count_if(m_racers.begin(), m_racers.end(), [](const Racer& r) { return r.remote; }));
    const int cpus = static_cast<int>(m_racers.size()) - humans() - others;
    for (Racer& r : m_racers) {
        glm::vec3 feet = r.seat >= 0 ? onGround(cardSpot(r.seat))
                         : r.remote  ? onGround(onlineSpot(other++, others, cpus)) - glm::vec3(0.0f, 0.0f, kOnlineBack)
                                     : onGround(cpuSpot(cpu++, cpus)) - glm::vec3(0.0f, 0.0f, kCpuBack);
        feet.y = 0.05f;
        glm::vec3 face = cam.position - feet;
        face.y = 0.0f;
        if (r.remote) {
            // Another screen's player: stands here, as their machine
            // doesn't send a pose until the race.
            r.net = netrace::Pose{};
            r.net.feet = feet;
            r.net.yaw = glm::degrees(std::atan2(face.x, -face.z));
            r.net.loco = static_cast<uint8_t>(kke::Locomotion::State::Ground);
            w.moveCharacter(r.id, feet);
            continue;
        }
        // Only when its spot moved: the height is the floor's (it drops the
        // last few cm onto it), and putting it back up each frame made it
        // bob up and down, a shake on screen.
        const glm::vec3 at = w.characterPosition(r.id);
        if (glm::length(glm::vec2(at.x - feet.x, at.z - feet.z)) > 0.05f) r.loco->teleport(feet);
        r.loco->setFacing(glm::normalize(face));
        r.loco->update(kke::Locomotion::Input{}, dt);
        r.climber->recover(30.0f, dt); // fresh for the start
    }
    for (Racer& r : m_racers) animateBody(r, dt);

    if (m_lobby->lobby().takeStart()) startFromLobby();
}

void ClimbRaceModule::startFromLobby() {
    if (netClient()) {
        // In someone else's game: they start it (applySetup).
        if (m_lobby) m_lobby->lobby().toast("The host starts the race", 3.0f);
        return;
    }
    const bool fromMenu = m_lobby && m_lobby->isOpen();
    if (fromMenu) m_lobby->save();
    if (m_lobby) m_lobby->close();
    if (netHost()) syncNetPlayers(); // the CPU climbers get their ids before the race is sent
    std::vector<Entry> roster = netHost() ? onlineRoster() : wantedRoster();
    // Time trial (offline): one more face, for the ghost of the best run.
    if (chosenMode() == Mode::TimeTrial && !netHost()) {
        Entry ghost;
        ghost.name = "Ghost";
        ghost.tint = glm::vec3(0.85f, 0.95f, 1.0f);
        ghost.ghost = true;
        roster.push_back(std::move(ghost));
    }
    bool same = !netHost() && roster.size() == m_racers.size();
    for (size_t i = 0; same && i < roster.size(); ++i) same = roster[i].seat == m_racers[i].seat;
    if (!same) buildRacers(roster);
    applyLooks(roster);
    // One face per climber.
    if (const Mountain m = chosenMountain(); keyOf(m) != m_builtKey || static_cast<int>(m_lanes.size()) != faces()) {
        useMountain(m);
        buildMountain(faces());
    }
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
    const int online = static_cast<int>(std::count_if(m_racers.begin(), m_racers.end(), [](const Racer& r) { return r.remote && !r.ghost; }));
    kke::log::get(name())->info("race: {} climbers: {} playing here, {} online, {} CPU, on {} (seed {})", m_racers.size(), humans(), online,
                                faces() - humans() - online, m_mountain.name, m_mountain.desc.seed);
}

void ClimbRaceModule::backToLobby() {
    showHowTo(false);
    clearRocks();
    m_captured = false;
    SDL_SetWindowRelativeMouseMode(m_app->window().handle(), false);
    resetRace();
    m_phase = Phase::Lobby;
    m_rosterChanged = false;
    m_app->views().clear();
    m_lobby->open();
}

} // namespace climb_race
