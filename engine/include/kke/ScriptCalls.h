#pragma once

// net.call / net.handle (docs/SCRIPTING.md "Calls"): a player's script asks
// the server something and gets an answer, the way SpacetimeDB's reducers
// answer (docs/NETWORKING.md "Calls"). net.send stays for fire-and-forget.
//
//   -- sv_courts.lua (runs on the server, or on the host's game)
//   net.handle("join_court", function(data, from)
//       if courts[data.court].full then return nil, "that court is full" end
//       courts[data.court]:add(from)
//       return { seat = courts[data.court].seats }
//   end)
//
//   -- a player's script
//   net.call("join_court", { court = 3 }, function(ok, answer)
//       if ok then print("seat", answer.seat) else print("no:", answer) end
//   end)
//
// - Every call gets exactly one answer: what the handler returned, its
//   refusal (return nil, "why"), or an error: no handler, the handler
//   failed (the details go to the server's log, not to the player), the
//   server is busy, no answer in `timeoutSeconds`, or the connection went.
// - All or nothing: a handler runs inside the owner's `atomically`. On
//   kke_server that is a store transaction plus holding back what the
//   handler sends, spawns, removes and scores (and its synced table
//   changes) until it succeeded; a
//   refusal or an error undoes all of it. Lua variables it changed are not
//   undone: check first, then change.
// - Where the handlers live (a server, the host's game, an offline game)
//   a net.call runs there too, answered on the next update, so the same
//   script works alone and online.
//
// Pure scripting and bytes: the owner (ScriptModule in a game,
// ServerScripts on kke_server) moves the bytes as game events of kinds
// script_net::kScriptCall / kScriptReply (kke/net/ScriptSpawns.h).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kke {

class ScriptVM;

class ScriptCalls {
public:
    enum class Status : uint8_t { Ok = 0, Refused = 1, NoHandler = 2, Failed = 3, Busy = 4 };

    // Wire format, a game event's payload:
    //   call   id (u32, little endian), name, 0, value (ScriptVM::encodeValue)
    //   reply  id (u32, little endian), status (u8), value (the answer, or the reason as a string)
    struct Call {
        uint32_t id = 0;
        std::string name, value;
    };
    struct Reply {
        uint32_t id = 0;
        Status status = Status::Ok;
        std::string value;
    };
    static std::vector<uint8_t> encodeCall(const Call& c);
    static std::vector<uint8_t> encodeReply(const Reply& r);
    // nullopt: malformed (they come off the network).
    static std::optional<Call> decodeCall(const std::vector<uint8_t>& bytes);
    static std::optional<Reply> decodeReply(const std::vector<uint8_t>& bytes);

    // How this machine reaches the others; unset parts are absent.
    struct Link {
        std::function<bool()> serves;                                          // calls run here (server, host, offline)
        std::function<bool(const std::vector<uint8_t>&)> sendCall;             // to the server; false: not connected
        std::function<void(int player, const std::vector<uint8_t>&)> sendReply; // to the player that called
        std::function<int()> localPlayer;                                      // `from` for a call made here
        // Runs a handler (the function returns true when the call
        // succeeded); false from either undoes it. Unset: just runs it.
        std::function<bool(const std::function<bool()>&)> atomically;
    };

    ScriptCalls(ScriptVM& vm, Link link);
    ~ScriptCalls();
    ScriptCalls(const ScriptCalls&) = delete;
    ScriptCalls& operator=(const ScriptCalls&) = delete;

    void bind(); // net.call, net.handle

    // From the network (the owner routes the event kinds here).
    void callReceived(int fromPlayer, const std::vector<uint8_t>& payload);
    void replyReceived(const std::vector<uint8_t>& payload);
    // Runs waiting calls, delivers answers, times out old calls. `now` in
    // seconds, monotonic.
    void update(double now);
    // The connection went: every waiting call answers false, `why`.
    void disconnected(const std::string& why);
    // A script was unloaded: its handlers and waiting callbacks go.
    void release(const std::string& source);

    std::function<void(const std::string& line)> warn;

    double timeoutSeconds = 10.0;
    size_t maxPayloadBytes = 512; // net::kMaxEventBytes: one game event each way
    size_t maxWaiting = 64;       // this machine's calls without an answer yet
    size_t maxQueued = 256;       // calls waiting to run here (more: Busy)

    size_t waiting() const { return m_pending.size(); }
    size_t handlers() const { return m_handlers.size(); }

private:
    struct Handler {
        int ref = 0;
        std::string source;
    };
    struct Pending {
        int ref = 0; // the callback (0: none)
        std::string source;
        double deadline = 0.0;
    };
    struct Queued {
        Call call;
        int from = 0;
        bool local = false; // made on this machine: answered here, not sent
    };
    Reply run(const Queued& q);
    void answer(uint32_t id, Status status, const std::string& value); // runs a pending callback
    void answerText(uint32_t id, Status status, const std::string& text);

    ScriptVM& m_vm;
    Link m_link;
    std::map<std::string, Handler> m_handlers;
    std::map<uint32_t, Pending> m_pending;
    std::vector<Queued> m_queue;
    std::vector<Reply> m_replies; // received, delivered on update
    uint32_t m_nextId = 1;
    double m_now = 0.0;
};

} // namespace kke
