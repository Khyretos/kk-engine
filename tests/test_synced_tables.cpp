// Synced tables (kke/net/SyncedTables.h): a server's keyed rows, watched
// whole or through a filter, reach every watcher as their first rows and
// then only the changes; a new server shows only the differences; an
// undone change is never sent; damaged bytes are dropped.

#include "kke/net/SyncedTables.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>

using namespace kke::net;
using Kind = TableClient::Change::Kind;

namespace {

TableRow row(const std::string& bytes, std::map<std::string, std::string> fields = {}) { return TableRow{ bytes, std::move(fields) }; }

// One server and one watcher, joined the way ScriptTables joins them.
struct Pair {
    TableServer server;
    TableClient client;
    int player = 7;
    void pump() {
        for (auto& [kind, bytes] : client.takeSends()) server.received(player, kind, bytes);
        for (auto& [to, bytes] : server.flush()) {
            EXPECT_LE(bytes.size(), server.limits().maxEventBytes);
            if (to == player) client.received(bytes);
        }
    }
};

std::vector<std::pair<Kind, std::string>> changes(TableClient& c) {
    std::vector<std::pair<Kind, std::string>> out;
    for (const auto& ch : c.takeChanges()) out.emplace_back(ch.kind, ch.kind == Kind::Error ? ch.row : ch.key);
    return out;
}

} // namespace

TEST(SyncedTables, FirstRowsThenOnlyChanges) {
    Pair p;
    p.server.set("courts", "1", row("a"));
    p.server.set("courts", "2", row("b"));
    p.client.resubscribe();
    const uint32_t w = p.client.watch("courts", {});
    ASSERT_NE(w, 0u);
    p.pump();
    const auto first = changes(p.client);
    ASSERT_EQ(first.size(), 3u);
    EXPECT_EQ(first[2].first, Kind::Ready);
    EXPECT_TRUE(p.client.ready(w));
    EXPECT_EQ(p.client.rows(w)->size(), 2u);

    // Several changes to a row in one flush are one change; a row made and
    // removed in between is none.
    p.server.set("courts", "1", row("a2"));
    p.server.set("courts", "1", row("a3"));
    p.server.set("courts", "3", row("c"));
    p.server.remove("courts", "3");
    p.server.remove("courts", "2");
    p.server.set("courts", "2", row("b")); // back as it was: nothing
    p.pump();
    const auto next = changes(p.client);
    ASSERT_EQ(next.size(), 1u);
    EXPECT_EQ(next[0], std::make_pair(Kind::Update, std::string("1")));
    EXPECT_EQ(p.client.rows(w)->at("1"), "a3");

    p.server.clear("courts");
    p.pump();
    EXPECT_EQ(changes(p.client).size(), 2u); // two deletes
    EXPECT_TRUE(p.client.rows(w)->empty());
}

TEST(SyncedTables, FiltersFollowRowsBetweenWatchers) {
    Pair p;
    p.client.resubscribe();
    const uint32_t three = p.client.watch("courts", { { "court", "3" } });
    const uint32_t four = p.client.watch("courts", { { "court", "4" } });
    p.pump();
    p.client.takeChanges();
    p.server.set("courts", "kees", row("on 3", { { "court", "3" } }));
    p.pump();
    auto c = p.client.takeChanges();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].watch, three);
    EXPECT_EQ(c[0].kind, Kind::Insert);
    // Kees moves to court 4: gone from one watch, new in the other.
    p.server.set("courts", "kees", row("on 4", { { "court", "4" } }));
    p.pump();
    c = p.client.takeChanges();
    ASSERT_EQ(c.size(), 2u);
    for (const auto& ch : c) {
        if (ch.watch == three) {
            EXPECT_EQ(ch.kind, Kind::Delete);
        } else {
            EXPECT_EQ(ch.watch, four);
            EXPECT_EQ(ch.kind, Kind::Insert);
        }
    }
    EXPECT_TRUE(p.client.rows(three)->empty());
    EXPECT_EQ(p.client.rows(four)->at("kees"), "on 4");
}

TEST(SyncedTables, ANewServerShowsOnlyTheDifferences) {
    Pair p;
    p.server.set("t", "same", row("1"));
    p.server.set("t", "changes", row("1"));
    p.server.set("t", "goes", row("1"));
    p.client.resubscribe();
    const uint32_t w = p.client.watch("t", {});
    p.pump();
    p.client.takeChanges();
    // Disconnected; meanwhile the server moves on; then back.
    p.client.sourceLost();
    EXPECT_FALSE(p.client.ready(w));
    EXPECT_EQ(p.client.rows(w)->size(), 3u); // still shown
    p.server.dropPlayer(p.player);
    p.server.set("t", "changes", row("2"));
    p.server.remove("t", "goes");
    p.server.set("t", "new", row("1"));
    p.pump();
    EXPECT_TRUE(p.client.takeChanges().empty());
    p.client.resubscribe();
    p.pump();
    auto c = changes(p.client);
    std::sort(c.begin(), c.end());
    ASSERT_EQ(c.size(), 4u);
    EXPECT_EQ(c[0], std::make_pair(Kind::Insert, std::string("new")));
    EXPECT_EQ(c[1], std::make_pair(Kind::Update, std::string("changes")));
    EXPECT_EQ(c[2], std::make_pair(Kind::Delete, std::string("goes")));
    EXPECT_EQ(c[3].first, Kind::Ready);
}

TEST(SyncedTables, UndoneChangesAreNeverSent) {
    Pair p;
    p.server.set("t", "a", row("1"));
    p.client.resubscribe();
    p.client.watch("t", {});
    p.pump();
    p.client.takeChanges();
    p.server.beginUndo();
    p.server.set("t", "a", row("2"));
    p.server.set("t", "b", row("1"));
    p.server.clear("t");
    p.server.rollback();
    ASSERT_NE(p.server.get("t", "a"), nullptr);
    EXPECT_EQ(p.server.get("t", "a")->bytes, "1");
    EXPECT_EQ(p.server.get("t", "b"), nullptr);
    p.pump();
    EXPECT_TRUE(p.client.takeChanges().empty());
    // Kept: sent.
    p.server.beginUndo();
    p.server.set("t", "a", row("3"));
    p.server.endUndo();
    p.pump();
    EXPECT_EQ(changes(p.client).size(), 1u);
}

TEST(SyncedTables, LimitsSayWhy) {
    TableLimits l;
    l.maxWatchesPerPlayer = 1;
    l.maxRowsPerTable = 1;
    Pair p{ TableServer(l), TableClient(l) };
    std::string why;
    EXPECT_FALSE(p.server.set("t", "k", row(std::string(l.maxRowBytes + 1, 'x')), &why));
    EXPECT_NE(why.find("too big"), std::string::npos) << why;
    EXPECT_TRUE(p.server.set("t", "k", row("x")));
    EXPECT_FALSE(p.server.set("t", "k2", row("x"), &why));
    EXPECT_NE(why.find("full"), std::string::npos) << why;
    EXPECT_FALSE(p.server.set("", "k", row("x")));
    EXPECT_EQ(p.client.watch("", {}), 0u);
    p.client.resubscribe();
    p.client.watch("t", {});
    const uint32_t second = p.client.watch("t", {});
    p.pump();
    bool refused = false;
    for (const auto& ch : p.client.takeChanges()) refused |= ch.watch == second && ch.kind == Kind::Error && ch.row.find("too many") != std::string::npos;
    EXPECT_TRUE(refused);
    EXPECT_EQ(p.server.watches(p.player), 1u);
}

TEST(SyncedTables, ManyRowsSplitIntoEvents) {
    Pair p;
    for (int i = 0; i < 100; ++i) p.server.set("t", std::to_string(i), row(std::string(300, char('a' + i % 26))));
    p.client.resubscribe();
    const uint32_t w = p.client.watch("t", {});
    p.pump(); // each event checked against maxEventBytes
    EXPECT_EQ(p.client.rows(w)->size(), 100u);
}

TEST(SyncedTables, DamagedBytesAreDropped) {
    std::mt19937 rng(42);
    TableServer server;
    TableClient client;
    client.resubscribe();
    client.watch("t", {});
    for (int i = 0; i < 5000; ++i) {
        std::vector<uint8_t> junk(rng() % 64);
        for (auto& b : junk) b = uint8_t(rng());
        server.received(1, kTableSubscribe, junk);
        server.received(1, kTableUnsubscribe, junk);
        client.received(junk);
    }
    EXPECT_FALSE(tables_wire::decodeRows({}).has_value());
    EXPECT_FALSE(tables_wire::decodeRows({ 1, 0, 0, 0, 9, 0, 0, 0, 0 }).has_value()); // no such op
    EXPECT_FALSE(tables_wire::decodeSubscribe(tables_wire::subscribe(1, std::string(40, 't'), {}), TableLimits{}).has_value());
    const auto ok = tables_wire::decodeSubscribe(tables_wire::subscribe(5, "t", { { "court", "3" } }), TableLimits{});
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(ok->sub, 5u);
    EXPECT_EQ(ok->filter.size(), 1u);
}
