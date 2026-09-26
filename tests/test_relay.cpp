// Encryption, join codes and the relay (issue #44, docs/SERVER_HOSTING.md
// "Join codes", docs/NETWORKING.md "Encryption"): the handshake and its
// failures, tampering, the relay's rules, and a whole join by code over
// this machine's UDP, direct and relayed.

#include "kke/net/NetSession.h"
#include "kke/net/Relay.h"
#include "kke/net/SecureTransport.h"
#include "kke/net/Transport.h"
#include "kke/server/ServerConfig.h"

#if KKE_ENABLE_NET
#include "kke/net/EnetTransport.h"
#include "kke/server/DedicatedServer.h"
#include "kke/server/RelayService.h"
#endif

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <thread>

using namespace kke::net;

namespace {

std::string tempDir(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() / ("kke_relay_test_" + std::string(name));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir.string();
}

using Key32 = std::array<uint8_t, 32>;

// A server and clients, all encrypted, on a simulated network.
struct Secure {
    LoopbackNetwork net;
    LoopbackTransport serverT{ net };
    SecureTransport serverS{ serverT };
    std::unique_ptr<NetServer> server;
    std::vector<std::unique_ptr<LoopbackTransport>> clientT;
    std::vector<std::unique_ptr<SecureTransport>> clientS;
    std::vector<std::unique_ptr<NetClient>> clients;
    std::vector<GameEventMsg> serverEvents;
    double now = 0;

    explicit Secure(const std::string& password = {}) {
        NetConfig c;
        c.password = password;
        c.dedicated = true;
        server = std::make_unique<NetServer>(serverS, c);
        server->onEvent = [this](const GameEventMsg& e) { serverEvents.push_back(e); };
        EXPECT_TRUE(server->start(7000, "server", ""));
    }
    NetClient& join(const std::string& name, const std::string& password = {}, std::optional<Key32> expect = std::nullopt) {
        NetConfig c;
        c.password = password;
        clientT.push_back(std::make_unique<LoopbackTransport>(net));
        clientS.push_back(std::make_unique<SecureTransport>(*clientT.back()));
        clientS.back()->expectServerKey(expect);
        clients.push_back(std::make_unique<NetClient>(*clientS.back(), c));
        EXPECT_TRUE(clients.back()->connect("localhost", 7000, name, ""));
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

// Flips a byte in everything it sends once armed: an attacker on the path.
class Tamper : public ITransport {
public:
    explicit Tamper(ITransport& inner) : m_inner(inner) {}
    bool armed = false;
    bool host(uint16_t port, size_t maxPeers, std::string* error) override { return m_inner.host(port, maxPeers, error); }
    PeerId connect(const std::string& a, uint16_t p, std::string* error) override { return m_inner.connect(a, p, error); }
    void send(PeerId peer, Channel channel, const uint8_t* data, size_t size) override {
        std::vector<uint8_t> d(data, data + size);
        if (armed && d.size() > 30) d[d.size() / 2] ^= 0x40;
        m_inner.send(peer, channel, d);
    }
    void disconnect(PeerId peer) override { m_inner.disconnect(peer); }
    void poll(std::vector<NetEvent>& out) override { m_inner.poll(out); }
    PeerStats stats(PeerId peer) const override { return m_inner.stats(peer); }
    uint16_t port() const override { return m_inner.port(); }
    void close() override { m_inner.close(); }
    const char* backendName() const override { return "tamper"; }
    using ITransport::send;

private:
    ITransport& m_inner;
};

} // namespace

// ---------------------------------------------------------------- encryption

TEST(SecureTransport, PlayersJoinAndTalkEncrypted) {
    Secure s("secret");
    s.net.conditions.latencyMs = 40;
    s.net.conditions.jitterMs = 15; // unreliable packets arrive out of order: the replay window must allow it
    s.net.conditions.lossPercent = 5;
    NetClient& a = s.join("A", "secret");
    NetClient& wrong = s.join("B", "nope");
    s.run(1.5);
    EXPECT_EQ(a.status(), NetClient::Status::Connected);
    EXPECT_EQ(wrong.status(), NetClient::Status::Rejected); // the password check still runs, inside the encryption
    NetPlayerState st;
    for (int i = 0; i < 60; ++i) {
        st.position = { i * 0.05f, 0, 0 };
        a.setLocalState(st);
        s.run(1.0 / 60.0);
    }
    a.sendEvent(9, { 1, 2, 3 });
    s.run(0.5);
    ASSERT_FALSE(s.serverEvents.empty());
    EXPECT_EQ(s.serverEvents.back().payload, (std::vector<uint8_t>{ 1, 2, 3 }));
    const auto players = s.server->players(s.now);
    ASSERT_EQ(players.size(), 1u);
    EXPECT_GT(players[0].state.position.x, 1.0f);
    EXPECT_EQ(s.serverS.rejectedPackets(), 0u);
}

TEST(SecureTransport, RefusesAServerWithTheWrongKey) {
    Secure s;
    ServerIdentity other;
    ASSERT_TRUE(ServerIdentity::generate(other));
    NetClient& pinnedWrong = s.join("Wary", {}, other.publicKey);
    NetClient& pinnedRight = s.join("Trusting", {}, s.serverS.identity().publicKey);
    s.run(1.0);
    EXPECT_EQ(pinnedRight.status(), NetClient::Status::Connected);
    EXPECT_NE(pinnedWrong.status(), NetClient::Status::Connected);
    EXPECT_NE(s.clientS[0]->failure().find("isn't the one expected"), std::string::npos) << s.clientS[0]->failure();
    EXPECT_EQ(s.server->clientCount(), 1u);
}

TEST(SecureTransport, TamperedPacketsAreDroppedThenThePeer) {
    LoopbackNetwork net;
    LoopbackTransport serverT(net), clientT(net);
    SecureTransport serverS(serverT);
    Tamper tamper(clientT);
    SecureTransport clientS(tamper);
    NetConfig c;
    c.dedicated = true;
    NetServer server(serverS, c);
    std::vector<GameEventMsg> got;
    server.onEvent = [&](const GameEventMsg& e) { got.push_back(e); };
    ASSERT_TRUE(server.start(7100, "s", ""));
    NetClient client(clientS);
    ASSERT_TRUE(client.connect("localhost", 7100, "Mallory's victim", ""));
    double now = 0;
    auto run = [&](double sec) {
        for (double t = 0; t < sec; t += 1.0 / 60.0) {
            now += 1.0 / 60.0;
            net.advance(1.0 / 60.0);
            server.update(now);
            client.update(now);
        }
    };
    run(0.5);
    ASSERT_EQ(client.status(), NetClient::Status::Connected);
    tamper.armed = true;
    for (int i = 0; i < 40; ++i) client.sendEvent(5, std::vector<uint8_t>(40, uint8_t(i)));
    run(1.0);
    EXPECT_TRUE(got.empty()) << "a changed packet must never reach the game";
    EXPECT_GT(serverS.rejectedPackets(), 0u);
    EXPECT_EQ(server.clientCount(), 0u) << "past maxBadPackets the peer is dropped";
}

TEST(SecureTransport, APlainClientNeverGetsIn) {
    Secure s;
    LoopbackTransport plainT(s.net);
    NetClient plain(plainT);
    ASSERT_TRUE(plain.connect("localhost", 7000, "Old", ""));
    for (int i = 0; i < 60; ++i) {
        s.now += 1.0 / 60.0;
        s.net.advance(1.0 / 60.0);
        s.server->update(s.now);
        plain.update(s.now);
    }
    EXPECT_NE(plain.status(), NetClient::Status::Connected);
    EXPECT_EQ(s.server->clientCount(), 0u);
    EXPECT_EQ(s.serverS.failedHandshakes(), 1u);
}

TEST(SecureTransport, ServerKeyIsKeptAcrossRestarts) {
    const std::string dir = tempDir("key");
    ServerIdentity a, b;
    std::string error;
    ASSERT_TRUE(ServerIdentity::loadOrCreate(dir + "/server.key", a, &error)) << error;
    ASSERT_TRUE(ServerIdentity::loadOrCreate(dir + "/server.key", b, &error)) << error;
    EXPECT_EQ(a.publicKey, b.publicKey);
    EXPECT_EQ(a.fingerprint().size(), 19u); // "xxxx-xxxx-xxxx-xxxx"
#ifndef _WIN32
    const auto perms = std::filesystem::status(dir + "/server.key").permissions();
    EXPECT_EQ(perms & (std::filesystem::perms::group_all | std::filesystem::perms::others_all), std::filesystem::perms::none);
#endif
    std::ofstream(dir + "/bad.key") << "not hex";
    EXPECT_FALSE(ServerIdentity::loadOrCreate(dir + "/bad.key", b, &error));
}

// ---------------------------------------------------------------- codes and datagrams

TEST(JoinCodes, AreEasyToTypeAndRead) {
    const std::string c = newJoinCode();
    ASSERT_EQ(c.size(), kJoinCodeLength);
    EXPECT_EQ(normalizeJoinCode(formatJoinCode(c)), c);
    EXPECT_EQ(normalizeJoinCode("k7m-q2p"), "K7MQ2P");
    EXPECT_EQ(normalizeJoinCode(" K7M Q2P "), "K7MQ2P");
    EXPECT_EQ(normalizeJoinCode("OIL-123"), "011123"); // O reads as 0, I and L as 1
    EXPECT_FALSE(normalizeJoinCode("K7M-Q2").has_value());
    EXPECT_FALSE(normalizeJoinCode("192.168.1.20").has_value());
    EXPECT_FALSE(normalizeJoinCode("K7M-Q2U").has_value()); // U isn't used
    EXPECT_EQ(formatJoinCode("K7MQ2P"), "K7M-Q2P");

    std::string error;
    auto t = parseJoinTarget("k7m-q2p@relay.example.org:4000", "", &error);
    ASSERT_TRUE(t.has_value()) << error;
    EXPECT_EQ(t->code, "K7MQ2P");
    EXPECT_EQ(t->relayHost, "relay.example.org");
    EXPECT_EQ(t->relayPort, 4000);
    t = parseJoinTarget("K7M-Q2P", "my.relay", &error);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->relayPort, kDefaultRelayPort);
    EXPECT_FALSE(parseJoinTarget("K7M-Q2P", "", &error).has_value());
    EXPECT_NE(error.find("relay"), std::string::npos);
    EXPECT_TRUE(looksLikeJoinCode("K7M-Q2P@x.org"));
    EXPECT_FALSE(looksLikeJoinCode("192.168.1.20"));
}

TEST(RelayDatagrams, RoundTripAndRefuseJunk) {
    relay::Register r;
    r.game = "kke";
    r.code = "K7MQ2P";
    r.secret[3] = 7;
    r.serverKey[31] = 9;
    const auto rr = relay::decodeRegister(relay::encode(r));
    ASSERT_TRUE(rr.has_value());
    EXPECT_EQ(rr->code, "K7MQ2P");
    EXPECT_EQ(rr->secret, r.secret);
    EXPECT_EQ(rr->serverKey, r.serverKey);

    relay::Introduce in{ {}, "203.0.113.5", 50000, 27971 };
    const auto ri = relay::decodeIntroduce(relay::encode(in));
    ASSERT_TRUE(ri.has_value());
    EXPECT_EQ(ri->clientHost, "203.0.113.5");
    EXPECT_EQ(ri->relayPort, 27971);

    // A Lookup is padded; every answer to it is smaller (no amplification).
    const auto lookup = relay::encode(relay::Lookup{ "K7MQ2P", "kke" });
    EXPECT_EQ(lookup.size(), relay::kLookupSize);
    relay::Found f;
    f.serverHost = std::string(64, '9');
    f.serverPort = 1;
    EXPECT_LT(relay::encode(f).size(), lookup.size());
    EXPECT_LT(relay::encode(relay::Refused{ std::string(200, 'x') }).size(), lookup.size());
    std::vector<uint8_t> unpadded = lookup;
    unpadded.resize(40);
    EXPECT_FALSE(relay::decodeLookup(unpadded).has_value());

    // Random bytes and cut-off messages never decode (nor crash).
    std::mt19937 rng(7);
    for (int i = 0; i < 2000; ++i) {
        std::vector<uint8_t> d = i % 2 ? lookup : relay::encode(r);
        d.resize(rng() % (d.size() + 1));
        for (auto& b : d)
            if (rng() % 8 == 0) b = static_cast<uint8_t>(rng());
        (void)relay::decodeRegister(d);
        (void)relay::decodeLookup(d);
        (void)relay::decodeFound(d);
        (void)relay::decodeIntroduce(d);
        (void)relay::decodePunch(d);
    }
    std::vector<uint8_t> cut = relay::encode(r);
    cut.pop_back();
    EXPECT_FALSE(relay::decodeRegister(cut).has_value());
}

TEST(RelayConfig, PortsDontCollideAndCodesNeedPlayers) {
    kke::server::ServerConfig c;
    std::vector<std::string> errors;
    c.roles = { "players", "relay" };
    c.port = 27980; // inside the relay's 27970-28034
    EXPECT_FALSE(c.validate(errors));
    errors.clear();
    c.port = 27960;
    EXPECT_TRUE(c.validate(errors)) << errors.front();
    c.roles = { "directory" };
    c.relay = "relay.example.org";
    EXPECT_FALSE(c.validate(errors)); // a join code for a server nobody plays on
    errors.clear();
    std::map<std::string, std::string> env{ { "KKE_SERVER_RELAY", "r.example.org:4000" }, { "KKE_SERVER_RELAY_SLOTS", "9" } };
    EXPECT_TRUE(c.applyEnv([&](const char* k) -> const char* { auto it = env.find(k); return it == env.end() ? nullptr : it->second.c_str(); }, errors));
    EXPECT_EQ(c.relay, "r.example.org:4000");
    EXPECT_EQ(c.relaySlots, 9);
    EXPECT_TRUE(c.applyArgs({ "--relay-port", "5000", "--relay-slots", "2000" }, errors) == false);
    EXPECT_EQ(c.relayPort, 5000);
}

// ---------------------------------------------------------------- relay rules

#if KKE_ENABLE_NET
namespace {

using kke::server::RelayCore;

struct Sent { size_t socket; std::string host; uint16_t port; std::vector<uint8_t> data; };

struct Relay {
    RelayCore core;
    std::vector<Sent> sent;
    double now = 0;
    explicit Relay(RelayCore::Limits l = {}) : core(l) {
        core.send = [this](size_t s, const std::string& h, uint16_t p, const std::vector<uint8_t>& d) { sent.push_back({ s, h, p, d }); };
        core.slotPort = [](size_t slot) { return static_cast<uint16_t>(27970 + slot); };
    }
    void in(size_t socket, const std::string& host, uint16_t port, const std::vector<uint8_t>& d) { core.receive(socket, host, port, d.data(), d.size(), now); }
    std::vector<Sent> take() { auto s = std::move(sent); sent.clear(); return s; }
};

relay::Register reg(const std::string& code = {}, uint8_t secret = 1) {
    relay::Register r;
    r.game = "kke";
    r.code = code;
    r.secret.fill(secret);
    r.serverKey.fill(0xAB);
    return r;
}

} // namespace

TEST(RelayCore, GivesCodesIntroducesAndPassesPacketsOn) {
    Relay r;
    r.in(0, "198.51.100.1", 40000, relay::encode(reg()));
    auto out = r.take();
    ASSERT_EQ(out.size(), 1u);
    const auto registered = relay::decodeRegistered(out[0].data);
    ASSERT_TRUE(registered.has_value());
    EXPECT_EQ(registered->publicHost, "198.51.100.1"); // where it really is, not what it says
    const std::string code = registered->code;

    // A player looks it up: they get the server, the server hears of them.
    r.in(0, "203.0.113.9", 50000, relay::encode(relay::Lookup{ code, "kke" }));
    out = r.take();
    ASSERT_EQ(out.size(), 2u);
    const auto found = relay::decodeFound(out[0].data);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->serverHost, "198.51.100.1");
    EXPECT_EQ(found->serverKey[0], 0xAB);
    const auto intro = relay::decodeIntroduce(out[1].data);
    ASSERT_TRUE(intro.has_value());
    EXPECT_EQ(out[1].host, "198.51.100.1");
    EXPECT_EQ(intro->clientHost, "203.0.113.9");
    EXPECT_EQ(intro->relayPort, 27971);
    EXPECT_EQ(intro->token, found->token);

    // Nothing passes until the server opens its side of the slot.
    const std::vector<uint8_t> game{ 1, 2, 3 };
    r.in(0, "203.0.113.9", 50000, game);
    EXPECT_TRUE(r.take().empty());
    relay::Open wrongToken{};
    r.in(1, "198.51.100.1", 40001, relay::encode(wrongToken));
    r.in(1, "198.51.100.2", 40001, relay::encode(relay::Open{ intro->token })); // right token, wrong server
    r.in(0, "203.0.113.9", 50000, game);
    EXPECT_TRUE(r.take().empty());
    r.in(1, "198.51.100.1", 40005, relay::encode(relay::Open{ intro->token })); // its router picked another port: fine
    r.in(0, "203.0.113.9", 50000, game);
    out = r.take();
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].socket, 1u);
    EXPECT_EQ(out[0].host, "198.51.100.1");
    EXPECT_EQ(out[0].port, 40005);
    r.in(1, "198.51.100.1", 40005, { 9, 9 });
    out = r.take();
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].socket, 0u);
    EXPECT_EQ(out[0].host, "203.0.113.9");

    // Strangers get nowhere: not through the relay port, not through the slot.
    r.in(0, "192.0.2.66", 1234, game);
    r.in(1, "192.0.2.66", 1234, game);
    EXPECT_TRUE(r.take().empty());
    EXPECT_GE(r.core.refused(), 2u);
}

TEST(RelayCore, CodesBelongToTheirServer) {
    Relay r;
    r.in(0, "198.51.100.1", 40000, relay::encode(reg("K7MQ2P", 1))); // asks for its old code: free, so granted
    auto out = r.take();
    ASSERT_EQ(relay::decodeRegistered(out[0].data)->code, "K7MQ2P");
    r.in(0, "192.0.2.66", 999, relay::encode(reg("K7MQ2P", 2))); // someone else asks for it
    out = r.take();
    EXPECT_NE(relay::decodeRegistered(out[0].data)->code, "K7MQ2P");
    r.in(0, "198.51.100.77", 40000, relay::encode(reg("K7MQ2P", 1))); // the owner, from a new address
    out = r.take();
    EXPECT_EQ(relay::decodeRegistered(out[0].data)->code, "K7MQ2P");
    // A bye with the wrong secret does nothing; the right one frees the code.
    relay::Bye bye;
    bye.secret.fill(2);
    r.in(0, "198.51.100.77", 40000, relay::encode(bye));
    EXPECT_EQ(r.core.servers(), 2u);
    bye.secret.fill(1);
    r.in(0, "198.51.100.77", 40000, relay::encode(bye));
    EXPECT_EQ(r.core.servers(), 1u);
}

TEST(RelayCore, LookupsAreLimitedAndSilentServersForgotten) {
    Relay r;
    r.in(0, "198.51.100.1", 40000, relay::encode(reg()));
    const std::string code = relay::decodeRegistered(r.take()[0].data)->code;
    // Guessing: a burst, then one a second; over that, no answer at all.
    size_t answers = 0;
    for (int i = 0; i < 50; ++i) {
        r.in(0, "192.0.2.66", 2000, relay::encode(relay::Lookup{ "000000", "kke" }));
        answers += r.take().size();
    }
    EXPECT_LE(answers, 11u);
    // The wrong game doesn't find it either.
    r.in(0, "203.0.113.1", 1, relay::encode(relay::Lookup{ code, "othergame" }));
    EXPECT_TRUE(relay::decodeRefused(r.take()[0].data).has_value());
    // No heartbeat for 30 s: gone, and so are its players' slots.
    r.in(0, "203.0.113.9", 50000, relay::encode(relay::Lookup{ code, "kke" }));
    EXPECT_EQ(r.core.sessions(), 1u);
    r.now = 31;
    r.core.update(r.now);
    EXPECT_EQ(r.core.servers(), 0u);
    EXPECT_EQ(r.core.sessions(), 0u);
}

TEST(RelayCore, SlotsRunOutAndBandwidthIsCapped) {
    RelayCore::Limits l;
    l.slots = 2;
    l.bytesPerSecond = 10000;
    Relay r(l);
    r.in(0, "198.51.100.1", 40000, relay::encode(reg()));
    const std::string code = relay::decodeRegistered(r.take()[0].data)->code;
    for (int i = 0; i < 3; ++i) r.in(0, "203.0.113." + std::to_string(i), 50000, relay::encode(relay::Lookup{ code, "kke" }));
    auto out = r.take();
    EXPECT_TRUE(relay::decodeRefused(out.back().data).has_value()) << "the third player finds the relay full";
    const auto intro = relay::decodeIntroduce(out[1].data);
    r.in(1, "198.51.100.1", 40000, relay::encode(relay::Open{ intro->token }));
    size_t passed = 0;
    for (int i = 0; i < 100; ++i) {
        r.in(0, "203.0.113.0", 50000, std::vector<uint8_t>(1000, 1));
        passed += r.take().size();
    }
    EXPECT_LE(passed, 11u) << "about a second's worth, then dropped";
}

// ---------------------------------------------------------------- the whole thing, on real UDP

namespace {

struct Pump {
    kke::server::DedicatedServer* server = nullptr;
    kke::server::RelayService* relay = nullptr;
    std::vector<std::pair<RelayJoin*, EnetTransport*>> joiners;
    std::vector<NetClient*> clients;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    double t() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); }
    // Runs everything until `done` or `seconds` pass.
    bool until(const std::function<bool()>& done, double seconds) {
        const double end = t() + seconds;
        while (t() < end) {
            const double now = t();
            if (relay) relay->update(now);
            if (server) server->update(now);
            for (auto& [j, enet] : joiners) {
                if (!j->done()) {
                    std::vector<NetEvent> ignored;
                    enet->poll(ignored); // relay datagrams come through the socket's intercept
                    j->update(now);
                }
            }
            for (NetClient* c : clients) c->update(now);
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }
};

void joinByCode(bool forceRelay) {
    const uint16_t base = forceRelay ? 38970 : 39170;
    // A relay, as anyone would run one.
    kke::server::RelayService relaySvc;
    std::string error;
    ASSERT_TRUE(relaySvc.start(base, 4, &error)) << error;

    // A server with a join code from it.
    kke::server::ServerConfig c;
    c.port = static_cast<uint16_t>(base + 100);
    c.saveDir = tempDir(forceRelay ? "e2e_relayed" : "e2e_direct") + "/save";
    c.relay = "127.0.0.1:" + std::to_string(base);
    EnetTransport serverEnet;
    kke::server::DedicatedServer server(c, serverEnet);
    server.setRawSocket(&serverEnet);
    serverEnet.realAddress = [&server](const std::string& h, uint16_t p) { return server.relayedAddress(h, p); };
    std::vector<std::string> errors;
    ASSERT_TRUE(server.start(errors)) << (errors.empty() ? "" : errors.front());

    Pump pump;
    pump.server = &server;
    pump.relay = &relaySvc;
    ASSERT_TRUE(pump.until([&] { return !server.joinCode().empty(); }, 5.0)) << "no join code from the relay";
    const std::string code = server.joinCode();
    EXPECT_NE(code.find('@'), std::string::npos);

    // A player types it.
    EnetTransport enet;
    ASSERT_TRUE(enet.open(&error)) << error;
    RelayJoin joiner(enet);
    joiner.forceRelay = forceRelay;
    enet.onRaw = [&](const std::string& h, uint16_t p, const std::vector<uint8_t>& d) { joiner.onDatagram(h, p, d); };
    const auto target = parseJoinTarget(code, "", &error);
    ASSERT_TRUE(target.has_value()) << error;
    ASSERT_TRUE(joiner.start(*target, "kke", pump.t(), &error)) << error;
    pump.joiners.push_back({ &joiner, &enet });
    ASSERT_TRUE(pump.until([&] { return joiner.done(); }, 5.0));
    ASSERT_NE(joiner.state(), RelayJoin::State::Failed) << joiner.error();
    EXPECT_EQ(joiner.state(), forceRelay ? RelayJoin::State::Relayed : RelayJoin::State::Direct);

    SecureTransport secure(enet);
    secure.expectServerKey(joiner.serverKey());
    NetClient client(secure);
    ASSERT_TRUE(client.connect(joiner.connectHost(), joiner.connectPort(), "Friend", "", &error)) << error;
    pump.clients.push_back(&client);
    ASSERT_TRUE(pump.until([&] { return client.status() == NetClient::Status::Connected; }, 5.0))
        << client.statusText() << " / " << secure.failure();
    EXPECT_EQ(server.game()->clientCount(), 1u);
    EXPECT_EQ(server.game()->address(client.playerId()), "127.0.0.1"); // the player's address even when relayed
    if (forceRelay) EXPECT_GT(relaySvc.core()->bytesRelayed(), 0u);
    else EXPECT_EQ(relaySvc.core()->bytesRelayed(), 0u);

    client.disconnect();
    pump.clients.clear();
    pump.until([] { return false; }, 0.2);
    server.stop();
    pump.server = nullptr;
    pump.until([&] { return relaySvc.core()->servers() == 0; }, 1.0);
    EXPECT_EQ(relaySvc.core()->servers(), 0u) << "a stopping server frees its code at once";

    // Restarted, it gets the same code back: the one friends saved still works.
    EnetTransport againEnet;
    kke::server::DedicatedServer again(c, againEnet);
    again.setRawSocket(&againEnet);
    ASSERT_TRUE(again.start(errors)) << (errors.empty() ? "" : errors.front());
    pump.server = &again;
    ASSERT_TRUE(pump.until([&] { return !again.joinCode().empty(); }, 5.0));
    EXPECT_EQ(again.joinCode(), code);
    EXPECT_EQ(again.identity().publicKey, server.identity().publicKey);
}

} // namespace

TEST(JoinByCode, DirectWhenTheRoutersAllowIt) { joinByCode(false); }
TEST(JoinByCode, ThroughTheRelayOtherwise) { joinByCode(true); }

#endif
