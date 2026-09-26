#include "kke/net/Authority.h"

namespace kke::net {

namespace {

bool validRoleName(const std::string& role) {
    if (role.empty() || role.size() > 32) return false;
    for (const char c : role)
        if (c < 0x21 || c > 0x7e) return false; // printable ASCII, no spaces
    return true;
}

} // namespace

AuthorityPolicy AuthorityPolicy::coop() {
    AuthorityPolicy p;
    p.setRole(kDefaultRole, RolePolicy{ Authority::Checked, Authority::Client });
    p.setRole("spectator", RolePolicy{ Authority::Server, Authority::Server });
    return p;
}

AuthorityPolicy AuthorityPolicy::competitive() {
    AuthorityPolicy p;
    p.setRole(kDefaultRole, RolePolicy{ Authority::Checked, Authority::Checked });
    p.setRole("spectator", RolePolicy{ Authority::Server, Authority::Server });
    return p;
}

bool AuthorityPolicy::setRole(const std::string& role, const RolePolicy& policy) {
    if (!validRoleName(role)) return false;
    m_roles[role] = policy;
    return true;
}

const RolePolicy& AuthorityPolicy::role(const std::string& role) const {
    if (auto it = m_roles.find(role); it != m_roles.end()) return it->second;
    return m_roles.at(kDefaultRole); // always present: the constructor adds it and setRole only replaces
}

bool AuthorityPolicy::assign(uint8_t player, const std::string& role) {
    if (!hasRole(role)) return false;
    if (role == kDefaultRole) m_assigned.erase(player);
    else m_assigned[player] = role;
    return true;
}

const std::string& AuthorityPolicy::roleOf(uint8_t player) const {
    static const std::string defaultRole = kDefaultRole;
    if (auto it = m_assigned.find(player); it != m_assigned.end()) return it->second;
    return defaultRole;
}

const char* toString(Authority a) {
    switch (a) {
    case Authority::Client: return "client";
    case Authority::Checked: return "checked";
    case Authority::Server: return "server";
    }
    return "unknown";
}

} // namespace kke::net
