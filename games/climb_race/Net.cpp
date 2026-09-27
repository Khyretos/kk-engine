// Climb Race online (README.md "Online", docs/NETWORKING.md): player 1
// sets Online to Host or Join in the start menu; everyone on each screen
// races (a second player on a screen is a NetModule local player, like
// split screen in Halo). The host decides the race: its Setup event says
// which mountain and who climbs which face, and starts every countdown.
// Each machine climbs its own racers and sends their poses (NetRace.h);
// the others' climbers are drawn from them. CPU climbers are the host's:
// they go to everyone as the host's local players too.

#include "ClimbRaceModule.h"

#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace climb_race {

namespace {
constexpr int kModeOff = 0, kModeJoin = 1, kModeHost = 2; // Join first: going to Host passes a harmless search, not a game others see
constexpr float kSearchEvery = 3.0f; // s between LAN searches while Join is picked
} // namespace

bool ClimbRaceModule::netClient() const { return m_net && m_net->role() == kke::NetModule::Role::Client; }
bool ClimbRaceModule::netHost() const { return m_net && m_net->role() == kke::NetModule::Role::Host; }

void ClimbRaceModule::setupNet() {
    if (!m_net) return;
    m_net->standIns = false; // every climber has a face of their own: nobody bumps into anyone
    // The host's wall check reads a climber pulling over an edge as going
    // through the rock; the speed limits still hold every move.
    m_net->checkMoves = false;
    m_net->addEventListener([this](const kke::net::GameEventMsg& e) { onNetEvent(e); });
    m_net->onPlayer = [this](uint8_t id, bool joined) {
        if (!netHost()) return;
        std::string who = "Player " + std::to_string(id);
        for (const kke::net::RemotePlayer& p : m_net->remotePlayers())
            if (p.id == id) who = p.name;
        if (joined && m_phase != Phase::Lobby) {
            m_rosterChanged = true; // in from the next race
            if (m_lobby) m_lobby->lobby().toast(who + " joined online: " + m_input->promptText("{race.again}") + " to race again with them", 6.0f);
        } else if (joined && m_lobby) {
            m_lobby->lobby().toast(who + " joined online", 4.0f);
        }
    };
    if (const char* who = kke::dev::env("KKE_NET_NAME")) m_netName = who;
    if (const char* wait = kke::dev::env("KKE_CLIMB_WAIT")) m_netWait = std::max(0, std::atoi(wait));
    if (!m_lobby) {
        syncNetPlayers();
        return;
    }
    // Player 1's rows, before Start: Online (Off / Host / Join), then the
    // games found on the LAN and on this PC, and Join.
    kke::Lobby& l = m_lobby->lobby();
    kke::Lobby::Option mode{ "net.mode", "Online", { "Off", "Join", "Host" }, kModeOff, true, {}, {} };
    mode.onChange = [this](int value) {
        std::string error;
        m_wasOnline = false; // leaving on purpose: no "left the game" toast
        if (value == kModeOff) {
            m_net->leave();
        } else if (value == kModeHost) {
            m_net->leave();
            syncNetPlayers(); // our names before the host() announces them
            if (!m_net->host(0, &error)) {
                m_lobby->lobby().toast("Can't host: " + error, 5.0f);
                if (kke::Lobby::Option* o = m_lobby->lobby().option("net.mode")) o->value = kModeOff;
            }
        } else {
            m_net->leave();
            m_net->searchLan();
            m_netSearchAt = m_netTime + kSearchEvery;
        }
        m_lastNetStatus.clear(); // show it
    };
    l.addOption(std::move(mode));
    l.addOption({ "net.game", "Game", { "Searching..." }, 0, false, {}, {} });
    kke::Lobby::Option join{ "net.join", "Join", {}, 0, false, {}, {} };
    join.onPress = [this]() {
        const kke::Lobby::Option* game = m_lobby->lobby().option("net.game");
        std::vector<kke::NetModule::LanGame> games = m_net->lanGames();
        std::erase_if(games, [](const kke::NetModule::LanGame& g) { return !g.ours; });
        if (!game || game->value < 0 || game->value >= static_cast<int>(games.size())) return;
        const kke::NetModule::LanGame& g = games[static_cast<size_t>(game->value)];
        syncNetPlayers();
        std::string error;
        if (!m_net->join(g.address, g.port, &error)) m_lobby->lobby().toast("Can't join: " + error, 5.0f);
        m_lastNetStatus.clear();
    };
    l.addOption(std::move(join));
    syncNetPlayers(); // KKE_NET hosts or joins in the first frame, with these names and colours
}

// This screen's players as the game's players: the first lobby seat is
// NetModule's own player, the others its local players (slots 1..); on
// the host the CPU climbers come after them, so everyone sees them too.
void ClimbRaceModule::syncNetPlayers() {
    if (!m_net) return;
    const std::vector<Entry> ours = wantedRoster();
    int slots = 0;
    for (const Entry& e : ours) {
        if (e.seat < 0 && netClient()) continue; // a client's CPU climbers stay home
        if (slots >= kke::NetModule::kMaxLocalPlayers) break;
        if (slots == 0) {
            m_net->playerName = e.name;
            m_net->playerCharacter = netrace::tintText(e.tint);
        } else {
            m_net->addLocalPlayer(slots, e.name, netrace::tintText(e.tint));
        }
        ++slots;
    }
    for (int s = std::max(1, slots); s < kke::NetModule::kMaxLocalPlayers; ++s) m_net->removeLocalPlayer(s);
}

std::vector<std::string> ClimbRaceModule::onlineNames() const {
    std::vector<std::string> out;
    if (!netHost()) return out;
    for (const kke::net::RemotePlayer& p : m_net->remotePlayers()) out.push_back(p.name.substr(0, p.name.find(" (")));
    return out;
}

std::vector<ClimbRaceModule::Entry> ClimbRaceModule::onlineRoster() const {
    std::vector<Entry> out = wantedRoster();
    int slot = 0;
    for (Entry& e : out) {
        e.netId = slot < kke::NetModule::kMaxLocalPlayers ? m_net->localPlayerId(slot) : 0;
        if (slot > 0 && e.netId == 0) e.netId = -1; // no room online: it races here only
        ++slot;
    }
    for (const kke::net::RemotePlayer& p : m_net->remotePlayers()) {
        Entry e;
        e.name = p.name;
        e.tint = netrace::tintFromText(p.character, glm::vec3(0.8f));
        e.netId = p.id;
        e.remote = true;
        out.push_back(std::move(e));
    }
    return out;
}

void ClimbRaceModule::sendSetup() {
    netrace::Setup s;
    s.mountain = m_mountain;
    s.mode = static_cast<uint8_t>(m_mode);
    s.round = m_round;
    for (const Racer& r : m_racers) {
        if (r.netId < 0) continue;
        s.seats.push_back({ static_cast<uint8_t>(r.netId), static_cast<uint8_t>(r.lane), r.bot && r.seat < 0, r.name, r.tint });
    }
    m_netRound = m_round;
    m_net->sendEvent(netrace::kEventSetup, netrace::encode(s));
    // The countdown waits for everyone else's machine to build it.
    m_netPending.clear();
    for (const Racer& r : m_racers)
        if (r.remote) m_netPending.push_back(r.netId);
    m_netHold = !m_netPending.empty();
    m_netHeld = 0.0f;
    kke::log::get(name())->info("online race {}: {} climbers on {} (seed {})", s.round, s.seats.size(), s.mountain.name, s.mountain.desc.seed);
}

// A client: the host's race. Our own racers are this screen's seats (by
// their player ids), everyone else's are drawn from what they send.
void ClimbRaceModule::applySetup(const netrace::Setup& s) {
    std::vector<netrace::Seat> seats = s.seats;
    std::sort(seats.begin(), seats.end(), [](const netrace::Seat& a, const netrace::Seat& b) { return a.lane < b.lane; });
    const std::vector<int> joined = m_lobby ? m_lobby->lobby().joinedSeats() : std::vector<int>{ 0 };
    std::vector<Entry> roster;
    bool mine = false;
    for (const netrace::Seat& seat : seats) {
        Entry e;
        e.name = seat.name;
        e.tint = seat.tint;
        e.netId = seat.player;
        e.remote = !m_net->isLocalPlayer(seat.player);
        if (!e.remote) {
            for (int slot = 0; slot < kke::NetModule::kMaxLocalPlayers && slot < static_cast<int>(joined.size()); ++slot)
                if (m_net->localPlayerId(slot) == seat.player) e.seat = joined[static_cast<size_t>(slot)];
            if (e.seat < 0) e.remote = true; // not a seat of ours any more
            // Our own players look as they picked here.
            for (const Entry& own : wantedRoster())
                if (!e.remote && own.seat == e.seat) {
                    e.name = own.name;
                    e.tint = own.tint;
                }
            mine = mine || !e.remote;
        }
        roster.push_back(std::move(e));
    }
    if (!mine) {
        kke::log::get(name())->info("online race {}: not in it (joined after it was set up), watching", s.round);
    }
    if (m_lobby && m_lobby->isOpen()) {
        m_lobby->save();
        m_lobby->close();
    }
    if (keyOf(s.mountain) != m_builtKey || m_lanes.size() != roster.size()) {
        useMountain(s.mountain);
        buildMountain(static_cast<int>(roster.size()));
    }
    buildRacers(roster);
    if (m_lobby) {
        m_lobby->applyInput();
        for (Racer& r : m_racers)
            if (r.seat >= 0) r.player = std::max(0, m_lobby->playerOf(r.seat));
    }
    m_rosterChanged = false;
    m_mode = static_cast<Mode>(std::min<int>(s.mode, kModes - 1));
    m_netRound = s.round; // before the reset: the rocks are seeded with it
    resetRace();
    // At the line: the host starts everyone's countdown together (Go).
    netrace::Ready ready{ s.round, {} };
    for (const Racer& r : m_racers)
        if (!r.remote && r.netId >= 0) ready.players.push_back(static_cast<uint8_t>(r.netId));
    m_net->sendEvent(netrace::kEventReady, netrace::encode(ready));
    m_netHold = true;
    kke::log::get(name())->info("online race {}: {} climbers on {} (seed {}, {} of them here)", s.round, roster.size(), s.mountain.name,
                                s.mountain.desc.seed, humans());
}

void ClimbRaceModule::onNetEvent(const kke::net::GameEventMsg& e) {
    if (e.kind == netrace::kEventSetup) {
        if (!netClient() || e.fromPlayer != 0) return; // only the host sets up a race
        if (auto s = netrace::decodeSetup(e.payload)) applySetup(*s);
        else kke::log::get(name())->warn("online: a damaged race setup");
        return;
    }
    if (e.kind == netrace::kEventReady) {
        const auto r = netrace::decodeReady(e.payload);
        if (!netHost() || !r || r->round != m_netRound) return;
        for (uint8_t id : r->players) std::erase(m_netPending, static_cast<int>(id));
        return;
    }
    if (e.kind == netrace::kEventGo) {
        const auto r = netrace::decodeReady(e.payload);
        if (netClient() && e.fromPlayer == 0 && r && r->round == m_netRound && m_netHold) {
            m_netHold = false;
            kke::log::get(name())->info("online race {}: everyone at the line, go", m_netRound);
        }
        return;
    }
    if (e.kind == netrace::kEventOut) {
        // Elimination: the host says who's out, ours included.
        const auto f = netrace::decodeFinish(e.payload);
        if (!netClient() || e.fromPlayer != 0 || !f || f->round != m_netRound) return;
        for (Racer& r : m_racers)
            if (r.netId == f->player) eliminate(r);
        return;
    }
    if (e.kind != netrace::kEventFinish && e.kind != netrace::kEventLoose) return;
    if (netHost()) m_net->relayEvent(e); // everyone else hears it too
    if (e.kind == netrace::kEventFinish) {
        const auto f = netrace::decodeFinish(e.payload);
        if (!f || f->round != m_netRound) return;
        for (Racer& r : m_racers) {
            if (!r.remote || r.netId != f->player || r.finished) continue;
            r.finished = true;
            r.time = f->time;
            if (m_winner.empty()) m_winner = r.name;
            kke::log::get(name())->info("{} topped out in {:.2f} s (online)", r.name, r.time);
        }
    } else if (const auto l = netrace::decodeLoose(e.payload); l && l->lane < m_lanes.size()) {
        m_netApplying = true;
        dropLoose(*m_lanes[l->lane], l->hold, l->push);
        m_netApplying = false;
    }
}

void ClimbRaceModule::netFinished(Racer& r) {
    if (!m_net || !m_net->connected() || r.remote || r.netId < 0) return;
    m_net->sendEvent(netrace::kEventFinish, netrace::encode(netrace::Finish{ static_cast<uint8_t>(r.netId), m_netRound, r.time }));
}

void ClimbRaceModule::netLoose(int lane, int hold, const glm::vec3& push) {
    if (!m_net || !m_net->connected() || m_netApplying || lane < 0 || hold < 0) return;
    m_net->sendEvent(netrace::kEventLoose, netrace::encode(netrace::Loose{ static_cast<uint8_t>(lane), static_cast<uint16_t>(hold), push }));
}

std::string ClimbRaceModule::netStatus() const {
    if (netHost()) {
        const size_t others = m_net->remotePlayers().size();
        std::string text = m_net->statusText(); // "hosting on port 27960"
        if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
        return text + ": " + std::to_string(others) +
               (others == 1 ? " player" : " players") + " online. On another PC or window, set Online to Join.";
    }
    if (netClient()) {
        if (!m_net->connected()) return "Joining: " + m_net->statusText();
        return "Online, " + m_net->statusText() + ": the host starts the race.";
    }
    const kke::Lobby::Option* mode = m_lobby ? m_lobby->lobby().option("net.mode") : nullptr;
    if (mode && mode->value == kModeJoin) return m_net->statusText() == "offline" ? "Looking for games on this network and this PC..." : m_net->statusText();
    // Offline: the mountain picked, then how to join.
    const std::string record = recordText(m_mountain);
    return m_mountain.name + (record.empty() ? std::string() : " (" + record + ")") + ": " + m_mountain.about +
           " Another controller? Press {a} on it to join.";
}

void ClimbRaceModule::updateNet(float dt) {
    if (!m_net) return;
    m_netTime += dt;
    const bool online = m_net->role() != kke::NetModule::Role::Offline;
    // Lost the game (the host left, or we were turned away): back to the menu.
    if (m_wasOnline && !online) {
        m_netHold = false;
        m_netPending.clear();
        if (m_lobby) m_lobby->lobby().toast("Left the online game: " + m_net->statusText(), 6.0f);
        for (size_t i = m_racers.size(); i-- > 0;)
            if (m_racers[i].remote) {
                removeRacer(m_racers[i]);
                m_racers.erase(m_racers.begin() + static_cast<std::ptrdiff_t>(i));
            }
        if (m_lobby && m_phase != Phase::Lobby) backToLobby();
    }
    m_wasOnline = online;

    if (m_lobby) {
        kke::Lobby& l = m_lobby->lobby();
        kke::Lobby::Option* mode = l.option("net.mode");
        kke::Lobby::Option* game = l.option("net.game");
        kke::Lobby::Option* join = l.option("net.join");
        bool rows = false;
        if (mode && game && join) {
            const int want = netHost() ? kModeHost : netClient() ? kModeJoin : mode->value == kModeHost ? kModeOff : mode->value;
            if (mode->value != want) {
                mode->value = want;
                rows = true;
            }
            // Join: the games found, asked again every few seconds.
            const bool looking = !online && mode->value == kModeJoin;
            std::vector<std::string> found;
            if (looking) {
                if (m_netTime >= m_netSearchAt) {
                    m_net->searchLan();
                    m_netSearchAt = m_netTime + kSearchEvery;
                }
                for (const kke::NetModule::LanGame& g : m_net->lanGames())
                    if (g.ours) found.push_back(g.hostName + " (" + g.players + ")");
                if (found.empty()) found.push_back(m_net->searchingLan() ? "Searching..." : "None found yet");
            }
            const bool any = looking && found.front() != "Searching..." && found.front() != "None found yet";
            if (looking && game->choices != found) {
                game->choices = found;
                game->value = std::clamp(game->value, 0, static_cast<int>(found.size()) - 1);
                rows = true;
            }
            if (game->visible != looking || join->visible != any) {
                game->visible = looking;
                join->visible = any;
                rows = true;
            }
            if (any) {
                const std::string label = "Join " + game->choices[static_cast<size_t>(game->value)];
                if (join->label != label) {
                    join->label = label;
                    rows = true;
                }
            }
        }
        const std::string status = netStatus();
        if (rows || status != m_lastNetStatus) {
            m_lastNetStatus = status;
            m_lobby->setTitle("CLIMB RACE", status); // also redraws the rows
        }
    }
    if (!online) return;
    syncNetPlayers();

    if (netHost()) {
        // Each race started here goes to everyone.
        if (m_phase != Phase::Lobby && m_round != m_sentRound) {
            m_sentRound = m_round;
            if (!m_net->remotePlayers().empty()) sendSetup(); // nobody to tell: a joiner gets the next race
        }
        // Everyone at the line (or a machine that's taking too long): go.
        if (m_netHold) {
            m_netHeld += dt;
            if (m_netPending.empty() || m_netHeld > 8.0f) {
                if (!m_netPending.empty()) kke::log::get(name())->info("online: {} climbers not ready after 8 s, starting anyway", m_netPending.size());
                m_netHold = false;
                m_netPending.clear();
                m_net->sendEvent(netrace::kEventGo, netrace::encode(netrace::Ready{ m_netRound, {} }));
                kke::log::get(name())->info("online race {}: everyone at the line, go", m_netRound);
            }
        }
        // Tests and demos: start once enough others are in.
        if (m_netWait > 0 && static_cast<int>(m_net->remotePlayers().size()) >= m_netWait) {
            m_netWait = 0;
            kke::log::get(name())->info("online: {} players in, starting", m_net->remotePlayers().size());
            startFromLobby();
        }
    }

    // Everyone else's climbers, where their machines say they are.
    kke::RigidWorld& w = m_rigid->world();
    const std::vector<kke::net::RemotePlayer>& remote = m_net->remotePlayers();
    for (size_t i = m_racers.size(); i-- > 0;) {
        Racer& r = m_racers[i];
        if (!r.remote) continue;
        const auto it = std::find_if(remote.begin(), remote.end(), [&r](const kke::net::RemotePlayer& p) { return p.id == r.netId; });
        if (it == remote.end()) {
            // Gone (left the game): their face stays empty.
            kke::log::get(name())->info("{} left the online race", r.name);
            removeRacer(r);
            m_racers.erase(m_racers.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        if (!it->hasState) continue;
        const netrace::Pose p = netrace::fromState(it->state);
        using LS = kke::Locomotion::State;
        const bool air = !p.climbing && p.loco == static_cast<uint8_t>(LS::Air);
        const bool wasGround = !r.net.climbing && r.net.loco == static_cast<uint8_t>(LS::Ground);
        const bool ground = !p.climbing && p.loco == static_cast<uint8_t>(LS::Ground);
        r.netJumped = wasGround && air && p.velocity.y > 1.0f;
        r.netLanded = !wasGround && ground;
        r.netStateTime = p.loco == r.netLoco && p.climbing == r.net.climbing ? r.netStateTime + dt : 0.0f;
        r.netLoco = p.loco;
        r.net = p;
        w.moveCharacter(r.id, p.feet);
    }
}

// Our racers' poses, for everyone else (after they moved and were posed).
void ClimbRaceModule::sendNet() {
    if (!m_net || !m_net->connected() || m_phase == Phase::Lobby) return;
    const kke::RigidWorld& w = m_rigid->world();
    int slot = -1;
    for (const Racer& r : m_racers) {
        if (r.remote || r.netId < 0) continue;
        slot = -1;
        for (int s = 0; s < kke::NetModule::kMaxLocalPlayers; ++s)
            if (m_net->localPlayerId(s) == r.netId && (s == 0 || r.netId != 0)) slot = s;
        if (slot < 0) continue;
        const BodyInput b = bodyInput(r);
        netrace::Pose p;
        p.feet = b.feet;
        p.yaw = b.yaw;
        p.loco = static_cast<uint8_t>(b.loco);
        p.climbing = b.climbing;
        p.mantle = b.mantle;
        p.finished = r.finished;
        p.mantleProgress = b.mantleProgress;
        p.groundSpeed = b.groundSpeed;
        p.fallHeight = b.fallHeight;
        p.velocity = w.characterVelocity(r.id);
        for (int s = 0; s < 2; ++s) {
            p.grip[s] = b.grip[s];
            p.normal[s] = b.normal[s];
            p.closed[s] = b.closed[s];
            p.onRock[s] = b.onRock[s];
            p.held[s] = b.held[s];
            p.foot[s] = b.foot[s];
        }
        p.hips = b.hips;
        m_net->setLocalPlayer(slot, netrace::toState(p));
    }
}

} // namespace climb_race
