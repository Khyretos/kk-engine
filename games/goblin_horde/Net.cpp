// Online (README.md "Online", docs/NETWORKING.md). Player 1 sets Online to
// Host or Join in the start menu; everyone at each screen plays (a second
// person at a screen is a NetModule local player), so split screen and
// online work together.
//
//  - The host runs the horde: the waves, every goblin's mind and moves.
//    About 15 times a second it sends everyone where each goblin is, what
//    it's doing and how hurt it is (Horde). Its goblins' shots, the
//    warnings on the ground and the fire that follows go to everyone as
//    they happen (Fx), so every screen sees them.
//  - Each screen runs its own players (their NetPlayerState: where they
//    are, the move, the bow's draw) and decides what its swings and
//    arrows hit; it tells the host (Hit), and the host's goblin takes it.
//  - A goblin's blow on someone else's player: the host sends it to that
//    player's screen (Hurt), which decides the block, the parry or the
//    roll, as if the goblin were there.

#include "HordeModule.h"

#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/net/BitStream.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace horde {

namespace {

namespace kn = kke::net;

constexpr int kModeOff = 0, kModeJoin = 1, kModeHost = 2;
constexpr float kSearchEvery = 3.0f;
constexpr float kSendEvery = 1.0f / 15.0f;

constexpr uint16_t kGameEventBase = 0x4800;           // Goblin Horde's own event kinds
constexpr uint16_t kEventHorde = kGameEventBase + 0;  // host -> all: the wave and every goblin (Horde)
constexpr uint16_t kEventFx = kGameEventBase + 1;     // host -> all, or a screen -> host -> all: a shot, a warning, a burst (Fx)
constexpr uint16_t kEventHit = kGameEventBase + 2;    // a screen -> host: its player hit a goblin (Hit)
constexpr uint16_t kEventHurt = kGameEventBase + 3;   // host -> a screen: a goblin hit its player (Hurt)

constexpr float kRange = 120.0f; // metres from the middle of the fort, either way

struct FoeNet {
    uint16_t id = 0;
    bool boss = false, dead = false;
    uint8_t type = 0, look = 0, skin = 0;
    int8_t weapon = -1, offhand = -1;
    glm::vec3 position{ 0.0f };
    float yaw = 0.0f;
    uint8_t state = 0;
    int8_t move = -1;
    uint8_t comboHit = 0;
    float health = 1.0f;
};

struct HordeMsg {
    uint8_t round = 0, wave = 1, phase = 0;
    uint16_t left = 0, kills = 0;
    std::vector<FoeNet> foes;
};

template <typename Stream> void serialize(Stream& s, FoeNet& f) {
    s.integer(f.id, 0, 65535);
    s.boolean(f.boss);
    s.boolean(f.dead);
    s.integer(f.type, 0, 31);
    s.integer(f.look, 0, 31);
    s.integer(f.skin, 0, 15);
    s.integer(f.weapon, -1, 30);
    s.integer(f.offhand, -1, 30);
    s.vec3(f.position, kRange, 0.02f);
    s.real(f.yaw, -180.0f, 180.0f, 360.0f / 255.0f);
    s.integer(f.state, 0, 7);
    s.integer(f.move, -1, 6);
    s.integer(f.comboHit, 0, 3);
    s.real(f.health, 0.0f, 1.0f, 1.0f / 127.0f);
}

template <typename Stream> void serialize(Stream& s, HordeMsg& m) {
    s.integer(m.round, 0, 255);
    s.integer(m.wave, 0, 255);
    s.integer(m.phase, 0, 7);
    s.integer(m.left, 0, 65535);
    s.integer(m.kills, 0, 65535);
    uint16_t n = static_cast<uint16_t>(m.foes.size());
    s.integer(n, 0, 1023);
    if constexpr (Stream::kReading) m.foes.resize(n);
    for (FoeNet& f : m.foes) serialize(s, f);
}

// One thing to show: a shot (kind), a warning (shape), a burst (blast).
struct FxMsg {
    uint8_t what = 0; // 0 shot, 1 mark, 2 blast
    std::string kind; // shot: arrow, bolt, fireball, zap, boulder, heal; blast: element
    glm::vec3 position{ 0.0f }, velocity{ 0.0f };
    float gravity = 0.0f, radius = 0.0f, length = 0.0f, time = 0.0f;
    uint8_t shape = 0, team = 1;
    bool super = false;
};

template <typename Stream> void serialize(Stream& s, FxMsg& m) {
    s.integer(m.what, 0, 3);
    s.string(m.kind, 16);
    s.vec3(m.position, kRange, 0.01f);
    s.vec3(m.velocity, 127.0f, 0.02f); // a mark: its direction
    s.real(m.gravity, 0.0f, 20.0f, 0.05f);
    s.real(m.radius, 0.0f, 180.0f, 0.05f); // a cone: its half angle
    s.real(m.length, 0.0f, 30.0f, 0.05f);
    s.real(m.time, 0.0f, 10.0f, 0.01f);
    s.integer(m.shape, 0, 3);
    s.integer(m.team, 0, 1);
    s.boolean(m.super);
}

// A blow, either way: what it does, where from, where it landed.
struct BlowMsg {
    uint16_t foe = 0;     // Hit: the goblin
    uint8_t player = 0;   // Hit: who struck; Hurt: who's hit
    float damage = 0.0f, poise = 0.0f, knockback = 0.0f, stun = 0.0f, chip = 0.0f, guard = 0.0f;
    bool unblockable = false;
    glm::vec3 from{ 0.0f }, point{ 0.0f };
};

template <typename Stream> void serialize(Stream& s, BlowMsg& m) {
    s.integer(m.foe, 0, 65535);
    s.integer(m.player, 0, 255);
    s.real(m.damage, 0.0f, 500.0f, 0.1f);
    s.real(m.poise, 0.0f, 500.0f, 0.1f);
    s.real(m.knockback, 0.0f, 30.0f, 0.05f);
    s.real(m.stun, 0.0f, 4.0f, 0.01f);
    s.real(m.chip, 0.0f, 1.0f, 0.01f);
    s.real(m.guard, 0.0f, 200.0f, 0.1f);
    s.boolean(m.unblockable);
    s.vec3(m.from, kRange, 0.02f);
    s.vec3(m.point, kRange, 0.02f);
}

template <typename T> std::vector<uint8_t> encode(T m) {
    std::vector<uint8_t> out;
    {
        kn::WriteStream w(out);
        serialize(w, m);
    }
    return out;
}

template <typename T> bool decode(const std::vector<uint8_t>& bytes, T& m) {
    kn::ReadStream r(bytes.data(), bytes.size());
    serialize(r, m);
    return r.ok() && r.bitsLeft() < 8;
}

// NetPlayerState::extra: the weapon, the move it plays, its health.
constexpr uint8_t kFlagAiming = 1u << 0, kFlagDrawing = 1u << 1, kFlagCharged = 1u << 2;

} // namespace

bool HordeModule::netClient() const { return m_net && m_net->role() == kke::NetModule::Role::Client; }
bool HordeModule::netHost() const { return m_net && m_net->role() == kke::NetModule::Role::Host; }

void HordeModule::setupNet() {
    if (!m_net) return;
    m_net->standIns = false; // the players are drawn here, as they dressed
    m_net->addEventListener([this](const kke::net::GameEventMsg& e) { onNetEvent(e); });
    m_net->onPlayer = [this](uint8_t id, bool joined) {
        if (!netHost() || !m_lobby) return;
        std::string who = "Player " + std::to_string(id);
        for (const kke::net::RemotePlayer& p : m_net->remotePlayers())
            if (p.id == id) who = p.name;
        m_lobby->lobby().toast(who + (joined ? " joined online" : " left"), 5.0f);
    };
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
    // Or type it: the host's address (over a VPN, where the search can't
    // reach) or a join code. Remembered in the lobby file.
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

// This screen's players as network players: the first is NetModule's own
// player, the others its local players. What they picked travels in the
// character text ("character,skin,accessory,weapon,colour").
void HordeModule::syncNetPlayers() {
    if (!m_net) return;
    int slot = 0;
    for (const Entry& e : wantedHeroes()) {
        if (e.remote) continue;
        if (slot >= kke::NetModule::kMaxLocalPlayers) break;
        int colour = 0;
        const glm::vec3 colours[] = { { 0.95f, 0.72f, 0.25f }, { 0.35f, 0.65f, 1.0f }, { 0.4f, 0.85f, 0.4f }, { 0.9f, 0.4f, 0.75f } };
        for (int c = 0; c < 4; ++c)
            if (glm::distance(colours[c], e.color) < 0.01f) colour = c;
        char character[64];
        std::snprintf(character, sizeof(character), "%d,%d,%d,%d,%d", e.character, e.skin, e.accessory, e.weapon, colour);
        if (slot == 0) {
            m_net->playerName = e.name;
            m_net->playerCharacter = character;
        } else {
            m_net->addLocalPlayer(slot, e.name, character);
        }
        ++slot;
    }
    for (int s = std::max(1, slot); s < kke::NetModule::kMaxLocalPlayers; ++s) m_net->removeLocalPlayer(s);
}

void HordeModule::updateNet(float dt) {
    if (!m_net) return;
    m_netTime += dt;
    const bool online = m_net->role() != kke::NetModule::Role::Offline;
    if (m_wasOnline && !online) {
        if (m_lobby) m_lobby->lobby().toast("Left the online game: " + m_net->statusText(), 6.0f);
        for (size_t i = m_heroes.size(); i-- > 0;)
            if (m_heroes[i]->remote) {
                removeHero(*m_heroes[i]);
                m_heroes.erase(m_heroes.begin() + static_cast<std::ptrdiff_t>(i));
            }
        if (m_phase != Phase::Lobby) backToLobby();
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
        std::string status = "Hold the ruins together. Another controller? Press {a} on it to join.";
        if (netHost()) {
            const size_t others = m_net->remotePlayers().size();
            status = "Hosting: " + std::to_string(others) + (others == 1 ? " player" : " players") + " online. On another PC, set Online to Join.";
        } else if (netClient()) {
            status = m_net->connected() ? "Online: the host starts the fight." : "Joining: " + m_net->statusText();
        } else if (mode && mode->value == kModeJoin) {
            status = "Looking for games on this network and this PC...";
        }
        if (rows || status != m_lastNetStatus) {
            m_lastNetStatus = status;
            m_lobby->setTitle("GOBLIN HORDE", status);
        }
    }
    if (!online) return;
    syncNetPlayers();

    // Every other screen's players, where they say they are; in and out
    // as they come and go.
    const std::vector<kke::net::RemotePlayer>& remote = m_net->remotePlayers();
    for (size_t i = m_heroes.size(); i-- > 0;) {
        Hero& h = *m_heroes[i];
        if (!h.remote) continue;
        const auto it = std::find_if(remote.begin(), remote.end(), [&h](const kke::net::RemotePlayer& r) { return r.id == h.netId; });
        if (it == remote.end() || m_net->isLocalPlayer(static_cast<uint8_t>(h.netId))) {
            kke::log::get(name())->info("{} left the fight", h.name);
            removeHero(h);
            m_heroes.erase(m_heroes.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        if (!it->hasState) continue;
        const kn::NetPlayerState& s = it->state;
        h.position = s.position;
        h.velocity = s.velocity;
        h.facing = glm::vec3(std::sin(glm::radians(s.yaw)), 0.0f, std::cos(glm::radians(s.yaw)));
        h.netState = s.state;
        h.netFlags = s.flags;
        h.aiming = (s.flags & kFlagAiming) != 0;
        h.drawing = (s.flags & kFlagDrawing) != 0;
        h.charged = (s.flags & kFlagCharged) != 0;
        h.draw = s.progress * weaponOf(h).draw;
        h.spin = s.aux / 32.0f * 360.0f;
        if (s.extra.size() >= 3) {
            if (s.extra[0] != h.weapon && s.extra[0] < m_roster.weapons.size()) equip(h, s.extra[0]);
            h.moveState = static_cast<int8_t>(s.extra[1]);
            h.netProgress = static_cast<float>(s.extra[2]) / 255.0f;
        }
    }
    if (m_phase != Phase::Lobby)
        for (const kke::net::RemotePlayer& p : remote) {
            if (m_net->isLocalPlayer(p.id)) continue;
            bool known = false;
            for (const auto& h : m_heroes) known = known || (h->remote && h->netId == p.id);
            if (known) continue;
            // In from now on, as they dressed.
            for (const Entry& e : wantedHeroes()) {
                if (!e.remote || e.netId != p.id) continue;
                auto h = std::make_unique<Hero>();
                h->remote = true;
                h->netId = e.netId;
                h->name = e.name;
                h->color = e.color;
                h->character = e.character;
                h->skin = e.skin;
                h->accessory = e.accessory;
                h->weapon = e.weapon;
                spawnHero(*h, p.hasState ? p.state.position : glm::vec3(0.0f, 0.05f, 2.0f));
                kke::log::get(name())->info("{} joined the fight", h->name);
                m_heroes.push_back(std::move(h));
            }
        }
    // Our players' ids, for the host's hits.
    for (auto& h : m_heroes)
        if (!h->remote && h->slot < kke::NetModule::kMaxLocalPlayers) h->netId = m_net->localPlayerId(h->slot);
}

// Ours, for everyone else; on the host, the horde too.
void HordeModule::sendNet(float dt) {
    if (!m_net || !m_net->connected()) return;
    for (const auto& hp : m_heroes) {
        const Hero& h = *hp;
        if (h.remote || h.slot >= kke::NetModule::kMaxLocalPlayers || !h.id) continue;
        const kke::Combatant& c = m_combat.get(h.id);
        kn::NetPlayerState s;
        s.position = heroFeet(h);
        s.velocity = h.body ? m_rigid->world().characterVelocity(h.body) : h.velocity;
        s.yaw = glm::degrees(std::atan2(h.facing.x, h.facing.z));
        s.state = static_cast<uint8_t>(c.state());
        s.flags = static_cast<uint8_t>((h.aiming ? kFlagAiming : 0) | (h.drawing ? kFlagDrawing : 0) | (h.charged ? kFlagCharged : 0));
        s.speed = std::min(20.0f, glm::length(glm::vec2(s.velocity.x, s.velocity.z)));
        s.progress = h.drawing ? std::clamp(h.draw / std::max(0.1f, weaponOf(h).draw), 0.0f, 1.0f) : 0.0f;
        s.aux = std::clamp(h.spin / 360.0f * 32.0f, 0.0f, 32.0f);
        s.extra = { static_cast<uint8_t>(h.weapon), static_cast<uint8_t>(static_cast<int8_t>(h.moveState)),
                    static_cast<uint8_t>(std::clamp(c.healthFraction(), 0.0f, 1.0f) * 255.0f) };
        m_net->setLocalPlayer(h.slot, s);
    }
    if (!netHost()) return;
    m_netSendAt -= dt;
    if (m_netSendAt > 0.0f) return;
    m_netSendAt = kSendEvery;
    HordeMsg m;
    m.round = static_cast<uint8_t>(m_round & 0xFF);
    m.wave = static_cast<uint8_t>(std::clamp(m_wave, 0, 255));
    m.phase = static_cast<uint8_t>(m_phase);
    m.left = static_cast<uint16_t>(std::clamp(aliveFoes() + m_toSpawn, 0, 65535));
    m.kills = static_cast<uint16_t>(std::clamp(m_kills, 0, 65535));
    for (const auto& fp : m_foes) {
        const Foe& f = *fp;
        if (f.dead && f.deadTime > 6.0f) continue;
        FoeNet n;
        n.id = f.netId;
        n.boss = f.type->boss;
        n.dead = f.dead;
        n.type = static_cast<uint8_t>(std::clamp(f.typeIndex, 0, 31));
        n.look = static_cast<uint8_t>(std::clamp(f.lookIndex, 0, 31));
        n.skin = static_cast<uint8_t>(std::clamp(f.skin, 0, 15));
        n.weapon = static_cast<int8_t>(std::clamp(f.weaponIndex, -1, 30));
        n.offhand = static_cast<int8_t>(std::clamp(f.offhandIndex, -1, 30));
        n.position = f.position;
        n.yaw = std::fmod(f.yaw + 540.0f, 360.0f) - 180.0f;
        n.state = f.dead ? 7 : static_cast<uint8_t>(m_combat.get(f.id).state());
        n.move = static_cast<int8_t>(std::clamp(f.move, -1, 6));
        n.comboHit = static_cast<uint8_t>(std::clamp(f.comboHit, 0, 3));
        n.health = f.dead ? 0.0f : f.health;
        m.foes.push_back(n);
    }
    m_net->sendEvent(kEventHorde, encode(m));
}

void HordeModule::sendHeroHit(int foeNet, const kke::AttackDesc& a, const glm::vec3& from, const glm::vec3& point, int byHero) {
    if (!m_net || !netClient()) return;
    BlowMsg b;
    b.foe = static_cast<uint16_t>(foeNet);
    b.player = byHero >= 0 && byHero < static_cast<int>(m_heroes.size()) ? static_cast<uint8_t>(m_heroes[static_cast<size_t>(byHero)]->netId) : 0;
    b.damage = a.damage;
    b.poise = a.poiseDamage;
    b.knockback = a.knockback;
    b.stun = a.hitStun;
    b.from = from;
    b.point = point;
    m_net->sendEvent(kEventHit, encode(b));
}

void HordeModule::sendHurt(const Hero& h, const kke::AttackDesc& a, const glm::vec3& from, const glm::vec3& point) {
    if (!m_net || !netHost() || h.netId < 0) return;
    BlowMsg b;
    b.player = static_cast<uint8_t>(h.netId);
    b.damage = a.damage;
    b.poise = a.poiseDamage;
    b.knockback = a.knockback;
    b.stun = a.hitStun;
    b.chip = a.chip;
    b.guard = a.guardDamage;
    b.unblockable = a.unblockable;
    b.from = from;
    b.point = point;
    m_net->sendEventTo(b.player, kEventHurt, encode(b));
}

void HordeModule::sendFx(const Shot* shot, const Mark* mark, const Blast* blast) {
    if (!m_net || !m_net->connected()) return;
    FxMsg m;
    if (shot) {
        m.what = 0;
        m.kind = shot->kind;
        m.position = shot->position;
        m.velocity = shot->velocity;
        m.gravity = shot->gravity;
        m.time = std::min(shot->life, 10.0f);
        m.team = static_cast<uint8_t>(shot->team);
    } else if (mark) {
        m.what = 1;
        m.shape = static_cast<uint8_t>(mark->shape);
        m.position = mark->center;
        m.velocity = mark->dir;
        m.radius = mark->radius;
        m.length = mark->length;
        m.time = mark->total;
        m.super = mark->super;
    } else if (blast) {
        m.what = 2;
        m.kind = blast->element;
        m.position = blast->center;
        m.radius = blast->radius;
        m.time = blast->delay;
    }
    m_net->sendEvent(kEventFx, encode(m));
}

void HordeModule::onNetEvent(const kke::net::GameEventMsg& e) {
    auto log = kke::log::get(name());
    if (e.kind == kEventHorde) {
        if (!netClient() || e.fromPlayer != 0) return; // only the host's horde
        HordeMsg m;
        if (!decode(e.payload, m)) {
            log->warn("online: a damaged horde update");
            return;
        }
        const Phase phase = static_cast<Phase>(std::min<int>(m.phase, static_cast<int>(Phase::Overrun)));
        if (m_phase == Phase::Lobby && phase != Phase::Lobby) {
            // The host started: in we go, as we picked.
            if (m_lobby && m_lobby->isOpen()) {
                m_lobby->save();
                m_lobby->close();
            }
            if (m_lobby) m_lobby->applyInput();
            buildHeroes(wantedHeroes());
            m_startWave = m.wave;
            m_phase = phase;
            restart();
            m_round = m.round;
            log->info("online: the host's fight, wave {}", m.wave);
        } else if (phase == Phase::Lobby && m_phase != Phase::Lobby) {
            backToLobby();
            return;
        }
        if (m.round != static_cast<uint8_t>(m_round & 0xFF)) {
            // Started over.
            m_startWave = m.wave;
            restart();
            m_round = m.round;
        }
        if (m.wave != m_wave) {
            m_wave = m.wave;
            // A new wave: whoever went down here is back.
            for (auto& h : m_heroes)
                if (!h->remote && h->id && !m_combat.get(h->id).alive()) {
                    m_combat.get(h->id).reset();
                    h->downTime = 0.0f;
                    h->look.lastState = -1;
                }
        }
        if (phase != m_phase) {
            m_phase = phase;
            m_phaseTime = 0.0f;
        }
        m_toSpawn = std::max(0, static_cast<int>(m.left) - aliveFoes());
        m_kills = m.kills;
        // The horde as the host sees it.
        for (const FoeNet& n : m.foes) {
            Foe* f = foeByNet(n.id);
            const std::vector<FoeType>& list = n.boss ? m_roster.bosses : m_roster.types;
            if (!f) {
                if (n.dead || n.type >= list.size()) continue;
                f = &spawnFoe(n.type, n.boss, n.position, n.id, n.look, n.skin, n.weapon, n.offhand);
            }
            f->netPos = n.position;
            f->netYaw = n.yaw;
            f->netState = n.state;
            f->netMove = n.move;
            f->netComboHit = n.comboHit;
            f->health = n.health;
            if (n.dead && !f->dead) {
                f->velocity = glm::vec3(0.0f);
                killFoe(*f, glm::vec3(0.0f, 1.0f, 0.0f));
            }
        }
        // Gone on the host: gone here.
        for (size_t i = 0; i < m_foes.size();) {
            Foe& f = *m_foes[i];
            const bool listed = std::any_of(m.foes.begin(), m.foes.end(), [&f](const FoeNet& n) { return n.id == f.netId; });
            if (!listed && !(f.dead && f.deadTime < 9.0f)) {
                releaseFoe(f);
                m_foes.erase(m_foes.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            ++i;
        }
        return;
    }
    if (e.kind == kEventFx) {
        FxMsg m;
        if (!decode(e.payload, m)) {
            log->warn("online: a damaged effect");
            return;
        }
        if (netHost()) m_net->relayEvent(e); // a client's arrow: everyone else sees it too
        if (m.what == 0) {
            Shot s;
            s.kind = m.kind;
            s.position = m.position;
            s.velocity = m.velocity;
            s.gravity = m.gravity;
            s.life = std::max(0.1f, m.time);
            s.team = m.team;
            s.local = false; // only a sight: whoever shot it decides what it hits
            fire(std::move(s));
        } else if (m.what == 1 && netClient()) {
            Mark k;
            k.shape = static_cast<Mark::Shape>(std::min<int>(m.shape, 2));
            k.center = m.position;
            k.dir = glm::length(m.velocity) > 1e-3f ? glm::normalize(m.velocity) : glm::vec3(0, 0, 1);
            k.radius = m.radius;
            k.length = m.length;
            k.total = m.time;
            k.super = m.super;
            k.color = m.super ? glm::vec3(1.0f, 0.1f, 0.05f) : glm::vec3(1.0f, 0.45f, 0.1f);
            mark(k);
        } else if (m.what == 2 && netClient()) {
            Blast b;
            b.center = m.position;
            b.radius = m.radius;
            b.delay = m.time;
            b.element = m.kind;
            b.hit.damage = 0.0f;
            blast(b);
        }
        return;
    }
    if (e.kind == kEventHit) {
        if (!netHost()) return;
        BlowMsg b;
        if (!decode(e.payload, b)) {
            log->warn("online: a damaged hit");
            return;
        }
        Foe* f = foeByNet(b.foe);
        if (!f || f->dead) return;
        int by = -1;
        for (size_t i = 0; i < m_heroes.size(); ++i)
            if (m_heroes[i]->remote && m_heroes[i]->netId == b.player) by = static_cast<int>(i);
        if (by < 0) return; // not someone in this fight
        kke::AttackDesc a = kke::AttackDesc::light();
        a.name = "online";
        a.damage = b.damage;
        a.poiseDamage = b.poise;
        a.knockback = b.knockback;
        a.hitStun = b.stun;
        hurtFoe(*f, a, b.from, b.point, by);
        return;
    }
    if (e.kind == kEventHurt) {
        if (!netClient() || e.fromPlayer != 0) return;
        BlowMsg b;
        if (!decode(e.payload, b)) {
            log->warn("online: a damaged blow");
            return;
        }
        for (auto& h : m_heroes)
            if (!h->remote && h->netId == b.player) {
                kke::AttackDesc a = kke::AttackDesc::light();
                a.name = "goblin";
                a.damage = b.damage;
                a.poiseDamage = b.poise;
                a.knockback = b.knockback;
                a.hitStun = b.stun;
                a.chip = b.chip;
                a.guardDamage = b.guard;
                a.unblockable = b.unblockable;
                hurtHero(*h, a, b.from, b.point);
            }
    }
}

} // namespace horde
