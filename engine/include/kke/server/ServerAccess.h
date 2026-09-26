#pragma once

#include <string>
#include <vector>

namespace kke::server {

// Who may join and who may run console commands (docs/SERVER_HOSTING.md
// "Security"), like 7 Days to Die's serveradmin.xml: saveDir/access.json.
//
//   { "admins": ["Kees"],
//     "bans": [ { "name": "griefer", "address": "203.0.113.7", "reason": "..." } ],
//     "allow": [] }                // non-empty: only these names may join
//
// Names compare without case ("Kees" == "kees"). A ban matches on its name
// or its address, whichever it has. The console edits it and save()
// writes it atomically; an owner may also edit the file while the server
// is stopped.
class ServerAccess {
public:
    struct Ban {
        std::string name;    // "" = address only
        std::string address; // "" = name only
        std::string reason;
    };

    // "" when the player may join; otherwise why not, as shown to them.
    std::string admit(const std::string& name, const std::string& address) const;

    bool isAdmin(const std::string& name) const;
    // Adds (or updates) a ban; false when it names nothing.
    bool ban(const std::string& name, const std::string& address, const std::string& reason);
    // Removes every ban on this name or address; how many went.
    size_t unban(const std::string& nameOrAddress);
    void addAdmin(const std::string& name);
    void allow(const std::string& name);

    const std::vector<Ban>& bans() const { return m_bans; }
    const std::vector<std::string>& admins() const { return m_admins; }
    const std::vector<std::string>& allowList() const { return m_allow; }

    std::string toJson() const;
    // Keeps what it could read; "where: what" lines in `errors` for the rest.
    bool fromJson(const std::string& text, std::vector<std::string>& errors);
    // A missing file is an empty list (true). Errors are reported, never thrown.
    bool load(const std::string& path, std::vector<std::string>& errors);
    bool save(const std::string& path, std::string* error = nullptr) const;

private:
    std::vector<std::string> m_admins, m_allow;
    std::vector<Ban> m_bans;
};

// Lower-cased ASCII, for comparing names.
std::string foldName(const std::string& name);

} // namespace kke::server
