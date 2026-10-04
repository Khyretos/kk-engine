// Online (README.md "Online", docs/NETWORKING.md), like Climb Race:
// player 1 sets Online to Host or Join in the start menu; everyone at
// each screen flies (a second person at a screen is a NetModule local
// player). The host decides the flight: its Setup event says which
// island, mode, laps and rings, and who starts where, and starts every
// countdown. Each screen flies its own planes and sends where they are
// (FlyNet.h); the others' planes are drawn from that. CPU pilots are the
// host's: they go to everyone as the host's local players.
//
// Which controller flies which plane stays on each screen: moving to
// another controller in the pause menu changes nothing online.

#include "FlyingModule.h"

#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace flying {

namespace {
constexpr int kModeOff = 0, kModeJoin = 1, kModeHost = 2;
constexpr float kSearchEvery = 3.0f;
} // namespace

bool FlyingModule::netClient() const { return m_net && m_net->role() == kke::NetModule::Role::Client; }
bool FlyingModule::netHost() const { return m_net && m_net->role() == kke::NetModule::Role::Host; }

void FlyingModule::setupNet() {
    if (!m_net) return;
    m_net->standIns = false;   // planes, not walking characters
    m_net->checkMoves = false; // nothing to walk through up there; the speed limits below still hold every move
    // A stunt plane: up to 88 m/s in a dive, climbing hard out of a loop.
    kke::net::MovementLimits limits;
    limits.horizontalSpeed = 95.0f;
    limits.riseSpeed = 95.0f;
    limits.fallSpeed = 120.0f;
    limits.slack = 4.0f;
    limits.teleportCooldown = 1.0; // a crash puts the plane back (kRespawn is 2.5 s)
    m_net->movementLimits = limits;
    m_net->addEventListener([this](const kke::net::GameEventMsg& e) { onNetEvent(e); });
    m_net->onPlayer = [this](uint8_t id, bool joined) {
        if (!netHost() || !m_lobby) return;
        std::string who = "Player " + std::to_string(id);
        for (const kke::net::RemotePlayer& p : m_net->remotePlayers())
            if (p.id == id) who = p.name;
        if (joined) m_lobby->lobby().toast(who + (m_phase == Phase::Lobby ? " joined online" : " joined online: in from the next flight"), 5.0f);
    };
    if (const char* who = kke::dev::env("KKE_NET_NAME")) m_netName = who;
    if (!m_lobby) {
        syncNetPlayers();
        return;
    }
    kke::Lobby& l = m_lobby->lobby();
    kke::Lobby::Option mode{ "net.mode", "Online", { "Off", "Join", "Host" }, kModeOff, true, {}, {} };
    mode.onChange = [this](int value) {
        std::string error;
        m_wasOnline = false;
        m_net->leave();
        if (value == kModeHost) {
            syncNetPlayers();
            if (!m_net->host(0, &error)) {
                m_lobby->lobby().toast("Can't host: " + error, 5.0f);
                if (kke::Lobby::Option* o = m_lobby->lobby().option("net.mode")) o->value = kModeOff;
            }
        } else if (value == kModeJoin) {
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
    // Or type it: the host's address (a VPN's too, where the LAN search
    // finds nothing) or a join code. Remembered in the lobby file.
    l.addTextOption("net.address", "Address or code", "type the host's IP or join code", [this](const std::string& typed) {
        if (typed.empty()) return;
        m_lobby->save();
        syncNetPlayers();
        std::string error;
        if (!m_net->joinTyped(typed, kke::NetModule::kDefaultPort, &error)) m_lobby->lobby().toast("Can't join: " + error, 5.0f);
        m_lastNetStatus.clear();
    });
    if (kke::Lobby::Option* typed = l.option("net.address")) typed->visible = false;
    syncNetPlayers();
}

// This screen's pilots as network players: the first seat is NetModule's
// own player, the others its local players; on the host the CPU pilots
// follow, so everyone sees them.
void FlyingModule::syncNetPlayers() {
    if (!m_net) return;
    int slots = 0;
    for (const Entry& e : wantedRoster()) {
        if (e.cpu && netClient()) continue; // a client's CPU pilots stay home
        if (slots >= kke::NetModule::kMaxLocalPlayers) break;
        // Its lobby look (every menu shows it), the exact paint and colour:
        // picking another in the menu tells everyone.
        const std::string character = net::characterText(kke::Lobby::lookText(e.look), e.cpu, e.livery, e.tint);
        if (slots == 0) {
            m_net->playerName = e.name;
            m_net->playerCharacter = character;
        } else {
            m_net->addLocalPlayer(slots, e.name, character);
        }
        ++slots;
    }
    for (int s = std::max(1, slots); s < kke::NetModule::kMaxLocalPlayers; ++s) m_net->removeLocalPlayer(s);
}

std::vector<FlyingModule::Entry> FlyingModule::onlineRoster() const {
    std::vector<Entry> out;
    const std::vector<Entry> ours = wantedRoster();
    for (size_t slot = 0; slot < ours.size(); ++slot) {
        Entry e = ours[slot];
        e.netId = slot < static_cast<size_t>(kke::NetModule::kMaxLocalPlayers) ? m_net->localPlayerId(static_cast<int>(slot)) : 0;
        if (slot > 0 && e.netId == 0) e.netId = -1;
        if (e.netId < 0 && e.cpu) continue; // no room online for this CPU pilot: it sits this one out
        out.push_back(std::move(e));
    }
    for (const kke::net::RemotePlayer& p : m_net->remotePlayers()) {
        Entry e;
        e.name = p.name;
        e.tint = net::tintOfCharacter(p.character, glm::vec3(0.8f));
        e.netId = p.id;
        e.remote = true;
        e.livery = net::liveryOfCharacter(p.character, p.id % 4); // the paint they picked
        out.push_back(std::move(e));
    }
    return out;
}

void FlyingModule::sendSetup() {
    if (!m_net || !netHost()) return;
    net::Setup s;
    s.seed = m_seed;
    s.round = m_round;
    s.mode = static_cast<uint8_t>(m_mode);
    s.laps = static_cast<uint8_t>(m_laps);
    s.rings = static_cast<uint8_t>(m_ringCount);
    s.ringRadius = m_ringRadius;
    s.mood = m_mood;
    s.killsToWin = static_cast<uint8_t>(std::clamp(m_killsToWin, 1, 63));
    for (const Pilot& p : m_pilots) {
        if (p.netId < 0) continue;
        s.seats.push_back({ static_cast<uint8_t>(p.netId), static_cast<uint8_t>(p.slot), p.cpu, static_cast<uint8_t>(p.skill),
                            static_cast<uint8_t>(p.livery), p.name, p.tint });
    }
    m_net->sendEvent(net::kEventSetup, net::encode(s));
    kke::log::get(name())->info("online flight {}: {} planes on island {}", s.round, s.seats.size(), s.seed);
}

// A client: the host's flight. Our own planes are this screen's seats
// (found by their player ids); the others are drawn from what they send.
void FlyingModule::applySetup(const net::Setup& s) {
    const std::vector<int> joined = m_lobby ? m_lobby->lobby().joinedSeats() : std::vector<int>{ 0 };
    const std::vector<Entry> ours = wantedRoster();
    std::vector<Entry> roster;
    for (const net::Seat& seat : s.seats) {
        Entry e;
        e.name = seat.name;
        e.tint = seat.tint;
        e.livery = seat.livery;
        e.skill = seat.skill;
        e.slot = seat.slot;
        e.netId = seat.player;
        e.cpu = seat.cpu; // the host's CPU pilot: flown there, named so here
        e.remote = !m_net->isLocalPlayer(seat.player);
        if (!e.remote) {
            for (int slot = 0; slot < kke::NetModule::kMaxLocalPlayers && slot < static_cast<int>(joined.size()); ++slot)
                if (m_net->localPlayerId(slot) == seat.player) e.seat = joined[static_cast<size_t>(slot)];
            if (e.seat < 0) e.remote = true;
            for (const Entry& own : ours)
                if (!e.remote && own.seat == e.seat) {
                    e.name = own.name;
                    e.tint = own.tint;
                    e.livery = own.livery;
                }
        }
        roster.push_back(std::move(e));
    }
    std::sort(roster.begin(), roster.end(), [](const Entry& a, const Entry& b) { return a.slot < b.slot; });
    if (m_lobby && m_lobby->isOpen()) {
        m_lobby->save();
        m_lobby->close();
    }
    m_mode = static_cast<Mode>(std::min<int>(s.mode, kModes - 1));
    m_seed = s.seed;
    m_laps = std::clamp<int>(s.laps, 1, 7);
    m_ringCount = std::clamp<int>(s.rings, 3, 64);
    m_ringRadius = std::clamp(s.ringRadius, 6.0f, 30.0f);
    m_killsToWin = std::clamp<int>(s.killsToWin, 1, 63);
    if (!s.mood.empty() && s.mood != m_mood) {
        m_mood = s.mood;
        m_app->setMood(m_mood);
    }
    closePause();
    buildPilots(roster);
    // Start positions are the host's: its slot numbers.
    for (size_t i = 0; i < m_pilots.size(); ++i) m_pilots[i].slot = roster[i].slot;
    if (m_lobby) {
        m_lobby->applyInput();
        for (Pilot& p : m_pilots)
            if (p.seat >= 0) p.player = std::max(0, m_lobby->playerOf(p.seat));
    }
    m_phase = Phase::Countdown;
    newFlight();
    m_phase = Phase::Countdown;
    m_round = s.round;
    kke::log::get(name())->info("online flight {}: {} planes on island {}", s.round, roster.size(), s.seed);
}

void FlyingModule::onNetEvent(const kke::net::GameEventMsg& e) {
    if (e.kind == net::kEventSetup) {
        if (!netClient() || e.fromPlayer != 0) return; // only the host sets up a flight
        if (auto s = net::decodeSetup(e.payload)) applySetup(*s);
        else kke::log::get(name())->warn("online: a damaged flight setup");
        return;
    }
    if (e.kind != net::kEventDamage && e.kind != net::kEventDent && e.kind != net::kEventDown) return;
    if (netHost()) m_net->relayEvent(e); // everyone else hears it too
    if (e.kind == net::kEventDamage) {
        if (auto d = net::decodeDamage(e.payload)) onDamage(*d);
    } else if (e.kind == net::kEventDent) {
        if (auto d = net::decodeDent(e.payload)) {
            if (m_net->isLocalPlayer(d->plane)) return; // ours: dented already
            onDent(*d);
        }
    } else if (auto d = net::decodeDown(e.payload)) {
        if (m_net->isLocalPlayer(d->plane)) return; // ours: counted already
        onDown(*d);
    }
}

std::string FlyingModule::netStatus() const {
    if (netHost()) {
        const size_t others = m_net->remotePlayers().size();
        std::string text = m_net->statusText();
        if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
        return text + ": " + std::to_string(others) + (others == 1 ? " player" : " players") + " online. On another PC or window, set Online to Join.";
    }
    if (netClient()) {
        if (!m_net->connected()) return "Joining: " + m_net->statusText();
        return "Online, " + m_net->statusText() + ": the host starts the flight.";
    }
    const kke::Lobby::Option* mode = m_lobby ? m_lobby->lobby().option("net.mode") : nullptr;
    if (mode && mode->value == kModeJoin) return m_net->statusText() == "offline" ? "Looking for games on this network and this PC..." : m_net->statusText();
    return "Pick your pilot and plane. A controller, a flight stick or the keyboard: press {a}, the trigger or Enter to join.";
}

void FlyingModule::updateNet(float dt) {
    if (!m_net) return;
    m_netTime += dt;
    const bool online = m_net->role() != kke::NetModule::Role::Offline;
    if (m_wasOnline && !online) {
        if (m_lobby) m_lobby->lobby().toast("Left the online game: " + m_net->statusText(), 6.0f);
        std::erase_if(m_pilots, [this](Pilot& p) {
            if (p.remote) removeArt(p);
            return p.remote;
        });
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
            if (kke::Lobby::Option* typed = l.option("net.address"); typed && typed->visible != looking) {
                typed->visible = looking;
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
            m_lobby->setTitle("STUNT PLANES", status);
        }
    }
    if (!online) return;
    syncNetPlayers();
    // The host: each flight it starts goes to everyone.
    if (netHost() && m_phase != Phase::Lobby && m_round != m_sentRound) {
        m_sentRound = m_round;
        if (!m_net->remotePlayers().empty()) sendSetup();
    }

    // Tests and demos (KKE_FLY_WAIT=n): start once n others are in.
    if (netHost() && m_netWait > 0 && static_cast<int>(m_net->remotePlayers().size()) >= m_netWait) {
        m_netWait = 0;
        kke::log::get(name())->info("online: {} players in, starting", m_net->remotePlayers().size());
        startFromLobby();
    }

    // Everyone else's planes, where their screens say they are.
    const std::vector<kke::net::RemotePlayer>& remote = m_net->remotePlayers();
    for (size_t i = m_pilots.size(); i-- > 0;) {
        Pilot& p = m_pilots[i];
        if (!p.remote) continue;
        const auto it = std::find_if(remote.begin(), remote.end(), [&p](const kke::net::RemotePlayer& r) { return r.id == p.netId; });
        if (it == remote.end()) {
            kke::log::get(name())->info("{} left the online flight", p.name);
            removeArt(p);
            m_pilots.erase(m_pilots.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        if (!it->hasState) continue;
        const net::Plane plane = net::fromState(it->state);
        if (!p.netSeen) p.net.position = plane.position; // no slide from the origin
        p.netSeen = true;
        const bool jump = plane.teleported || glm::length(plane.position - p.net.position) > 150.0f;
        p.previous = jump ? plane.position : p.net.position;
        // It went down: its explosion here too. Back up: a new plane, no dents.
        if (plane.crashed && !p.wasDown && m_phase != Phase::Lobby) explode(plane.position, plane.velocity, p.tint);
        if (!plane.crashed && p.wasDown) clearDents(p);
        p.wasDown = plane.crashed;
        p.net = plane;
        p.plane.position = plane.position;
        p.plane.velocity = plane.velocity;
        p.plane.rotation = plane.rotation;
        p.drawnRotation = jump ? plane.rotation : glm::slerp(p.drawnRotation, plane.rotation, std::min(1.0f, dt * 12.0f));
        if (jump) p.trail.dropping = false;
        p.smoke = plane.smoke;
    }
}

// Our planes, for everyone else (after they moved).
void FlyingModule::sendNet() {
    if (!m_net || !m_net->connected() || m_phase == Phase::Lobby) return;
    for (Pilot& p : m_pilots) {
        if (p.remote || p.netId < 0) continue;
        int slot = -1;
        for (int s = 0; s < kke::NetModule::kMaxLocalPlayers; ++s)
            if (m_net->localPlayerId(s) == p.netId && (s == 0 || p.netId != 0)) slot = s;
        if (slot < 0) continue;
        net::Plane n;
        n.position = p.plane.position;
        n.velocity = p.plane.velocity;
        n.rotation = p.plane.rotation;
        n.throttle = p.controls.throttle;
        n.smoke = p.smoke;
        n.crashed = down(p);
        n.onGround = p.plane.onGround;
        n.safe = p.safe;
        n.finished = p.finished;
        n.teleported = p.teleported;
        n.nextRing = static_cast<uint8_t>(std::clamp(p.nextRing, 0, 63));
        n.lap = static_cast<uint8_t>(std::clamp(p.lap, 0, 7));
        n.finishTime = p.finishTime;
        n.score = static_cast<uint32_t>(std::clamp(p.stunts.score(), 0, 65535));
        n.round = static_cast<uint8_t>(m_round & 0xFFu);
        n.firing = p.firing && !down(p);
        n.health = static_cast<uint8_t>(std::clamp(static_cast<int>(std::ceil(p.health)), 0, 100));
        n.kills = static_cast<uint8_t>(std::clamp(p.kills, 0, 63));
        n.deaths = static_cast<uint8_t>(std::clamp(p.deaths, 0, 63));
        p.teleported = false;
        m_net->setLocalPlayer(slot, net::toState(n));
    }
}

} // namespace flying
