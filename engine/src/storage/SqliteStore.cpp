#include "kke/storage/SqliteStore.h"

#include "kke/storage/MemoryStore.h"

#include <sqlite3.h>

#include <filesystem>
#include <limits>

namespace kke::storage {

namespace {
struct Reset { // leaves a statement ready for its next use, whatever happens
    sqlite3_stmt* s;
    ~Reset() {
        sqlite3_reset(s);
        sqlite3_clear_bindings(s);
    }
};
void bindText(sqlite3_stmt* s, int i, const std::string& t) { sqlite3_bind_text(s, i, t.data(), static_cast<int>(t.size()), SQLITE_TRANSIENT); }
} // namespace

SqliteStore::~SqliteStore() { close(); }

bool SqliteStore::fail(const char* what) {
    setError(std::string(what) + ": " + (m_db ? sqlite3_errmsg(m_db) : "not open"));
    return false;
}

bool SqliteStore::exec(const char* sql) {
    char* msg = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &msg) == SQLITE_OK) return true;
    setError(std::string(sql) + ": " + (msg ? msg : "failed"));
    sqlite3_free(msg);
    return false;
}

bool SqliteStore::open(const std::string& path, std::string* error) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    close();
    auto bad = [&](const std::string& why) {
        if (error) *error = path + ": " + why;
        setError(path + ": " + why);
        close();
        return false;
    };
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    if (ec) return bad("can't make its folder: " + ec.message());
    if (sqlite3_open_v2(path.c_str(), &m_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
        return bad(m_db ? sqlite3_errmsg(m_db) : "can't open");
    sqlite3_busy_timeout(m_db, 5000); // another process (a backup tool) holding it briefly
    if (!exec("PRAGMA journal_mode=WAL") || !exec("PRAGMA synchronous=NORMAL") ||
        !exec("CREATE TABLE IF NOT EXISTS kv (collection TEXT NOT NULL, key BLOB NOT NULL, value BLOB NOT NULL, PRIMARY KEY (collection, key)) WITHOUT ROWID"))
        return bad(lastError());
    auto prep = [&](const char* sql, sqlite3_stmt** out) { return sqlite3_prepare_v3(m_db, sql, -1, SQLITE_PREPARE_PERSISTENT, out, nullptr) == SQLITE_OK; };
    if (!prep("INSERT INTO kv (collection, key, value) VALUES (?1, ?2, ?3) ON CONFLICT (collection, key) DO UPDATE SET value = excluded.value", &m_put) ||
        !prep("SELECT value FROM kv WHERE collection = ?1 AND key = ?2", &m_get) || !prep("DELETE FROM kv WHERE collection = ?1 AND key = ?2", &m_erase) ||
        !prep("SELECT key, value FROM kv WHERE collection = ?1 AND key >= ?2 ORDER BY key LIMIT ?3", &m_list))
        return bad(sqlite3_errmsg(m_db));
    m_path = path;
    setError({});
    return true;
}

void SqliteStore::close() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (sqlite3_stmt** s : { &m_put, &m_get, &m_erase, &m_list }) {
        sqlite3_finalize(*s);
        *s = nullptr;
    }
    if (m_db) sqlite3_close_v2(m_db);
    m_db = nullptr;
    m_depth = 0;
}

bool SqliteStore::put(const std::string& collection, const std::string& key, const std::string& value) {
    if (!check(collection, key, value.size())) return false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return fail("put");
    Reset r{ m_put };
    bindText(m_put, 1, collection);
    sqlite3_bind_blob(m_put, 2, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(m_put, 3, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    return sqlite3_step(m_put) == SQLITE_DONE || fail("put");
}

std::optional<std::string> SqliteStore::get(const std::string& collection, const std::string& key) {
    if (!check(collection, key)) return std::nullopt;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) {
        fail("get");
        return std::nullopt;
    }
    Reset r{ m_get };
    bindText(m_get, 1, collection);
    sqlite3_bind_blob(m_get, 2, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
    const int rc = sqlite3_step(m_get);
    if (rc == SQLITE_DONE) return std::nullopt;
    if (rc != SQLITE_ROW) {
        fail("get");
        return std::nullopt;
    }
    const auto* data = static_cast<const char*>(sqlite3_column_blob(m_get, 0));
    return std::string(data ? data : "", static_cast<size_t>(sqlite3_column_bytes(m_get, 0)));
}

bool SqliteStore::erase(const std::string& collection, const std::string& key) {
    if (!check(collection, key)) return false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return fail("erase");
    Reset r{ m_erase };
    bindText(m_erase, 1, collection);
    sqlite3_bind_blob(m_erase, 2, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
    return sqlite3_step(m_erase) == SQLITE_DONE || fail("erase");
}

std::vector<Store::Item> SqliteStore::list(const std::string& collection, const std::string& prefix, size_t limit) {
    std::vector<Item> out;
    if (!validCollection(collection)) {
        setError("collection '" + collection.substr(0, 80) + "': 1-64 of a-z 0-9 _ - .");
        return out;
    }
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) {
        fail("list");
        return out;
    }
    setError({});
    Reset r{ m_list };
    bindText(m_list, 1, collection);
    sqlite3_bind_blob(m_list, 2, prefix.data(), static_cast<int>(prefix.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(m_list, 3, static_cast<sqlite3_int64>(std::min<size_t>(limit, static_cast<size_t>(std::numeric_limits<int32_t>::max()))));
    int rc;
    while ((rc = sqlite3_step(m_list)) == SQLITE_ROW) {
        auto col = [&](int i) {
            const auto* d = static_cast<const char*>(sqlite3_column_blob(m_list, i));
            return std::string(d ? d : "", static_cast<size_t>(sqlite3_column_bytes(m_list, i)));
        };
        std::string k = col(0);
        if (k.compare(0, prefix.size(), prefix) != 0) break; // past the prefix (keys sort bytewise)
        out.push_back({ std::move(k), col(1) });
    }
    if (rc != SQLITE_ROW && rc != SQLITE_DONE) fail("list");
    return out;
}

std::optional<int64_t> SqliteStore::increment(const std::string& collection, const std::string& key, int64_t delta) {
    if (!check(collection, key)) return std::nullopt;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::optional<int64_t> result;
    transaction([&] {
        const std::optional<std::string> v = get(collection, key);
        if (!v && !lastError().empty()) return false;
        const std::optional<int64_t> now = v ? parseCounter(*v) : std::optional<int64_t>(0);
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

bool SqliteStore::transaction(const std::function<bool()>& fn) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return fail("transaction");
    // Nested ones are savepoints: calling one off undoes only its part.
    const std::string name = "kke" + std::to_string(m_depth);
    if (!exec(m_depth == 0 ? "BEGIN IMMEDIATE" : ("SAVEPOINT " + name).c_str())) return false;
    ++m_depth;
    bool ok = false;
    try {
        ok = fn();
    } catch (...) {
        --m_depth;
        exec(m_depth == 0 ? "ROLLBACK" : ("ROLLBACK TO " + name + "; RELEASE " + name).c_str());
        throw;
    }
    --m_depth;
    if (ok) {
        if (exec(m_depth == 0 ? "COMMIT" : ("RELEASE " + name).c_str())) return true;
        const std::string why = lastError();
        exec(m_depth == 0 ? "ROLLBACK" : ("ROLLBACK TO " + name + "; RELEASE " + name).c_str());
        setError(why);
        return false;
    }
    const std::string why = lastError().empty() ? "the transaction was called off" : lastError();
    exec(m_depth == 0 ? "ROLLBACK" : ("ROLLBACK TO " + name + "; RELEASE " + name).c_str());
    setError(why);
    return false;
}

bool SqliteStore::backup(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db) return fail("backup");
    if (m_depth > 0) {
        setError("backup: not inside a transaction");
        return false;
    }
    // VACUUM INTO writes a compact, consistent copy while the store stays
    // usable; to a temporary name, so a failed copy never replaces a good one.
    const std::string temp = path + ".part";
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::filesystem::remove(temp, ec);
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(m_db, "VACUUM INTO ?1", -1, &s, nullptr) != SQLITE_OK) return fail("backup");
    bindText(s, 1, temp);
    const int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    if (rc != SQLITE_DONE) {
        fail(("backup to " + path).c_str());
        std::filesystem::remove(temp, ec);
        return false;
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        setError("backup: " + path + ": " + ec.message());
        std::filesystem::remove(temp, ec);
        return false;
    }
    setError({});
    return true;
}

} // namespace kke::storage
