#include "kke/storage/Store.h"

#include "kke/storage/MemoryStore.h"
#include "kke/storage/SqliteStore.h"
#if KKE_ENABLE_VALKEY
#include "kke/storage/ValkeyStore.h"
#endif
#if KKE_ENABLE_POSTGRES
#include "kke/storage/PostgresStore.h"
#endif

#include <algorithm>

namespace kke::storage {

std::string Store::lastError() const {
    std::lock_guard<std::mutex> lock(m_errorMutex);
    return m_error;
}

void Store::setError(std::string e) const {
    std::lock_guard<std::mutex> lock(m_errorMutex);
    m_error = std::move(e);
}

bool Store::backup(const std::string&) {
    setError(std::string("a ") + backendName() + " store is backed up with its own tools (docs/STORAGE.md \"Backups\")");
    return false;
}

bool Store::validCollection(const std::string& c) {
    return !c.empty() && c.size() <= kMaxCollection &&
           std::all_of(c.begin(), c.end(), [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.'; });
}

bool Store::validKey(const std::string& k) { return !k.empty() && k.size() <= kMaxKey; }

bool Store::check(const std::string& collection, const std::string& key, size_t valueSize) const {
    if (!validCollection(collection)) {
        setError("collection '" + collection.substr(0, 80) + "': 1-64 of a-z 0-9 _ - .");
        return false;
    }
    if (!validKey(key)) {
        setError("key: 1 to 256 bytes");
        return false;
    }
    if (valueSize > kMaxValue) {
        setError("value: at most 16 MiB (" + std::to_string(valueSize) + " bytes given)");
        return false;
    }
    setError({});
    return true;
}

std::unique_ptr<Store> openStore(const std::string& url, std::string* error) {
    auto fail = [&](const std::string& why) -> std::unique_ptr<Store> {
        if (error) *error = why;
        return nullptr;
    };
    auto startsWith = [&](const char* p) { return url.rfind(p, 0) == 0; };
    if (url == "memory:" || url == "memory") return std::make_unique<MemoryStore>();
    if (startsWith("valkey://") || startsWith("redis://")) {
#if KKE_ENABLE_VALKEY
        auto s = std::make_unique<ValkeyStore>();
        std::string e;
        if (!s->open(url, &e)) return fail(e);
        return s;
#else
        return fail("this build has no Valkey support (configure with -DKKE_ENABLE_VALKEY=ON)");
#endif
    }
    if (startsWith("postgres://") || startsWith("postgresql://")) {
#if KKE_ENABLE_POSTGRES
        auto s = std::make_unique<PostgresStore>();
        std::string e;
        if (!s->open(url, &e)) return fail(e);
        return s;
#else
        return fail("this build has no PostgreSQL support (configure with -DKKE_ENABLE_POSTGRES=ON)");
#endif
    }
    std::string path = startsWith("sqlite:") ? url.substr(7) : url;
    if (path.rfind("//", 0) == 0) path = path.substr(2); // sqlite://file.db
    if (path.empty()) return fail("sqlite: needs a file, like sqlite:save/world.db");
    if (url.find("://") != std::string::npos && !startsWith("sqlite:")) return fail("'" + url + "': unknown kind of store (memory:, sqlite:, valkey://, postgres://)");
    auto s = std::make_unique<SqliteStore>();
    std::string e;
    if (!s->open(path, &e)) return fail(e);
    return s;
}

} // namespace kke::storage
