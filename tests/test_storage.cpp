// kke::storage (docs/STORAGE.md): every backend passes the same tests.
// Valkey and PostgreSQL run when KKE_TEST_VALKEY / KKE_TEST_POSTGRES name
// a server (e.g. valkey://127.0.0.1:6379); otherwise they're skipped.

#include "kke/storage/Store.h"
#include "kke/storage/SqliteStore.h"
#if KKE_ENABLE_POSTGRES
#include "kke/storage/PostgresStore.h"
#endif

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace kke::storage;

namespace {

std::string tempDb(const std::string& name) {
    const auto dir = std::filesystem::temp_directory_path() / "kke_storage_test";
    std::filesystem::create_directories(dir);
    const auto p = dir / name;
    for (const char* ext : { "", "-wal", "-shm" }) std::filesystem::remove(p.string() + ext);
    return p.string();
}

class StoreTest : public ::testing::TestWithParam<std::string> {
protected:
    void SetUp() override {
        std::string url = GetParam();
        if (url == "sqlite") url = "sqlite:" + tempDb("conformance.db");
        if (url == "valkey" || url == "postgres") {
            const char* env = std::getenv(url == "valkey" ? "KKE_TEST_VALKEY" : "KKE_TEST_POSTGRES");
            if (!env || !*env) GTEST_SKIP() << "set KKE_TEST_" << (url == "valkey" ? "VALKEY" : "POSTGRES") << " to a server to test it";
            url = env;
        }
        std::string error;
        store = openStore(url, &error);
        ASSERT_TRUE(store) << error;
        // A clean slate on shared servers.
        for (const char* c : { "players", "counters", "t", "list" })
            for (const auto& item : store->list(c, "", 100000)) store->erase(c, item.key);
    }
    std::unique_ptr<Store> store;
};

} // namespace

TEST_P(StoreTest, PutGetEraseAndBinaryValues) {
    EXPECT_FALSE(store->get("players", "kees"));
    EXPECT_EQ(store->lastError(), "");
    ASSERT_TRUE(store->put("players", "kees", R"({"level":3})"));
    EXPECT_EQ(*store->get("players", "kees"), R"({"level":3})");
    ASSERT_TRUE(store->put("players", "kees", "changed"));
    EXPECT_EQ(*store->get("players", "kees"), "changed");
    const std::string bytes("\0\x01\xff\0end", 6);
    ASSERT_TRUE(store->put("players", std::string("bin\0key", 7), bytes));
    EXPECT_EQ(*store->get("players", std::string("bin\0key", 7)), bytes);
    ASSERT_TRUE(store->put("players", "empty", ""));
    ASSERT_TRUE(store->get("players", "empty"));
    EXPECT_EQ(*store->get("players", "empty"), "");
    ASSERT_TRUE(store->erase("players", "kees"));
    EXPECT_FALSE(store->get("players", "kees"));
    EXPECT_TRUE(store->erase("players", "never-there"));
}

TEST_P(StoreTest, RefusesBadNamesAndSizes) {
    EXPECT_FALSE(store->put("Bad Name", "k", "v"));
    EXPECT_NE(store->lastError(), "");
    EXPECT_FALSE(store->put("players", "", "v"));
    EXPECT_FALSE(store->put("players", std::string(257, 'k'), "v"));
    EXPECT_FALSE(store->put("players", "big", std::string(Store::kMaxValue + 1, 'x')));
    EXPECT_TRUE(store->put("players", "ok", "v"));
    EXPECT_EQ(store->lastError(), "");
}

TEST_P(StoreTest, ListsByPrefixInKeyOrder) {
    for (const char* k : { "b2", "a1", "b1", "c", "b3" }) ASSERT_TRUE(store->put("list", k, std::string("v") + k));
    const auto bs = store->list("list", "b");
    ASSERT_EQ(bs.size(), 3u);
    EXPECT_EQ(bs[0].key, "b1");
    EXPECT_EQ(bs[2].value, "vb3");
    EXPECT_EQ(store->list("list").size(), 5u);
    EXPECT_EQ(store->list("list", "", 2).size(), 2u);
    EXPECT_TRUE(store->list("players", "zzz").empty());
}

TEST_P(StoreTest, CountersAddUpAndRefuseNonNumbers) {
    EXPECT_EQ(*store->increment("counters", "kills", 1), 1);
    EXPECT_EQ(*store->increment("counters", "kills", 41), 42);
    EXPECT_EQ(*store->increment("counters", "kills", -2), 40);
    EXPECT_EQ(*store->get("counters", "kills"), "40");
    ASSERT_TRUE(store->put("counters", "name", "kees"));
    EXPECT_FALSE(store->increment("counters", "name", 1));
    EXPECT_NE(store->lastError(), "");
}

TEST_P(StoreTest, TransactionsAreAllOrNothing) {
    ASSERT_TRUE(store->put("t", "gold", "100"));
    // A trade that fails halfway leaves nothing changed.
    EXPECT_FALSE(store->transaction([&] {
        store->put("t", "gold", "0");
        store->put("t", "sword", "1");
        return false;
    }));
    EXPECT_EQ(*store->get("t", "gold"), "100");
    EXPECT_FALSE(store->get("t", "sword"));
    EXPECT_TRUE(store->transaction([&] { return store->put("t", "gold", "0") && store->put("t", "sword", "1"); }));
    EXPECT_EQ(*store->get("t", "gold"), "0");
    EXPECT_EQ(*store->get("t", "sword"), "1");
}

TEST_P(StoreTest, ManyThreadsCountingLoseNothing) {
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&] {
            for (int i = 0; i < 50; ++i) store->increment("counters", "shared", 1);
        });
    for (auto& t : threads) t.join();
    EXPECT_EQ(*store->get("counters", "shared"), "200");
}

INSTANTIATE_TEST_SUITE_P(Backends, StoreTest, ::testing::Values("memory:", "sqlite", "valkey", "postgres"),
                         [](const ::testing::TestParamInfo<std::string>& i) { return i.param == "memory:" ? std::string("memory") : i.param; });

TEST(SqliteStore, KeepsDataAcrossReopensAndNestedTransactions) {
    const std::string path = tempDb("reopen.db");
    {
        SqliteStore s;
        ASSERT_TRUE(s.open(path));
        ASSERT_TRUE(s.put("world", "seed", "1234"));
        // Inner part called off, outer kept.
        EXPECT_TRUE(s.transaction([&] {
            s.put("world", "outer", "yes");
            s.transaction([&] {
                s.put("world", "inner", "no");
                return false;
            });
            return true;
        }));
    }
    SqliteStore again;
    ASSERT_TRUE(again.open(path));
    EXPECT_EQ(*again.get("world", "seed"), "1234");
    EXPECT_EQ(*again.get("world", "outer"), "yes");
    EXPECT_FALSE(again.get("world", "inner"));
}

TEST(StoreBackup, SqliteAndMemoryCopyEverythingIntoOneFile) {
    for (const std::string kind : { "memory", "sqlite" }) {
        SCOPED_TRACE(kind);
        auto store = openStore(kind == "memory" ? "memory:" : "sqlite:" + tempDb("backup_source.db"));
        ASSERT_TRUE(store);
        EXPECT_TRUE(store->canBackup());
        ASSERT_TRUE(store->put("players", "kees", "{\"gold\": 3}"));
        ASSERT_TRUE(store->put("world", "day", std::string("\0bin", 4)));
        const std::string path = tempDb("backup_" + kind + ".db");
        { std::ofstream(path) << "an older backup"; } // replaced, not appended to
        ASSERT_TRUE(store->backup(path)) << store->lastError();
        EXPECT_FALSE(std::filesystem::exists(path + ".part"));
        auto copy = openStore("sqlite:" + path);
        ASSERT_TRUE(copy);
        EXPECT_EQ(copy->get("players", "kees").value_or(""), "{\"gold\": 3}");
        EXPECT_EQ(copy->get("world", "day").value_or(""), std::string("\0bin", 4));
        // The store goes on as before.
        EXPECT_TRUE(store->put("players", "ann", "{}"));
    }
    // Inside a transaction a SQLite store can't copy itself: said, not crashed.
    auto store = openStore("sqlite:" + tempDb("backup_tx.db"));
    ASSERT_TRUE(store);
    bool refused = false;
    store->transaction([&] {
        refused = !store->backup(tempDb("backup_tx_copy.db"));
        return true;
    });
    EXPECT_TRUE(refused);
}

TEST(OpenStore, ExplainsWhatItCantOpen) {
    std::string error;
    EXPECT_FALSE(openStore("ftp://nope", &error));
    EXPECT_NE(error.find("unknown kind"), std::string::npos);
    EXPECT_FALSE(openStore("sqlite:", &error));
    auto s = openStore(tempDb("plain.db"), &error); // a plain path is SQLite
    ASSERT_TRUE(s) << error;
    EXPECT_STREQ(s->backendName(), "sqlite");
#if !KKE_ENABLE_VALKEY
    EXPECT_FALSE(openStore("valkey://127.0.0.1:6379", &error));
    EXPECT_NE(error.find("KKE_ENABLE_VALKEY"), std::string::npos);
#endif
}

#if KKE_ENABLE_POSTGRES
// A second open of the same database (a second server, a restart) finds
// the table and says nothing; notices reach onNotice, not stderr.
TEST(Storage, PostgresReopenIsQuiet) {
    const char* env = std::getenv("KKE_TEST_POSTGRES");
    if (!env || !*env) GTEST_SKIP() << "set KKE_TEST_POSTGRES to a server to test it";
    std::vector<std::string> notices;
    for (int i = 0; i < 2; ++i) {
        PostgresStore store;
        store.onNotice = [&](const std::string& m) { notices.push_back(m); };
        std::string error;
        ASSERT_TRUE(store.open(env, &error)) << error;
        EXPECT_TRUE(store.put("t", "k", "v")) << store.lastError();
    }
    EXPECT_TRUE(notices.empty()) << notices.front();
}
#endif
