// kke_server's scripts role (issue #43, docs/SERVER_HOSTING.md "Scripts"):
// sv_ scripts run headless, what they spawn reaches every client, and
// net.send works both ways, over a loopback network.

#include "kke/net/NetSession.h"
#include "kke/net/ScriptSpawns.h"
#include "kke/net/SecureTransport.h"
#include "kke/net/Transport.h"
#include "kke/server/DedicatedServer.h"
#include "kke/server/ServerScripts.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>

using namespace kke::server;
using namespace kke::net;

namespace {

std::string tempDir(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() / ("kke_scripts_test_" + std::string(name));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir.string();
}

void write(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

struct Game {
    LoopbackNetwork net;
    LoopbackTransport serverT{ net };
    std::unique_ptr<DedicatedServer> server;
    LoopbackTransport clientT{ net };
    SecureTransport clientSecure{ clientT }; // a server's connections are always encrypted
    std::unique_ptr<NetClient> client;
    std::map<uint16_t, SpawnMsg> spawns;
    std::vector<uint16_t> despawns;
    std::vector<GameEventMsg> events;
    std::vector<std::string> logs, warnings;
    double now = 0;

    explicit Game(ServerConfig c) {
        server = std::make_unique<DedicatedServer>(std::move(c), serverT);
        server->log = [this](const std::string& s) { logs.push_back(s); };
        server->warn = [this](const std::string& s) { warnings.push_back(s); };
        std::vector<std::string> errors;
        EXPECT_TRUE(server->start(errors)) << (errors.empty() ? "" : errors.front());
    }
    ~Game() {
        // Before the logs it writes to go.
        client.reset();
        server.reset();
    }
    void join(const std::string& name) {
        client = std::make_unique<NetClient>(clientSecure);
        client->onSpawn = [this](const SpawnMsg& m) { spawns[m.id] = m; };
        client->onDespawn = [this](uint16_t id) { despawns.push_back(id); };
        client->onEvent = [this](const GameEventMsg& e) { events.push_back(e); };
        EXPECT_TRUE(client->connect("localhost", server->config().port, name, ""));
    }
    void run(double seconds) {
        for (double t = 0; t < seconds; t += 1.0 / 60.0) {
            now += 1.0 / 60.0;
            net.advance(1.0 / 60.0);
            server->update(now);
            if (client) client->update(now);
        }
    }
    bool logged(const std::string& text) const {
        for (const std::string& l : logs)
            if (l.find(text) != std::string::npos) return true;
        return false;
    }
};

ServerConfig scriptsConfig(const std::string& dir) {
    ServerConfig c;
    c.port = 6100;
    c.maxPlayers = 4;
    c.saveDir = dir + "/save";
    c.scripts = dir + "/scripts";
    c.roles = { "players", "scripts", "leaderboard" };
    std::filesystem::create_directories(c.scripts);
    return c;
}

std::vector<uint8_t> scriptMessage(const std::string& name, const std::string& valueBytes) {
    std::vector<uint8_t> p(name.begin(), name.end());
    p.push_back(0);
    p.insert(p.end(), valueBytes.begin(), valueBytes.end());
    return p;
}

} // namespace

TEST(ServerScripts, OnlyServerRealmScriptsRun) {
    const std::string dir = tempDir("realms");
    ServerConfig c = scriptsConfig(dir);
    write(c.scripts + "/sv_rules.lua", "print('sv here')");
    write(c.scripts + "/sh_shared.lua", "print('sh here')");
    write(c.scripts + "/cl_hud.lua", "print('cl here')");
    write(c.scripts + "/menu.lua", "ui.open('<p/>')"); // a player's script: no ui on a server
    Game g(c);
    g.run(0.1);
    EXPECT_TRUE(g.logged("sv here"));
    EXPECT_TRUE(g.logged("sh here"));
    EXPECT_FALSE(g.logged("cl here"));
    ASSERT_NE(g.server->scripts(), nullptr);
    EXPECT_EQ(g.server->scripts()->scripts(), (std::vector<std::string>{ "sh_shared.lua", "sv_rules.lua" }));
    EXPECT_TRUE(g.warnings.empty()) << g.warnings.front();
}

TEST(ServerScripts, MissingFolderRefusesToStart) {
    ServerConfig c = scriptsConfig(tempDir("nofolder"));
    c.scripts += "/nope";
    LoopbackNetwork net;
    LoopbackTransport t(net);
    DedicatedServer s(c, t);
    std::vector<std::string> errors;
    EXPECT_FALSE(s.start(errors));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors.front().find("no folder"), std::string::npos);
}

#if KKE_ENABLE_JOLT
TEST(ServerScripts, SpawnsReachPlayersAndMoveInSnapshots) {
    const std::string dir = tempDir("spawns");
    ServerConfig c = scriptsConfig(dir);
    write(c.scripts + "/sv_crates.lua", R"lua(
        floor = physics.box{ pos = Vec(0, -0.5, 0), size = Vec(40, 1, 40), static = true }
        crates = {}
        hook.Add("PlayerJoin", "crates", function(id, name)
            print("hello " .. name .. " (" .. id .. ")")
            crates[id] = physics.box{ pos = Vec(0, 3, 0), size = Vec(1, 1, 1), color = Vec(1, 0, 0) }
        end)
        hook.Add("PlayerLeave", "crates", function(id, name)
            physics.remove(crates[id])
            print("bye " .. name)
        end)
    )lua");
    Game g(c);
    g.join("Kees");
    g.run(1.0);
    EXPECT_TRUE(g.logged("hello Kees (1)"));
    ASSERT_EQ(g.spawns.size(), 2u); // the floor and the crate
    uint16_t crate = 0;
    for (const auto& [id, m] : g.spawns) {
        EXPECT_EQ(m.kind, kke::script_net::kSpawnBody);
        const auto b = kke::script_net::decodeBody(m.desc);
        ASSERT_TRUE(b.has_value());
        if (!b->isStatic) crate = id;
    }
    ASSERT_NE(crate, 0);
    // The crate falls on the server and lands on the floor; the client
    // sees it in snapshots (the static floor needs none).
    g.run(2.0);
    NetBodyState s;
    ASSERT_TRUE(g.client->body(crate, g.now, s));
    EXPECT_NEAR(s.position.y, 0.5f, 0.1f);
    EXPECT_EQ(g.client->bodyIds().size(), 1u);
    EXPECT_EQ(g.server->scripts()->bodyCount(), 2u);

    g.client->disconnect();
    g.run(0.5);
    EXPECT_TRUE(g.logged("bye Kees"));
    EXPECT_EQ(g.server->scripts()->bodyCount(), 1u);
}

TEST(ServerScripts, PlayersPushWhatScriptsSpawn) {
    const std::string dir = tempDir("push");
    ServerConfig c = scriptsConfig(dir);
    write(c.scripts + "/sv_ball.lua", R"lua(
        physics.box{ pos = Vec(0, -0.5, 0), size = Vec(40, 1, 40), static = true }
        ball = physics.sphere{ pos = Vec(0, 0.3, 0), radius = 0.3 }
        hook.Add("Think", "watch", function() shared.x = physics.position(ball).x end)
    )lua");
    Game g(c);
    g.join("Pusher");
    g.run(0.5);
    // Walk through where the ball is, from the left.
    for (int i = 0; i < 90; ++i) {
        NetPlayerState st;
        st.position = { -2.0f + i * (4.0f / 90.0f), 0.0f, 0.0f };
        st.velocity = { 3.0f, 0.0f, 0.0f };
        g.client->setLocalState(st);
        g.run(1.0 / 60.0);
    }
    g.run(0.5);
    g.server->command("lua kke.log('ball x', shared.x)");
    bool pushed = false;
    for (const std::string& l : g.logs)
        if (l.find("ball x") != std::string::npos) pushed = std::stof(l.substr(l.find("ball x") + 7)) > 0.3f;
    EXPECT_TRUE(pushed) << "the player's capsule should have pushed the ball along +x";
}
#endif

TEST(ServerScripts, NetMessagesBothWaysAndServerScores) {
    const std::string dir = tempDir("messages");
    ServerConfig c = scriptsConfig(dir);
    write(c.scripts + "/sv_score.lua", R"lua(
        hook.Add("NetMessage", "score", function(name, data, from)
            if name ~= "finished" then return end
            server.score("race", from, data.time)
            net.send("rank", { best = server.top("race", 1)[1].score }, from)
            server.say("someone finished")
        end)
    )lua");
    Game g(c);
    g.join("Runner");
    g.run(0.5);
    // The bytes a game's net.send makes of { time = 42 }: the server's own
    // net.send makes the same (ScriptVM::encodeValue), so capture those.
    ASSERT_TRUE(g.server->scripts()->vm().runString("net.send('selftest', { time = 42 })", "probe"));
    g.run(0.3);
    std::vector<uint8_t> encoded;
    for (const GameEventMsg& e : g.events)
        if (e.kind == kke::script_net::kScriptEvent && e.payload.size() > 9 && std::string(e.payload.begin(), e.payload.begin() + 8) == "selftest")
            encoded.assign(e.payload.begin() + 9, e.payload.end());
    ASSERT_FALSE(encoded.empty()) << "the server's net.send should reach the player";
    g.events.clear();

    g.client->sendEvent(kke::script_net::kScriptEvent, scriptMessage("finished", std::string(encoded.begin(), encoded.end())));
    g.run(0.5);
    bool rank = false, said = false;
    for (const GameEventMsg& e : g.events) {
        if (e.kind == kke::script_net::kScriptEvent && std::string(e.payload.begin(), e.payload.begin() + 4) == "rank") rank = true;
        if (e.kind == kEventServerMessage && std::string(e.payload.begin(), e.payload.end()) == "someone finished") said = true;
    }
    EXPECT_TRUE(rank);
    EXPECT_TRUE(said);
    const auto top = g.server->leaderboards().top("race", 1);
    ASSERT_EQ(top.size(), 1u);
    EXPECT_EQ(top[0].name, "Runner");
    EXPECT_EQ(top[0].score, 42);
    EXPECT_TRUE(g.warnings.empty()) << g.warnings.front();
}

TEST(ServerScripts, BrokenAndRunawayScriptsDontStopTheServer) {
    const std::string dir = tempDir("broken");
    ServerConfig c = scriptsConfig(dir);
    write(c.scripts + "/sv_a_syntax.lua", "this is not lua");
    write(c.scripts + "/sv_b_loop.lua", "hook.Add('Think', 'spin', function() while true do end end)");
    write(c.scripts + "/sv_c_fine.lua", "hook.Add('Think', 'ok', function() shared.alive = (shared.alive or 0) + 1 end)");
    Game g(c);
    g.run(0.5);
    EXPECT_GE(g.warnings.size(), 2u); // the syntax error and the stopped loop
    g.server->command("lua kke.log('alive', shared.alive > 10)");
    EXPECT_TRUE(g.logged("alive true"));
    const std::string list = g.server->command("scripts");
    EXPECT_NE(list.find("sv_b_loop.lua  STOPPED"), std::string::npos) << list;

    // Fixed and reloaded from the console.
    write(c.scripts + "/sv_b_loop.lua", "print('fixed')");
    EXPECT_EQ(g.server->command("reload sv_b_loop.lua").rfind("reloaded 1", 0), 0u);
    EXPECT_TRUE(g.logged("fixed"));
}
