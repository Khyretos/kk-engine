#pragma once

// Synced tables for Lua (docs/SCRIPTING.md "Synced tables"): the server's
// scripts keep tables of rows, every player's scripts watch them and are
// told what changed (kke/net/SyncedTables.h does the work).
//
//   -- sv_courts.lua (the host, a server, or alone)
//   local courts = net.table("courts")
//   courts:set(3, { court = 3, players = { "Kees" }, score = "15-0" })
//   courts:remove(3)
//
//   -- any script
//   local view = net.watch("courts", { court = 3 }, function(event, key, row, old)
//       -- event: "insert", "update", "delete", "ready" (the first rows are in), "error"
//   end)
//   view:rows()  view:get(3)  view:ready()  view:count()  view:stop()
//
// Keys are whole numbers or strings; rows are what net.send carries (at
// most TableLimits::maxRowBytes encoded). Filters compare a row's
// top-level fields for equality (3 and 3.0 are the same). A table belongs
// to the script that made it: reloading that script empties it, and only
// the differences reach the watchers. Changes a net.call handler made are
// undone with the rest of it (ScriptCalls "all or nothing").

#include "kke/net/SyncedTables.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct lua_State;

namespace kke {

class ScriptVM;

class ScriptTables {
public:
    struct Link {
        std::function<bool()> serves;  // the tables live here (server, host, offline)
        std::function<bool()> online;  // connected to a server that has them (a client)
        std::function<void(uint16_t kind, const std::vector<uint8_t>&)> sendToServer;
        std::function<void(int player, const std::vector<uint8_t>&)> sendToPlayer; // kTableRows
    };

    ScriptTables(ScriptVM& vm, Link link, net::TableLimits limits = {});
    ~ScriptTables();
    ScriptTables(const ScriptTables&) = delete;
    ScriptTables& operator=(const ScriptTables&) = delete;

    void bind(); // net.table, net.watch

    // From the network (the owner routes kTableSubscribe / kTableUnsubscribe
    // / kTableRows here).
    void received(uint16_t kind, int fromPlayer, const std::vector<uint8_t>& payload);
    void playerLeft(int player);
    // Follows the role (hosting, joined, offline), sends what changed, and
    // runs the watchers' callbacks.
    void update();
    void release(const std::string& source); // a script unloaded: its tables and watches go

    net::TableServer& server() { return m_server; } // for all-or-nothing calls
    std::function<void(const std::string& line)> warn;

private:
    enum class Source { None, Local, Remote };
    struct View {
        int ref = 0; // callback (0: none)
        std::string source;
    };
    void flush();
    bool rowFromLua(lua_State* L, int index, net::TableRow& row, std::string& error) const;
    static bool scalar(lua_State* L, int index, std::string& out);
    static bool keyFromLua(lua_State* L, int index, std::string& out, const char* what);
    uint32_t viewArg(lua_State* L, const char* what) const;
    std::string tableArg(lua_State* L, const char* what) const;

    ScriptVM& m_vm;
    Link m_link;
    net::TableServer m_server;
    net::TableClient m_client;
    Source m_source = Source::None;
    std::map<uint32_t, View> m_views;
    std::map<std::string, std::string> m_owners; // table -> script that made it
};

} // namespace kke
