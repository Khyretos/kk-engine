#include "kke/storage/MemoryStore.h"

#include "kke/storage/SqliteStore.h"

#include <charconv>
#include <filesystem>
#include <limits>

namespace kke::storage {

std::optional<int64_t> parseCounter(const std::string& text) {
    int64_t v = 0;
    const char* end = text.data() + text.size();
    const auto r = std::from_chars(text.data(), end, v);
    if (r.ec != std::errc() || r.ptr != end) return std::nullopt;
    return v;
}

bool MemoryStore::put(const std::string& collection, const std::string& key, const std::string& value) {
    if (!check(collection, key, value.size())) return false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_data[{ collection, key }] = value;
    return true;
}

std::optional<std::string> MemoryStore::get(const std::string& collection, const std::string& key) {
    if (!check(collection, key)) return std::nullopt;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_data.find({ collection, key });
    if (it == m_data.end()) return std::nullopt;
    return it->second;
}

bool MemoryStore::erase(const std::string& collection, const std::string& key) {
    if (!check(collection, key)) return false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_data.erase({ collection, key });
    return true;
}

std::vector<Store::Item> MemoryStore::list(const std::string& collection, const std::string& prefix, size_t limit) {
    std::vector<Item> out;
    if (!validCollection(collection)) {
        setError("collection '" + collection.substr(0, 80) + "': 1-64 of a-z 0-9 _ - .");
        return out;
    }
    setError({});
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (auto it = m_data.lower_bound({ collection, prefix }); it != m_data.end() && out.size() < limit; ++it) {
        if (it->first.first != collection || it->first.second.compare(0, prefix.size(), prefix) != 0) break;
        out.push_back({ it->first.second, it->second });
    }
    return out;
}

std::optional<int64_t> MemoryStore::increment(const std::string& collection, const std::string& key, int64_t delta) {
    if (!check(collection, key)) return std::nullopt;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::string& v = m_data[{ collection, key }];
    const std::optional<int64_t> now = v.empty() ? std::optional<int64_t>(0) : parseCounter(v);
    if (!now) {
        setError(collection + "/" + key + " isn't a number");
        return std::nullopt;
    }
    if ((delta > 0 && *now > std::numeric_limits<int64_t>::max() - delta) || (delta < 0 && *now < std::numeric_limits<int64_t>::min() - delta)) {
        setError(collection + "/" + key + ": the counter would overflow");
        return std::nullopt;
    }
    v = std::to_string(*now + delta);
    return *now + delta;
}

bool MemoryStore::transaction(const std::function<bool()>& fn) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    const auto before = m_data;
    bool ok = false;
    try {
        ok = fn();
    } catch (...) {
        m_data = before;
        throw;
    }
    if (!ok) {
        m_data = before;
        if (lastError().empty()) setError("the transaction was called off");
    }
    return ok;
}

bool MemoryStore::backup(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    const std::string temp = path + ".part";
    std::error_code ec;
    std::filesystem::remove(temp, ec);
    bool ok = false;
    {
        SqliteStore copy;
        std::string error;
        if (!copy.open(temp, &error)) {
            setError("backup: " + error);
            return false;
        }
        ok = copy.transaction([&] {
            for (const auto& [ck, value] : m_data)
                if (!copy.put(ck.first, ck.second, value)) return false;
            return true;
        });
        if (!ok) setError("backup: " + copy.lastError());
    }
    if (ok) std::filesystem::rename(temp, path, ec);
    if (ok && ec) {
        setError("backup: " + path + ": " + ec.message());
        ok = false;
    }
    if (!ok) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    setError({});
    return true;
}

} // namespace kke::storage
