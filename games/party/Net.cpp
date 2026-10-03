// Party online (README.md "Online", NetParty.h): player 1 sets Online to
// Host or Join in the start menu; everyone on each screen plays (a second
// player on a screen is a NetModule local player, like split screen). The
// host runs the show: each Round (minigame, seed, who plays, the points)
// and each Phase go to everyone. Each machine moves its own beans and
// sends their poses; everyone else's beans are drawn from them. A bean's
// own machine says when it finished or went out; the host puts those in
// order. The host's CPU beans are the host's local players.

#include "PartyModule.h"

#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/ImpactSynth.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace party {

namespace {
constexpr int kModeOff = 0, kModeJoin = 1, kModeHost = 2; // Join first: going to Host passes a harmless search
constexpr float kSearchEvery = 3.0f;                      // s between LAN searches while Join is picked
} // namespace

bool PartyModule::netClient() const { return m_net && m_net->role() == kke::NetModule::Role::Client; }
bool PartyModule::netHost() const { return m_net && m_net->role() == kke::NetModule::Role::Host; }

Bean* PartyModule::beanOfNet(int netId) {
    for (Bean& b : m_beans)
        if (b.netId == netId && netId >= 0) return &b;
    return nullptr;
}

void PartyModule::setupNet() {
    if (!m_net) return;
    m_net->standIns = false;  // beans bump each other in the game's own code (Beans.cpp)
    m_net->checkMoves = false; // launch pads, knocks and sweepers move beans faster than a run
    m_net->addEventListener([this](const kke::net::GameEventMsg& e) { onNetEvent(e); });
    m_net->addPlayerListener([this](uint8_t id, bool joined) {
        if (!netHost()) return;
        std::string who = "Player " + std::to_string(id);
        for (const kke::net::RemotePlayer& p : m_net->remotePlayers())
            if (p.id == id) who = p.name;
        if (!m_lobby) return;
        if (joined && m_phase != Phase::Lobby) m_lobby->lobby().toast(who + " joined online: in from the next party", 5.0f);
        else if (joined) m_lobby->lobby().toast(who + " joined online", 4.0f);
    });
    if (const char* who = kke::dev::env("KKE_NET_NAME")) m_netName = who;
    if (!m_lobby) {
        syncNetPlayers();
        return;
    }
    kke::Lobby& l = m_lobby->lobby();
    kke::Lobby::Option mode{ "net.mode", "Online", { "Off", "Join", "Host" }, kModeOff, true, {}, {} };
    mode.onChange = [this](int value) {
        std::string error;
        m_wasOnline = false; // leaving on purpose: no "left the game" toast
        m_net->leave();
        if (value == kModeHost) {
            syncNetPlayers(); // our names before host() announces them
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
    syncNetPlayers(); // KKE_NET hosts or joins in the first frame, with these names
}

// This screen's players as the game's players: the first lobby seat is
// NetModule's own player, the others its local players (slots 1..); on
// the host the CPU beans come after them, so everyone sees them too.
void PartyModule::syncNetPlayers() {
    if (!m_net) return;
    const std::vector<Entry> ours = wantedRoster();
    int slots = 0;
    for (const Entry& e : ours) {
        if (e.seat < 0 && netClient()) continue; // a client's CPU beans stay home
        if (slots >= kke::NetModule::kMaxLocalPlayers) break;
        if (slots == 0) {
            m_net->playerName = e.name.substr(0, 16);
            m_net->playerCharacter = e.seat < 0 ? "cpu" : "bean";
        } else {
            m_net->addLocalPlayer(slots, e.name.substr(0, 16), e.seat < 0 ? "cpu" : "bean"); // "cpu": no microphone (Pause.cpp)
        }
        ++slots;
    }
    for (int s = std::max(1, slots); s < kke::NetModule::kMaxLocalPlayers; ++s) m_net->removeLocalPlayer(s);
}

std::vector<PartyModule::Entry> PartyModule::onlineRoster() const {
    std::vector<Entry> ours = wantedRoster(), out;
    // Every bean here needs a network player, or the other machines see
    // fewer: CPU beans with no room online sit this party out.
    for (size_t slot = 0; slot < ours.size(); ++slot) {
        Entry e = ours[slot];
        e.netId = slot < static_cast<size_t>(kke::NetModule::kMaxLocalPlayers) ? m_net->localPlayerId(static_cast<int>(slot)) : 0;
        if (slot > 0 && e.netId == 0) e.netId = -1;
        if (e.netId < 0 && e.seat < 0) continue;
        out.push_back(std::move(e));
    }
    for (const kke::net::RemotePlayer& p : m_net->remotePlayers()) {
        Entry e;
        e.name = p.name;
        e.netId = p.id;
        e.remote = true;
        if (p.hasState) e.look = netparty::fromState(p.state).look;
        out.push_back(std::move(e));
    }
    return out;
}

// Host: the round (whenever it's a new one, or the points changed) and
// the phase it's in.
void PartyModule::sendRound() {
    if (!m_net || !m_net->connected()) return;
    netparty::Round r;
    r.round = m_netRound;
    r.index = static_cast<uint8_t>(std::clamp(m_show.round, 0, 255));
    r.rounds = static_cast<uint8_t>(std::clamp(m_show.rounds, 1, 255));
    r.seed = m_roundSeed;
    r.game = m_game ? m_game->id() : "";
    for (const Bean& b : m_beans) {
        if (b.netId < 0) continue;
        netparty::Seat s;
        s.player = static_cast<uint8_t>(b.netId);
        s.cpu = b.bot && b.seat < 0;
        s.name = b.name.substr(0, 16);
        s.look = b.look;
        s.points = static_cast<size_t>(b.index) < m_show.points.size() ? m_show.points[static_cast<size_t>(b.index)] : 0;
        r.seats.push_back(std::move(s));
    }
    // A vote sends the roster and the points with no game yet (the round
    // after it names the game).
    if ((m_game || m_phase == Phase::Vote) &&
        (m_sentRound != m_netRound || m_phase == Phase::Results || m_phase == Phase::Podium || m_phase == Phase::Vote)) {
        m_sentRound = m_netRound;
        m_net->sendEvent(netparty::kEventRound, netparty::encode(r));
    }
    m_net->sendEvent(netparty::kEventPhase, netparty::encode(netparty::Phase{ m_netRound, static_cast<uint8_t>(m_phase) }));
}

// A client: the host's round. Our own beans are this screen's seats (by
// their player ids); everyone else's are drawn from what they send.
void PartyModule::applyRound(const netparty::Round& r) {
    const std::vector<int> joined = m_lobby ? m_lobby->lobby().joinedSeats() : std::vector<int>{ 0 };
    std::vector<Entry> roster;
    const std::vector<Entry> own = wantedRoster();
    for (const netparty::Seat& seat : r.seats) {
        Entry e;
        e.name = seat.name;
        e.look = seat.look;
        e.netId = seat.player;
        e.remote = !m_net->isLocalPlayer(seat.player);
        if (!e.remote) {
            for (int slot = 0; slot < kke::NetModule::kMaxLocalPlayers && slot < static_cast<int>(joined.size()); ++slot)
                if (m_net->localPlayerId(slot) == seat.player) e.seat = joined[static_cast<size_t>(slot)];
            if (e.seat < 0) e.remote = true; // not a seat of ours any more
            for (const Entry& o : own)
                if (!e.remote && o.seat == e.seat) {
                    e.name = o.name;
                    e.look = o.look;
                }
        }
        roster.push_back(std::move(e));
    }
    bool same = roster.size() == m_beans.size();
    for (size_t i = 0; same && i < roster.size(); ++i) same = roster[i].netId == m_beans[i].netId && roster[i].remote == m_beans[i].remote;
    if (!same) {
        buildBeans(roster);
        if (m_lobby) {
            if (m_lobby->isOpen()) m_lobby->close();
            m_lobby->applyInput();
            for (Bean& b : m_beans)
                if (b.seat >= 0) b.player = std::max(0, m_lobby->playerOf(b.seat));
        }
    }
    // The show as the host has it: points, which round of how many.
    const std::vector<int> before = m_show.points;
    m_show.rounds = r.rounds;
    m_show.round = r.index;
    m_show.points.assign(m_beans.size(), 0);
    for (size_t i = 0; i < r.seats.size() && i < m_show.points.size(); ++i) m_show.points[i] = r.seats[i].points;
    m_roundPoints.assign(m_show.points.size(), 0);
    for (size_t i = 0; i < m_show.points.size() && i < before.size(); ++i) m_roundPoints[i] = m_show.points[i] - before[i];
    if (r.game.empty()) return; // before a vote: who plays and the points, the game comes after it
    if (r.round != m_netRound || !m_game || r.game != m_game->id()) {
        buildRound(r.game, r.seed);
        m_netRound = r.round;
    }
}

void PartyModule::applyPhase(Phase phase) {
    if (phase == m_phase) return;
    switch (phase) {
    case Phase::Lobby: backToLobby(); return;
    case Phase::Play: startPlay(); return;
    case Phase::Podium: showPodium(); return;
    case Phase::Vote: return; // opened by the host's Vote (applyVote)
    case Phase::RoundOver:
        tone(static_cast<int>(kke::Earcon::ToggleOn), 0.8f);
        break;
    default: break;
    }
    m_phase = phase;
    m_phaseTime = 0.0f;
}

void PartyModule::sendResult(uint8_t kind, const Bean& b) {
    if (!m_net || !m_net->connected() || b.netId < 0 || m_netApplying) return;
    netparty::Result r;
    r.round = m_netRound;
    r.player = static_cast<uint8_t>(b.netId);
    r.kind = kind;
    // The host's order is the one that counts; a client's is only a claim.
    r.order = static_cast<int16_t>(netHost() ? (kind == netparty::kResultFinish ? b.result.finishOrder : b.result.outOrder) : -1);
    m_net->sendEvent(netparty::kEventResult, netparty::encode(r));
}

void PartyModule::sendKnock(Bean& b, const glm::vec3& velocity, float stun) {
    if (!m_net || !m_net->connected() || b.netId < 0) return;
    netparty::Knock k;
    k.round = m_netRound;
    k.player = static_cast<uint8_t>(b.netId);
    k.velocity = glm::clamp(velocity, glm::vec3(-40.0f), glm::vec3(40.0f));
    k.stun = std::clamp(stun, 0.0f, 3.0f);
    m_net->sendEvent(netparty::kEventKnock, netparty::encode(k));
}

void PartyModule::event(int kind, int a, int b) {
    if (m_game) m_game->onEvent(*this, kind, a, b);
    if (!m_net || !m_net->connected() || m_netApplying) return;
    m_net->sendEvent(netparty::kEventGame, netparty::encode(netparty::Game{ m_netRound, static_cast<uint8_t>(kind), a, b }));
}

void PartyModule::onNetEvent(const kke::net::GameEventMsg& e) {
    m_netApplying = true;
    if (e.kind == netparty::kEventRound && netClient()) {
        if (const auto r = netparty::decodeRound(e.payload)) applyRound(*r);
    } else if (e.kind == netparty::kEventPhase && netClient()) {
        if (const auto p = netparty::decodePhase(e.payload); p && p->round == m_netRound) applyPhase(static_cast<Phase>(std::min<uint8_t>(p->phase, kLastPhase)));
    } else if (e.kind == netparty::kEventResult) {
        const auto r = netparty::decodeResult(e.payload);
        Bean* b = r && r->round == m_netRound ? beanOfNet(r->player) : nullptr;
        if (b) {
            const bool finish = r->kind == netparty::kResultFinish;
            if (netHost()) {
                // A client's claim: in order of arrival, then to everyone.
                if (!b->finished && !b->out) {
                    b->active = false;
                    if (finish) {
                        b->finished = b->result.finished = true;
                        b->result.finishOrder = m_finishOrder++;
                    } else {
                        b->out = b->result.out = true;
                        b->result.outOrder = m_outOrder++;
                    }
                    kke::log::get(name())->info("online: {} {}", b->name, finish ? "finished" : "is out");
                    if (!finish) flash(b->name + " is out!", 1.4f);
                    m_netApplying = false;
                    netparty::Result out = *r;
                    out.order = static_cast<int16_t>(finish ? b->result.finishOrder : b->result.outOrder);
                    m_net->sendEvent(netparty::kEventResult, netparty::encode(out));
                }
            } else if (r->order >= 0) {
                // The host's order (for our own beans too: it replaces ours).
                b->active = false;
                if (finish) {
                    b->finished = b->result.finished = true;
                    b->result.finishOrder = r->order;
                } else {
                    if (!b->out && b->remote) flash(b->name + " is out!", 1.4f);
                    b->out = b->result.out = true;
                    b->result.outOrder = r->order;
                }
            }
        }
    } else if (e.kind == netparty::kEventVote && netClient()) {
        if (const auto v = netparty::decodeVote(e.payload)) applyVote(*v);
    } else if (e.kind == netparty::kEventBallot && netHost()) {
        if (const auto b = netparty::decodeBallot(e.payload)) applyBallot(*b);
    } else if (e.kind == netparty::kEventKnock) {
        if (const auto k = netparty::decodeKnock(e.payload); k && k->round == m_netRound) {
            if (Bean* b = beanOfNet(k->player); b && !b->remote) knock(*b, k->velocity, k->stun);
            else if (netHost()) m_net->relayEvent(e); // someone else's: pass it on
        }
    } else if (e.kind == netparty::kEventGame) {
        if (const auto g = netparty::decodeGame(e.payload); g && g->round == m_netRound && m_game) {
            m_game->onEvent(*this, g->kind, g->a, g->b);
            if (netHost()) m_net->relayEvent(e); // pass a client's on to the others
        }
    }
    m_netApplying = false;
}

std::string PartyModule::netStatus() const {
    if (netHost()) {
        const size_t others = m_net->remotePlayers().size();
        std::string text = m_net->statusText();
        if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
        return text + ": " + std::to_string(others) + (others == 1 ? " player" : " players") + " online. On another PC or window, set Online to Join.";
    }
    if (netClient()) {
        if (!m_net->connected()) return "Joining: " + m_net->statusText();
        return "Online, " + m_net->statusText() + ": the host starts the party.";
    }
    const kke::Lobby::Option* mode = m_lobby ? m_lobby->lobby().option("net.mode") : nullptr;
    if (mode && mode->value == kModeJoin) return m_net->statusText() == "offline" ? "Looking for parties on this network and this PC..." : m_net->statusText();
    return "Dress your bean. Another controller? Press {a} on it to join.";
}

void PartyModule::updateNet(float dt) {
    if (!m_net) return;
    m_netTime += dt;
    const bool isOnline = m_net->role() != kke::NetModule::Role::Offline;
    if (m_wasOnline && !isOnline) {
        // Lost the game (the host left, or we were turned away): the menu.
        if (m_lobby) m_lobby->lobby().toast("Left the online party: " + m_net->statusText(), 6.0f);
        std::vector<Entry> roster = wantedRoster();
        buildBeans(roster);
        if (m_lobby && m_phase != Phase::Lobby) backToLobby();
    }
    m_wasOnline = isOnline;

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
            const bool looking = !isOnline && mode->value == kModeJoin;
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
            m_lobby->setTitle("PARTY", status);
        }
    }
    if (!isOnline) return;
    syncNetPlayers();

    // Everyone else's beans, where their machines say they are.
    const std::vector<kke::net::RemotePlayer>& remote = m_net->remotePlayers();
    for (Bean& b : m_beans) {
        if (!b.remote) continue;
        const auto it = std::find_if(remote.begin(), remote.end(), [&b](const kke::net::RemotePlayer& p) { return p.id == b.netId; });
        if (it == remote.end()) {
            // Gone (left the game): out of this round, not drawn.
            if (!b.hidden) kke::log::get(name())->info("{} left the online party", b.name);
            b.hidden = true;
            if (b.active && netHost() && m_phase == Phase::Play) {
                b.active = false;
                b.out = b.result.out = true;
                b.result.outOrder = m_outOrder++;
            }
            continue;
        }
        if (!it->hasState) continue;
        const netparty::Pose p = netparty::fromState(it->state);
        b.drawFeet = glm::length(b.drawFeet - p.feet) > 3.0f ? p.feet : glm::mix(b.drawFeet, p.feet, std::min(1.0f, dt * 20.0f));
        b.velocity = p.velocity;
        b.yaw = p.yaw;
        b.grounded = p.grounded;
        b.dive = p.diving ? 0.3f : 0.0f;
        b.stun = p.stunned ? 0.3f : 0.0f;
        b.hidden = p.hidden;
        b.look = p.look;
        b.result.score = p.score;
        world().moveCharacter(b.id, p.feet);
    }
}

// Our beans' poses, for everyone else.
void PartyModule::sendNet() {
    if (!m_net || !m_net->connected() || m_phase == Phase::Lobby) return;
    for (const Bean& b : m_beans) {
        if (b.remote || b.netId < 0) continue;
        int slot = -1;
        for (int s = 0; s < kke::NetModule::kMaxLocalPlayers; ++s)
            if (m_net->localPlayerId(s) == b.netId && (s == 0 || b.netId != 0)) slot = s;
        if (slot < 0) continue;
        netparty::Pose p;
        const kke::RigidWorld& w = m_rigid->world();
        p.feet = w.characterPosition(b.id);
        p.velocity = w.characterVelocity(b.id);
        p.yaw = b.yaw;
        p.grounded = b.grounded;
        p.diving = b.dive > 0.0f;
        p.stunned = b.stun > 0.0f;
        p.hidden = b.hidden;
        p.look = b.look;
        p.score = b.result.score;
        m_net->setLocalPlayer(slot, netparty::toState(p));
    }
}

} // namespace party
