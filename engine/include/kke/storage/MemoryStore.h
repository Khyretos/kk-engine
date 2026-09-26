#pragma once

#include "kke/storage/Store.h"

#include <map>
#include <mutex>

namespace kke::storage {

// Everything in memory, gone when it is (tests, a match nobody saves).
class MemoryStore : public Store {
public:
    const char* backendName() const override { return "memory"; }
    bool put(const std::string& collection, const std::string& key, const std::string& value) override;
    std::optional<std::string> get(const std::string& collection, const std::string& key) override;
    bool erase(const std::string& collection, const std::string& key) override;
    std::vector<Item> list(const std::string& collection, const std::string& prefix = {}, size_t limit = 1000) override;
    std::optional<int64_t> increment(const std::string& collection, const std::string& key, int64_t delta) override;
    bool transaction(const std::function<bool()>& fn) override;
    bool backup(const std::string& path) override;
    bool canBackup() const override { return true; }

private:
    std::recursive_mutex m_mutex;
    std::map<std::pair<std::string, std::string>, std::string> m_data;
};

// Counters are stored as decimal text in every backend; shared parsing.
std::optional<int64_t> parseCounter(const std::string& text);

} // namespace kke::storage
