// Tennis online (README.md "Online", NetTennis.h): player 1 sets Online
// to Host or Join in the start menu; everyone at each screen plays (a
// second person at a screen is a NetModule local player). The host is the
// umpire: its Setup says who plays where, its Serve starts each point and
// its Point ends it. Each machine runs its own players, hits for them and
// sends the hit; every machine flies the ball from the same hit. The
// host's CPU players go to everyone in one event (Cpus). In the sport
// center the host runs every court: clients ask at a gate (Gate) and see
// the queues and the wins it sends (Board).

#include "TennisModule.h"

#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace tennis {

namespace {
constexpr int kModeOff = 0, kModeJoin = 1, kModeHost = 2; // Join first: going to Host passes a harmless search, not a game others see
constexpr float kSearchEvery = 3.0f;  // s between LAN searches while Join is picked
constexpr float kBallEvery = 0.2f;    // s between the host's ball corrections (each match)
constexpr float kCpusEvery = 0.1f;    // s between the host's CPU player moves
constexpr float kBoardEvery = 0.5f;   // s between the sport center's boards
} // namespace

bool TennisModule::netClient() const { return m_net && m_net->role() == kke::NetModule::Role::Client; }
bool TennisModule::netHost() const { return m_net && m_net->role() == kke::NetModule::Role::Host; }

bool TennisModule::waitsOnline() const {
    const char* how = kke::dev::env("KKE_NET");
    return m_netWait > 0 || (how && std::strncmp(how, "join", 4) == 0);
}

void TennisModule::setupNet() {
    if (!m_net) return;
    m_net->standIns = false;   // players never block each other (nor the ball)
    m_net->checkMoves = false; // the court is flat and open: the speed limits are enough
    m_net->addEventListener([this](const kke::net::GameEventMsg& e) { onNetEvent(e); });
    m_net->onPlayer = [this](uint8_t id, bool joined) {
        if (!netHost()) return;
        if (joined && !m_lobby) m_newcomers.push_back(id);
        if (!m_lobby) return;
        std::string who = "Player " + std::to_string(id);
        for (const kke::net::RemotePlayer& p : m_net->remotePlayers())
            if (p.id == id) who = p.name;
        if (joined) m_newcomers.push_back(id); // the sport center and its matches, next update
        if (joined) m_lobby->lobby().toast(who + (m_inMenu || m_inCenter ? " joined online" : " joined online: plays from the next match"), 5.0f);
    };
    if (const char* wait = kke::dev::env("KKE_TENNIS_WAIT")) m_netWait = std::max(0, std::atoi(wait));
    if (!m_lobby) {
        syncNetPlayers();
        return;
    }
    // Player 1's rows, before Start: Online (Off / Join / Host), then the
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
            syncNetPlayers(); // our names before host() announces them
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

// This screen's people as network players: the menu's seats (the first is
// NetModule's own player, the others local players 1..). CPU players stay
// out: the host sends theirs in one event (Cpus).
std::vector<TennisModule::Entry> TennisModule::netEntries() const {
    std::vector<Entry> out = seatEntries();
    std::erase_if(out, [](const Entry& e) { return e.cpu; });
    if (out.size() > static_cast<size_t>(kke::NetModule::kMaxLocalPlayers)) out.resize(static_cast<size_t>(kke::NetModule::kMaxLocalPlayers));
    if (m_net)
        for (size_t slot = 0; slot < out.size(); ++slot) {
            out[slot].netId = m_net->localPlayerId(static_cast<int>(slot));
            if (slot > 0 && out[slot].netId == 0) out[slot].netId = -1; // not registered yet
        }
    return out;
}

void TennisModule::syncNetPlayers() {
    if (!m_net) return;
    const std::vector<Entry> ours = netEntries();
    int slots = 0;
    for (const Entry& e : ours) {
        if (slots == 0) {
            m_net->playerName = e.name;
            m_net->playerCharacter = net::tintText(e.tint);
        } else {
            m_net->addLocalPlayer(slots, e.name, net::tintText(e.tint));
        }
        ++slots;
    }
    for (int s = std::max(1, slots); s < kke::NetModule::kMaxLocalPlayers; ++s) m_net->removeLocalPlayer(s);
}

TennisModule::Match* TennisModule::matchByNet(uint32_t id) {
    if (id == 0) return nullptr;
    for (const auto& m : m_matches)
        if (m->netId == id) return m.get();
    return nullptr;
}

size_t TennisModule::matchIndex(const Match& m) const {
    for (size_t i = 0; i < m_matches.size(); ++i)
        if (m_matches[i].get() == &m) return i;
    return m_matches.size();
}

int TennisModule::indexInMatch(const Match& m, const Player& p) const {
    for (size_t i = 0; i < m.players.size(); ++i)
        if (&m_players[static_cast<size_t>(m.players[i])] == &p) return static_cast<int>(i);
    return -1;
}

namespace {
// The host's CPU players have no network id; everyone else does.
bool hostCpu(const TennisModule::Player& p) { return !p.remote && p.netId < 0; }
} // namespace

net::Setup TennisModule::setupOf(const Match& m) const {
    net::Setup s;
    s.match = m.netId;
    s.court = static_cast<uint8_t>(m.court);
    s.teamSize = static_cast<uint8_t>(m.rules.teamSize);
    s.gamesPerSet = static_cast<uint8_t>(m.rules.gamesPerSet);
    s.setsToWin = static_cast<uint8_t>(m.rules.setsToWin);
    s.center = m_inCenter;
    s.point = m.serial;
    s.history.assign(m.history.begin(), m.history.begin() + static_cast<std::ptrdiff_t>(std::min(m.history.size(), net::kMaxHistory)));
    for (int idx : m.players) {
        const Player& p = m_players[static_cast<size_t>(idx)];
        s.seats.push_back({ static_cast<uint8_t>(std::max(0, p.netId)), static_cast<uint8_t>(p.team), static_cast<uint8_t>(p.slot), hostCpu(p), p.name, p.tint });
    }
    return s;
}

void TennisModule::sendSetup(Match& m) {
    if (m.netId == 0) m.netId = ++m_netMatch;
    const net::Setup s = setupOf(m);
    m_net->sendEvent(net::kEventSetup, net::encode(s));
    kke::log::get(name())->info("online match {} on court {}: {} players, sent to everyone", s.match, m.court + 1, s.seats.size());
}

// A client: the host's match. Our own people are this screen's seats (by
// their network ids); everyone else's players are drawn from what they send.
void TennisModule::applySetup(const net::Setup& s) {
    if (m_lobby) {
        if (m_lobby->isOpen()) {
            m_lobby->save();
            m_lobby->close();
        }
        m_lobby->applyInput();
    }
    m_inMenu = false;
    if (s.center) {
        if (!m_inCenter) enterCenterOnline();
        if (matchByNet(s.match)) return; // sent again (to someone who joined)
        syncRemoteWalkers();             // the host may name people we haven't drawn yet
    } else {
        clearPlayers();
    }
    const int court = std::clamp<int>(s.court, 0, SportCenter::kCourts - 1);
    if (Match* old = matchOn(court)) closeMatch(matchIndex(*old)); // missed its End
    const std::vector<Entry> ours = netEntries();
    std::vector<Entry> entries;
    int mine = 0;
    for (const net::Seat& seat : s.seats) {
        Entry e;
        e.name = seat.name;
        e.tint = seat.tint;
        e.team = seat.team;
        e.slot = seat.slot;
        e.remote = true;
        e.netCpu = seat.cpu;
        if (!seat.cpu) {
            e.netId = seat.player;
            if (m_net->isLocalPlayer(seat.player)) {
                for (const Entry& own : ours)
                    if (own.netId == seat.player) {
                        e = own; // our own person, as picked here
                        e.team = seat.team;
                        e.slot = seat.slot;
                        ++mine;
                    }
            }
            // Walking about in the sport center: they step onto the court.
            for (size_t wi = 0; wi < m_walkers.size(); ++wi)
                if (!m_walkers[wi].cpu && !m_walkers[wi].gone && m_walkers[wi].netId == seat.player) e.walker = static_cast<int>(wi);
        }
        entries.push_back(std::move(e));
    }
    MatchRules rules;
    rules.teamSize = std::clamp<int>(s.teamSize, 1, 2);
    rules.gamesPerSet = s.gamesPerSet;
    rules.setsToWin = s.setsToWin;
    const int teams = m_teams;
    m_teams = 0; // the seats say who plays where
    Match* m = buildMatch(std::move(entries), rules, court);
    m_teams = teams;
    m->netId = s.match;
    m->serial = s.point;
    for (uint8_t team : s.history) {
        m->score.pointTo(team ? 1 : 0);
        m->history.push_back(team ? 1 : 0);
    }
    walkersOn(*m);
    kke::log::get(name())->info("online match {} on court {}: {} players, {} of them here", s.match, court + 1, s.seats.size(), mine);
}

// A client, when the host is in the sport center: our people walk in; the
// host's matches, people and gates come in its events. The crowd is this
// screen's own (it only watches).
void TennisModule::enterCenterOnline() {
    const bool cpus = m_cpuMatches;
    m_cpuMatches = false; // the host's call
    enterCenter();
    m_cpuMatches = cpus;
    syncRemoteWalkers();
}

void TennisModule::closeMatchByNet(uint32_t id, const std::string& why) {
    if (id == 0 || !m_inCenter) {
        if (m_matches.empty() && !m_inCenter) return;
        if (m_lobby && !why.empty()) m_lobby->lobby().toast(why, 5.0f);
        kke::log::get(name())->info("online: the host ended it: {}", why.empty() ? "played out" : why);
        backToMenu();
        return;
    }
    Match* m = matchByNet(id);
    if (!m) return;
    if (!why.empty() && m_lobby) m_lobby->lobby().toast(why, 5.0f);
    closeMatch(matchIndex(*m));
}

void TennisModule::onNetEvent(const kke::net::GameEventMsg& e) {
    const bool fromHost = e.fromPlayer == 0;
    auto log = [this]() { return kke::log::get(name()); };
    switch (e.kind) {
    case net::kEventSetup: {
        if (!netClient() || !fromHost) return; // only the host sets up a match
        if (auto s = net::decodeSetup(e.payload)) applySetup(*s);
        else log()->warn("online: a damaged match setup");
        return;
    }
    case net::kEventEnd: {
        const auto end = net::decodeEnd(e.payload);
        if (!netClient() || !fromHost || !end) return;
        closeMatchByNet(end->match, end->why);
        return;
    }
    case net::kEventBoard: {
        const auto b = net::decodeBoard(e.payload);
        if (!netClient() || !fromHost || !b) return;
        m_board = *b;
        m_wins.clear();
        for (const auto& [who, won] : b->wins) m_wins.emplace_back(who, won);
        if (b->center && !m_inCenter) {
            if (m_lobby && m_lobby->isOpen()) {
                m_lobby->save();
                m_lobby->close();
                m_lobby->applyInput();
            }
            m_inMenu = false;
            enterCenterOnline();
        }
        return;
    }
    case net::kEventGate: {
        const auto g = net::decodeGate(e.payload);
        if (!netHost() || !g || !m_inCenter || g->court >= SportCenter::kCourts) return;
        int who = -1;
        for (size_t wi = 0; wi < m_walkers.size(); ++wi)
            if (m_walkers[wi].remote && !m_walkers[wi].gone && m_walkers[wi].netId == g->player) who = static_cast<int>(wi);
        if (who < 0) return;
        Walker& w = m_walkers[static_cast<size_t>(who)];
        if (g->action == net::Gate::Join) {
            gateJoin(who, g->court);
        } else if (g->action == net::Gate::Leave) {
            if (w.queued == g->court) gateLeave(who);
        } else if (g->action == net::Gate::CpuNow) {
            if (w.queued == g->court && !matchOn(g->court)) startCourt(g->court);
        }
        return;
    }
    case net::kEventCpus: {
        const auto c = net::decodeCpus(e.payload);
        if (!netClient() || !fromHost || !c) return;
        for (const net::CpuMatch& cm : c->matches) {
            Match* m = matchByNet(cm.match);
            if (!m) continue;
            const CourtPlace& place = m_center.courts[static_cast<size_t>(m->court)];
            for (const net::CpuPose& cp : cm.players) {
                if (cp.player >= m->players.size()) continue;
                Player& p = player(m->players[cp.player]);
                if (!p.netCpu) continue;
                const float yaw = glm::radians(cp.yaw);
                p.pose.feet = place.toWorld(cp.feet);
                p.pose.velocity = place.dirToWorld(glm::vec3(cp.velocity.x, 0.0f, cp.velocity.y));
                p.pose.facing = place.dirToWorld(glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw)));
                p.pose.stroke = cp.stroke;
                p.pose.backhand = cp.backhand;
                p.pose.swingT = cp.swingT;
                p.pose.contact = cp.contact;
                p.pose.tossing = cp.tossing;
                p.pose.celebrating = cp.celebrating;
                p.pose.cheer = cp.cheer;
                p.hasPose = true;
            }
        }
        return;
    }
    default: break;
    }

    // The rest belong to one match.
    switch (e.kind) {
    case net::kEventServe: {
        const auto s = net::decodeServe(e.payload);
        if (!netClient() || !fromHost || !s) return;
        Match* m = matchByNet(s->match);
        if (!m) return;
        m->serial = s->point;
        m->serveAgain = s->again;
        startPoint(*m);
        m->rally.setSecondServe(s->second);
        if (s->second) m->sub = "Second serve";
        return;
    }
    case net::kEventPoint: {
        const auto p = net::decodePoint(e.payload);
        if (!netClient() || !fromHost || !p) return;
        Match* m = matchByNet(p->match);
        if (!m || p->point != m->serial) return;
        if (p->result == 0 || p->result > static_cast<uint8_t>(Rally::Result::Let)) return;
        resolve(*m, static_cast<Rally::Result>(p->result), p->call);
        return;
    }
    case net::kEventHit: {
        const auto h = net::decodeHit(e.payload);
        if (!h) return;
        Match* m = matchByNet(h->match);
        if (!m || h->point != m->serial || h->player >= m->players.size()) return;
        const int idx = m->players[h->player];
        Player& p = player(idx);
        if (!p.remote) return; // ours: already hit here
        if (netHost()) {
            // The umpire checks it could be: a person's, their turn, this shot, the right phase.
            const bool phaseOk = h->serve ? m->phase == Match::Phase::Serve && idx == serverIndex(*m) : m->phase == Match::Phase::Rally;
            if (p.netCpu || !phaseOk || !m->rally.mayHit(p.team) || h->shot != m->rallyShots) {
                log()->info("online: {}'s hit came too late (the point had moved on)", p.name);
                return;
            }
            m_net->relayEvent(e);
        }
        applyHit(*m, idx, *h);
        return;
    }
    case net::kEventToss: {
        const auto t = net::decodeToss(e.payload);
        if (!t) return;
        Match* m = matchByNet(t->match);
        if (!m || t->point != m->serial || m->phase != Match::Phase::Serve || t->player >= m->players.size()) return;
        const int idx = m->players[t->player];
        Player& p = player(idx);
        if (!p.remote || idx != serverIndex(*m)) return;
        if (netHost()) {
            if (p.netCpu) return;
            m_net->relayEvent(e);
        }
        p.tossAge = 0.0f;
        m->ball->place(t->at, t->velocity);
        return;
    }
    case net::kEventBall: {
        const auto b = net::decodeBall(e.payload);
        if (!netClient() || !fromHost || !b) return;
        Match* m = matchByNet(b->match);
        if (!m || b->point != m->serial || m->phase != Match::Phase::Rally) return;
        if (b->shot < m->rallyShots) return; // a hit of ours is on its way to the host
        Ball& ball = *m->ball;
        if (b->shot > m->rallyShots || glm::length(b->flight.pos - ball.position()) > 0.25f || glm::length(b->flight.vel - ball.velocity()) > 1.0f) {
            ball.follow(b->flight, b->rolling);
            m->rallyShots = b->shot;
        }
        return;
    }
    default: return;
    }
}

std::string TennisModule::netStatus() const {
    if (netHost()) {
        const size_t others = m_net->remotePlayers().size();
        std::string text = m_net->statusText(); // "hosting on port 27960"
        if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
        return text + ": " + std::to_string(others) + (others == 1 ? " player" : " players") +
               " online. On another PC or window, set Online to Join. Start plays with everyone.";
    }
    if (netClient()) {
        if (!m_net->connected()) return "Joining: " + m_net->statusText();
        return "Online, " + m_net->statusText() + ": the host starts the match.";
    }
    const kke::Lobby::Option* mode = m_lobby ? m_lobby->lobby().option("net.mode") : nullptr;
    if (mode && mode->value == kModeJoin) return m_net->statusText() == "offline" ? "Looking for games on this network and this PC..." : m_net->statusText();
    return "Two to four players, or you against the CPU. Another controller? Press {a} on it to join.";
}

// The people at other screens in the sport center: walking about (drawn
// where their machines say) or, while they play, their players instead.
void TennisModule::syncRemoteWalkers() {
    if (!m_inCenter || !m_net) return;
    const std::vector<kke::net::RemotePlayer>& remote = m_net->remotePlayers();
    kke::RigidWorld& world = m_rigid->world();
    for (Walker& w : m_walkers) {
        if (!w.remote || w.gone) continue;
        if (std::none_of(remote.begin(), remote.end(), [&w](const kke::net::RemotePlayer& r) { return r.id == w.netId; })) {
            w.gone = true;
            if (m_net->role() == kke::NetModule::Role::Host) gateLeave(static_cast<int>(&w - m_walkers.data()));
            if (w.body) world.removeCharacter(w.body);
            w.body = 0;
            if (w.look) w.look->setVisible(false);
        }
    }
    for (const kke::net::RemotePlayer& r : remote) {
        const bool known = std::any_of(m_walkers.begin(), m_walkers.end(), [&r](const Walker& w) { return w.remote && !w.gone && w.netId == r.id; });
        if (known) continue;
        const glm::vec3 at = r.hasState ? net::fromState(r.state).feet : m_center.arrival(r.id * kke::NetModule::kMaxLocalPlayers);
        spawnWalker(r.name, net::tintFromText(r.character, glm::vec3(0.8f)), false, -1, at);
        Walker& w = m_walkers.back();
        w.remote = true;
        w.netId = r.id;
        world.setCharacterKinematic(w.body, true);
        kke::log::get(name())->info("sport center: {} is here from another screen", r.name);
        if (w.look) w.look->setVisible(true);
    }
    for (Walker& w : m_walkers) {
        if (!w.remote || w.gone || w.playing >= 0 || !w.body) continue;
        const auto it = std::find_if(remote.begin(), remote.end(), [&w](const kke::net::RemotePlayer& r) { return r.id == w.netId; });
        if (it == remote.end() || !it->hasState) continue;
        const net::Pose pose = net::fromState(it->state);
        world.moveCharacter(w.body, pose.feet);
        world.setCharacterVelocity(w.body, pose.velocity);
        if (glm::length(pose.facing) > 0.1f) w.facing = glm::normalize(pose.facing);
    }
}

void TennisModule::sendBoard() {
    net::Board b;
    b.center = m_inCenter;
    for (size_t c = 0; c < net::Board::kCourts && c < m_gates.size(); ++c) {
        b.waiting[c] = static_cast<uint8_t>(std::min<size_t>(m_gates[c].waiting.size(), 255));
        b.countdown[c] = m_gates[c].countdown;
    }
    for (size_t i = 0; i < m_wins.size() && i < net::Board::kWins; ++i)
        b.wins.emplace_back(m_wins[i].first, static_cast<uint16_t>(std::min(m_wins[i].second, 65535)));
    m_net->sendEvent(net::kEventBoard, net::encode(b));
}

void TennisModule::updateNet(float dt) {
    if (!m_net) return;
    m_netTime += dt;
    const bool isOnline = online();
    // Lost the game (the host left, or we were turned away): back to the menu.
    if (m_wasOnline && !isOnline) {
        if (m_lobby) m_lobby->lobby().toast("Left the online game: " + m_net->statusText(), 6.0f);
        bool shared = m_inCenter;
        for (const Player& p : m_players) shared = shared || (p.alive && p.remote);
        for (const Walker& w : m_walkers) shared = shared || w.remote;
        if (shared) backToMenu();
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
            // Join: the games found, asked again every few seconds.
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
            m_lobby->setTitle("TENNIS", status); // also redraws the rows
        }
    }
    if (!isOnline) return;
    syncNetPlayers();

    if (netHost() && m_netWait > 0 && static_cast<int>(m_net->remotePlayers().size()) >= m_netWait) {
        // Tests and demos: start once enough others are in.
        m_netWait = 0;
        kke::log::get(name())->info("online: {} players in, starting", m_net->remotePlayers().size());
        startFromMenu();
    }
    syncRemoteWalkers();

    // Everyone else's players, where their machines say they are (a
    // client's view of the host's CPU players comes in Cpus).
    kke::RigidWorld& w = m_rigid->world();
    const std::vector<kke::net::RemotePlayer>& remote = m_net->remotePlayers();
    for (size_t mi = m_matches.size(); mi-- > 0;) {
        Match& m = *m_matches[mi];
        bool ended = false;
        for (int idx : m.players) {
            Player& p = player(idx);
            if (!p.remote) continue;
            if (p.netCpu) {
                if (!p.hasPose) continue;
            } else {
                const auto it = std::find_if(remote.begin(), remote.end(), [&p](const kke::net::RemotePlayer& r) { return r.id == p.netId; });
                if (it == remote.end()) {
                    if (!netHost()) continue; // the host says when a match is off
                    const std::string why = p.name + " left the match";
                    kke::log::get(name())->info("online: {}", why);
                    if (m_lobby) m_lobby->lobby().toast(why, 5.0f);
                    if (m_inCenter) {
                        // The others win it (a forfeit); the court turns over.
                        endCenterMatch(mi, p.team);
                    } else {
                        m_net->sendEvent(net::kEventEnd, net::encode(net::End{ m.netId, why }));
                        clearPlayers(); // the End went out already
                        if (m_lobby) {
                            m_inMenu = true;
                            m_lobby->open();
                        }
                        return;
                    }
                    ended = true;
                    break;
                }
                if (!it->hasState) continue;
                p.pose = net::fromState(it->state);
                p.hasPose = true;
            }
            w.moveCharacter(p.body, p.pose.feet);
            w.setCharacterVelocity(p.body, p.pose.velocity);
        }
        if (ended) continue;

        // The host's ball, a few times a second: anyone drifting is put right.
        if (netHost() && m.netId && m.phase == Match::Phase::Rally && m_netTime >= m.ballSentAt) {
            m.ballSentAt = m_netTime + kBallEvery;
            net::BallState b;
            b.match = m.netId;
            b.point = m.serial;
            b.shot = static_cast<uint8_t>(std::min(m.rallyShots, 255));
            b.flight = m.ball->flight();
            b.rolling = m.ball->rolling();
            m_net->sendEvent(net::kEventBall, net::encode(b));
        }
    }
    if (!netHost()) return;

    // Matches started before we hosted go out now.
    for (const auto& m : m_matches)
        if (!m->netId) sendSetup(*m);
    // Someone new: the sport center as it is, and every match on.
    if (!m_newcomers.empty()) {
        for (uint8_t id : m_newcomers) {
            if (m_inCenter) {
                m_boardDirty = true;
                for (const auto& m : m_matches)
                    if (m->netId) m_net->sendEventTo(id, net::kEventSetup, net::encode(setupOf(*m)));
            }
        }
        m_newcomers.clear();
    }
    if (m_inCenter && (m_boardDirty || m_netTime >= m_boardSentAt)) {
        m_boardDirty = false;
        m_boardSentAt = m_netTime + kBoardEvery;
        sendBoard();
    }
    // The CPU players, every match in one event.
    if (m_netTime >= m_cpusSentAt) {
        m_cpusSentAt = m_netTime + kCpusEvery;
        net::Cpus c;
        for (const auto& mp : m_matches) {
            const Match& m = *mp;
            if (!m.netId) continue;
            net::CpuMatch cm;
            cm.match = m.netId;
            for (size_t i = 0; i < m.players.size(); ++i) {
                const Player& p = m_players[static_cast<size_t>(m.players[i])];
                if (!hostCpu(p)) continue;
                net::CpuPose cp;
                cp.player = static_cast<uint8_t>(i);
                cp.feet = p.feet;
                cp.velocity = glm::vec2(p.vel.x, p.vel.z);
                cp.yaw = glm::degrees(std::atan2(p.facing.x, p.facing.z));
                cp.stroke = p.stroke;
                cp.backhand = p.backhand;
                cp.swingT = p.swingT;
                cp.contact = p.swingContact;
                cp.tossing = p.tossAge >= 0.0f;
                cp.celebrating = p.celebrate > 0.0f;
                cp.cheer = p.cheer;
                cm.players.push_back(cp);
            }
            if (!cm.players.empty()) c.matches.push_back(std::move(cm));
            if (c.matches.size() >= net::kMaxMatches) break;
        }
        if (!c.matches.empty()) m_net->sendEvent(net::kEventCpus, net::encode(c));
    }
}

// Our people's poses, for everyone else (after they moved and swung): in a
// match, their player; in the sport center, walking about.
void TennisModule::sendNet() {
    if (!m_net || !m_net->connected()) return;
    auto slotOf = [this](int netId) {
        for (int s = 0; s < kke::NetModule::kMaxLocalPlayers; ++s)
            if (m_net->localPlayerId(s) == netId && (s == 0 || netId != 0)) return s;
        return -1;
    };
    // People who were walking before this screen went online (KKE_NET
    // with no menu enters the sport center before hosting starts) get
    // their network ids now.
    for (Walker& wk : m_walkers) {
        if (wk.cpu || wk.remote || wk.gone || wk.localSlot < 0 || wk.netId >= 0) continue;
        const int id = m_net->localPlayerId(wk.localSlot);
        if (wk.localSlot == 0 || id != 0) wk.netId = id;
    }
    kke::RigidWorld& w = m_rigid->world();
    for (const auto& mp : m_matches) {
        const Match& m = *mp;
        const CourtPlace& place = m_center.courts[static_cast<size_t>(m.court)];
        for (int idx : m.players) {
            const Player& p = player(idx);
            if (p.remote || p.netId < 0) continue;
            const int slot = slotOf(p.netId);
            if (slot < 0) continue;
            net::Pose pose;
            pose.feet = w.characterPosition(p.body);
            pose.velocity = w.characterVelocity(p.body);
            pose.facing = place.dirToWorld(p.facing);
            pose.stroke = p.stroke;
            pose.backhand = p.backhand;
            pose.swingT = p.swingT;
            pose.contact = p.swingContact;
            pose.tossing = p.tossAge >= 0.0f;
            pose.cheer = p.cheer;
            pose.celebrating = p.celebrate > 0.0f;
            m_net->setLocalPlayer(slot, net::toState(pose));
        }
    }
    for (const Walker& wk : m_walkers) {
        if (wk.cpu || wk.remote || wk.gone || wk.playing >= 0 || !wk.body || wk.netId < 0) continue;
        const int slot = slotOf(wk.netId);
        if (slot < 0) continue;
        net::Pose pose;
        pose.feet = w.characterPosition(wk.body);
        pose.velocity = w.characterVelocity(wk.body);
        pose.facing = wk.facing;
        m_net->setLocalPlayer(slot, net::toState(pose));
    }
}

} // namespace tennis
