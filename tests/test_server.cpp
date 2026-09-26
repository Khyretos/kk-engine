// kke_server's pieces (docs/SERVER_HOSTING.md): settings, the access list,
// leaderboards, the directory, and the dedicated server over a loopback
// network.

#include "kke/server/Directory.h"
#include "kke/server/Leaderboard.h"
#include "kke/server/ServerAccess.h"
#include "kke/server/ServerConfig.h"
#include "kke/server/ServerFiles.h"

#if KKE_ENABLE_NET
#include "kke/net/NetSession.h"
#include "kke/net/Transport.h"
#include "kke/server/DedicatedServer.h"
#include "kke/server/DirectoryNet.h"
#endif

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <map>
#include <thread>

using namespace kke::server;

namespace {

std::string tempDir(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() / ("kke_server_test_" + std::string(name));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir.string();
}

} // namespace

// ---------------------------------------------------------------- config

TEST(ServerConfig, LayersFileThenEnvironmentThenFlags) {
    ServerConfig c;
    std::vector<std::string> errors;
    EXPECT_TRUE(c.loadJson(R"({"name":"File","port":3000,"maxPlayers":4,"roles":["players","leaderboard"],"password":"a"})", errors));
    std::map<std::string, std::string> env{ { "KKE_SERVER_NAME", "Env" }, { "KKE_SERVER_PORT", "3001" }, { "KKE_SERVER_PASSWORD", "" } };
    EXPECT_TRUE(c.applyEnv([&](const char* k) -> const char* { auto it = env.find(k); return it == env.end() ? nullptr : it->second.c_str(); }, errors));
    EXPECT_TRUE(c.applyArgs({ "--port", "3002", "--public", "--directory", "dir.example.org:27950" }, errors));
    EXPECT_TRUE(errors.empty()) << errors.front();
    EXPECT_EQ(c.name, "Env");        // env over file
    EXPECT_EQ(c.port, 3002);         // flag over env
    EXPECT_EQ(c.maxPlayers, 4);      // file over default
    EXPECT_EQ(c.password, "");       // an empty variable still means "no password"
    EXPECT_TRUE(c.hasRole("leaderboard"));
    EXPECT_TRUE(c.validate(errors)) << errors.front();
}

TEST(ServerConfig, ReportsEveryProblemInsteadOfGuessing) {
    ServerConfig c;
    std::vector<std::string> errors;
    EXPECT_FALSE(c.loadJson(R"({"nmae":"x","port":70000,"maxPlayers":"8","roles":"players","public":1})", errors));
    EXPECT_EQ(errors.size(), 5u); // typo, port range, 2 wrong types, public not a bool
    errors.clear();
    EXPECT_FALSE(c.loadJson("{ not json", errors));
    errors.clear();
    EXPECT_FALSE(c.applyArgs({ "--port", "0", "--bogus", "--name" }, errors));
    EXPECT_EQ(errors.size(), 3u);

    ServerConfig v;
    v.roles = { "players", "teleporter", "physics" }; // unknown role; physics without a scene
    v.isPublic = true;                                // with nowhere to register
    v.password = std::string(65, 'x');
    errors.clear();
    EXPECT_FALSE(v.validate(errors));
    EXPECT_EQ(errors.size(), 4u);
}

TEST(ServerConfig, NeverShowsThePassword) {
    ServerConfig c;
    c.password = "hunter2";
    c.storage = "postgres://kke:hunter2@db.example.org/kke";
    EXPECT_EQ(c.describe().find("hunter2"), std::string::npos);
    EXPECT_NE(c.describe().find("***"), std::string::npos);
}

// ---------------------------------------------------------------- access

TEST(ServerAccess, BansByNameOrAddressAndAllowList) {
    ServerAccess a;
    EXPECT_EQ(a.admit("Kees", "10.0.0.1"), "");
    EXPECT_TRUE(a.ban("Griefer", "", "tnt"));
    EXPECT_TRUE(a.ban("", "203.0.113.7", ""));
    EXPECT_FALSE(a.ban("", "", ""));
    EXPECT_NE(a.admit("griefer", "10.0.0.2").find("tnt"), std::string::npos); // names ignore case
    EXPECT_NE(a.admit("Alt", "203.0.113.7"), "");
    EXPECT_EQ(a.unban("GRIEFER"), 1u);
    EXPECT_EQ(a.admit("Griefer", "10.0.0.2"), "");

    a.addAdmin("Kees");
    a.allow("Friend");
    EXPECT_EQ(a.admit("Friend", "10.0.0.3"), "");
    EXPECT_EQ(a.admit("kees", "10.0.0.1"), ""); // admins are always allowed
    EXPECT_NE(a.admit("Stranger", "10.0.0.4"), "");
}

TEST(ServerAccess, SavesAtomicallyAndLoadsWhatItCan) {
    const std::string dir = tempDir("access");
    const std::string path = dir + "/sub/access.json";
    ServerAccess a;
    a.addAdmin("Kees");
    a.ban("Griefer", "203.0.113.7", "tnt");
    std::string error;
    ASSERT_TRUE(a.save(path, &error)) << error;
    EXPECT_FALSE(std::filesystem::exists(path + ".tmp"));

    ServerAccess b;
    std::vector<std::string> errors;
    ASSERT_TRUE(b.load(path, errors));
    EXPECT_TRUE(b.isAdmin("kees"));
    EXPECT_NE(b.admit("x", "203.0.113.7"), "");

    // A damaged entry is skipped and reported; the rest still counts.
    ServerAccess c;
    EXPECT_FALSE(c.fromJson(R"({"admins":["A", 3],"bans":[{"reason":"no target"},{"name":"B"}]})", errors));
    EXPECT_TRUE(c.isAdmin("A"));
    EXPECT_EQ(c.bans().size(), 1u);
    // No file: an empty list, not an error.
    ServerAccess d;
    errors.clear();
    EXPECT_TRUE(d.load(dir + "/missing.json", errors));
    EXPECT_TRUE(errors.empty());
}

// ---------------------------------------------------------------- leaderboard

TEST(Leaderboard, KeepsEachPlayersBestInOrder) {
    Leaderboard lb;
    EXPECT_TRUE(lb.submit("race", "Ann", 10, 1));
    EXPECT_TRUE(lb.submit("race", "Bob", 30, 2));
    EXPECT_TRUE(lb.submit("race", "Cy", 20, 3));
    EXPECT_FALSE(lb.submit("race", "bob", 5, 4)); // worse than Bob's best (names ignore case)
    EXPECT_TRUE(lb.submit("race", "Ann", 40, 5));
    const auto top = lb.top("race", 10);
    ASSERT_EQ(top.size(), 3u);
    EXPECT_EQ(top[0].name, "Ann");
    EXPECT_EQ(top[1].name, "Bob");
    EXPECT_EQ(*lb.rank("race", "cy"), 3u);

    lb.setLowerIsBetter("race", true); // a time: lowest first
    EXPECT_EQ(lb.top("race", 1)[0].name, "Cy");

    EXPECT_FALSE(lb.submit("Bad Name!", "Ann", 1, 0));
    EXPECT_FALSE(lb.submit("race", std::string(25, 'x'), 1, 0));
}

TEST(Leaderboard, CapsBoardsAndEntries) {
    Leaderboard lb;
    for (size_t i = 0; i < Leaderboard::kMaxEntries + 10; ++i) lb.submit("big", "p" + std::to_string(i), static_cast<int32_t>(i), i);
    EXPECT_EQ(lb.top("big", 100000).size(), Leaderboard::kMaxEntries);
    EXPECT_EQ(lb.top("big", 1)[0].score, static_cast<int32_t>(Leaderboard::kMaxEntries + 9));
    EXPECT_FALSE(lb.submit("big", "low", -1, 0)); // below the last place of a full board
    for (size_t i = 0; i < Leaderboard::kMaxBoards; ++i) lb.submit("b" + std::to_string(i), "p", 1, 0);
    EXPECT_EQ(lb.boards().size(), Leaderboard::kMaxBoards);
}

TEST(Leaderboard, SurvivesASaveAndLoad) {
    const std::string path = tempDir("leaderboard") + "/leaderboards.json";
    Leaderboard lb;
    lb.submit("race", "Ann", -5, 100);
    lb.setLowerIsBetter("race", true);
    lb.submit("race", "Bob", -9, 200);
    ASSERT_TRUE(lb.save(path));
    EXPECT_FALSE(lb.dirty());
    Leaderboard back;
    std::vector<std::string> errors;
    ASSERT_TRUE(back.load(path, errors));
    const auto top = back.top("race", 5);
    ASSERT_EQ(top.size(), 2u);
    EXPECT_EQ(top[0].name, "Bob");
    EXPECT_EQ(top[0].time, 200u);
}

TEST(Leaderboard, MessagesRoundTripAndFitInOneEvent) {
    LeaderboardReply r;
    r.board = std::string(Leaderboard::kMaxBoardName, 'b');
    r.error = std::string(kMaxLeaderboardError, 'e');
    for (size_t i = 0; i < kMaxLeaderboardReply; ++i) r.entries.push_back({ std::string(Leaderboard::kMaxPlayerName, 'n'), -123456, ~0ull });
    const auto bytes = encode(r);
    EXPECT_LE(bytes.size(), 512u); // kMaxEventBytes: never cut off
    const auto back = decodeLeaderboardReply(bytes);
    ASSERT_TRUE(back);
    ASSERT_EQ(back->entries.size(), kMaxLeaderboardReply);
    EXPECT_EQ(back->entries[3].score, -123456);
    EXPECT_EQ(back->entries[3].time, ~0ull);

    const auto s = decodeLeaderboardSubmit(encode(LeaderboardSubmit{ "race", -7 }));
    ASSERT_TRUE(s);
    EXPECT_EQ(s->score, -7);
    EXPECT_FALSE(decodeLeaderboardQuery({ 0xFF }));
}

// ---------------------------------------------------------------- directory

namespace {
DirectoryEntry entry(const std::string& name, uint16_t port, const std::string& game = "kke") {
    DirectoryEntry e;
    e.name = name;
    e.game = game;
    e.port = port;
    e.players = 1;
    e.maxPlayers = 8;
    e.roles = { "players" };
    e.protocol = 3;
    return e;
}
} // namespace

TEST(Directory, ListsServersAtTheirRealAddressAndForgetsSilentOnes) {
    DirectoryRegistry reg;
    DirectoryEntry e = entry("A", 27960);
    e.address = "1.2.3.4"; // what the packet claims is ignored
    EXPECT_EQ(reg.heartbeat(e, "198.51.100.1", 0.0), "");
    EXPECT_EQ(reg.heartbeat(entry("B", 27960, "other"), "198.51.100.2", 5.0), "");
    ASSERT_EQ(reg.list("kke").size(), 1u);
    EXPECT_EQ(reg.list("kke")[0].address, "198.51.100.1");
    EXPECT_EQ(reg.list("").size(), 2u);
    reg.expire(kDirectoryTimeoutSeconds + 1.0);
    EXPECT_EQ(reg.size(), 1u); // A went quiet; B's heartbeat is newer
    EXPECT_FALSE(reg.bye("198.51.100.9", 27960)); // only from its own address
    EXPECT_TRUE(reg.bye("198.51.100.2", 27960));
}

TEST(Directory, CapsEntriesPerAddressAndRefusesJunk) {
    DirectoryRegistry reg;
    for (uint16_t i = 0; i < DirectoryRegistry::kMaxPerAddress; ++i) EXPECT_EQ(reg.heartbeat(entry("S", static_cast<uint16_t>(1000 + i)), "203.0.113.1", 0), "");
    EXPECT_NE(reg.heartbeat(entry("S", 2000), "203.0.113.1", 0), "");
    EXPECT_EQ(reg.heartbeat(entry("S", 1000), "203.0.113.1", 1), ""); // a refresh is fine
    DirectoryEntry bad = entry("S", 1);
    bad.players = 9; // more than max
    EXPECT_NE(reg.heartbeat(bad, "203.0.113.2", 0), "");
    bad = entry(std::string("tab\there"), 1);
    EXPECT_NE(reg.heartbeat(bad, "203.0.113.2", 0), "");
}

TEST(Directory, QueriesAreRateLimitedPerAddress) {
    RateLimiter rl(3, 1);
    EXPECT_TRUE(rl.allow("a", 0));
    EXPECT_TRUE(rl.allow("a", 0));
    EXPECT_TRUE(rl.allow("a", 0));
    EXPECT_FALSE(rl.allow("a", 0));
    EXPECT_TRUE(rl.allow("b", 0));
    EXPECT_TRUE(rl.allow("a", 1.0)); // one back per second
}

TEST(Directory, DatagramsCantAmplifyAndPage) {
    const auto q = encode(DirectoryQuery{ "kke", 0 });
    EXPECT_GE(q.size(), kMinQueryBytes);
    auto d = decodeDirectory(q.data(), q.size());
    EXPECT_EQ(d.type, DirectoryMessage::Query);
    EXPECT_EQ(d.query.game, "kke");
    // An unpadded query gets no answer.
    const std::string small = std::string(kDirectoryMagic) + R"({"t":"q","game":"kke","from":0})";
    EXPECT_EQ(decodeDirectory(reinterpret_cast<const uint8_t*>(small.data()), small.size()).type, DirectoryMessage::None);

    std::vector<DirectoryEntry> all;
    for (int i = 0; i < 40; ++i) {
        all.push_back(entry(std::string(24, static_cast<char>('a' + i % 26)), static_cast<uint16_t>(1000 + i)));
        all.back().address = "203.0.113." + std::to_string(i);
    }
    std::vector<DirectoryEntry> got;
    uint32_t from = 0;
    int pages = 0;
    do {
        const auto page = encodeList(all, from);
        EXPECT_LE(page.size(), kMaxDatagram);
        const auto l = decodeDirectory(page.data(), page.size());
        ASSERT_EQ(l.type, DirectoryMessage::List);
        EXPECT_EQ(l.list.total, 40u);
        got.insert(got.end(), l.list.servers.begin(), l.list.servers.end());
        from = l.list.next;
        ASSERT_LT(++pages, 40);
    } while (from);
    EXPECT_GT(pages, 1);
    ASSERT_EQ(got.size(), all.size());
    EXPECT_EQ(got[39].address, "203.0.113.39");

    const auto hb = encode(DirectoryHeartbeat{ entry("Kees", 27960), false });
    d = decodeDirectory(hb.data(), hb.size());
    ASSERT_EQ(d.type, DirectoryMessage::Heartbeat);
    EXPECT_EQ(d.heartbeat.entry.name, "Kees");
    // Invalid UTF-8 in a name is replaced, never a crash.
    DirectoryEntry odd = entry("x\xff", 1);
    EXPECT_FALSE(encode(DirectoryHeartbeat{ odd, false }).empty());
    for (size_t cut = 0; cut < hb.size(); ++cut) decodeDirectory(hb.data(), cut); // truncations: no crash
}

TEST(ServerFiles, AtomicWriteReplacesWhole) {
    const std::string path = tempDir("files") + "/a.txt";
    ASSERT_TRUE(writeFileAtomic(path, "one"));
    ASSERT_TRUE(writeFileAtomic(path, "two"));
    std::string text;
    ASSERT_TRUE(readFile(path, text));
    EXPECT_EQ(text, "two");
    bool exists = true;
    EXPECT_FALSE(readFile(path + ".nope", text, &exists));
    EXPECT_FALSE(exists);
}

#if KKE_ENABLE_NET

// ---------------------------------------------------------------- dedicated server

namespace {

using namespace kke::net;

struct World {
    LoopbackNetwork net;
    LoopbackTransport serverT{ net };
    std::unique_ptr<DedicatedServer> server;
    std::vector<std::unique_ptr<LoopbackTransport>> clientT;
    std::vector<std::unique_ptr<NetClient>> clients;
    std::vector<std::vector<GameEventMsg>> events;
    double now = 0;

    explicit World(ServerConfig c) {
        server = std::make_unique<DedicatedServer>(std::move(c), serverT);
        std::vector<std::string> errors;
        EXPECT_TRUE(server->start(errors)) << (errors.empty() ? "" : errors.front());
    }
    NetClient& join(const std::string& name, const std::string& password = {}) {
        NetConfig nc;
        nc.password = password;
        clientT.push_back(std::make_unique<LoopbackTransport>(net));
        clients.push_back(std::make_unique<NetClient>(*clientT.back(), nc));
        events.emplace_back();
        const size_t i = events.size() - 1;
        clients.back()->onEvent = [this, i](const GameEventMsg& e) { events[i].push_back(e); };
        EXPECT_TRUE(clients.back()->connect("localhost", server->config().port, name, ""));
        return *clients.back();
    }
    void run(double seconds) {
        for (double t = 0; t < seconds; t += 1.0 / 60.0) {
            now += 1.0 / 60.0;
            net.advance(1.0 / 60.0);
            server->update(now);
            for (auto& c : clients) c->update(now);
        }
    }
};

ServerConfig testConfig(const char* dir) {
    ServerConfig c;
    c.port = 6000;
    c.maxPlayers = 2;
    c.saveDir = tempDir(dir);
    return c;
}

} // namespace

TEST(DedicatedServer, AllSlotsAreForPlayersAndThePasswordIsChecked) {
    ServerConfig c = testConfig("dedicated_password");
    c.password = "secret";
    c.motd = "Welcome!";
    World w(c);
    NetClient& none = w.join("NoPass");
    NetClient& wrong = w.join("Wrong", "nope");
    NetClient& a = w.join("A", "secret");
    NetClient& b = w.join("B", "secret");
    NetClient& full = w.join("C", "secret");
    w.run(1.0);
    EXPECT_EQ(none.status(), NetClient::Status::Rejected);
    EXPECT_EQ(none.statusText().find("needs a password") != std::string::npos, true) << none.statusText();
    EXPECT_EQ(wrong.status(), NetClient::Status::Rejected);
    EXPECT_NE(wrong.statusText().find("wrong password"), std::string::npos);
    EXPECT_EQ(a.status(), NetClient::Status::Connected);
    EXPECT_EQ(b.status(), NetClient::Status::Connected); // 2 of maxPlayers 2: no slot kept for a host
    EXPECT_EQ(full.status(), NetClient::Status::Rejected);
    // No host player: A sees only B.
    ASSERT_EQ(a.players(w.now).size(), 1u);
    EXPECT_EQ(a.players(w.now)[0].name, "B");
    // The MOTD, to each joiner.
    ASSERT_FALSE(w.events[2].empty());
    EXPECT_EQ(w.events[2][0].kind, kEventServerMessage);
    EXPECT_EQ(std::string(w.events[2][0].payload.begin(), w.events[2][0].payload.end()), "Welcome!");
}

TEST(DedicatedServer, ConsoleKicksBansAndBansHold) {
    World w(testConfig("dedicated_console"));
    NetClient& g = w.join("Griefer");
    NetClient& k = w.join("Kees");
    w.run(0.5);
    ASSERT_EQ(g.status(), NetClient::Status::Connected);
    EXPECT_NE(w.server->command("players").find("Griefer"), std::string::npos);
    EXPECT_NE(w.server->command("ban griefer broke everything").find("banned Griefer"), std::string::npos);
    w.run(0.5);
    EXPECT_NE(g.status(), NetClient::Status::Connected);
    EXPECT_EQ(k.status(), NetClient::Status::Connected);
    // Saved, so it holds after a restart; and it stops them coming back.
    ServerAccess saved;
    std::vector<std::string> errors;
    ASSERT_TRUE(saved.load(w.server->config().saveDir + "/access.json", errors));
    EXPECT_NE(saved.admit("Griefer", "x"), "");
    NetClient& again = w.join("GRIEFER");
    w.run(0.5);
    EXPECT_EQ(again.status(), NetClient::Status::Rejected);
    EXPECT_NE(again.statusText().find("broke everything"), std::string::npos);

    EXPECT_NE(w.server->command("say hello all").find("said"), std::string::npos);
    w.run(0.2);
    ASSERT_FALSE(w.events[1].empty());
    EXPECT_EQ(w.events[1].back().kind, kEventServerMessage);
    EXPECT_NE(w.server->command("mute kees").find("isn't passed on"), std::string::npos);
    EXPECT_EQ(w.server->game()->voice.muted.count(k.playerId()), 1u);
    EXPECT_NE(w.server->command("unmute Kees").find("heard again"), std::string::npos);
    EXPECT_NE(w.server->command("kick Kees").find("kicked"), std::string::npos);
    EXPECT_NE(w.server->command("frobnicate").find("unknown"), std::string::npos);
    EXPECT_EQ(w.server->command("unban griefer"), "unbanned griefer");
    EXPECT_FALSE(w.server->stopRequested());
    w.server->command("stop");
    EXPECT_TRUE(w.server->stopRequested());
}

TEST(DedicatedServer, LeaderboardAnswersQueriesAndGuardsScores) {
    ServerConfig c = testConfig("dedicated_board");
    c.roles = { "players", "leaderboard" };
    World w(c);
    w.server->leaderboards().submit("race", "Server", 50, 1);
    NetClient& a = w.join("Ann");
    w.run(0.5);
    a.sendEvent(kEventLeaderboardSubmit, encode(LeaderboardSubmit{ "race", 99 }));
    a.sendEvent(kEventLeaderboardQuery, encode(LeaderboardQuery{ "race", 5 }));
    w.run(0.5);
    std::vector<LeaderboardReply> replies;
    for (const GameEventMsg& e : w.events[0])
        if (e.kind == kEventLeaderboardReply)
            if (auto r = decodeLeaderboardReply(e.payload)) replies.push_back(*r);
    ASSERT_EQ(replies.size(), 2u);
    EXPECT_NE(replies[0].error, ""); // client scores are off by default
    ASSERT_EQ(replies[1].entries.size(), 1u);
    EXPECT_EQ(replies[1].entries[0].name, "Server");

    // With client scores on, the name is the player's own, never one they claim.
    ServerConfig open = testConfig("dedicated_board_open");
    open.roles = { "players", "leaderboard" };
    open.clientScores = true;
    World w2(open);
    NetClient& b = w2.join("Bob");
    w2.run(0.5);
    b.sendEvent(kEventLeaderboardSubmit, encode(LeaderboardSubmit{ "race", 7 }));
    w2.run(0.5);
    ASSERT_EQ(w2.server->leaderboards().top("race", 5).size(), 1u);
    EXPECT_EQ(w2.server->leaderboards().top("race", 5)[0].name, "Bob");
    w2.server->stop();
    Leaderboard saved;
    std::vector<std::string> errors;
    ASSERT_TRUE(saved.load(open.saveDir + "/leaderboards.json", errors));
    EXPECT_EQ(saved.top("race", 1).size(), 1u);
}

TEST(DedicatedServer, PassesPlayersEventsOnToTheOthers) {
    World w(testConfig("dedicated_relay"));
    NetClient& a = w.join("A");
    w.join("B");
    w.run(0.5);
    a.sendEvent(42, { 1, 2, 3 });
    w.run(0.3);
    ASSERT_FALSE(w.events[1].empty());
    EXPECT_EQ(w.events[1].back().kind, 42);
    for (const GameEventMsg& e : w.events[0]) EXPECT_NE(e.kind, 42); // not echoed back
}

TEST(DedicatedServer, KeepsDataInItsStore) {
    ServerConfig c = testConfig("dedicated_store");
    {
        World w(c);
        ASSERT_NE(w.server->store(), nullptr);
        EXPECT_STREQ(w.server->store()->backendName(), "sqlite");
        EXPECT_TRUE(w.server->store()->put("world", "day", "12"));
    }
    World again(c); // a restart
    EXPECT_EQ(*again.server->store()->get("world", "day"), "12");
    ServerConfig bad = testConfig("dedicated_store_bad");
    bad.storage = "ftp://nowhere";
    LoopbackNetwork net;
    LoopbackTransport t(net);
    DedicatedServer s(bad, t);
    std::vector<std::string> errors;
    EXPECT_FALSE(s.start(errors));
}

TEST(DedicatedServer, RefusesToStartOnBadSettings) {
    ServerConfig c = testConfig("dedicated_bad");
    c.roles = { "physics" }; // no scene
    LoopbackNetwork net;
    LoopbackTransport t(net);
    DedicatedServer s(c, t);
    std::vector<std::string> errors;
    EXPECT_FALSE(s.start(errors));
    EXPECT_FALSE(errors.empty());
}

// A real directory on this machine's UDP: a public server registers, a
// browser lists it, the bye removes it.
TEST(DirectoryNet, PublicServerIsListedAndGoneAfterBye) {
    DirectoryService dir;
    std::string error;
    uint16_t port = 0;
    for (uint16_t p = 38950; p < 38990 && !dir.running(); ++p)
        if (dir.start(p, &error)) port = p;
    ASSERT_TRUE(dir.running()) << error;

    DirectoryPublisher pub;
    pub.setEntry(entry("Kees world", 27960));
    ASSERT_TRUE(pub.start({ "127.0.0.1:" + std::to_string(port) }, &error)) << error;

    DirectoryBrowser browser;
    double now = 0;
    bool asked = false;
    for (int i = 0; i < 200 && !browser.done(); ++i) {
        pub.update(now);
        dir.update(now);
        if (!asked && dir.registry().size() == 1) asked = browser.query("127.0.0.1", port, "kke", &error);
        if (asked) browser.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        now += 0.005;
    }
    ASSERT_TRUE(browser.done()) << error;
    ASSERT_EQ(browser.servers().size(), 1u);
    EXPECT_EQ(browser.servers()[0].name, "Kees world");
    EXPECT_EQ(browser.servers()[0].address, "127.0.0.1");

    pub.stop();
    for (int i = 0; i < 100 && dir.registry().size(); ++i) {
        dir.update(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_EQ(dir.registry().size(), 0u);
}

#endif
