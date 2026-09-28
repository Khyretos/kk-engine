// Racing online (README.md "Online", docs/NETWORKING.md): player 1 sets
// Online to Host or Join in the start menu; everyone on each screen
// drives (a second player on a screen is a NetModule local player). The
// host decides the race: its Setup event says which track and who starts
// where, and starts every set of lights. Each machine drives its own cars
// and sends their poses (NetRace.h); the others' cars are solid copies
// moved to where they say. The host's CPU cars go to everyone in one
// event a few times a second. Dents go to everyone as they happen.

#include "RacingModule.h"

#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/RigidBodyModule.h"


#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace racing {

namespace {
constexpr int kModeOff = 0, kModeJoin = 1, kModeHost = 2; // Join first: going to Host passes a harmless search, not a game others see
constexpr float kSearchEvery = 3.0f;                      // s between LAN searches while Join is picked
constexpr float kCpuEvery = 1.0f / 15.0f;                 // s between the host's CPU car messages
} // namespace

bool RacingModule::netClient() const { return m_net && m_net->role() == kke::NetModule::Role::Client; }
bool RacingModule::netHost() const { return m_net && m_net->role() == kke::NetModule::Role::Host; }
bool RacingModule::netConnected() const { return m_net && m_net->connected(); }

void RacingModule::setupNet() {
    if (!m_net) return;
    m_net->standIns = false; // the cars are their own solid copies (updateRemoteCar)
    m_net->checkMoves = false; // a car is faster than any walking limit
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
    if (const char* wait = kke::dev::env("KKE_RACE_WAIT")) m_netWait = std::max(0, std::atoi(wait));
    if (!m_lobby) {
        syncNetPlayers();
        return;
    }
    kke::Lobby& l = m_lobby->lobby();
    kke::Lobby::Option mode{ "net.mode", "Online", { "Off", "Join", "Host" }, kModeOff, true, {}, {} };
    mode.onChange = [this](int value) {
        std::string error;
        m_wasOnline = false; // leaving on purpose: no "left the game" toast
        if (value == kModeOff) {
            m_net->leave();
        } else if (value == kModeHost) {
            m_net->leave();
            syncNetPlayers();
            if (!m_net->host(0, &error)) {
                m_lobby->lobby().toast("Can't host: " + error, 5.0f);
                if (kke::Lobby::Option* o = m_lobby->lobby().option("net.mode")) o->value = kModeOff;
            }
        } else {
            m_net->leave();
            m_net->searchLan();
            m_netSearchAt = m_netTime + kSearchEvery;
        }
        m_lastNetStatus.clear();
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
    syncNetPlayers(); // KKE_NET hosts or joins in the first frame, with these names
}

// This screen's drivers as the game's players: the first lobby seat is
// NetModule's own player, the others its local players (slots 1..). The
// host's CPU cars aren't players: they go to everyone in kEventCpu.
void RacingModule::syncNetPlayers() {
    if (!m_net) return;
    const std::vector<Entry> ours = wantedRoster();
    int slots = 0;
    for (const Entry& e : ours) {
        if (e.cpu || e.seat < 0) continue;
        if (slots >= kke::NetModule::kMaxLocalPlayers) break;
        const std::string car = std::to_string(e.type) + "/" + std::to_string(e.kit) + "/" + std::to_string(e.paint);
        if (slots == 0) {
            m_net->playerName = e.name;
            m_net->playerCharacter = car;
        } else {
            m_net->addLocalPlayer(slots, e.name, car);
        }
        ++slots;
    }
    for (int s = std::max(1, slots); s < kke::NetModule::kMaxLocalPlayers; ++s) m_net->removeLocalPlayer(s);
}

std::vector<RacingModule::Entry> RacingModule::onlineRoster() const {
    // Our players first (with their network ids), then everyone else's,
    // then the CPU cars filling the field.
    std::vector<Entry> ours = wantedRoster();
    std::vector<Entry> out, cpus;
    int slot = 0;
    for (Entry& e : ours) {
        if (e.cpu) {
            cpus.push_back(std::move(e));
            continue;
        }
        e.netId = slot < kke::NetModule::kMaxLocalPlayers ? m_net->localPlayerId(slot) : -1;
        ++slot;
        out.push_back(std::move(e));
    }
    for (const kke::net::RemotePlayer& p : m_net->remotePlayers()) {
        Entry e;
        e.name = p.name;
        e.netId = p.id;
        e.remote = true;
        // Their car, as they picked it ("type/kit/paint").
        int type = 0, kit = 0, paint = 0;
        if (std::sscanf(p.character.c_str(), "%d/%d/%d", &type, &kit, &paint) == 3) {
            e.type = std::clamp(type, 0, static_cast<int>(carTypes().size()) - 1);
            e.kit = std::clamp(kit, 0, kKits - 1);
            e.paint = std::clamp(paint, 0, static_cast<int>(paints().size()) - 1);
        }
        out.push_back(std::move(e));
    }
    const int field = std::max(chosenCars(), static_cast<int>(out.size()));
    for (Entry& e : cpus)
        if (static_cast<int>(out.size()) < field) out.push_back(std::move(e));
    return out;
}

void RacingModule::sendSetup() {
    netrace::Setup s;
    s.track = m_track->desc().id;
    s.round = m_round;
    s.laps = static_cast<uint8_t>(std::clamp(m_laps, 1, 99));
    s.damage = static_cast<uint8_t>(m_damage);
    for (size_t i = 0; i < m_cars.size(); ++i) {
        const Car& c = m_cars[i];
        netrace::Seat seat;
        seat.player = c.netId >= 0 ? static_cast<uint8_t>(c.netId) : netrace::kCpu;
        seat.slot = static_cast<uint8_t>(i);
        seat.type = static_cast<uint8_t>(c.type);
        seat.kit = static_cast<uint8_t>(c.kit);
        seat.paint = static_cast<uint8_t>(c.paint);
        seat.skill = static_cast<uint8_t>(c.skill);
        seat.name = c.name;
        s.seats.push_back(std::move(seat));
    }
    m_netRound = m_round;
    m_net->sendEvent(netrace::kEventSetup, netrace::encode(s));
    // The lights wait for everyone else's machine to build the grid.
    m_netPending.clear();
    for (const Car& c : m_cars)
        if (c.remote) m_netPending.push_back(c.netId);
    m_netHold = !m_netPending.empty();
    m_netHeld = 0.0f;
    kke::log::get(name())->info("online race {}: {} cars on {}", s.round, s.seats.size(), m_track->desc().name);
}

// A client: the host's race. Our cars are this screen's seats (by their
// player ids); everyone else's, the host's CPU cars included, are copies.
void RacingModule::applySetup(const netrace::Setup& s) {
    int track = -1;
    for (size_t i = 0; i < m_tracks.size(); ++i)
        if (m_tracks[i].id == s.track) track = static_cast<int>(i);
    if (track < 0) {
        kke::log::get(name())->warn("online: the host's track '{}' isn't in this game's tracks folder", s.track);
        if (m_lobby) m_lobby->lobby().toast("The host's track (" + s.track + ") isn't here: update the game", 6.0f);
        return;
    }
    const std::vector<int> joined = m_lobby ? m_lobby->lobby().joinedSeats() : std::vector<int>{ 0 };
    const std::vector<Entry> own = wantedRoster();
    std::vector<netrace::Seat> seats = s.seats;
    std::sort(seats.begin(), seats.end(), [](const netrace::Seat& a, const netrace::Seat& b) { return a.slot < b.slot; });
    std::vector<Entry> roster;
    for (const netrace::Seat& seat : seats) {
        Entry e;
        e.name = seat.name;
        e.type = seat.type;
        e.kit = seat.kit;
        e.paint = seat.paint;
        e.skill = seat.skill;
        e.netId = seat.player == netrace::kCpu ? -1 : seat.player;
        e.remote = seat.player == netrace::kCpu || !m_net->isLocalPlayer(seat.player);
        if (!e.remote) {
            for (int slot = 0; slot < kke::NetModule::kMaxLocalPlayers && slot < static_cast<int>(joined.size()); ++slot)
                if (m_net->localPlayerId(slot) == seat.player) e.seat = joined[static_cast<size_t>(slot)];
            if (e.seat < 0) e.remote = true; // not a seat of ours any more
        }
        roster.push_back(std::move(e));
    }
    if (m_lobby && m_lobby->isOpen()) {
        m_lobby->save();
        m_lobby->close();
    }
    if (m_tracks[static_cast<size_t>(track)].id != m_builtTrack) buildTrack(m_tracks[static_cast<size_t>(track)]);
    m_forceLaps = s.laps;
    m_forceDamage = s.damage;
    buildRace(roster);
    if (m_lobby) {
        m_lobby->applyInput();
        for (Car& c : m_cars)
            if (c.seat >= 0) c.player = std::max(0, m_lobby->playerOf(c.seat));
    }
    m_netRound = s.round;
    resetRace();
    // On the grid: the host starts everyone's lights together (Go).
    netrace::Ready ready{ s.round, {} };
    for (size_t i = 0; i < m_cars.size(); ++i)
        if (!m_cars[i].remote) ready.slots.push_back(static_cast<uint8_t>(i));
    m_net->sendEvent(netrace::kEventReady, netrace::encode(ready));
    m_netHold = true;
    kke::log::get(name())->info("online race {}: {} cars on {} ({} of them here)", s.round, m_cars.size(), s.track, humans());
}

void RacingModule::onNetEvent(const kke::net::GameEventMsg& e) {
    if (e.kind == netrace::kEventSetup) {
        if (!netClient() || e.fromPlayer != 0) return; // only the host sets up a race
        if (auto s = netrace::decodeSetup(e.payload)) applySetup(*s);
        else kke::log::get(name())->warn("online: a damaged race setup");
        return;
    }
    if (e.kind == netrace::kEventReady) {
        const auto r = netrace::decodeReady(e.payload);
        if (!netHost() || !r || r->round != m_netRound) return;
        std::erase(m_netPending, static_cast<int>(e.fromPlayer));
        return;
    }
    if (e.kind == netrace::kEventGo) {
        const auto r = netrace::decodeReady(e.payload);
        if (netClient() && e.fromPlayer == 0 && r && r->round == m_netRound && m_netHold) m_netHold = false;
        return;
    }
    if (e.kind == netrace::kEventCpu) {
        const auto cpu = netrace::decodeCpuCars(e.payload);
        if (!netClient() || e.fromPlayer != 0 || !cpu || cpu->round != m_netRound) return;
        for (const netrace::CpuCar& cc : cpu->cars) {
            if (cc.slot >= m_cars.size()) continue;
            Car& c = m_cars[cc.slot];
            if (!c.remote) continue;
            c.net = cc.pose;
            c.hasNet = true;
            c.health = cc.pose.health;
            c.totalled = cc.pose.totalled;
            c.finished = cc.pose.finished;
            c.lap = cc.pose.lap;
        }
        return;
    }
    if (e.kind != netrace::kEventFinish && e.kind != netrace::kEventHit) return;
    if (netHost()) m_net->relayEvent(e); // everyone else hears it too
    if (e.kind == netrace::kEventFinish) {
        const auto f = netrace::decodeFinish(e.payload);
        if (!f || f->round != m_netRound || f->slot >= m_cars.size()) return;
        Car& c = m_cars[f->slot];
        if (!c.remote || c.finished) return;
        c.finished = true;
        c.finishTime = f->time;
        if (m_leaderDone < 0.0f) m_leaderDone = 0.0f;
        if (m_winner.empty() && event() != Event::Drift) m_winner = c.name;
        kke::log::get(name())->info("{} finished in {} (online)", c.name, clockText(c.finishTime));
    } else if (const auto h = netrace::decodeHit(e.payload); h && h->round == m_netRound && h->slot < m_cars.size()) {
        Car& c = m_cars[h->slot];
        if (!c.remote) return;
        m_netApplying = true;
        dent(c, h->point, h->direction, h->depth);
        m_netApplying = false;
    }
}

void RacingModule::netFinished(const Car& c) {
    if (!netConnected() || c.remote) return;
    m_net->sendEvent(netrace::kEventFinish,
                     netrace::encode(netrace::Finish{ static_cast<uint8_t>(&c - m_cars.data()), m_netRound, c.finishTime }));
}

void RacingModule::netHit(const Car& c, const glm::vec3& localPoint, const glm::vec3& localDir, float depth) {
    if (!netConnected() || c.remote || m_netApplying) return;
    netrace::Hit h;
    h.slot = static_cast<uint8_t>(&c - m_cars.data());
    h.round = m_netRound;
    h.point = localPoint;
    h.direction = localDir;
    h.depth = depth;
    m_net->sendEvent(netrace::kEventHit, netrace::encode(h));
}

netrace::CarPose RacingModule::poseOf(const Car& c) const {
    netrace::CarPose p;
    p.position = carPosition(c);
    p.rotation = glm::quat_cast(glm::mat3(c.xf));
    p.velocity = c.velocity;
    p.speed = carSpeed(c);
    p.steer = c.input.steer;
    const CarType& type = carTypes()[static_cast<size_t>(c.type)];
    p.rpm = c.state.rpm / type.maxRpm;
    p.gear = c.state.gear;
    p.health = c.health;
    p.lap = std::max(0, c.lap);
    p.progress = std::max(0.0f, c.progress);
    p.finished = c.finished;
    p.totalled = c.totalled;
    p.braking = c.input.brake > 0.2f;
    p.handBrake = c.input.handBrake > 0.5f;
    // Which tyres smoke: the same test as the effect (Damage.cpp), roughly.
    for (size_t w = 0; w < c.state.wheels.size() && w < 4; ++w) {
        const kke::VehicleWheelState& ws = c.state.wheels[w];
        if (ws.contact && (std::fabs(ws.lateralSlip) > 12.0f || std::fabs(ws.angularVelocity * c.art->wheelRadius - p.speed) > 4.0f))
            p.smoke = static_cast<uint8_t>(p.smoke | (1u << w));
    }
    return p;
}

std::string RacingModule::netStatus() const {
    if (netHost()) {
        const size_t others = m_net->remotePlayers().size();
        std::string text = m_net->statusText();
        if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
        return text + ": " + std::to_string(others) + (others == 1 ? " player" : " players") + " online. On another PC or window, set Online to Join.";
    }
    if (netClient()) {
        if (!m_net->connected()) return "Joining: " + m_net->statusText();
        return "Online, " + m_net->statusText() + ": the host starts the race.";
    }
    const kke::Lobby::Option* mode = m_lobby ? m_lobby->lobby().option("net.mode") : nullptr;
    if (mode && mode->value == kModeJoin) return m_net->statusText() == "offline" ? "Looking for games on this network and this PC..." : m_net->statusText();
    const TrackDesc& t = m_tracks[static_cast<size_t>(chosenTrack())];
    std::string text = t.name + ": " + t.about + " Another controller? Press {a} on it to join.";
    if (!m_garage->hasPack()) text += " (Street Racer pack not found: block cars.)";
    return text;
}

void RacingModule::updateNet(float dt) {
    m_netTime += dt;
    if (!m_net) {
        if (m_lobby && m_lastNetStatus.empty()) {
            m_lastNetStatus = "offline";
            const TrackDesc& t = m_tracks[static_cast<size_t>(chosenTrack())];
            m_lobby->setTitle("RACING", t.name + ": " + t.about);
        }
        return;
    }
    const bool online = m_net->role() != kke::NetModule::Role::Offline;
    // Lost the game (the host left, or we were turned away): back to the menu.
    if (m_wasOnline && !online) {
        m_netHold = false;
        m_netPending.clear();
        m_forceLaps = m_forceDamage = -1;
        if (m_lobby) m_lobby->lobby().toast("Left the online game: " + m_net->statusText(), 6.0f);
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
            m_lobby->setTitle("RACING", status);
        }
    }
    if (!online) return;
    syncNetPlayers();

    if (netHost()) {
        if (m_phase != Phase::Lobby && m_round != m_sentRound) {
            m_sentRound = m_round;
            if (!m_net->remotePlayers().empty()) sendSetup();
        }
        if (m_netHold) {
            m_netHeld += dt;
            if (m_netPending.empty() || m_netHeld > 8.0f) {
                if (!m_netPending.empty()) kke::log::get(name())->info("online: {} machines not ready after 8 s, starting anyway", m_netPending.size());
                m_netHold = false;
                m_netPending.clear();
                m_net->sendEvent(netrace::kEventGo, netrace::encode(netrace::Ready{ m_netRound, {} }));
            }
        }
        if (m_netWait > 0 && static_cast<int>(m_net->remotePlayers().size()) >= m_netWait) {
            m_netWait = 0;
            kke::log::get(name())->info("online: {} players in, starting", m_net->remotePlayers().size());
            startFromLobby();
        }
    }

    // Everyone else's cars, where their machines say they are.
    const std::vector<kke::net::RemotePlayer>& remote = m_net->remotePlayers();
    for (Car& c : m_cars) {
        if (!c.remote || c.netId < 0) continue;
        const auto it = std::find_if(remote.begin(), remote.end(), [&c](const kke::net::RemotePlayer& p) { return p.id == c.netId; });
        if (it == remote.end()) {
            if (!c.totalled) kke::log::get(name())->info("{} left the online race", c.name);
            c.totalled = true; // gone: parked where it was
            continue;
        }
        if (!it->hasState) continue;
        c.net = netrace::fromState(it->state);
        c.hasNet = true;
        c.health = c.net.health;
        c.totalled = c.net.totalled;
        c.lap = c.net.lap;
        if (c.net.finished && !c.finished) {
            c.finished = true;
            c.finishTime = m_raceClock;
        }
    }
}

// Our cars' poses for everyone else; on the host, the CPU cars too.
void RacingModule::sendNet() {
    if (!netConnected() || m_phase == Phase::Lobby) return;
    for (const Car& c : m_cars) {
        if (c.remote || c.netId < 0) continue;
        for (int s = 0; s < kke::NetModule::kMaxLocalPlayers; ++s)
            if (m_net->localPlayerId(s) == c.netId) {
                m_net->setLocalPlayer(s, netrace::toState(poseOf(c)));
                break;
            }
    }
    if (!netHost() || m_netTime < m_cpuSendAt) return;
    m_cpuSendAt = m_netTime + kCpuEvery;
    netrace::CpuCars msg;
    msg.round = m_netRound;
    for (size_t i = 0; i < m_cars.size(); ++i)
        if (!m_cars[i].remote && m_cars[i].netId < 0) msg.cars.push_back({ static_cast<uint8_t>(i), poseOf(m_cars[i]) });
    if (!msg.cars.empty()) m_net->sendEvent(netrace::kEventCpu, netrace::encode(msg));
}

} // namespace racing
