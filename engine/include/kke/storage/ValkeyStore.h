#pragma once

#include "kke/storage/Store.h"

#include <map>
#include <mutex>

struct redisContext;
struct redisReply;

namespace kke::storage {

// A store on a Valkey server (BSD, https://valkey.io; Redis speaks the same
// protocol): fast shared state for several game servers, e.g. who is
// online where, match queues, counters. Client: hiredis (BSD).
//
//   valkey://[:password@]host[:port][/db]      (redis://... works too)
//
// Layout: each collection is a hash (kke:v:<collection>, key -> value)
// plus a sorted set of its keys (kke:k:<collection>) for listing in key
// order. A transaction's writes are held back and sent as one MULTI/EXEC
// when it succeeds, so they land together or not at all; reads inside it
// see its own writes. It does not lock what it read against other
// servers (use a counter, which is atomic on the server, for that).
class ValkeyStore : public Store {
public:
    ValkeyStore() = default;
    ~ValkeyStore() override;
    ValkeyStore(const ValkeyStore&) = delete;
    ValkeyStore& operator=(const ValkeyStore&) = delete;

    bool open(const std::string& url, std::string* error = nullptr);
    void close();

    const char* backendName() const override { return "valkey"; }
    bool put(const std::string& collection, const std::string& key, const std::string& value) override;
    std::optional<std::string> get(const std::string& collection, const std::string& key) override;
    bool erase(const std::string& collection, const std::string& key) override;
    std::vector<Item> list(const std::string& collection, const std::string& prefix = {}, size_t limit = 1000) override;
    std::optional<int64_t> increment(const std::string& collection, const std::string& key, int64_t delta) override;
    bool transaction(const std::function<bool()>& fn) override;

private:
    using Args = std::vector<std::string>;
    // One command; the reply (freed by the caller via Reply), or null with lastError set.
    redisReply* command(const Args& args);
    bool connect();
    struct Pending { bool erase; std::string value; };

    std::recursive_mutex m_mutex;
    redisContext* m_ctx = nullptr;
    std::string m_host, m_password;
    int m_port = 6379, m_db = 0;
    int m_depth = 0;
    std::map<std::pair<std::string, std::string>, Pending> m_pending; // writes of the open transaction
};

} // namespace kke::storage
