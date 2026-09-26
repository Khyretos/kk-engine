#include "kke/storage/PostgresStore.h"

#include "kke/storage/MemoryStore.h"

#include <libpq-fe.h>

#include <limits>

namespace kke::storage {

struct PostgresStore::Result {
    PGresult* r = nullptr;
    ~Result() { PQclear(r); }
    std::string value(int row, int col) const { return std::string(PQgetvalue(r, row, col), static_cast<size_t>(PQgetlength(r, row, col))); }
    int rows() const { return PQntuples(r); }
};

PostgresStore::~PostgresStore() { close(); }

void PostgresStore::close() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_conn) PQfinish(m_conn);
    m_conn = nullptr;
    m_depth = 0;
}

bool PostgresStore::open(const std::string& url, std::string* error) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    close();
    m_conn = PQconnectdb(url.c_str());
    if (!m_conn || PQstatus(m_conn) != CONNECTION_OK) {
        std::string why = m_conn ? PQerrorMessage(m_conn) : "out of memory";
        while (!why.empty() && (why.back() == '\n' || why.back() == ' ')) why.pop_back();
        if (error) *error = "postgres: " + why;
        setError(why);
        close();
        return false;
    }
    if (!exec("CREATE TABLE IF NOT EXISTS kke_kv (collection TEXT NOT NULL, key BYTEA NOT NULL, value BYTEA NOT NULL, PRIMARY KEY (collection, key))")) {
        if (error) *error = "postgres: " + lastError();
        close();
        return false;
    }
    setError({});
    return true;
}

bool PostgresStore::exec(const std::string& sql) {
    PGresult* r = PQexec(m_conn, sql.c_str());
    const bool ok = r && (PQresultStatus(r) == PGRES_COMMAND_OK || PQresultStatus(r) == PGRES_TUPLES_OK);
    if (!ok) setError(sql.substr(0, 40) + ": " + PQerrorMessage(m_conn));
    PQclear(r);
    return ok;
}

std::unique_ptr<PostgresStore::Result> PostgresStore::run(const char* sql, const std::vector<std::string>& params, bool binaryResult) {
    if (!m_conn) {
        setError("not connected");
        return nullptr;
    }
    // A dropped connection (the database restarted): one reset, outside transactions.
    if (PQstatus(m_conn) != CONNECTION_OK && m_depth == 0) PQreset(m_conn);
    std::vector<const char*> values;
    std::vector<int> lengths, formats;
    for (const std::string& p : params) {
        values.push_back(p.data());
        lengths.push_back(static_cast<int>(p.size()));
        formats.push_back(1); // binary: keys and values are any bytes
    }
    auto res = std::make_unique<Result>();
    res->r = PQexecParams(m_conn, sql, static_cast<int>(params.size()), nullptr, values.data(), lengths.data(), formats.data(), binaryResult ? 1 : 0);
    const ExecStatusType st = res->r ? PQresultStatus(res->r) : PGRES_FATAL_ERROR;
    if (st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK) {
        std::string why = PQerrorMessage(m_conn);
        while (!why.empty() && (why.back() == '\n' || why.back() == ' ')) why.pop_back();
        setError(why);
        return nullptr;
    }
    return res;
}

bool PostgresStore::put(const std::string& collection, const std::string& key, const std::string& value) {
    if (!check(collection, key, value.size())) return false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return run("INSERT INTO kke_kv (collection, key, value) VALUES (convert_from($1, 'UTF8'), $2, $3) "
               "ON CONFLICT (collection, key) DO UPDATE SET value = excluded.value",
               { collection, key, value }) != nullptr;
}

std::optional<std::string> PostgresStore::get(const std::string& collection, const std::string& key) {
    if (!check(collection, key)) return std::nullopt;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto r = run("SELECT value FROM kke_kv WHERE collection = convert_from($1, 'UTF8') AND key = $2", { collection, key });
    if (!r || r->rows() == 0) return std::nullopt;
    return r->value(0, 0);
}

bool PostgresStore::erase(const std::string& collection, const std::string& key) {
    if (!check(collection, key)) return false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return run("DELETE FROM kke_kv WHERE collection = convert_from($1, 'UTF8') AND key = $2", { collection, key }) != nullptr;
}

std::vector<Store::Item> PostgresStore::list(const std::string& collection, const std::string& prefix, size_t limit) {
    std::vector<Item> out;
    if (!validCollection(collection)) {
        setError("collection '" + collection.substr(0, 80) + "': 1-64 of a-z 0-9 _ - .");
        return out;
    }
    setError({});
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    // bytea compares bytewise, like the other stores.
    auto r = run("SELECT key, value FROM kke_kv WHERE collection = convert_from($1, 'UTF8') AND key >= $2 "
                 "AND substring(key from 1 for length($2)) = $2 ORDER BY key LIMIT convert_from($3, 'UTF8')::bigint",
                 { collection, prefix, std::to_string(std::min<size_t>(limit, 1000000)) });
    if (!r) return out;
    for (int i = 0; i < r->rows(); ++i) out.push_back({ r->value(i, 0), r->value(i, 1) });
    return out;
}

std::optional<int64_t> PostgresStore::increment(const std::string& collection, const std::string& key, int64_t delta) {
    if (!check(collection, key)) return std::nullopt;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::optional<int64_t> result;
    transaction([&] {
        // Make the row if it's new, then lock it: servers sharing a counter queue up here.
        if (!run("INSERT INTO kke_kv (collection, key, value) VALUES (convert_from($1, 'UTF8'), $2, '\\x30'::bytea) ON CONFLICT DO NOTHING", { collection, key }))
            return false;
        auto r = run("SELECT value FROM kke_kv WHERE collection = convert_from($1, 'UTF8') AND key = $2 FOR UPDATE", { collection, key });
        if (!r || r->rows() != 1) return false;
        const std::optional<int64_t> now = parseCounter(r->value(0, 0));
        if (!now) {
            setError(collection + "/" + key + " isn't a number");
            return false;
        }
        if ((delta > 0 && *now > std::numeric_limits<int64_t>::max() - delta) || (delta < 0 && *now < std::numeric_limits<int64_t>::min() - delta)) {
            setError(collection + "/" + key + ": the counter would overflow");
            return false;
        }
        if (!put(collection, key, std::to_string(*now + delta))) return false;
        result = *now + delta;
        return true;
    });
    return result;
}

bool PostgresStore::transaction(const std::function<bool()>& fn) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_conn) {
        setError("not connected");
        return false;
    }
    const std::string name = "kke" + std::to_string(m_depth);
    if (!exec(m_depth == 0 ? "BEGIN" : "SAVEPOINT " + name)) return false;
    ++m_depth;
    const std::string undo = m_depth == 1 ? "ROLLBACK" : "ROLLBACK TO SAVEPOINT " + name + "; RELEASE SAVEPOINT " + name;
    bool ok = false;
    try {
        ok = fn();
    } catch (...) {
        --m_depth;
        exec(undo);
        throw;
    }
    --m_depth;
    if (ok && exec(m_depth == 0 ? "COMMIT" : "RELEASE SAVEPOINT " + name)) return true;
    const std::string why = lastError().empty() ? "the transaction was called off" : lastError();
    exec(undo);
    setError(why);
    return false;
}

} // namespace kke::storage
