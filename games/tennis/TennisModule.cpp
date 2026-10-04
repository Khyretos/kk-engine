#include "TennisModule.h"

#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/PhysicsModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <system_error>
#include <typeindex>

namespace tennis {

namespace {

float envFloat(const char* name, float fallback) {
    const char* v = kke::dev::env(name);
    return v && *v ? static_cast<float>(std::atof(v)) : fallback;
}
bool envOn(const char* name) { return kke::dev::flag(name); }

} // namespace

TennisModule::TennisModule() = default;
TennisModule::~TennisModule() = default;

std::vector<kke::ModuleDependency> TennisModule::dependencies() const {
    return { { std::type_index(typeid(kke::RigidBodyModule)), true, "the people, the fences and the stands (Jolt)" },
             { std::type_index(typeid(kke::PhysicsModule)), true, "the FEMFX rubber balls" },
             { std::type_index(typeid(kke::InputModule)), true, "the controls, rebindable" },
             { std::type_index(typeid(kke::ModelModule)), false, "the players' animated bodies" },
             { std::type_index(typeid(kke::LobbyModule)), false, "the start menu: players join, CPU players" },
             { std::type_index(typeid(kke::NetModule)), false, "the sport center online" } };
}

void TennisModule::init(kke::Application& app) {
    m_app = &app;
    m_rigid = app.getModule<kke::RigidBodyModule>();
    m_physics = app.getModule<kke::PhysicsModule>();
    m_input = app.getModule<kke::InputModule>();
    m_models = app.getModule<kke::ModelModule>();
    m_lobby = app.getModule<kke::LobbyModule>();
    m_net = app.getModule<kke::NetModule>();

    m_allBots = envOn("KKE_TENNIS_BOTS");
    m_doubles = envOn("KKE_TENNIS_DOUBLES");
    m_ballTest = envOn("KKE_TENNIS_BALLTEST");
    m_level = static_cast<int>(envFloat("KKE_TENNIS_LEVEL", 1.0f));
    m_seed = static_cast<uint32_t>(envFloat("KKE_TENNIS_SEED", 1.0f));
    m_quitAfter = envFloat("KKE_TENNIS_QUIT", -1.0f);
    m_swingLog = kke::dev::flag("KKE_TENNIS_SWINGLOG");
    if (const char* r = kke::dev::env("KKE_TENNIS_REPLAYS")) m_replays = std::string(r) != "0";
    m_closeUp = static_cast<int>(envFloat("KKE_TENNIS_CLOSEUP", -1.0f));
    m_closeUpDistance = envFloat("KKE_TENNIS_CLOSEUP_DISTANCE", 3.8f);
    if (const char* pose = kke::dev::env("KKE_TENNIS_POSE")) m_poseTest = pose;
    m_stringTest = kke::dev::env("KKE_TENNIS_STRINGTEST") != nullptr;
    m_autoplay = envOn("KKE_TENNIS_AUTOPLAY");
    m_serveAt = envFloat("KKE_TENNIS_SERVEAT", -1.0f);
    if (envOn("KKE_TENNIS_CENTER")) m_where = 1;
    m_crowd = static_cast<int>(envFloat("KKE_TENNIS_CROWD", static_cast<float>(m_crowd)));


    app.camera().farPlane = 400.0f;
    app.camera().fovDegrees = 50.0f;
    defineControls();
    m_rig = std::make_unique<Rig>(app);
    if (m_models) {
        m_rig->load(*m_models);
        m_rig->loadRacket(*m_models);
        m_rig->setPhysics(m_physics);
    }
    buildWorld();
    buildNets();
    buildHud();
    setupLobby();
    setupBench();
    setupNet();
    if (m_ballTest) {
        kke::log::get(name())->info("ball test: test shots at the net, the court and the fence (KKE_TENNIS_BALLTEST)");
        if (m_lobby) m_lobby->close();
        m_inMenu = false;
    } else if (!m_inMenu && !waitsOnline()) {
        startLocalMatch();
    }
}

void TennisModule::defineControls() {
    using IM = kke::InputModule;
    m_input->setPlayers(4);
    for (int p = 0; p < 4; ++p) {
        kke::InputMap& in = m_input->map(p);
        kke::InputModule::defineCharacterActions(in);
        // Tennis needs the move stick, the camera stick, the shot buttons and
        // push to talk, nothing else.
        for (const char* a : { "jump", "sprint", "walk", "crouch", "fire", "aim", "interact", "camera.toggle", "camera.zoom", "look", "voice.talk" })
            in.clearBindings(a);
        in.addBinding(IM::bind("voice.talk", IM::key(SDL_SCANCODE_V))); // B is a shot on the pad; keep one talk key everywhere
        in.addBinding(IM::bind("voice.talk", IM::pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)));
        in.defineAction({ "tennis.topspin", "Topspin (the all-round shot)", "Shots", "game" });
        in.defineAction({ "tennis.flat", "Flat (hard and fast)", "Shots", "game" });
        in.defineAction({ "tennis.slice", "Slice (low and slow)", "Shots", "game" });
        in.defineAction({ "tennis.lob", "Lob (over their head)", "Shots", "game" });
        in.defineAction({ "tennis.menu", "Back to the menu", "Game", "game" });
        in.defineAction({ "panels", "Developer panels", "Game", "game" });
        in.addBinding(IM::bind("tennis.topspin", IM::key(SDL_SCANCODE_SPACE)));
        in.addBinding(IM::bind("tennis.topspin", IM::mouse(SDL_BUTTON_LEFT)));
        in.addBinding(IM::bind("tennis.flat", IM::key(SDL_SCANCODE_J)));
        in.addBinding(IM::bind("tennis.slice", IM::key(SDL_SCANCODE_K)));
        in.addBinding(IM::bind("tennis.slice", IM::mouse(SDL_BUTTON_RIGHT)));
        in.addBinding(IM::bind("tennis.lob", IM::key(SDL_SCANCODE_L)));
        // Esc and Select are the pause menu (kke::GameShellModule), whose Main
        // menu does the same; M stays a keyboard shortcut.
        in.addBinding(IM::bind("tennis.menu", IM::key(SDL_SCANCODE_M)));
        in.addBinding(IM::bind("panels", IM::key(SDL_SCANCODE_F1)));
        in.addBinding(IM::bind("tennis.topspin", IM::pad(SDL_GAMEPAD_BUTTON_SOUTH)));
        in.addBinding(IM::bind("tennis.flat", IM::pad(SDL_GAMEPAD_BUTTON_WEST)));
        in.addBinding(IM::bind("tennis.slice", IM::pad(SDL_GAMEPAD_BUTTON_EAST)));
        in.addBinding(IM::bind("tennis.lob", IM::pad(SDL_GAMEPAD_BUTTON_NORTH)));
    }
    m_input->commitDefaults();
    if (auto* shell = m_app->getModule<kke::GameShellModule>()) {
        shell->onMainMenu = [this] { leaveToMenu(); };
        shell->addPauseItem("Leave the court (a walkover)", [this] { leaveCenterMatch(0); },
                            [this] { return m_inCenter && !netClient() && inCenterMatch(0); });
    }
}

// The menu button (M, or the pause menu's Main menu): back to the start
// menu; a client's leaves the online game.
void TennisModule::leaveToMenu() {
    if (netClient()) {
        m_wasOnline = false; // on purpose: no "left" toast
        m_net->leave();
    }
    if (!m_inMenu) backToMenu();
}

// In the sport center: the player on input map `input` walks off their
// court (the other side wins the match). False when they aren't playing.
bool TennisModule::leaveCenterMatch(int input) {
    for (size_t mi = 0; mi < m_matches.size(); ++mi)
        for (int idx : m_matches[mi]->players)
            if (player(idx).walker >= 0 && player(idx).input == input) {
                endCenterMatch(mi, player(idx).team);
                return true;
            }
    return false;
}

bool TennisModule::inCenterMatch(int input) const {
    for (const auto& m : m_matches)
        for (int idx : m->players)
            if (m_players[static_cast<size_t>(idx)].walker >= 0 && m_players[static_cast<size_t>(idx)].input == input) return true;
    return false;
}

void TennisModule::clearPlayers() {
    kke::RigidWorld& w = m_rigid->world();
    for (Player& p : m_players)
        if (p.body) w.removeCharacter(p.body);
    m_players.clear();
    m_matches.clear();
    m_app->setTimeScale(1.0f); // left in the middle of a replay
    for (Walker& wk : m_walkers)
        if (wk.body) w.removeCharacter(wk.body);
    m_walkers.clear();
    for (Gate& g : m_gates) g = Gate{};
    for (auto& seats : m_seatTaken) seats.clear();
    m_inCenter = false;
}

void TennisModule::freePlayer(int index) {
    Player& p = player(index);
    if (p.body) m_rigid->world().removeCharacter(p.body);
    p = Player{};
}

MatchRules TennisModule::menuRules(int teamSize) const {
    MatchRules rules;
    rules.teamSize = teamSize;
    switch (m_length) {
    case 1: rules.gamesPerSet = 6; break;
    case 2: rules.gamesPerSet = 4; rules.setsToWin = 2; break;
    case 3: rules.gamesPerSet = 2; break; // quick: to 2 games
    default: rules.gamesPerSet = 4; break;
    }
    if (const char* g = kke::dev::env("KKE_TENNIS_GAMES")) rules.gamesPerSet = std::max(1, std::atoi(g));
    return rules;
}

int TennisModule::spawnPlayer(const Entry& e, int team) {
    Player p;
    p.alive = true;
    p.walker = e.walker;
    p.name = e.name;
    p.tint = e.tint;
    p.team = team;
    p.remote = e.remote;
    p.netId = e.netId;
    p.netCpu = e.netCpu;
    // KKE_TENNIS_AUTOPLAY: this screen's people play themselves (tests).
    p.cpu = !e.remote && (e.cpu || m_autoplay);
    p.input = e.remote ? -1 : e.input;
    p.level = e.cpu ? e.level : std::max(e.level, 2);
    kke::RigidWorld::CharacterDesc cd;
    cd.radius = 0.3f;
    cd.height = 1.8f;
    cd.pushStrength = 0.0f;
    kke::RigidWorld& w = m_rigid->world();
    p.body = w.addCharacter(cd);
    // Another machine's player goes where that machine says (Net.cpp).
    if (p.remote) w.setCharacterKinematic(p.body, true);
    if (p.cpu) p.bot = std::make_unique<Bot>(m_seed * 7919u + static_cast<uint32_t>(m_players.size()) * 104729u, p.level);
    p.look = std::make_unique<Body>(*m_rig, *m_models, e.tint, true);
    // Clothes: a CPU player's from its name, another screen's person as
    // they picked there, ours as picked here.
    if (e.cpu || e.netCpu) {
        p.look->setOutfit(cpuOutfit(e.name, e.tint));
    } else if (e.remote && m_net) {
        kke::Outfit o = e.dressed ? e.outfit : outfitOf({}, e.tint);
        int person = e.person;
        for (const kke::net::RemotePlayer& rp : m_net->remotePlayers())
            if (rp.id == e.netId) {
                o = outfitOfCharacter(rp.character, e.tint);
                person = personOfCharacter(rp.character);
            }
        p.look->setOutfit(o);
        p.look->setPerson(person);
    } else if (e.dressed) {
        p.look->setOutfit(e.outfit);
        p.look->setPerson(e.person);
    }
    // A free slot (a match that ended) or a new one.
    for (size_t i = 0; i < m_players.size(); ++i)
        if (!m_players[i].alive) {
            m_players[i] = std::move(p);
            return static_cast<int>(i);
        }
    m_players.push_back(std::move(p));
    return static_cast<int>(m_players.size()) - 1;
}

namespace {
const glm::vec3 kTints[] = { { 0.35f, 0.6f, 1.25f }, { 1.25f, 0.45f, 0.32f }, { 0.45f, 1.1f, 0.45f }, { 1.2f, 1.0f, 0.35f } };
const char* const kCpuNames[] = { "Ace", "Deuce", "Volley", "Lobster" };
} // namespace

kke::Outfit TennisModule::outfitOf(const std::vector<int>& look, const glm::vec3& tint) const {
    auto pick = [&look](size_t field, const std::vector<kke::NamedColour>& from, int fallback) {
        const int c = field < look.size() ? look[field] : fallback;
        return from[static_cast<size_t>(std::clamp(c, 0, static_cast<int>(from.size()) - 1))].rgb;
    };
    kke::Outfit o;
    o.top = glm::clamp(tint / 1.25f, 0.0f, 1.0f); // the players' tints are a brightened swatch
    o.skin = pick(2, kke::skinTones(), 4);
    o.bottom = pick(3, kke::clothColours(), 11);
    o.shoes = pick(4, kke::clothColours(), 0);
    return o;
}

kke::Outfit TennisModule::outfitOfCharacter(const std::string& character, const glm::vec3& tint) const {
    return outfitOf(m_lobby ? m_lobby->lobby().lookFromText(character) : std::vector<int>{}, tint);
}

int TennisModule::personOf(const std::vector<int>& look) const {
    if (!m_lobby) return 0;
    const auto& fields = m_lobby->lobby().lookFields();
    for (size_t f = 0; f < fields.size() && f < look.size(); ++f)
        if (fields[f].id == "body") return std::max(0, look[f]);
    return 0;
}

int TennisModule::personOfCharacter(const std::string& character) const {
    return m_lobby ? personOf(m_lobby->lobby().lookFromText(character)) : 0;
}

kke::Outfit TennisModule::cpuOutfit(const std::string& name, const glm::vec3& tint) {
    uint32_t h = 2166136261u; // FNV-1a: the same name, the same clothes, on every screen
    for (const char c : name) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    const auto& skins = kke::skinTones();
    const auto& cloth = kke::clothColours();
    kke::Outfit o;
    o.top = glm::clamp(tint / 1.25f, 0.0f, 1.0f);
    o.skin = skins[h % skins.size()].rgb;
    o.bottom = cloth[(h >> 8) % cloth.size()].rgb;
    o.shoes = cloth[(h >> 16) % cloth.size()].rgb;
    return o;
}

// This screen's players: the menu's seats, then its CPU players (the
// switches without the menu). Online, the same order is their network
// slots (syncNetPlayers).
std::vector<TennisModule::Entry> TennisModule::seatEntries() const {
    std::vector<Entry> entries;
    if (m_lobby && !m_allBots && !m_lobby->lobby().joinedSeats().empty()) {
        kke::Lobby& l = m_lobby->lobby();
        for (int seat : l.joinedSeats()) {
            Entry e;
            e.name = l.seatName(seat);
            e.tint = kTints[entries.size() % 4];
            const auto& fields = l.lookFields();
            for (size_t f = 0; f < fields.size(); ++f)
                if (fields[f].id == "colour" && !fields[f].swatches.empty())
                    e.tint = fields[f].swatches[static_cast<size_t>(l.seat(seat).look[f]) % fields[f].swatches.size()] * 1.25f;
            e.outfit = outfitOf(l.seat(seat).look, e.tint);
            e.person = personOf(l.seat(seat).look);
            e.dressed = true;
            e.input = m_lobby->playerOf(seat);
            // The menu was skipped (KKE_TENNIS_LOBBY=0): seats in order, the first on player 0's devices.
            if (e.input < 0) e.input = static_cast<int>(entries.size());
            entries.push_back(std::move(e));
        }
        for (int i = 0; i < l.cpuCount(); ++i) {
            Entry e;
            e.tint = kTints[entries.size() % 4];
            e.cpu = true;
            e.level = l.cpuDifficulty(i);
            entries.push_back(std::move(e));
        }
    } else if (!m_allBots) {
        // No one joined at the menu: you, on the keyboard or the first controller.
        Entry e;
        e.name = "You";
        e.tint = kTints[0];
        e.outfit = outfitOf({}, e.tint);
        e.dressed = true;
        e.input = 0;
        entries.push_back(std::move(e));
    }
    int cpuNumber = 0;
    for (Entry& e : entries)
        if (e.cpu && e.name.empty()) e.name = std::string(kCpuNames[cpuNumber++ % 4]) + " (CPU)";
    // KKE_NET_NAME: player 1's name (online tests, two windows on one PC).
    if (const char* who = kke::dev::env("KKE_NET_NAME"); who && *who && !entries.empty() && !entries.front().cpu) entries.front().name = who;
    return entries;
}

void TennisModule::startLocalMatch() {
    if (m_where == 1 && !netClient()) {
        enterCenter();
        return;
    }
    std::vector<Entry> entries = seatEntries();
    if (online()) {
        // People first (ours, then everyone online), then the menu's CPU
        // players, then more if a side is short.
        std::vector<Entry> cpus;
        for (const Entry& e : seatEntries())
            if (e.cpu) cpus.push_back(e);
        entries = netEntries();
        for (const kke::net::RemotePlayer& rp : m_net->remotePlayers()) {
            Entry e;
            e.name = rp.name;
            e.tint = net::tintFromText(rp.character, glm::vec3(0.8f));
            e.outfit = outfitOfCharacter(rp.character, e.tint);
            e.person = personOfCharacter(rp.character);
            e.dressed = true;
            e.netId = rp.id;
            e.remote = true;
            entries.push_back(std::move(e));
        }
        if (entries.size() > 4)
            kke::log::get(name())->info("online: {} people for one court, the first 4 play (the sport center's other courts come later)", entries.size());
        for (Entry& c : cpus) entries.push_back(std::move(c));
    }
    // At least an opponent; doubles when there are more than two.
    const size_t want = (m_doubles || entries.size() > 2) ? 4 : 2;
    int cpuNumber = static_cast<int>(std::count_if(entries.begin(), entries.end(), [](const Entry& e) { return e.cpu; }));
    while (entries.size() < want) {
        Entry e;
        e.tint = kTints[entries.size() % 4];
        e.cpu = true;
        e.level = m_level;
        e.name = std::string(kCpuNames[cpuNumber++ % 4]) + " (CPU)";
        entries.push_back(std::move(e));
    }
    entries.resize(want);
    clearPlayers();
    Match* m = buildMatch(std::move(entries), menuRules(static_cast<int>(want) / 2), 0);
    if (netHost()) sendSetup(*m);
}

TennisModule::Match* TennisModule::buildMatch(std::vector<Entry> entries, const MatchRules& rules, int court) {
    auto m = std::make_unique<Match>();
    m->court = court;
    const size_t want = entries.size();
    // Teams: as given (the host's Setup); else seats alternate across the
    // net, unless the menu put the people at this screen on one side
    // against the CPU players.
    const int size = rules.teamSize;
    std::vector<int> team(want, 0);
    if (!entries.empty() && entries.front().team >= 0) {
        for (size_t i = 0; i < want; ++i) team[i] = entries[i].team;
    } else if (m_teams == 1) {
        for (size_t i = 0; i < want; ++i) team[i] = entries[i].cpu ? 1 : 0;
        int n0 = static_cast<int>(std::count(team.begin(), team.end(), 0));
        for (size_t i = want; i-- > 0 && n0 > size;)
            if (team[i] == 0) { team[i] = 1; --n0; }
        for (size_t i = 0; i < want && n0 < size; ++i)
            if (team[i] == 1) { team[i] = 0; ++n0; }
    } else {
        for (size_t i = 0; i < want; ++i) team[i] = static_cast<int>(i % 2);
    }
    for (size_t i = 0; i < want; ++i) m->players.push_back(spawnPlayer(entries[i], team[i]));
    // Slots within each team.
    int slots[2] = { 0, 0 };
    for (size_t i = 0; i < want; ++i) {
        Player& p = player(m->players[i]);
        p.slot = entries[i].slot >= 0 ? entries[i].slot : slots[p.team]++;
    }

    m->rules = rules;
    m->score = Score(m->rules);
    m->ball = std::make_unique<Ball>(*m_physics, m_center.courts[static_cast<size_t>(m->court)]);
    m->phase = Match::Phase::Warmup;
    m->phaseTime = 0.0f;
    m_matches.push_back(std::move(m));
    std::string who;
    for (int idx : m_matches.back()->players)
        who += (who.empty() ? "" : ", ") + player(idx).name + (player(idx).remote ? " (online)" : "") + " (team " + std::to_string(player(idx).team + 1) + ")";
    kke::log::get(name())->info("{} match on court {}: {}; first to {} games{}", size == 2 ? "doubles" : "singles", court + 1, who,
                                m_matches.back()->rules.gamesPerSet, m_matches.back()->rules.setsToWin > 1 ? ", best of three" : "");
    return m_matches.back().get();
}

void TennisModule::backToMenu() {
    if (netHost() && (!m_matches.empty() || m_inCenter)) m_net->sendEvent(net::kEventEnd, net::encode(net::End{ 0, "The host went back to the menu" }));
    clearPlayers();
    if (!m_lobby) return;
    m_inMenu = true;
    m_lobby->open();
}

void TennisModule::fixedUpdate(const kke::FixedUpdateContext& ctx) {
    if (m_ballTest) {
        ballTest(ctx.fixedDt);
        return;
    }
    for (auto& m : m_matches) {
        if (m->replay && m->replay->playing) {
            stepReplay(*m, ctx.fixedDt);
            continue;
        }
        stepMatch(*m, ctx.fixedDt);
        recordReplay(*m);
    }
    if (m_inCenter) stepCenter(ctx.fixedDt);
    // KKE_TENNIS_QUIT: a report every 10 s of game time, then quit.
    m_clock += ctx.fixedDt;
    if (m_quitAfter > 0.0f) {
        if (m_clock >= m_reportAt) {
            m_reportAt += 10.0f;
            if (m_inCenter) {
                int watching = 0, going = 0, strolling = 0;
                for (const Walker& w : m_walkers) {
                    if (w.remote && !w.gone && w.body) {
                        const glm::vec3 at = m_rigid->world().characterPosition(w.body);
                        kke::log::get(name())->info("t {:.0f} s: {} (another screen) at ({:.1f}, {:.1f}), {}", m_clock, w.name, at.x, at.z,
                                                    w.playing >= 0 ? "playing" : w.look && w.look->visible() ? "walking, drawn" : "walking, NOT drawn");
                    }
                    if (!w.cpu) continue;
                    (w.doing == Walker::Doing::Watch ? watching : w.doing == Walker::Doing::ToSeat ? going : strolling)++;
                }
                kke::log::get(name())->info("t {:.0f} s: {} matches on, {} points played, longest rally {} shots; crowd: {} watching, {} going to a "
                                            "seat, {} strolling; {} matches won so far",
                                            m_clock, m_matches.size(), m_pointsPlayed, m_longestRally, watching, going, strolling,
                                            std::accumulate(m_wins.begin(), m_wins.end(), 0, [](int n, const auto& w) { return n + w.second; }));
            } else
            for (const auto& m : m_matches)
                kke::log::get(name())->info("t {:.0f} s: sets {}-{}, games {} / {}, points {}-{} ({}), {} points played, longest rally {} shots",
                                            m_clock, m->score.sets(0), m->score.sets(1), m->score.setsText(0), m->score.setsText(1),
                                            m->score.pointText(0), m->score.pointText(1), m->score.callText(), m_pointsPlayed, m_longestRally);
        }
        if (m_clock >= m_quitAfter) {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
            m_quitAfter = -1.0f;
        }
    }
}

void TennisModule::update(const kke::UpdateContext& ctx) {
    const float dt = ctx.dt;
    if (m_stringTest) stringTest(dt);
    // A shot button skips a replay.
    for (auto& m : m_matches) {
        if (!m->replay || !m->replay->playing) continue;
        for (int i = 0; i < m_input->players(); ++i)
            for (const char* id : { "tennis.topspin", "tennis.flat", "tennis.slice", "tennis.lob" })
                if (m_input->map(i).pressed(id)) endReplay(*m);
    }
    kke::InputMap& p1 = m_input->map(0);
    if (p1.pressed("panels")) m_app->debugUi().setVisible(!m_app->debugUi().visible());
    if (m_inMenu) {
        updateLobby(dt);
    } else if (m_inCenter) {
        // The sport center: the menu button leaves a match (the other side
        // wins it) or, walking, goes back to the menu. Online, a client's
        // leaves the game (the host calls its match off).
        for (int i = 0; i < m_input->players(); ++i) {
            if (!m_input->map(i).pressed("tennis.menu")) continue;
            if (netClient()) {
                leaveToMenu();
                break;
            }
            if (!leaveCenterMatch(i)) backToMenu();
            break;
        }
        for (Walker& w : m_walkers)
            if (!w.cpu && !w.remote && !w.gone && w.playing < 0) readWalker(w);
    } else if (m_lobby && !m_ballTest) {
        for (int i = 0; i < m_input->players(); ++i)
            if (m_input->map(i).pressed("tennis.menu")) {
                leaveToMenu();
                break;
            }
    }
    updateNet(dt);
    for (auto& m : m_matches)
        for (int idx : m->players)
            if (!player(idx).cpu && !player(idx).remote) readHuman(*m, player(idx));
    updateBodies(dt);
    updateCameras(dt);
    updateMarks();
    updateNets(dt);
    updateHud();
    sendNet();

}

void TennisModule::render(const kke::RenderContext& ctx) {
    renderCourts(ctx);
    for (Player& p : m_players)
        if (p.look) p.look->render(ctx);
}

void TennisModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    if (m_standMesh) m_standMesh->drawShadow(ctx);
    if (m_fenceMesh) m_fenceMesh->drawShadow(ctx);
    if (m_ballMesh && !m_ballIdx.empty()) m_ballMesh->drawShadow(ctx);
    for (Player& p : m_players)
        if (p.look) p.look->renderShadow(ctx);
}

void TennisModule::shutdown() {
    clearPlayers();
    m_testBall.reset();
    m_testBed.reset();
    if (m_rigid) {
        kke::RigidWorld& w = m_rigid->world();
        for (kke::RigidWorld::BodyId b : m_statics) w.remove(b);
        for (const CourtNet& n : m_nets) {
            w.removeCloth(n.cloth);
            w.remove(n.ball);
        }
    }
    m_statics.clear();
    m_nets.clear();
    m_netMesh.reset();
    m_ballMesh.reset();
    m_markMesh.reset();
    m_shadowMesh.reset();
    m_courtMesh.reset();
    m_standMesh.reset();
    m_fenceMesh.reset();
    m_rig.reset();
}

// ------------------------------------------------------------ string test

// One string bed on its own, hit in the middle by a 30 m/s ball every 2 s:
// logs how deep the pocket goes and how long it takes to ring out.
void TennisModule::stringTest(float dt) {
    if (!m_testBed) {
        m_testBed = m_rig->makeStringBed();
        m_testBedTime = 0.0f;
        if (!m_testBed) {
            m_stringTest = false;
            kke::log::get(name())->warn("string test: no string bed (no physics?)");
        }
        return;
    }
    const float was = m_testBedTime;
    m_testBedTime += dt;
    if (std::fmod(was, 2.0f) > std::fmod(m_testBedTime, 2.0f)) {
        const glm::vec2 c = m_rig->strings().centre;
        m_testBed->strike(glm::vec3(c, 0.0f), glm::vec3(0.0f, 0.0f, -30.0f));
        kke::log::get(name())->info("string test: hit at {:.1f} s", m_testBedTime);
    }
    std::vector<glm::vec3> bent;
    if (m_testBed->update(dt, bent)) kke::log::get(name())->info("string test: {:.2f} s pocket {:.1f} mm", m_testBedTime, m_testBed->depth() * 1000.0f);
}

// ------------------------------------------------------------ ball test

// Fires the same shots at the same spots and logs what the FEMFX ball
// does: how high it comes off the court (restitution), how much speed it
// keeps along the court, whether the net and the fence stop it, and
// whether the contact detection sees each touch.
void TennisModule::ballTest(float dt) {
    if (!m_testBall) {
        m_testBall = std::make_unique<Ball>(*m_physics, m_center.courts[0]);
        m_testTime = 10.0f;
    }
    Ball& b = *m_testBall;
    b.step(dt);
    for (const Ball::Event& e : b.takeEvents()) {
        const char* kind = e.kind == Ball::Event::Kind::Bounce ? "bounce" : e.kind == Ball::Event::Kind::Net ? "net" : "out";
        const float gap = glm::length(b.bodyPosition() - b.position());
        kke::log::get(name())->info("ball test {}: {} at ({:.2f}, {:.2f}, {:.2f}), velocity now ({:.2f}, {:.2f}, {:.2f}), FEMFX body {:.3f} m off", m_testShot,
                                    kind, e.at.x, e.at.y, e.at.z, b.velocity().x, b.velocity().y, b.velocity().z, gap);
    }
    m_testTime += dt;
    if (m_testTime < 3.0f) return;
    m_testTime = 0.0f;
    ++m_testShot;
    struct Test { const char* what; glm::vec3 from, vel; };
    const Test tests[] = {
        { "drop from 2 m", { 0.0f, 2.0f, 5.0f }, { 0.0f, 0.0f, 0.0f } },
        { "groundstroke 25 m/s", { 0.0f, 1.0f, 11.0f }, planShot({ 0.0f, 1.0f, 11.0f }, { 1.0f, 0.0f, -9.0f }, 25.0f, ShotKind::Flat).velocity },
        { "into the net at 30 m/s", { 0.0f, 0.5f, 6.0f }, { 0.0f, 0.0f, -30.0f } },
        { "serve 45 m/s", { 0.5f, 2.6f, 12.2f }, planShot({ 0.5f, 2.6f, 12.2f }, { -2.0f, 0.0f, -5.5f }, 45.0f, ShotKind::Flat).velocity },
        { "at the side fence 35 m/s", { 0.0f, 1.0f, 0.0f + 5.0f }, { 35.0f, 0.5f, 0.0f } },
        { "at the back fence 40 m/s", { 0.0f, 1.0f, -5.0f }, { 0.0f, 1.0f, -40.0f } },
        { "lob at the roof", { 0.0f, 1.0f, 10.0f }, { 0.0f, 22.0f, -2.0f } },
    };
    constexpr int kTests = static_cast<int>(sizeof(tests) / sizeof(tests[0]));
    if (m_testShot >= kTests) {
        kke::log::get(name())->info("ball test done");
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
        m_ballTest = false;
        return;
    }
    const Test& t = tests[m_testShot];
    b.place(t.from, glm::vec3(0.0f));
    b.strike(t.vel, glm::vec3(0.0f), 0.0f, 0.0f);
    kke::log::get(name())->info("ball test {}: {} from ({:.1f}, {:.1f}, {:.1f}) at ({:.1f}, {:.1f}, {:.1f}) m/s", m_testShot, t.what, t.from.x, t.from.y,
                                t.from.z, t.vel.x, t.vel.y, t.vel.z);
}

} // namespace tennis
