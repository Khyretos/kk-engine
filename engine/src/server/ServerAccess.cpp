#include "kke/server/ServerAccess.h"

#include "kke/server/ServerFiles.h"

#include "kke/DataFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace kke::server {

std::string foldName(const std::string& name) {
    std::string out = name;
    std::transform(out.begin(), out.end(), out.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    return out;
}

namespace {
bool containsName(const std::vector<std::string>& list, const std::string& name) {
    const std::string n = foldName(name);
    return std::any_of(list.begin(), list.end(), [&](const std::string& e) { return foldName(e) == n; });
}
} // namespace

std::string ServerAccess::admit(const std::string& name, const std::string& address) const {
    const std::string n = foldName(name);
    for (const Ban& b : m_bans) {
        const bool byName = !b.name.empty() && foldName(b.name) == n;
        const bool byAddress = !b.address.empty() && b.address == address;
        if (byName || byAddress) return b.reason.empty() ? "you are banned from this server" : "you are banned from this server: " + b.reason;
    }
    if (!m_allow.empty() && !containsName(m_allow, name) && !containsName(m_admins, name)) return "this server only lets in players on its list";
    return {};
}

bool ServerAccess::isAdmin(const std::string& name) const { return containsName(m_admins, name); }

bool ServerAccess::ban(const std::string& name, const std::string& address, const std::string& reason) {
    if (name.empty() && address.empty()) return false;
    for (Ban& b : m_bans)
        if ((!name.empty() && foldName(b.name) == foldName(name)) || (!address.empty() && b.address == address)) {
            if (!name.empty()) b.name = name;
            if (!address.empty()) b.address = address;
            b.reason = reason;
            return true;
        }
    m_bans.push_back({ name, address, reason });
    return true;
}

size_t ServerAccess::unban(const std::string& nameOrAddress) {
    const std::string n = foldName(nameOrAddress);
    const size_t before = m_bans.size();
    std::erase_if(m_bans, [&](const Ban& b) { return (!b.name.empty() && foldName(b.name) == n) || (!b.address.empty() && b.address == nameOrAddress); });
    return before - m_bans.size();
}

void ServerAccess::addAdmin(const std::string& name) {
    if (!name.empty() && !containsName(m_admins, name)) m_admins.push_back(name);
}

void ServerAccess::allow(const std::string& name) {
    if (!name.empty() && !containsName(m_allow, name)) m_allow.push_back(name);
}

std::string ServerAccess::toJson() const {
    nlohmann::json j;
    j["admins"] = m_admins;
    j["allow"] = m_allow;
    j["bans"] = nlohmann::json::array();
    for (const Ban& b : m_bans) j["bans"].push_back({ { "name", b.name }, { "address", b.address }, { "reason", b.reason } });
    return j.dump(2) + "\n";
}

bool ServerAccess::fromJson(const std::string& text, std::vector<std::string>& errors) {
    const size_t before = errors.size();
    nlohmann::json j;
    const bool parsed = datafile::parseAny(text, j);
    if (!parsed || !j.is_object()) {
        errors.push_back("access.json: not a JSON or YAML object");
        return false;
    }
    auto names = [&](const char* key, std::vector<std::string>& out) {
        out.clear();
        if (!j.contains(key)) return;
        if (!j[key].is_array()) {
            errors.push_back(std::string("access.json \"") + key + "\": expected a list of names");
            return;
        }
        for (const auto& e : j[key]) {
            if (e.is_string() && !e.get<std::string>().empty()) out.push_back(e.get<std::string>());
            else errors.push_back(std::string("access.json \"") + key + "\": skipped an entry that isn't a name");
        }
    };
    names("admins", m_admins);
    names("allow", m_allow);
    m_bans.clear();
    if (j.contains("bans")) {
        if (!j["bans"].is_array()) errors.push_back("access.json \"bans\": expected a list");
        else
            for (const auto& e : j["bans"]) {
                auto field = [&](const char* key) { return e.is_object() && e.contains(key) && e[key].is_string() ? e[key].get<std::string>() : std::string(); };
                Ban b{ field("name"), field("address"), field("reason") };
                if (b.name.empty() && b.address.empty()) errors.push_back("access.json \"bans\": skipped a ban with neither a name nor an address");
                else m_bans.push_back(std::move(b));
            }
    }
    return errors.size() == before;
}

bool ServerAccess::load(const std::string& path, std::vector<std::string>& errors) {
    std::string text;
    bool exists = false;
    if (!datafile::readText(path, text, &exists)) {
        if (!exists) return true;
        errors.push_back(path + ": can't read it");
        return false;
    }
    return fromJson(text, errors);
}

bool ServerAccess::save(const std::string& path, std::string* error) const {
    const std::string target = datafile::saveTarget(path).string(); // access.yml stays YAML
    return writeFileAtomic(target, datafile::forFile(toJson(), target), error);
}

} // namespace kke::server
