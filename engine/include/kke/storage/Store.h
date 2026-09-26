#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace kke::storage {

// Where a game or a server keeps what outlives a session (docs/STORAGE.md):
// player progress, inventories, world saves, scores, bans. One small
// interface, several places behind it:
//
//   "memory:"                  nothing on disk (tests, a throwaway match)
//   "sqlite:save/world.db"     built in, one file, nothing to install (the default;
//                              a plain path means the same)
//   "valkey://host:6379"       an optional Valkey/Redis server, for many game
//                              servers sharing fast state (built with KKE_ENABLE_VALKEY)
//   "postgres://user:pw@host/db"  an optional PostgreSQL server, for big shared
//                              databases (built with KKE_ENABLE_POSTGRES)
//
// Data is collections of keyed values: `collection` names a kind of thing
// ("players", "inventory.kees", "world"), `key` one of them, `value` any
// bytes (JSON, a packed save). Counters are values too (increment).
// Every call reports failure (false / nullopt, and lastError()); none
// throws. A Store is safe to use from several threads.
class Store {
public:
    struct Item {
        std::string key, value;
    };
    static constexpr size_t kMaxCollection = 64;
    static constexpr size_t kMaxKey = 256;
    static constexpr size_t kMaxValue = 16 * 1024 * 1024;

    virtual ~Store() = default;
    virtual const char* backendName() const = 0;

    virtual bool put(const std::string& collection, const std::string& key, const std::string& value) = 0;
    // nullopt: not there (lastError() empty) or it couldn't be read (lastError() says why).
    virtual std::optional<std::string> get(const std::string& collection, const std::string& key) = 0;
    virtual bool erase(const std::string& collection, const std::string& key) = 0;
    // Keys starting with `prefix`, in key order, at most `limit`.
    virtual std::vector<Item> list(const std::string& collection, const std::string& prefix = {}, size_t limit = 1000) = 0;
    // Adds `delta` to a counter (a missing one is 0) and returns the new
    // value; nullopt if the value there isn't a number.
    virtual std::optional<int64_t> increment(const std::string& collection, const std::string& key, int64_t delta) = 0;
    // All of `fn`'s writes happen, or none do (fn returned false, or a
    // write failed). Other threads' calls wait until it's done.
    virtual bool transaction(const std::function<bool()>& fn) = 0;

    // The last failure's reason ("" after a success), for this store.
    std::string lastError() const;
    // Messages from the database that aren't failures (a PostgreSQL
    // NOTICE or WARNING), for the owner's log. Unset: written to stderr.
    std::function<void(const std::string& message)> onNotice;

    // Collections: 1-64 of a-z 0-9 _ - . ; keys: 1-256 bytes; values: up to 16 MiB.
    static bool validCollection(const std::string& c);
    static bool validKey(const std::string& k);

protected:
    void setError(std::string e) const;
    // Checks names and sizes; false (with lastError set) when a call can't go ahead.
    bool check(const std::string& collection, const std::string& key, size_t valueSize = 0) const;

private:
    mutable std::mutex m_errorMutex;
    mutable std::string m_error;
};

// Opens the store `url` names (see above). Null, with `error`, when it
// can't: a bad url, a backend this build doesn't have, a file or server
// it can't reach.
std::unique_ptr<Store> openStore(const std::string& url, std::string* error = nullptr);

} // namespace kke::storage
