#pragma once

#include "kke/storage/Store.h"

#include <mutex>

struct sqlite3;
struct sqlite3_stmt;

namespace kke::storage {

// The built-in store: one SQLite file (public domain, https://sqlite.org),
// nothing to install or run. Write-ahead logging, so a crash or a power
// cut loses at most the last moments, never the file; readers don't
// block the writer.
class SqliteStore : public Store {
public:
    SqliteStore() = default;
    ~SqliteStore() override;
    SqliteStore(const SqliteStore&) = delete;
    SqliteStore& operator=(const SqliteStore&) = delete;

    // Makes the file (and its folder) if needed.
    bool open(const std::string& path, std::string* error = nullptr);
    void close();
    const std::string& path() const { return m_path; }

    const char* backendName() const override { return "sqlite"; }
    bool put(const std::string& collection, const std::string& key, const std::string& value) override;
    std::optional<std::string> get(const std::string& collection, const std::string& key) override;
    bool erase(const std::string& collection, const std::string& key) override;
    std::vector<Item> list(const std::string& collection, const std::string& prefix = {}, size_t limit = 1000) override;
    std::optional<int64_t> increment(const std::string& collection, const std::string& key, int64_t delta) override;
    bool transaction(const std::function<bool()>& fn) override;

private:
    bool exec(const char* sql);
    bool fail(const char* what);
    std::recursive_mutex m_mutex;
    sqlite3* m_db = nullptr;
    sqlite3_stmt *m_put = nullptr, *m_get = nullptr, *m_erase = nullptr, *m_list = nullptr;
    std::string m_path;
    int m_depth = 0; // nested transactions (savepoints)
};

} // namespace kke::storage
