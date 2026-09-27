#pragma once

// Synced tables (docs/NETWORKING.md "Synced tables"), the idea of
// SpacetimeDB's tables and subscriptions: the server keeps named tables of
// keyed rows; a player watches a table, or only the rows whose fields
// match (court == 3). They get the matching rows once, then only what
// changed: inserts, updates and deletes, at most one per row per flush.
// Late joiners and reconnects get the current rows; nothing is hand-rolled
// per game.
//
//   TableServer   where the truth is (kke_server, the host's game, an
//                 offline game): set / remove rows, subscriptions per
//                 player, flush() -> the bytes each player needs.
//   TableClient   every machine: its watches, a copy of the rows each one
//                 sees, and the changes to show.
//
// Rows and keys are bytes the caller chose (Lua uses ScriptVM::encodeValue);
// `fields` are the row's top-level values that filters compare, each
// encoded the same way. Pure logic and bytes: the owner moves the bytes as
// game events (kinds below, reliable and ordered) and routes them back.
// A watch on the server's own machine goes through the same path with
// player kLocal, so the same code works alone, hosting and joined.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace kke::net {

// Game event kinds, next to script_net's (kke/net/ScriptSpawns.h).
constexpr uint16_t kTableSubscribe = 0x4C06;   // player -> server: watch a table
constexpr uint16_t kTableUnsubscribe = 0x4C07; // player -> server: stop
constexpr uint16_t kTableRows = 0x4C08;        // server -> player: rows, changes, ready, errors

struct TableRow {
    std::string bytes;
    std::map<std::string, std::string> fields; // name -> encoded value (what filters compare)
    bool operator==(const TableRow& o) const { return bytes == o.bytes; }
};

// Every field must equal its value (an empty filter: every row).
using TableFilter = std::vector<std::pair<std::string, std::string>>;

struct TableLimits {
    size_t maxTables = 64;
    size_t maxRowsPerTable = 4096;
    size_t maxKeyBytes = 64;
    size_t maxRowBytes = 400;           // one row, and its key, fit in one event
    size_t maxNameBytes = 32;
    size_t maxFilterFields = 4;
    size_t maxWatchesPerPlayer = 32;
    size_t maxEventBytes = 512;         // net::kMaxEventBytes
};

class TableServer {
public:
    static constexpr int kLocal = -1; // the server's own machine

    explicit TableServer(TableLimits limits = {}) : m_limits(limits) {}

    // Insert or replace. False (and why) past a limit.
    bool set(const std::string& table, const std::string& key, TableRow row, std::string* error = nullptr);
    bool remove(const std::string& table, const std::string& key); // false: wasn't there
    void clear(const std::string& table);                            // every row, and the table
    const TableRow* get(const std::string& table, const std::string& key) const;
    const std::map<std::string, TableRow>* rows(const std::string& table) const;
    bool has(const std::string& table) const { return m_tables.count(table) > 0; }

    // From a player (the kTableSubscribe / kTableUnsubscribe payloads).
    void received(int player, uint16_t kind, const std::vector<uint8_t>& payload);
    void dropPlayer(int player);          // left: its watches go
    size_t watches(int player) const;

    // What each player needs since the last flush: kTableRows payloads,
    // in order. Changes first, then new watches' rows and "ready".
    std::vector<std::pair<int, std::vector<uint8_t>>> flush();

    // All or nothing (a net.call handler): changes after beginUndo() are
    // taken back by rollback(); endUndo() keeps them. Nothing was sent in
    // between (only flush sends).
    void beginUndo();
    void endUndo();
    void rollback();

    const TableLimits& limits() const { return m_limits; }

private:
    struct Watch {
        int player = 0;
        uint32_t id = 0;
        std::string table;
        TableFilter filter;
        bool fresh = true;  // its rows go out on the next flush
        std::string error;  // refused: said on the next flush, then dropped
    };
    static bool matches(const TableFilter& f, const TableRow& row);
    void touch(const std::string& table, const std::string& key); // remember the state before, once per flush

    TableLimits m_limits;
    std::map<std::string, std::map<std::string, TableRow>> m_tables;
    // Per table and key, the row as it was at the last flush (nullopt: none).
    std::map<std::string, std::map<std::string, std::optional<TableRow>>> m_before;
    std::vector<Watch> m_watches;
    bool m_undoing = false;
    std::map<std::pair<std::string, std::string>, std::optional<TableRow>> m_undo;
};

class TableClient {
public:
    struct Change {
        enum class Kind { Insert, Update, Delete, Ready, Error };
        uint32_t watch = 0;
        Kind kind = Kind::Insert;
        std::string key, row, old; // Error: row is the reason
    };

    explicit TableClient(TableLimits limits = {}) : m_limits(limits) {}

    // A new watch (0 when the filter is too long or the name bad). Its
    // subscription goes out with the next takeSends() while there is a
    // source; its rows come back as changes, then Ready.
    uint32_t watch(const std::string& table, const TableFilter& filter);
    void stop(uint32_t watch);
    bool ready(uint32_t watch) const;
    const std::map<std::string, std::string>* rows(uint32_t watch) const; // key -> row

    // The server's kTableRows payloads.
    void received(const std::vector<uint8_t>& payload);
    // A new server to watch (joined, reconnected, became the host): every
    // watch subscribes again; its rows stay until the new ones are in, and
    // then only the differences show as changes.
    void resubscribe();
    // No server any more: nothing is sent, the rows stay, not ready.
    void sourceLost();
    bool hasSource() const { return m_source; }

    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> takeSends(); // kind, payload for the server
    std::vector<Change> takeChanges();

private:
    struct Watch {
        std::string table;
        TableFilter filter;
        uint32_t sub = 0;   // current subscription id (0: none)
        bool ready = false;
        std::map<std::string, std::string> rows;
        std::map<std::string, std::string> incoming; // rows of a subscription not ready yet
    };
    void subscribe(uint32_t id, Watch& w);
    void apply(Watch& w, uint32_t id, uint8_t op, std::string key, std::string row);

    TableLimits m_limits;
    std::map<uint32_t, Watch> m_watches;
    std::map<uint32_t, uint32_t> m_subToWatch;
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> m_sends;
    std::vector<Change> m_changes;
    uint32_t m_nextWatch = 1, m_nextSub = 1;
    bool m_source = false;
};

// Wire format, for tests and tools.
namespace tables_wire {
enum Op : uint8_t { Insert = 0, Update = 1, Delete = 2, Ready = 3, Error = 4 };
std::vector<uint8_t> subscribe(uint32_t sub, const std::string& table, const TableFilter& filter);
std::vector<uint8_t> unsubscribe(uint32_t sub);
struct Subscribe {
    uint32_t sub = 0;
    std::string table;
    TableFilter filter;
};
std::optional<Subscribe> decodeSubscribe(const std::vector<uint8_t>& bytes, const TableLimits& limits);
std::optional<uint32_t> decodeUnsubscribe(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> error(uint32_t sub, const std::string& why); // a kTableRows payload refusing a watch
struct RowOp {
    uint32_t sub = 0;
    Op op = Insert;
    std::string key, row;
};
// nullopt: damaged (the whole payload is dropped).
std::optional<std::vector<RowOp>> decodeRows(const std::vector<uint8_t>& bytes);
} // namespace tables_wire

} // namespace kke::net
