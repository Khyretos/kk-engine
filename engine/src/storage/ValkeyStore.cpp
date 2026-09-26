#include "kke/storage/ValkeyStore.h"

#include "kke/storage/MemoryStore.h"

#include <hiredis.h>

#include <algorithm>
#include <limits>
#include <memory>

namespace kke::storage {

namespace {
struct FreeReply {
    void operator()(redisReply* r) const {
        if (r) freeReplyObject(r);
    }
};
using Reply = std::unique_ptr<redisReply, FreeReply>;
std::string hashKey(const std::string& c) { return "kke:v:" + c; }
std::string indexKey(const std::string& c) { return "kke:k:" + c; }
} // namespace

ValkeyStore::~ValkeyStore() { close(); }

void ValkeyStore::close() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_ctx) redisFree(m_ctx);
    m_ctx = nullptr;
}

bool ValkeyStore::open(const std::string& url, std::string* error) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    close();
    // valkey://[:password@]host[:port][/db]
    std::string rest = url.substr(url.find("://") + 3);
    if (const size_t at = rest.rfind('@'); at != std::string::npos) {
        std::string auth = rest.substr(0, at);
        rest = rest.substr(at + 1);
        if (const size_t colon = auth.find(':'); colon != std::string::npos) auth = auth.substr(colon + 1);
        m_password = auth;
    }
    if (const size_t slash = rest.find('/'); slash != std::string::npos) {
        const std::string db = rest.substr(slash + 1);
        rest = rest.substr(0, slash);
        if (!db.empty()) {
            const std::optional<int64_t> n = parseCounter(db);
            if (!n || *n < 0 || *n > 255) {
                if (error) *error = url + ": the database after / must be a number";
                return false;
            }
            m_db = static_cast<int>(*n);
        }
    }
    if (const size_t colon = rest.rfind(':'); colon != std::string::npos) {
        const std::optional<int64_t> p = parseCounter(rest.substr(colon + 1));
        if (!p || *p < 1 || *p > 65535) {
            if (error) *error = url + ": bad port";
            return false;
        }
        m_port = static_cast<int>(*p);
        rest = rest.substr(0, colon);
    }
    m_host = rest.empty() ? "127.0.0.1" : rest;
    if (!connect()) {
        if (error) *error = "valkey " + m_host + ":" + std::to_string(m_port) + ": " + lastError();
        return false;
    }
    return true;
}

bool ValkeyStore::connect() {
    if (m_ctx) redisFree(m_ctx);
    const timeval timeout{ 5, 0 };
    m_ctx = redisConnectWithTimeout(m_host.c_str(), m_port, timeout);
    if (!m_ctx || m_ctx->err) {
        setError(m_ctx ? m_ctx->errstr : "out of memory");
        if (m_ctx) redisFree(m_ctx);
        m_ctx = nullptr;
        return false;
    }
    redisSetTimeout(m_ctx, timeout);
    if (!m_password.empty()) {
        Reply r(static_cast<redisReply*>(redisCommand(m_ctx, "AUTH %b", m_password.data(), m_password.size())));
        if (!r || r->type == REDIS_REPLY_ERROR) {
            setError(r ? std::string("AUTH: ") + r->str : "AUTH failed");
            return false;
        }
    }
    if (m_db) {
        Reply r(static_cast<redisReply*>(redisCommand(m_ctx, "SELECT %d", m_db)));
        if (!r || r->type == REDIS_REPLY_ERROR) {
            setError(r ? std::string("SELECT: ") + r->str : "SELECT failed");
            return false;
        }
    }
    setError({});
    return true;
}

redisReply* ValkeyStore::command(const Args& args) {
    std::vector<const char*> argv;
    std::vector<size_t> lens;
    for (const std::string& a : args) {
        argv.push_back(a.data());
        lens.push_back(a.size());
    }
    // One reconnect for a dropped connection (the server restarted).
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!m_ctx && !connect()) return nullptr;
        auto* r = static_cast<redisReply*>(redisCommandArgv(m_ctx, static_cast<int>(argv.size()), argv.data(), lens.data()));
        if (r) {
            if (r->type == REDIS_REPLY_ERROR) {
                setError(args[0] + ": " + std::string(r->str, r->len));
                freeReplyObject(r);
                return nullptr;
            }
            return r;
        }
        setError(m_ctx->errstr);
        redisFree(m_ctx);
        m_ctx = nullptr;
    }
    return nullptr;
}

bool ValkeyStore::put(const std::string& collection, const std::string& key, const std::string& value) {
    if (!check(collection, key, value.size())) return false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_depth) {
        m_pending[{ collection, key }] = { false, value };
        return true;
    }
    Reply r(command({ "MULTI" }));
    if (!r) return false;
    Reply a(command({ "HSET", hashKey(collection), key, value }));
    Reply b(command({ "ZADD", indexKey(collection), "0", key }));
    Reply e(command({ "EXEC" }));
    if (!a || !b || !e) {
        Reply d(command({ "DISCARD" }));
        return false;
    }
    return true;
}

std::optional<std::string> ValkeyStore::get(const std::string& collection, const std::string& key) {
    if (!check(collection, key)) return std::nullopt;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (auto p = m_pending.find({ collection, key }); p != m_pending.end()) {
        if (p->second.erase) return std::nullopt;
        return p->second.value;
    }
    Reply r(command({ "HGET", hashKey(collection), key }));
    if (!r || r->type != REDIS_REPLY_STRING) return std::nullopt;
    return std::string(r->str, r->len);
}

bool ValkeyStore::erase(const std::string& collection, const std::string& key) {
    if (!check(collection, key)) return false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_depth) {
        m_pending[{ collection, key }] = { true, {} };
        return true;
    }
    Reply r(command({ "MULTI" }));
    if (!r) return false;
    Reply a(command({ "HDEL", hashKey(collection), key }));
    Reply b(command({ "ZREM", indexKey(collection), key }));
    Reply e(command({ "EXEC" }));
    if (!a || !b || !e) {
        Reply d(command({ "DISCARD" }));
        return false;
    }
    return true;
}

std::vector<Store::Item> ValkeyStore::list(const std::string& collection, const std::string& prefix, size_t limit) {
    std::vector<Item> out;
    if (!validCollection(collection)) {
        setError("collection '" + collection.substr(0, 80) + "': 1-64 of a-z 0-9 _ - .");
        return out;
    }
    setError({});
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    const std::string from = prefix.empty() ? "-" : "[" + prefix;
    const std::string to = prefix.empty() ? "+" : "[" + prefix + std::string(1, '\xff');
    const size_t want = std::min<size_t>(limit, 100000);
    Reply keys(command({ "ZRANGE", indexKey(collection), from, to, "BYLEX", "LIMIT", "0", std::to_string(want) }));
    if (!keys || keys->type != REDIS_REPLY_ARRAY) return out;
    if (keys->elements == 0) return out;
    Args hm{ "HMGET", hashKey(collection) };
    for (size_t i = 0; i < keys->elements; ++i) hm.emplace_back(keys->element[i]->str, keys->element[i]->len);
    Reply values(command(hm));
    if (!values || values->type != REDIS_REPLY_ARRAY || values->elements != keys->elements) return out;
    for (size_t i = 0; i < keys->elements; ++i) {
        const redisReply* v = values->element[i];
        if (v->type != REDIS_REPLY_STRING) continue; // erased between the two calls
        out.push_back({ std::string(keys->element[i]->str, keys->element[i]->len), std::string(v->str, v->len) });
    }
    return out;
}

std::optional<int64_t> ValkeyStore::increment(const std::string& collection, const std::string& key, int64_t delta) {
    if (!check(collection, key)) return std::nullopt;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_depth) { // inside a transaction: computed here, written with the rest
        const std::optional<std::string> v = get(collection, key);
        const std::optional<int64_t> now = v ? parseCounter(*v) : std::optional<int64_t>(0);
        if (!now || (delta > 0 && *now > std::numeric_limits<int64_t>::max() - delta) || (delta < 0 && *now < std::numeric_limits<int64_t>::min() - delta)) {
            setError(collection + "/" + key + " isn't a number, or would overflow");
            return std::nullopt;
        }
        m_pending[{ collection, key }] = { false, std::to_string(*now + delta) };
        return *now + delta;
    }
    // HINCRBY is atomic on the server: many game servers can count together.
    Reply m(command({ "MULTI" }));
    if (!m) return std::nullopt;
    Reply a(command({ "HINCRBY", hashKey(collection), key, std::to_string(delta) }));
    Reply b(command({ "ZADD", indexKey(collection), "0", key }));
    Reply e(command({ "EXEC" }));
    if (!a || !b || !e || e->type != REDIS_REPLY_ARRAY || e->elements != 2) {
        Reply d(command({ "DISCARD" }));
        return std::nullopt;
    }
    const redisReply* n = e->element[0];
    if (n->type != REDIS_REPLY_INTEGER) {
        setError(collection + "/" + key + " isn't a number" + (n->type == REDIS_REPLY_ERROR ? std::string(" (") + std::string(n->str, n->len) + ")" : std::string()));
        return std::nullopt;
    }
    return static_cast<int64_t>(n->integer);
}

bool ValkeyStore::transaction(const std::function<bool()>& fn) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    // Nested: the inner one's writes join the outer's; calling it off undoes only its own.
    const auto before = m_pending;
    ++m_depth;
    bool ok = false;
    try {
        ok = fn();
    } catch (...) {
        --m_depth;
        m_pending = before;
        throw;
    }
    --m_depth;
    if (!ok) {
        m_pending = before;
        if (lastError().empty()) setError("the transaction was called off");
        return false;
    }
    if (m_depth) return true;
    auto pending = std::move(m_pending);
    m_pending.clear();
    if (pending.empty()) return true;
    Reply m(command({ "MULTI" }));
    if (!m) return false;
    bool queued = true;
    for (const auto& [ck, p] : pending) {
        const auto& [c, k] = ck;
        Reply a(p.erase ? command({ "HDEL", hashKey(c), k }) : command({ "HSET", hashKey(c), k, p.value }));
        Reply b(p.erase ? command({ "ZREM", indexKey(c), k }) : command({ "ZADD", indexKey(c), "0", k }));
        queued = queued && a && b;
    }
    if (!queued) {
        const std::string why = lastError();
        Reply d(command({ "DISCARD" }));
        setError(why);
        return false;
    }
    Reply e(command({ "EXEC" }));
    return e != nullptr;
}

} // namespace kke::storage
