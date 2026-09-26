#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace kke::net {

// Who decides what, per player role (docs/ANTI_CHEAT.md "Server authority").
//
// The first rule of multiplayer anti-cheat: the server never takes a
// client's word for anything that matters. How strictly that applies is
// the game's call. A co-op sandbox can let players place blocks freely; a
// ranked shooter can't. So each role ("player", "spectator", "builder",
// "moderator", or the game's own) gets a policy per kind of action:
//
//   Client    the client decides; the server takes its word (relays it).
//             Cheap and lag-free; for things nobody gains by faking.
//   Checked   the client proposes, the server checks and may refuse:
//             moves against MovementLimits and NetServer::checkMove
//             (kke/net/WorldMoveCheck.h), events through
//             NetServer::checkEvent when the game sets one.
//   Server    only the server decides; the client's messages for it are
//             dropped (spectators can't move a player; in a competitive
//             game, damage and score come from the server alone).
//
// The server applies it in NetServer (authority member). The defaults
// are today's behaviour: players' moves Checked, their events Checked
// (which passes when the game sets no checkEvent).
enum class Authority : uint8_t { Client, Checked, Server };

enum class Action : uint8_t {
    Move,   // the player's own state (position, velocity, animation)
    Event,  // a game event (a shot, a push, "place block")
    Count
};

struct RolePolicy {
    Authority move = Authority::Checked;
    Authority events = Authority::Checked;

    Authority of(Action a) const { return a == Action::Move ? move : events; }
    bool operator==(const RolePolicy&) const = default;
};

class AuthorityPolicy {
public:
    // Players' moves checked, events trusted unless the game checks them;
    // a "spectator" role that can do neither. Friends playing together.
    static AuthorityPolicy coop();
    // Players' moves and events all checked by the server; spectators
    // server-only. For anything with a leaderboard or a prize.
    static AuthorityPolicy competitive();

    static constexpr const char* kDefaultRole = "player";

    // Defines (or replaces) a role. Names are the game's own, 1..32
    // printable characters; false (and nothing changes) for others.
    bool setRole(const std::string& role, const RolePolicy& policy);
    bool hasRole(const std::string& role) const { return m_roles.count(role) != 0; }
    // The role's policy; an unknown role gets the default role's.
    const RolePolicy& role(const std::string& role) const;

    // Puts a player (NetServer id) in a role; false for an unknown role
    // (the player keeps its current one). Unassigned players have
    // kDefaultRole.
    bool assign(uint8_t player, const std::string& role);
    const std::string& roleOf(uint8_t player) const;
    void forget(uint8_t player) { m_assigned.erase(player); }

    Authority authority(uint8_t player, Action action) const { return role(roleOf(player)).of(action); }

    size_t roleCount() const { return m_roles.size(); }

private:
    std::map<std::string, RolePolicy> m_roles{ { kDefaultRole, RolePolicy{} } };
    std::map<uint8_t, std::string> m_assigned;
};

const char* toString(Authority a);

} // namespace kke::net
