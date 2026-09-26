#include "kke/server/Directory.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>

namespace kke::server {

namespace {

constexpr size_t kMaxName = 24, kMaxGame = 32, kMaxRoles = 8, kMaxRole = 16;

bool printable(const std::string& s) {
    return std::none_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7F; });
}

// Sane from anyone: names that fit a list row, known limits.
bool sane(const DirectoryEntry& e) {
    if (e.name.empty() || e.name.size() > kMaxName || !printable(e.name)) return false;
    if (e.game.empty() || e.game.size() > kMaxGame || !printable(e.game)) return false;
    if (e.port == 0 || e.maxPlayers == 0 || e.maxPlayers > 256 || e.players > e.maxPlayers) return false;
    if (e.roles.size() > kMaxRoles) return false;
    return std::all_of(e.roles.begin(), e.roles.end(), [](const std::string& r) { return !r.empty() && r.size() <= kMaxRole && printable(r); });
}

nlohmann::json toJson(const DirectoryEntry& e, bool withAddress) {
    nlohmann::json j{ { "name", e.name }, { "game", e.game }, { "port", e.port }, { "players", e.players }, { "max", e.maxPlayers },
                      { "password", e.password }, { "roles", e.roles }, { "protocol", e.protocol } };
    if (withAddress) j["address"] = e.address;
    return j;
}

template <typename T> bool getUint(const nlohmann::json& j, const char* key, T& out, uint64_t max) {
    if (!j.contains(key) || !j[key].is_number_unsigned() || j[key].get<uint64_t>() > max) return false;
    out = static_cast<T>(j[key].get<uint64_t>());
    return true;
}
bool getString(const nlohmann::json& j, const char* key, std::string& out, size_t max) {
    if (!j.contains(key) || !j[key].is_string()) return false;
    out = j[key].get<std::string>();
    return out.size() <= max;
}

bool fromJson(const nlohmann::json& j, DirectoryEntry& e, bool withAddress) {
    if (!j.is_object()) return false;
    bool ok = getString(j, "name", e.name, kMaxName) && getString(j, "game", e.game, kMaxGame) && getUint(j, "port", e.port, 65535) &&
              getUint(j, "players", e.players, 65535) && getUint(j, "max", e.maxPlayers, 65535) && getUint(j, "protocol", e.protocol, UINT32_MAX);
    if (!ok || !j.contains("password") || !j["password"].is_boolean()) return false;
    e.password = j["password"].get<bool>();
    if (!j.contains("roles") || !j["roles"].is_array() || j["roles"].size() > kMaxRoles) return false;
    e.roles.clear();
    for (const auto& r : j["roles"]) {
        if (!r.is_string()) return false;
        e.roles.push_back(r.get<std::string>());
    }
    if (withAddress && !getString(j, "address", e.address, 64)) return false;
    return sane(e);
}

std::vector<uint8_t> frame(const nlohmann::json& j) {
    // Replace, never throw, on text that isn't UTF-8 (a server's name).
    const std::string body = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    std::vector<uint8_t> out(kDirectoryMagic, kDirectoryMagic + std::strlen(kDirectoryMagic));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

} // namespace

std::string DirectoryRegistry::heartbeat(DirectoryEntry e, const std::string& sourceAddress, double now) {
    if (!sane(e)) return "the heartbeat's fields are out of range";
    e.address = sourceAddress;
    const std::string key = sourceAddress + ":" + std::to_string(e.port);
    auto it = m_entries.find(key);
    if (it != m_entries.end()) {
        it->second = { std::move(e), now };
        return {};
    }
    const size_t fromHere = static_cast<size_t>(std::count_if(m_entries.begin(), m_entries.end(), [&](const auto& kv) { return kv.second.entry.address == sourceAddress; }));
    if (fromHere >= kMaxPerAddress) return "too many servers from this address";
    if (m_entries.size() >= kMaxEntries) return "the directory is full";
    m_entries.emplace(key, Live{ std::move(e), now });
    return {};
}

bool DirectoryRegistry::bye(const std::string& sourceAddress, uint16_t port) { return m_entries.erase(sourceAddress + ":" + std::to_string(port)) > 0; }

void DirectoryRegistry::expire(double now) {
    std::erase_if(m_entries, [&](const auto& kv) { return now - kv.second.lastSeen > kDirectoryTimeoutSeconds; });
}

std::vector<DirectoryEntry> DirectoryRegistry::list(const std::string& game) const {
    std::vector<DirectoryEntry> out;
    for (const auto& [key, live] : m_entries)
        if (game.empty() || live.entry.game == game) out.push_back(live.entry);
    return out;
}

bool RateLimiter::allow(const std::string& address, double now) {
    // Forget the idle ones now and then, so spoofed addresses can't grow this forever.
    if (m_buckets.size() > 65536)
        std::erase_if(m_buckets, [&](const auto& kv) { return now - kv.second.last > m_burst / m_rate; });
    auto [it, inserted] = m_buckets.try_emplace(address, Bucket{ m_burst, now });
    Bucket& b = it->second;
    if (!inserted) b.tokens = std::min(m_burst, b.tokens + (now - b.last) * m_rate);
    b.last = now;
    if (b.tokens < 1.0) return false;
    b.tokens -= 1.0;
    return true;
}

std::vector<uint8_t> encode(const DirectoryHeartbeat& m) {
    nlohmann::json j = toJson(m.entry, false);
    j["t"] = m.bye ? "bye" : "hb";
    return frame(j);
}

std::vector<uint8_t> encode(const DirectoryQuery& m) {
    nlohmann::json j{ { "t", "q" }, { "game", m.game }, { "from", m.from }, { "pad", "" } };
    std::vector<uint8_t> out = frame(j);
    if (out.size() < kMinQueryBytes) {
        j["pad"] = std::string(kMinQueryBytes - out.size(), ' ');
        out = frame(j);
    }
    return out;
}

std::vector<uint8_t> encodeList(const std::vector<DirectoryEntry>& all, uint32_t from) {
    nlohmann::json j{ { "t", "list" }, { "servers", nlohmann::json::array() }, { "next", 0 }, { "total", all.size() } };
    // Room for "next" growing to its longest.
    const size_t budget = kMaxDatagram - 16;
    size_t i = from;
    for (; i < all.size(); ++i) {
        j["servers"].push_back(toJson(all[i], true));
        if (frame(j).size() > budget) {
            j["servers"].erase(j["servers"].size() - 1);
            break;
        }
    }
    j["next"] = i < all.size() ? i : 0;
    return frame(j);
}

DirectoryDecoded decodeDirectory(const uint8_t* data, size_t size) {
    DirectoryDecoded out;
    const size_t magic = std::strlen(kDirectoryMagic);
    if (!data || size <= magic || size > kMaxDatagram || std::memcmp(data, kDirectoryMagic, magic) != 0) return out;
    const nlohmann::json j = nlohmann::json::parse(data + magic, data + size, nullptr, false);
    std::string t;
    if (j.is_discarded() || !j.is_object() || !getString(j, "t", t, 8)) return out;
    if (t == "hb" || t == "bye") {
        if (!fromJson(j, out.heartbeat.entry, false)) return out;
        out.heartbeat.bye = t == "bye";
        out.type = DirectoryMessage::Heartbeat;
    } else if (t == "q") {
        if (size < kMinQueryBytes || !getString(j, "game", out.query.game, kMaxGame) || !getUint(j, "from", out.query.from, UINT32_MAX)) return out;
        out.type = DirectoryMessage::Query;
    } else if (t == "list") {
        if (!j.contains("servers") || !j["servers"].is_array() || !getUint(j, "next", out.list.next, UINT32_MAX) ||
            !getUint(j, "total", out.list.total, UINT32_MAX))
            return out;
        for (const auto& s : j["servers"]) {
            DirectoryEntry e;
            if (fromJson(s, e, true)) out.list.servers.push_back(std::move(e));
        }
        out.type = DirectoryMessage::List;
    }
    return out;
}

} // namespace kke::server
