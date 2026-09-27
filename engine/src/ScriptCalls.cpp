#include "kke/ScriptCalls.h"

#include "kke/ScriptVM.h"

#include <lauxlib.h>
#include <lua.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace kke {

namespace {

constexpr size_t kMaxName = 64;
constexpr size_t kMaxReason = 400; // bytes of a refusal's text

void putId(std::vector<uint8_t>& out, uint32_t id) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(id >> (8 * i)));
}

uint32_t getId(const std::vector<uint8_t>& in) {
    uint32_t id = 0;
    for (int i = 0; i < 4; ++i) id |= uint32_t(in[size_t(i)]) << (8 * i);
    return id;
}

bool checkName(lua_State* L, const char* what, std::string& out) {
    size_t len = 0;
    const char* name = luaL_checklstring(L, 1, &len);
    if (len == 0 || len > kMaxName || std::memchr(name, 0, len)) {
        luaL_error(L, "%s: the name must be 1-64 characters", what);
        return false;
    }
    out.assign(name, len);
    return true;
}

} // namespace

std::vector<uint8_t> ScriptCalls::encodeCall(const Call& c) {
    std::vector<uint8_t> out;
    out.reserve(5 + c.name.size() + c.value.size());
    putId(out, c.id);
    out.insert(out.end(), c.name.begin(), c.name.end());
    out.push_back(0);
    out.insert(out.end(), c.value.begin(), c.value.end());
    return out;
}

std::vector<uint8_t> ScriptCalls::encodeReply(const Reply& r) {
    std::vector<uint8_t> out;
    out.reserve(5 + r.value.size());
    putId(out, r.id);
    out.push_back(static_cast<uint8_t>(r.status));
    out.insert(out.end(), r.value.begin(), r.value.end());
    return out;
}

std::optional<ScriptCalls::Call> ScriptCalls::decodeCall(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 6) return std::nullopt;
    const auto nameBegin = bytes.begin() + 4;
    const auto zero = std::find(nameBegin, bytes.end(), uint8_t(0));
    if (zero == bytes.end() || zero == nameBegin || size_t(zero - nameBegin) > kMaxName) return std::nullopt;
    Call c;
    c.id = getId(bytes);
    c.name.assign(nameBegin, zero);
    c.value.assign(zero + 1, bytes.end());
    return c;
}

std::optional<ScriptCalls::Reply> ScriptCalls::decodeReply(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 5 || bytes[4] > static_cast<uint8_t>(Status::Busy)) return std::nullopt;
    Reply r;
    r.id = getId(bytes);
    r.status = static_cast<Status>(bytes[4]);
    r.value.assign(bytes.begin() + 5, bytes.end());
    return r;
}

ScriptCalls::ScriptCalls(ScriptVM& vm, Link link) : m_vm(vm), m_link(std::move(link)) {}

ScriptCalls::~ScriptCalls() {
    for (const auto& [name, h] : m_handlers) m_vm.unref(h.ref);
    for (const auto& [id, p] : m_pending) m_vm.unref(p.ref);
}

void ScriptCalls::bind() {
    // net.call(name, data [, function(ok, answer) end]) -> sent?
    m_vm.registerFunction("net", "call", [this](lua_State* L) {
        std::string name;
        checkName(L, "net.call", name);
        if (!lua_isnoneornil(L, 3)) luaL_checktype(L, 3, LUA_TFUNCTION);
        Call c;
        std::string err;
        if (!ScriptVM::encodeValue(L, 2, c.value, err, maxPayloadBytes - 5 - name.size()))
            return luaL_error(L, "net.call: %s", err.c_str());
        if (m_pending.size() >= maxWaiting) {
            if (warn) warn("net.call '" + name + "': " + std::to_string(maxWaiting) + " calls already wait for an answer");
            lua_pushboolean(L, 0);
            return 1;
        }
        c.name = std::move(name);
        c.id = m_nextId++;
        if (m_nextId == 0) m_nextId = 1;
        const bool here = m_link.serves && m_link.serves();
        if (here) {
            if (m_queue.size() >= maxQueued) m_replies.push_back({ c.id, Status::Busy, ScriptVM::encodeString("the server is busy") });
            else m_queue.push_back({ c, m_link.localPlayer ? m_link.localPlayer() : 0, true });
        } else if (!m_link.sendCall || !m_link.sendCall(encodeCall(c))) {
            lua_pushboolean(L, 0);
            return 1;
        }
        if (!lua_isnoneornil(L, 3)) m_pending[c.id] = { m_vm.ref(L, 3), m_vm.currentSource(), m_now + timeoutSeconds };
        lua_pushboolean(L, 1);
        return 1;
    });
    // net.handle(name, function(data, from) return answer end), or nil to stop.
    m_vm.registerFunction("net", "handle", [this](lua_State* L) {
        std::string name;
        checkName(L, "net.handle", name);
        if (!lua_isnoneornil(L, 2)) luaL_checktype(L, 2, LUA_TFUNCTION);
        if (auto it = m_handlers.find(name); it != m_handlers.end()) {
            m_vm.unref(it->second.ref);
            m_handlers.erase(it);
        }
        if (!lua_isnoneornil(L, 2)) m_handlers[name] = { m_vm.ref(L, 2), m_vm.currentSource() };
        return 0;
    });
}

void ScriptCalls::callReceived(int fromPlayer, const std::vector<uint8_t>& payload) {
    if (!m_link.serves || !m_link.serves()) return; // only a server answers
    const std::optional<Call> c = decodeCall(payload);
    if (!c) {
        if (warn) warn("dropped a script call from player " + std::to_string(fromPlayer) + ": damaged");
        return;
    }
    if (m_queue.size() >= maxQueued) {
        if (warn) warn("script call '" + c->name + "' from player " + std::to_string(fromPlayer) + " refused: " + std::to_string(maxQueued) + " already waiting");
        if (m_link.sendReply) m_link.sendReply(fromPlayer, encodeReply({ c->id, Status::Busy, ScriptVM::encodeString("the server is busy") }));
        return;
    }
    m_queue.push_back({ *c, fromPlayer, false });
}

void ScriptCalls::replyReceived(const std::vector<uint8_t>& payload) {
    const std::optional<Reply> r = decodeReply(payload);
    if (!r) {
        if (warn) warn("dropped a damaged answer to a script call");
        return;
    }
    // Only answers to calls still waiting are kept.
    if (m_pending.count(r->id)) m_replies.push_back(*r);
}

ScriptCalls::Reply ScriptCalls::run(const Queued& q) {
    Reply r{ q.call.id, Status::Failed, {} };
    const auto it = m_handlers.find(q.call.name);
    if (it == m_handlers.end()) {
        r.status = Status::NoHandler;
        r.value = ScriptVM::encodeString("no handler for '" + q.call.name + "' on the server");
        return r;
    }
    const Handler h = it->second; // the handler may replace itself
    std::string reason = "the server's handler for '" + q.call.name + "' failed";
    auto body = [&]() -> bool {
        const bool ran = m_vm.callRefResults(
            h.ref, h.source,
            [&](lua_State* S) {
                std::string err;
                if (!ScriptVM::decodeValue(S, q.call.value, err)) {
                    if (warn) warn("script call '" + q.call.name + "' from player " + std::to_string(q.from) + " is damaged (" + err + "): data is nil");
                    lua_pushnil(S);
                }
                lua_pushinteger(S, q.from);
                return 2;
            },
            2,
            [&](lua_State* S) {
                if (lua_isnil(S, -2) && !lua_isnil(S, -1)) {
                    r.status = Status::Refused;
                    const char* why = lua_type(S, -1) == LUA_TSTRING || lua_type(S, -1) == LUA_TNUMBER ? lua_tostring(S, -1) : nullptr;
                    reason = why ? why : "refused";
                    return;
                }
                std::string err;
                if (ScriptVM::encodeValue(S, -2, r.value, err, maxPayloadBytes - 5)) {
                    r.status = Status::Ok;
                } else {
                    r.value.clear();
                    reason = "the answer to '" + q.call.name + "' can't be sent: " + err;
                    if (warn) warn("script call '" + q.call.name + "': " + reason);
                }
            });
        if (!ran) r.status = Status::Failed;
        return r.status == Status::Ok;
    };
    const bool kept = m_link.atomically ? m_link.atomically(body) : body();
    if (r.status == Status::Ok && !kept) {
        r.status = Status::Failed;
        r.value.clear();
        reason = "the server couldn't keep the result of '" + q.call.name + "'";
        if (warn) warn("script call '" + q.call.name + "': its changes couldn't be kept, so they were undone");
    }
    if (reason.size() > kMaxReason) reason.resize(kMaxReason); // it must fit in one event
    if (r.status != Status::Ok) r.value = ScriptVM::encodeString(reason);
    return r;
}

void ScriptCalls::update(double now) {
    m_now = now;
    if (!m_queue.empty()) {
        auto queue = std::move(m_queue);
        m_queue.clear();
        for (const Queued& q : queue) {
            Reply r = run(q);
            if (q.local) m_replies.push_back(std::move(r));
            else if (m_link.sendReply) m_link.sendReply(q.from, encodeReply(r));
        }
    }
    if (!m_replies.empty()) {
        auto replies = std::move(m_replies);
        m_replies.clear();
        for (const Reply& r : replies) answer(r.id, r.status, r.value);
    }
    std::vector<uint32_t> late;
    for (const auto& [id, p] : m_pending)
        if (p.deadline <= now) late.push_back(id);
    for (uint32_t id : late) answerText(id, Status::Failed, "no answer from the server");
}

void ScriptCalls::answer(uint32_t id, Status status, const std::string& value) {
    const auto it = m_pending.find(id);
    if (it == m_pending.end()) return;
    const Pending p = it->second;
    m_pending.erase(it);
    m_vm.callRef(p.ref, p.source, [&](lua_State* L) {
        lua_pushboolean(L, status == Status::Ok);
        std::string err;
        if (!ScriptVM::decodeValue(L, value, err)) {
            if (warn) warn("a damaged answer to a script call (" + err + "): it is nil");
            lua_pushnil(L);
        }
        return 2;
    });
    m_vm.unref(p.ref);
}

void ScriptCalls::answerText(uint32_t id, Status status, const std::string& text) {
    answer(id, status, ScriptVM::encodeString(text));
}

void ScriptCalls::disconnected(const std::string& why) {
    std::vector<uint32_t> ids;
    for (const auto& [id, p] : m_pending) ids.push_back(id);
    for (uint32_t id : ids) answerText(id, Status::Failed, why);
    m_replies.clear();
    std::erase_if(m_queue, [](const Queued& q) { return !q.local; });
}

void ScriptCalls::release(const std::string& source) {
    for (auto it = m_handlers.begin(); it != m_handlers.end();) {
        if (it->second.source != source) {
            ++it;
            continue;
        }
        m_vm.unref(it->second.ref);
        it = m_handlers.erase(it);
    }
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->second.source != source) {
            ++it;
            continue;
        }
        m_vm.unref(it->second.ref);
        it = m_pending.erase(it);
    }
}

} // namespace kke
