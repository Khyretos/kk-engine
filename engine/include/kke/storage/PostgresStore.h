#pragma once

#include "kke/storage/Store.h"

#include <mutex>

struct pg_conn;

namespace kke::storage {

// A store in a PostgreSQL database (PostgreSQL licence, free): for a
// studio or community running many servers on one big shared database,
// with its own backups and tools. Client: libpq.
//
//   postgres://user:password@host[:port]/database[?sslmode=require]
//
// One table, kke_kv (collection, key, value), made on first use.
// Transactions are real database transactions (nested ones are
// savepoints); counters lock their row, so servers can share them.
class PostgresStore : public Store {
public:
    PostgresStore() = default;
    ~PostgresStore() override;
    PostgresStore(const PostgresStore&) = delete;
    PostgresStore& operator=(const PostgresStore&) = delete;

    bool open(const std::string& url, std::string* error = nullptr);
    void close();

    const char* backendName() const override { return "postgres"; }
    bool put(const std::string& collection, const std::string& key, const std::string& value) override;
    std::optional<std::string> get(const std::string& collection, const std::string& key) override;
    bool erase(const std::string& collection, const std::string& key) override;
    std::vector<Item> list(const std::string& collection, const std::string& prefix = {}, size_t limit = 1000) override;
    std::optional<int64_t> increment(const std::string& collection, const std::string& key, int64_t delta) override;
    bool transaction(const std::function<bool()>& fn) override;

private:
    struct Result;
    // Runs `sql` with binary parameters; null on failure (lastError set).
    std::unique_ptr<Result> run(const char* sql, const std::vector<std::string>& params, bool binaryResult = true);
    bool exec(const std::string& sql);

    std::recursive_mutex m_mutex;
    pg_conn* m_conn = nullptr;
    int m_depth = 0;
};

} // namespace kke::storage
