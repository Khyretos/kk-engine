// Tennis online (README.md "Online", NetTennis.h): player 1 sets Online
// to Host or Join in the start menu; everyone at each screen plays (a
// second person at a screen is a NetModule local player). The host is the
// umpire: its Setup says who plays where, its Serve starts each point and
// its Point ends it. Each machine runs its own players, hits for them and
// sends the hit; every machine flies the ball from the same hit. The
// host's CPU players are the host's local players, so everyone sees them.

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
constexpr float kBallEvery = 0.2f;    // s between the host's ball corrections
constexpr int kSpareCpus = 3;         // the host keeps this many CPU players online, for short sides
const char* const kSpareNames[] = { "Lobster (CPU)", "Topspin (CPU)", "Baseline (CPU)" };
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
        if (!netHost() || !m_lobby) return;
        std::string who = "Player " + std::to_string(id);
        for (const kke::net::RemotePlayer& p : m_net->remotePlayers())
            if (p.id == id) who = p.name;
        if (joined) m_lobby->lobby().toast(who + (m_inMenu ? " joined online" : " joined online: plays from the next match"), 5.0f);
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

// This screen's players as network players: the menu's seats (the first
// is NetModule's own player, the others local players 1..), then on the
// host its CPU players, the menu's and spare ones.
std::vector<TennisModule::Entry> TennisModule::netEntries() const {
    std::vector<Entry> out = seatEntries();
    if (netClient()) std::erase_if(out, [](const Entry& e) { return e.cpu; }); // a client's CPU players stay home
    else {
        for (int i = 0; i < kSpareCpus; ++i) {
            Entry e;
            e.name = kSpareNames[i];
            e.tint = glm::vec3(0.75f + 0.1f * static_cast<float>(i), 0.75f, 0.8f);
            e.cpu = true;
            e.filler = true;
            e.level = m_level;
            out.push_back(std::move(e));
        }
    }
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

TennisModule::Player* TennisModule::playerByNet(Match& m, int netId) {
    for (int idx : m.players)
        if (player(idx).netId == netId) return &player(idx);
    return nullptr;
}

void TennisModule::sendSetup(const Match& m) {
    net::Setup s;
    s.match = ++m_netMatch;
    s.court = static_cast<uint8_t>(m.court);
    s.teamSize = static_cast<uint8_t>(m.rules.teamSize);
    s.gamesPerSet = static_cast<uint8_t>(m.rules.gamesPerSet);
    s.setsToWin = static_cast<uint8_t>(m.rules.setsToWin);
    for (int idx : m.players) {
        const Player& p = m_players[static_cast<size_t>(idx)];
        s.seats.push_back({ static_cast<uint8_t>(std::max(0, p.netId)), static_cast<uint8_t>(p.team), static_cast<uint8_t>(p.slot),
                            p.cpu && !p.remote && p.input < 0, p.name, p.tint });
    }
    m_net->sendEvent(net::kEventSetup, net::encode(s));
    kke::log::get(name())->info("online match {}: {} players, sent to everyone", s.match, s.seats.size());
}

// A client: the host's match. Our own players are this screen's seats (by
// their network ids); everyone else's are drawn from what they send.
void TennisModule::applySetup(const net::Setup& s) {
    if (m_lobby) {
        if (m_lobby->isOpen()) {
            m_lobby->save();
            m_lobby->close();
        }
        m_lobby->applyInput();
    }
    m_inMenu = false;
    const std::vector<Entry> ours = netEntries();
    std::vector<Entry> entries;
    int mine = 0;
    for (const net::Seat& seat : s.seats) {
        Entry e;
        e.name = seat.name;
        e.tint = seat.tint;
        e.netId = seat.player;
        e.team = seat.team;
        e.slot = seat.slot;
        e.remote = true;
        if (m_net->isLocalPlayer(seat.player))
            for (const Entry& own : ours)
                if (own.netId == seat.player) {
                    e = own; // our own player, as picked here
                    e.team = seat.team;
                    e.slot = seat.slot;
                    e.remote = false;
                    ++mine;
                }
        entries.push_back(std::move(e));
    }
    MatchRules rules;
    rules.teamSize = std::clamp<int>(s.teamSize, 1, 2);
    rules.gamesPerSet = s.gamesPerSet;
    rules.setsToWin = s.setsToWin;
    m_netMatch = s.match;
    clearPlayers();
    buildMatch(std::move(entries), rules, std::clamp<int>(s.court, 0, SportCenter::kCourts - 1));
    kke::log::get(name())->info("online match {}: {} players, {} of them here", s.match, s.seats.size(), mine);
}

void TennisModule::onNetEvent(const kke::net::GameEventMsg& e) {
    const bool fromHost = e.fromPlayer == 0;
    if (e.kind == net::kEventSetup) {
        if (!netClient() || !fromHost) return; // only the host sets up a match
        if (auto s = net::decodeSetup(e.payload)) applySetup(*s);
        else kke::log::get(name())->warn("online: a damaged match setup");
        return;
    }
    if (e.kind == net::kEventEnd) {
        const auto end = net::decodeEnd(e.payload);
        if (!netClient() || !fromHost || !end || end->match != m_netMatch || m_matches.empty()) return;
        if (m_lobby) m_lobby->lobby().toast(end->why, 5.0f);
        kke::log::get(name())->info("online match {} ended: {}", end->match, end->why);
        backToMenu();
        return;
    }
    if (m_matches.empty()) return;
    Match& m = *m_matches.front();
    switch (e.kind) {
    case net::kEventServe: {
        const auto s = net::decodeServe(e.payload);
        if (!netClient() || !fromHost || !s || s->match != m_netMatch) return;
        m.serial = s->point;
        m.serveAgain = s->again;
        startPoint(m);
        m.rally.setSecondServe(s->second);
        if (s->second) m.sub = "Second serve";
        return;
    }
    case net::kEventPoint: {
        const auto p = net::decodePoint(e.payload);
        if (!netClient() || !fromHost || !p || p->match != m_netMatch || p->point != m.serial) return;
        if (p->result == 0 || p->result > static_cast<uint8_t>(Rally::Result::Let)) return;
        resolve(m, static_cast<Rally::Result>(p->result), p->call);
        return;
    }
    case net::kEventHit: {
        const auto h = net::decodeHit(e.payload);
        if (!h || h->match != m_netMatch || h->point != m.serial) return;
        Player* p = playerByNet(m, h->player);
        if (!p || !p->remote) return; // ours: already hit here
        const int idx = static_cast<int>(p - m_players.data());
        if (netHost()) {
            // The umpire checks it could be: their turn, this shot, the right phase.
            const bool phaseOk = h->serve ? m.phase == Match::Phase::Serve && idx == serverIndex(m) : m.phase == Match::Phase::Rally;
            if (!phaseOk || !m.rally.mayHit(p->team) || h->shot != m.rallyShots) {
                kke::log::get(name())->info("online: {}'s hit came too late (the point had moved on)", p->name);
                return;
            }
            m_net->relayEvent(e);
        }
        applyHit(m, idx, *h);
        return;
    }
    case net::kEventToss: {
        const auto t = net::decodeToss(e.payload);
        if (!t || t->match != m_netMatch || t->point != m.serial || m.phase != Match::Phase::Serve) return;
        Player* p = playerByNet(m, t->player);
        if (!p || !p->remote || static_cast<int>(p - m_players.data()) != serverIndex(m)) return;
        if (netHost()) m_net->relayEvent(e);
        p->tossAge = 0.0f;
        p->swingKind = SwingPose::Kind::Toss;
        m.ball->place(t->at, t->velocity);
        return;
    }
    case net::kEventBall: {
        const auto b = net::decodeBall(e.payload);
        if (!netClient() || !fromHost || !b || b->match != m_netMatch || b->point != m.serial || m.phase != Match::Phase::Rally) return;
        if (b->shot < m.rallyShots) return; // a hit of ours is on its way to the host
        Ball& ball = *m.ball;
        if (b->shot > m.rallyShots || glm::length(b->flight.pos - ball.position()) > 0.25f || glm::length(b->flight.vel - ball.velocity()) > 1.0f) {
            ball.follow(b->flight, b->rolling);
            m.rallyShots = b->shot;
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

void TennisModule::updateNet(float dt) {
    if (!m_net) return;
    m_netTime += dt;
    const bool isOnline = online();
    // Lost the game (the host left, or we were turned away): back to the menu.
    if (m_wasOnline && !isOnline) {
        if (m_lobby) m_lobby->lobby().toast("Left the online game: " + m_net->statusText(), 6.0f);
        bool shared = false;
        for (const Player& p : m_players) shared = shared || p.remote;
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
    if (m_matches.empty()) return;
    Match& m = *m_matches.front();

    // Everyone else's players, where their machines say they are.
    kke::RigidWorld& w = m_rigid->world();
    const std::vector<kke::net::RemotePlayer>& remote = m_net->remotePlayers();
    for (int idx : m.players) {
        Player& p = player(idx);
        if (!p.remote) continue;
        const auto it = std::find_if(remote.begin(), remote.end(), [&p](const kke::net::RemotePlayer& r) { return r.id == p.netId; });
        if (it == remote.end()) {
            if (!netHost()) continue; // the host says when a match is off
            const std::string why = p.name + " left the match";
            kke::log::get(name())->info("online: {}", why);
            m_net->sendEvent(net::kEventEnd, net::encode(net::End{ m_netMatch, why }));
            if (m_lobby) m_lobby->lobby().toast(why, 5.0f);
            clearPlayers(); // the End went out already
            if (m_lobby) {
                m_inMenu = true;
                m_lobby->open();
            }
            return;
        }
        if (!it->hasState) continue;
        p.pose = net::fromState(it->state);
        w.moveCharacter(p.body, p.pose.feet);
        w.setCharacterVelocity(p.body, p.pose.velocity);
    }

    // The host's ball, a few times a second: anyone drifting is put right.
    if (netHost() && m.phase == Match::Phase::Rally && m_netTime >= m_ballSentAt) {
        m_ballSentAt = m_netTime + kBallEvery;
        net::BallState b;
        b.match = m_netMatch;
        b.point = m.serial;
        b.shot = static_cast<uint8_t>(std::min(m.rallyShots, 255));
        b.flight = m.ball->flight();
        b.rolling = m.ball->rolling();
        m_net->sendEvent(net::kEventBall, net::encode(b));
    }
}

// Our players' poses, for everyone else (after they moved and swung).
void TennisModule::sendNet() {
    if (!m_net || !m_net->connected() || m_matches.empty()) return;
    const Match& m = *m_matches.front();
    const CourtPlace& place = m_center.courts[static_cast<size_t>(m.court)];
    kke::RigidWorld& w = m_rigid->world();
    for (int idx : m.players) {
        const Player& p = player(idx);
        if (p.remote || p.netId < 0) continue;
        int slot = -1;
        for (int s = 0; s < kke::NetModule::kMaxLocalPlayers && slot < 0; ++s)
            if (m_net->localPlayerId(s) == p.netId && (s == 0 || p.netId != 0)) slot = s;
        if (slot < 0) continue;
        net::Pose pose;
        pose.feet = w.characterPosition(p.body);
        pose.velocity = w.characterVelocity(p.body);
        pose.facing = place.dirToWorld(p.facing);
        pose.swing = p.tossAge >= 0.0f ? SwingPose::Kind::Toss : p.swingKind;
        pose.swingT = p.swingT;
        pose.contact = p.swingContact;
        pose.tossing = p.tossAge >= 0.0f;
        pose.cheer = p.cheer;
        pose.celebrating = p.celebrate > 0.0f;
        m_net->setLocalPlayer(slot, net::toState(pose));
    }
}

} // namespace tennis
