#include "kke/net/BitStream.h"
#include "kke/net/NetSession.h"
#include "kke/net/Protocol.h"
#include "kke/net/ScriptSpawns.h"
#include "kke/net/Transport.h"
#if KKE_ENABLE_NET
#include "kke/net/EnetTransport.h"
#endif

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <thread>

using namespace kke::net;
namespace script_net = kke::script_net;

// ---------------------------------------------------------------- bit stream

TEST(NetBitStream, RoundTripsEveryKindOfValue) {
    std::vector<uint8_t> buf;
    {
        WriteStream w(buf);
        uint32_t a = 5, big = 0xdeadbeef;
        int16_t neg = -300;
        bool yes = true;
        float f = 3.14159f;
        glm::vec3 v(1.5f, -2.25f, 1000.0f);
        glm::quat q = glm::normalize(glm::quat(0.3f, -0.5f, 0.7f, 0.1f));
        std::string name = "Kees";
        std::vector<uint8_t> bytes{ 1, 2, 3, 250 };
        w.integer(a, 0, 7);
        w.bits(big, 32);
        w.integer(neg, -1000, 1000);
        w.boolean(yes);
        w.real(f, -10.0f, 10.0f, 0.001f);
        w.vec3(v, 4096.0f, 1.0f / 1024.0f);
        w.quat(q);
        w.string(name, 24);
        w.bytes(bytes, 16);
    }
    ReadStream r(buf.data(), buf.size());
    uint32_t a = 0, big = 0;
    int16_t neg = 0;
    bool yes = false;
    float f = 0;
    glm::vec3 v(0.0f);
    glm::quat q;
    std::string name;
    std::vector<uint8_t> bytes;
    r.integer(a, 0, 7);
    r.bits(big, 32);
    r.integer(neg, -1000, 1000);
    r.boolean(yes);
    r.real(f, -10.0f, 10.0f, 0.001f);
    r.vec3(v, 4096.0f, 1.0f / 1024.0f);
    r.quat(q);
    r.string(name, 24);
    r.bytes(bytes, 16);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(a, 5u);
    EXPECT_EQ(big, 0xdeadbeefu);
    EXPECT_EQ(neg, -300);
    EXPECT_TRUE(yes);
    EXPECT_NEAR(f, 3.14159f, 0.0006f);
    EXPECT_NEAR(glm::length(v - glm::vec3(1.5f, -2.25f, 1000.0f)), 0.0f, 1e-3f);
    const glm::quat expect = glm::normalize(glm::quat(0.3f, -0.5f, 0.7f, 0.1f));
    EXPECT_GT(std::fabs(glm::dot(q, expect)), 0.9999f); // q and -q are the same rotation
    EXPECT_EQ(name, "Kees");
    EXPECT_EQ(bytes, (std::vector<uint8_t>{ 1, 2, 3, 250 }));
    EXPECT_LT(r.bitsLeft(), 8u);
}

TEST(NetBitStream, PacksIntoTheBitsTheRangeNeeds) {
    EXPECT_EQ(bitsRequired(0), 0);
    EXPECT_EQ(bitsRequired(1), 1);
    EXPECT_EQ(bitsRequired(7), 3);
    EXPECT_EQ(bitsRequired(8), 4);
    EXPECT_EQ(bitsRequired(0xffffffffu), 32);
    std::vector<uint8_t> buf;
    WriteStream w(buf);
    for (int i = 0; i < 8; ++i) { uint8_t x = static_cast<uint8_t>(i); w.integer(x, 0, 7); }
    w.flush();
    EXPECT_EQ(buf.size(), 3u); // 8 x 3 bits
}

TEST(NetBitStream, ReadingPastTheEndFailsSafely) {
    const uint8_t one[1] = { 0xff };
    ReadStream r(one, 1);
    EXPECT_EQ(r.readBits(8), 0xffu);
    EXPECT_EQ(r.readBits(1), 0u);
    EXPECT_FALSE(r.ok());
    uint32_t x = 99;
    r.bits(x, 4); // everything after a failure is a no-op
    EXPECT_EQ(x, 0u);
}

TEST(NetBitStream, OutOfRangeAndLyingLengthsAreRejected) {
    std::vector<uint8_t> buf;
    { WriteStream w(buf); w.bits(7, 3); } // 7 in a field whose range is 0..5
    ReadStream r(buf.data(), buf.size());
    int v = 42;
    r.integer(v, 0, 5);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(v, 0);
    // A string claiming 200 characters in a 3-byte packet: refused before
    // reading or allocating them.
    std::vector<uint8_t> lie;
    { WriteStream w(lie); w.bits(200, 8); w.bits(0x41, 8); }
    ReadStream r2(lie.data(), lie.size());
    std::string s;
    r2.string(s, 255);
    EXPECT_FALSE(r2.ok());
    EXPECT_TRUE(s.empty());
}

TEST(NetBitStream, NonFiniteValuesAreClampedNotSent) {
    std::vector<uint8_t> buf;
    {
        WriteStream w(buf);
        float nan = std::nanf(""), inf = INFINITY;
        w.real(nan, -1.0f, 1.0f, 0.01f);
        w.real(inf, -1.0f, 1.0f, 0.01f);
    }
    ReadStream r(buf.data(), buf.size());
    float a = 0, b = 0;
    r.real(a, -1.0f, 1.0f, 0.01f);
    r.real(b, -1.0f, 1.0f, 0.01f);
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(std::isfinite(a));
    EXPECT_TRUE(std::isfinite(b));
}

// ---------------------------------------------------------------- protocol

TEST(NetProtocol, SnapshotRoundTripWithinQuantization) {
    SnapshotMsg s;
    s.serverTimeMs = 123456;
    NetPlayerState p;
    p.position = glm::vec3(12.345f, 1.5f, -300.25f);
    p.velocity = glm::vec3(5.5f, -9.0f, 0.25f);
    p.yaw = 271.0f;
    p.state = 3;
    p.flags = 0x05;
    p.speed = 6.2f;
    p.progress = 0.5f;
    p.aux = 1.25f;
    s.players.push_back({ 2, p });
    for (uint16_t i = 0; i < 40; ++i) {
        NetBodyState b;
        b.id = static_cast<uint16_t>(i * 37);
        b.position = glm::vec3(i * 0.7f, 0.3f + i, -3.0f);
        b.rotation = glm::angleAxis(0.1f * i, glm::normalize(glm::vec3(1, 2, 3)));
        b.velocity = glm::vec3(0.0f, -1.0f * i, 0.5f);
        b.sleeping = i % 3 == 0;
        s.bodies.push_back(b);
    }
    const std::vector<uint8_t> bytes = encode(MessageType::Snapshot, s);
    EXPECT_LT(bytes.size(), kMaxUnreliableBytes) << "40 bodies + a player fit one packet";
    EXPECT_EQ(peekType(bytes.data(), bytes.size()), MessageType::Snapshot);
    auto r = decode<SnapshotMsg>(MessageType::Snapshot, bytes.data(), bytes.size());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->serverTimeMs, 123456u);
    ASSERT_EQ(r->players.size(), 1u);
    const NetPlayerState& q = r->players[0].state;
    EXPECT_EQ(r->players[0].id, 2);
    EXPECT_LT(glm::length(q.position - p.position), 2e-3f);
    EXPECT_LT(glm::length(q.velocity - p.velocity), 1e-2f);
    EXPECT_NEAR(q.yaw, 271.0f, 0.4f);
    EXPECT_EQ(q.state, 3);
    EXPECT_EQ(q.flags, 0x05);
    ASSERT_EQ(r->bodies.size(), 40u);
    for (size_t i = 0; i < 40; ++i) {
        EXPECT_EQ(r->bodies[i].id, s.bodies[i].id);
        EXPECT_EQ(r->bodies[i].sleeping, s.bodies[i].sleeping);
        EXPECT_LT(glm::length(r->bodies[i].position - s.bodies[i].position), 2e-3f);
        EXPECT_GT(std::fabs(glm::dot(r->bodies[i].rotation, s.bodies[i].rotation)), 0.9995f);
        if (!s.bodies[i].sleeping) {
            EXPECT_LT(glm::length(r->bodies[i].velocity - s.bodies[i].velocity), 1e-2f);
        }
    }
}

TEST(NetProtocol, WrongTypeTruncatedAndTrailingJunkAreRejected) {
    HelloMsg h{ kProtocolVersion, "kke", "Kees", "SK_Character_Dummy" };
    std::vector<uint8_t> bytes = encode(MessageType::Hello, h);
    EXPECT_TRUE(decode<HelloMsg>(MessageType::Hello, bytes.data(), bytes.size()).has_value());
    EXPECT_FALSE(decode<WelcomeMsg>(MessageType::Welcome, bytes.data(), bytes.size()).has_value());
    EXPECT_FALSE(decode<HelloMsg>(MessageType::Hello, bytes.data(), bytes.size() - 2).has_value());
    bytes.push_back(0);
    bytes.push_back(0);
    EXPECT_FALSE(decode<HelloMsg>(MessageType::Hello, bytes.data(), bytes.size()).has_value());
    const uint8_t zero[1] = { 0 };
    EXPECT_FALSE(peekType(zero, 1).has_value());
    EXPECT_FALSE(peekType(nullptr, 0).has_value());
}

// Random bytes, random bit flips of real messages, random truncations:
// every decoder must refuse or accept without crashing, reading out of
// bounds (run under ASan in CI's sanitizer job) or allocating wildly.
TEST(NetProtocol, FuzzedPacketsNeverCrashTheDecoders) {
    std::mt19937 rng(1234);
    std::vector<std::vector<uint8_t>> seeds;
    {
        HelloMsg h{ kProtocolVersion, "kke", "Name", "Char" };
        seeds.push_back(encode(MessageType::Hello, h));
        SnapshotMsg s;
        s.players.push_back({ 1, NetPlayerState{} });
        s.bodies.resize(5);
        seeds.push_back(encode(MessageType::Snapshot, s));
        GameEventMsg e{ 0, 7, { 1, 2, 3 } };
        seeds.push_back(encode(MessageType::GameEvent, e));
        PlayerStateMsg p{ 99, NetPlayerState{} };
        seeds.push_back(encode(MessageType::PlayerState, p));
        SpawnMsg sp{ 0x8001, 0x4C01, script_net::encode(script_net::BodySpawn{}) };
        seeds.push_back(encode(MessageType::Spawn, sp));
        BreakMsg br{ 3, 77, { { 1, 2 }, { 2, 5 }, { 9, 40 } } };
        seeds.push_back(encode(MessageType::Break, br));
    }
    size_t accepted = 0;
    for (int i = 0; i < 20000; ++i) {
        std::vector<uint8_t> pkt;
        if (i % 2 == 0) {
            pkt.resize(rng() % 64);
            for (uint8_t& b : pkt) b = static_cast<uint8_t>(rng());
        } else {
            pkt = seeds[rng() % seeds.size()];
            const int flips = 1 + static_cast<int>(rng() % 4);
            for (int f = 0; f < flips && !pkt.empty(); ++f) pkt[rng() % pkt.size()] ^= static_cast<uint8_t>(1u << (rng() % 8));
            if (rng() % 4 == 0 && !pkt.empty()) pkt.resize(rng() % pkt.size());
        }
        const uint8_t* d = pkt.data();
        const size_t n = pkt.size();
        accepted += decode<HelloMsg>(MessageType::Hello, d, n).has_value();
        accepted += decode<WelcomeMsg>(MessageType::Welcome, d, n).has_value();
        accepted += decode<RejectMsg>(MessageType::Reject, d, n).has_value();
        accepted += decode<PlayerInfoMsg>(MessageType::PlayerInfo, d, n).has_value();
        accepted += decode<CorrectionMsg>(MessageType::Correction, d, n).has_value();
        accepted += decode<GameEventMsg>(MessageType::GameEvent, d, n).has_value();
        accepted += decode<PlayerStateMsg>(MessageType::PlayerState, d, n).has_value();
        if (auto s = decode<SnapshotMsg>(MessageType::Snapshot, d, n)) {
            ++accepted;
            EXPECT_LE(s->bodies.size(), kMaxBodiesPerSnapshot);
            EXPECT_LE(s->players.size(), kMaxPlayers);
        }
        if (auto sp = decode<SpawnMsg>(MessageType::Spawn, d, n)) {
            ++accepted;
            EXPECT_LE(sp->desc.size(), kMaxSpawnBytes);
            if (auto body = script_net::decodeBody(sp->desc)) {
                EXPECT_TRUE(std::isfinite(body->position.x));
            }
        }
        accepted += decode<DespawnMsg>(MessageType::Despawn, d, n).has_value();
        if (auto br = decode<BreakMsg>(MessageType::Break, d, n)) {
            ++accepted;
            EXPECT_LE(br->borders.size(), kMaxBordersPerBreak);
        }
    }
    EXPECT_GT(accepted, 0u); // some flips land in don't-care bits: still valid messages
}

TEST(NetProtocol, SpawnDespawnAndBreakRoundTrip) {
    SpawnMsg sp{ 0x8123, 0x4C02, { 1, 2, 3, 4, 5 } };
    auto bytes = encode(MessageType::Spawn, sp);
    auto sp2 = decode<SpawnMsg>(MessageType::Spawn, bytes.data(), bytes.size());
    ASSERT_TRUE(sp2);
    EXPECT_EQ(sp2->id, 0x8123);
    EXPECT_EQ(sp2->kind, 0x4C02);
    EXPECT_EQ(sp2->desc, sp.desc);

    DespawnMsg de{ 0x8123 };
    bytes = encode(MessageType::Despawn, de);
    auto de2 = decode<DespawnMsg>(MessageType::Despawn, bytes.data(), bytes.size());
    ASSERT_TRUE(de2);
    EXPECT_EQ(de2->id, 0x8123);

    BreakMsg br{ 7, 0xCAFEBABE, {} };
    for (uint16_t i = 0; i < kMaxBordersPerBreak; ++i) br.borders.push_back({ i, static_cast<uint16_t>(i + 1) });
    bytes = encode(MessageType::Break, br);
    auto br2 = decode<BreakMsg>(MessageType::Break, bytes.data(), bytes.size());
    ASSERT_TRUE(br2);
    EXPECT_EQ(br2->id, 7);
    EXPECT_EQ(br2->seed, 0xCAFEBABEu);
    EXPECT_EQ(br2->borders, br.borders);
    // A border count the packet can't hold is refused before allocating.
    bytes.resize(bytes.size() / 2);
    EXPECT_FALSE(decode<BreakMsg>(MessageType::Break, bytes.data(), bytes.size()));
}

TEST(NetScriptSpawns, DescriptionsRoundTripAndDamagedOnesAreRefused) {
    script_net::BodySpawn body;
    body.sphere = true;
    body.position = glm::vec3(1.5f, 20.0f, -3.25f);
    body.velocity = glm::vec3(0.0f, 4.0f, 0.0f);
    body.radius = 0.4f;
    body.density = 750.0f;
    body.material = 3;
    body.color = glm::vec3(0.2f, 0.4f, 0.9f);
    auto b = script_net::decodeBody(script_net::encode(body));
    ASSERT_TRUE(b);
    EXPECT_TRUE(b->sphere);
    EXPECT_FALSE(b->isStatic);
    EXPECT_EQ(b->position, body.position);
    EXPECT_EQ(b->velocity, body.velocity);
    EXPECT_EQ(b->radius, body.radius);
    EXPECT_EQ(b->material, 3u);
    EXPECT_EQ(b->color, body.color);

    script_net::BreakableBoxSpawn box;
    box.cells = glm::ivec3(8, 6, 2);
    box.size = glm::vec3(1.4f, 1.0f, 0.25f);
    box.position = glm::vec3(3, 0.5f, -2);
    box.material.density = 2500.0f;
    box.material.textureId = 1;
    box.pattern = 1;
    box.chunk = 0.3f;
    box.seed = 123456789u;
    auto x = script_net::decodeBreakableBox(script_net::encode(box));
    ASSERT_TRUE(x);
    EXPECT_EQ(x->cells, box.cells);
    EXPECT_EQ(x->size, box.size);
    EXPECT_EQ(x->material.density, 2500.0f);
    EXPECT_EQ(x->material.textureId, 1);
    EXPECT_EQ(x->pattern, 1);
    EXPECT_EQ(x->seed, 123456789u);

    script_net::BallSpawn ball;
    ball.radius = 0.15f;
    ball.velocity = glm::vec3(0, 0, -22);
    auto y = script_net::decodeBall(script_net::encode(ball));
    ASSERT_TRUE(y);
    EXPECT_EQ(y->velocity, ball.velocity);

    // Not finite, out of range, truncated, or someone else's kind: refused.
    body.position.x = std::nanf("");
    EXPECT_FALSE(script_net::decodeBody(script_net::encode(body)));
    body.position.x = 1e9f;
    EXPECT_FALSE(script_net::decodeBody(script_net::encode(body)));
    box.size = glm::vec3(500.0f);
    EXPECT_FALSE(script_net::decodeBreakableBox(script_net::encode(box)));
    auto bytes = script_net::encode(ball);
    bytes.pop_back();
    EXPECT_FALSE(script_net::decodeBall(bytes));
    EXPECT_FALSE(script_net::decodeBreakableBox(script_net::encode(ball)));
}

// ---------------------------------------------------------------- loopback transport

TEST(NetLoopback, ConnectsSendsBothChannelsAndDisconnects) {
    LoopbackNetwork net;
    LoopbackTransport server(net), client(net);
    ASSERT_TRUE(server.host(4000, 4));
    std::string err;
    EXPECT_FALSE(LoopbackTransport(net).host(4000, 4, &err)) << "port taken";
    EXPECT_NE(err.find("taken"), std::string::npos);
    const PeerId toServer = client.connect("localhost", 4000);
    ASSERT_NE(toServer, kNoPeer);
    std::vector<NetEvent> se, ce;
    server.poll(se);
    client.poll(ce);
    ASSERT_EQ(se.size(), 1u);
    EXPECT_EQ(se[0].type, NetEvent::Type::Connected);
    ASSERT_EQ(ce.size(), 1u);
    EXPECT_EQ(ce[0].type, NetEvent::Type::Connected);
    const PeerId toClient = se[0].peer;
    client.send(toServer, Channel::Reliable, std::vector<uint8_t>{ 1, 2 });
    server.send(toClient, Channel::Unreliable, std::vector<uint8_t>{ 9 });
    se.clear();
    ce.clear();
    server.poll(se);
    client.poll(ce);
    ASSERT_EQ(se.size(), 1u);
    EXPECT_EQ(se[0].data, (std::vector<uint8_t>{ 1, 2 }));
    ASSERT_EQ(ce.size(), 1u);
    EXPECT_EQ(ce[0].channel, Channel::Unreliable);
    client.disconnect(toServer);
    se.clear();
    server.poll(se);
    ASSERT_EQ(se.size(), 1u);
    EXPECT_EQ(se[0].type, NetEvent::Type::Disconnected);
}

TEST(NetLoopback, LossDelaysAndReordersOnlyWhatUdpWould) {
    LoopbackNetwork net(42);
    net.conditions.latencyMs = 50.0f;
    net.conditions.jitterMs = 30.0f;
    net.conditions.lossPercent = 20.0f;
    LoopbackTransport server(net), client(net);
    ASSERT_TRUE(server.host(4001, 4));
    const PeerId toServer = client.connect("localhost", 4001);
    std::vector<NetEvent> se, ce;
    for (int i = 0; i < 20; ++i) { net.advance(0.01); server.poll(se); client.poll(ce); }
    ASSERT_FALSE(ce.empty());
    se.clear();
    for (uint32_t i = 0; i < 200; ++i) {
        const uint8_t b = static_cast<uint8_t>(i);
        client.send(toServer, Channel::Reliable, &b, 1);
        client.send(toServer, Channel::Unreliable, &b, 1);
        net.advance(0.002);
        server.poll(se);
    }
    for (int i = 0; i < 100; ++i) { net.advance(0.01); server.poll(se); }
    std::vector<int> reliable;
    size_t unreliable = 0;
    bool reordered = false;
    int lastUnreliable = -1;
    for (const NetEvent& e : se) {
        if (e.type != NetEvent::Type::Received) continue;
        if (e.channel == Channel::Reliable) reliable.push_back(e.data[0]);
        else {
            ++unreliable;
            if (e.data[0] < lastUnreliable) reordered = true;
            lastUnreliable = e.data[0];
        }
    }
    ASSERT_EQ(reliable.size(), 200u) << "reliable: all of them";
    for (int i = 0; i < 200; ++i) EXPECT_EQ(reliable[static_cast<size_t>(i)], i) << "reliable: in order";
    EXPECT_GT(unreliable, 120u);
    EXPECT_LT(unreliable, 190u) << "unreliable: about 20% lost";
    EXPECT_TRUE(reordered) << "30 ms of jitter reorders 2 ms-apart packets";
}

// ---------------------------------------------------------------- sessions

namespace {

// A server and N clients on one LoopbackNetwork, stepped together at 60 Hz.
struct Match {
    LoopbackNetwork net;
    LoopbackTransport serverT{ net };
    NetServer server{ serverT };
    std::vector<std::unique_ptr<LoopbackTransport>> clientT;
    std::vector<std::unique_ptr<NetClient>> clients;
    double now = 0.0;

    NetConfig config;

    explicit Match(size_t n, const NetConfig& cfg = {}) : server(serverT, cfg), config(cfg) {
        EXPECT_TRUE(server.start(5000, "Host", "SK_Host"));
        for (size_t i = 0; i < n; ++i) addClient();
    }
    NetClient& addClient() {
        clientT.push_back(std::make_unique<LoopbackTransport>(net));
        clients.push_back(std::make_unique<NetClient>(*clientT.back(), config));
        EXPECT_TRUE(clients.back()->connect("localhost", 5000, "P" + std::to_string(clients.size() - 1), ""));
        return *clients.back();
    }
    void step(double dt = 1.0 / 60.0) {
        now += dt;
        net.advance(dt);
        server.update(now);
        for (auto& c : clients) c->update(now);
    }
    void run(double seconds) {
        for (double t = 0; t < seconds; t += 1.0 / 60.0) step();
    }
};

NetPlayerState walking(double t, float x0) {
    NetPlayerState s;
    s.position = glm::vec3(x0, 0.0f, static_cast<float>(-3.0 * t)); // 3 m/s toward -Z
    s.velocity = glm::vec3(0, 0, -3);
    s.yaw = 180.0f;
    s.speed = 3.0f;
    return s;
}

} // namespace

TEST(NetSession, ClientsJoinSeeEachOtherAndTheHost) {
    Match m(2);
    std::vector<std::pair<int, bool>> joins;
    m.server.onPlayer = [&](uint8_t id, bool j) { joins.push_back({ id, j }); };
    m.run(0.5);
    ASSERT_EQ(m.clients[0]->status(), NetClient::Status::Connected);
    ASSERT_EQ(m.clients[1]->status(), NetClient::Status::Connected);
    EXPECT_NE(m.clients[0]->playerId(), m.clients[1]->playerId());
    EXPECT_EQ(m.server.clientCount(), 2u);
    EXPECT_EQ(joins.size(), 2u);
    // Everyone moves; everyone sees the others.
    for (double t = 0; t < 2.0; t += 1.0 / 60.0) {
        m.server.setLocalState(walking(m.now, 0.0f));
        m.clients[0]->setLocalState(walking(m.now, 5.0f));
        m.clients[1]->setLocalState(walking(m.now, 10.0f));
        m.step();
    }
    const auto seen = m.clients[0]->players(m.now);
    ASSERT_EQ(seen.size(), 2u); // the host and the other client
    for (const RemotePlayer& p : seen) {
        ASSERT_TRUE(p.hasState) << p.name;
        const float x = p.id == 0 ? 0.0f : 10.0f;
        EXPECT_NEAR(p.state.position.x, x, 0.01f) << p.name;
        // ~100 ms behind (the interpolation delay): 0.3 m back at 3 m/s.
        EXPECT_NEAR(p.state.position.z, -3.0 * m.now, 0.6) << p.name;
    }
    EXPECT_EQ(seen[0].name, "Host");
    const auto onHost = m.server.players(m.now);
    ASSERT_EQ(onHost.size(), 2u);
    EXPECT_TRUE(onHost[0].hasState);
}

TEST(NetSession, SmoothUnderLossLatencyAndJitter) {
    Match m(1);
    m.net.conditions = { 80.0f, 25.0f, 10.0f, 2.0f }; // 80 ms +-25, 10% loss, 2% duplicates
    m.run(1.0);
    ASSERT_EQ(m.clients[0]->status(), NetClient::Status::Connected);
    float worstJump = 0.0f, worstError = 0.0f;
    glm::vec3 last(0.0f);
    bool haveLast = false;
    for (double t = 0; t < 4.0; t += 1.0 / 60.0) {
        m.server.setLocalState(walking(m.now, 0.0f));
        m.step();
        const auto seen = m.clients[0]->players(m.now);
        if (seen.empty() || !seen[0].hasState) continue;
        const glm::vec3 p = seen[0].state.position;
        if (haveLast) worstJump = std::max(worstJump, glm::length(p - last));
        last = p;
        haveLast = true;
        // Where the host really was ~(latency + delay) ago.
        worstError = std::max(worstError, std::fabs(p.z - static_cast<float>(-3.0 * m.now)));
    }
    ASSERT_TRUE(haveLast);
    // 3 m/s at 60 fps = 5 cm a frame; interpolation keeps every frame's
    // step close to that despite lost and late packets.
    EXPECT_LT(worstJump, 0.12f);
    EXPECT_LT(worstError, 1.0f); // ~0.2 s behind at worst
    EXPECT_GT(m.net.packetsDropped(), 0u);
}

TEST(NetSession, BodiesArriveByPriorityWithinOnePacket) {
    NetConfig cfg;
    cfg.snapshotBytes = 600; // room for ~25 bodies a packet
    Match m(1, cfg);
    std::vector<NetBodyState> bodies;
    for (uint16_t i = 0; i < 200; ++i) {
        NetBodyState b;
        b.id = i;
        b.position = glm::vec3(i, 0.5f, 0);
        b.sleeping = i >= 10; // 10 awake, 190 asleep
        bodies.push_back(b);
    }
    m.server.setBodies(bodies);
    m.run(0.5);
    for (int f = 0; f < 60; ++f) {
        for (uint16_t i = 0; i < 10; ++i) bodies[i].position.y = 0.5f + 0.01f * f; // the awake ones move
        m.server.setBodies(bodies);
        m.step();
    }
    const auto ids = m.clients[0]->bodyIds();
    EXPECT_EQ(ids.size(), 200u) << "every body arrived within a second";
    NetBodyState b;
    ASSERT_TRUE(m.clients[0]->body(3, m.now, b));
    EXPECT_NEAR(b.position.x, 3.0f, 0.01f);
    EXPECT_GT(b.position.y, 0.9f) << "awake bodies are fresh";
    ASSERT_TRUE(m.clients[0]->body(150, m.now, b));
    EXPECT_NEAR(b.position.x, 150.0f, 0.01f);
}

TEST(NetSession, ServerCorrectsImpossibleMovesAndAllowsARealTeleport) {
    Match m(1);
    glm::vec3 corrected(-1.0f);
    int corrections = 0;
    m.clients[0]->onCorrection = [&](const glm::vec3& p) { corrected = p; ++corrections; };
    m.run(0.5);
    NetPlayerState s;
    s.position = glm::vec3(1, 0, 1);
    m.clients[0]->setLocalState(s);
    m.run(0.5);
    // 100 m in one step: refused, and the client is told where it is.
    s.position = glm::vec3(101, 0, 1);
    m.clients[0]->setLocalState(s);
    m.run(0.5);
    EXPECT_GE(corrections, 1);
    EXPECT_NEAR(corrected.x, 1.0f, 0.01f);
    EXPECT_GE(m.server.corrections(), 1u);
    auto onHost = m.server.players(m.now);
    ASSERT_EQ(onHost.size(), 1u);
    EXPECT_NEAR(onHost[0].state.position.x, 1.0f, 0.01f) << "the host still sees it where it was allowed to be";
    // A flagged teleport (respawn, scene change) is accepted.
    s.position = glm::vec3(200, 0, 1);
    s.flags = kPlayerTeleported;
    m.clients[0]->setLocalState(s);
    m.run(0.5);
    onHost = m.server.players(m.now);
    EXPECT_NEAR(onHost[0].state.position.x, 200.0f, 0.01f);
}

TEST(NetSession, WrongVersionWrongGameAndFullServerAreTurnedAway) {
    NetConfig cfg;
    cfg.maxPlayers = 2; // the host + one
    Match m(2, cfg);
    m.run(0.5);
    int connected = 0, rejected = 0;
    for (auto& c : m.clients) {
        if (c->status() == NetClient::Status::Connected) ++connected;
        if (c->status() == NetClient::Status::Rejected) {
            ++rejected;
            EXPECT_NE(c->statusText().find("full"), std::string::npos) << c->statusText();
        }
    }
    EXPECT_EQ(connected, 1);
    EXPECT_EQ(rejected, 1);

    LoopbackTransport other(m.net);
    NetConfig otherGame;
    otherGame.gameId = "another_game";
    NetClient stranger(other, otherGame);
    ASSERT_TRUE(stranger.connect("localhost", 5000, "X", ""));
    for (int i = 0; i < 30; ++i) { m.step(); stranger.update(m.now); }
    EXPECT_EQ(stranger.status(), NetClient::Status::Rejected);
    EXPECT_NE(stranger.statusText().find("another_game"), std::string::npos) << stranger.statusText();
}

TEST(NetSession, GarbageFromAPeerGetsItKickedNotTheServerCrashed) {
    Match m(1);
    m.run(0.5);
    ASSERT_EQ(m.server.clientCount(), 1u);
    // A raw transport next to the real client, speaking nonsense.
    LoopbackTransport rogue(m.net);
    const PeerId toServer = rogue.connect("localhost", 5000);
    std::vector<NetEvent> ev;
    for (int i = 0; i < 5; ++i) { m.step(); rogue.poll(ev); }
    std::mt19937 rng(9);
    for (int i = 0; i < 50; ++i) {
        std::vector<uint8_t> junk(1 + rng() % 40);
        for (uint8_t& b : junk) b = static_cast<uint8_t>(rng());
        rogue.send(toServer, i % 2 ? Channel::Reliable : Channel::Unreliable, junk);
        m.step();
        rogue.poll(ev);
    }
    EXPECT_GE(m.server.badPackets(), 20u);
    bool kicked = false;
    for (const NetEvent& e : ev) kicked |= e.type == NetEvent::Type::Disconnected;
    EXPECT_TRUE(kicked);
    EXPECT_EQ(m.server.clientCount(), 1u) << "the real client is untouched";
    EXPECT_EQ(m.clients[0]->status(), NetClient::Status::Connected);
}

TEST(NetSession, EventsReachTheServerAndAreRelayedWithTheSender) {
    Match m(2);
    m.run(0.5);
    std::vector<GameEventMsg> onServer, onOther, onSender;
    m.server.onEvent = [&](const GameEventMsg& e) { onServer.push_back(e); m.server.relayEvent(e); };
    m.clients[0]->onEvent = [&](const GameEventMsg& e) { onSender.push_back(e); };
    m.clients[1]->onEvent = [&](const GameEventMsg& e) { onOther.push_back(e); };
    m.clients[0]->sendEvent(12, { 4, 5, 6 });
    m.server.sendEvent(13, { 7 });
    m.run(0.3);
    ASSERT_EQ(onServer.size(), 1u);
    EXPECT_EQ(onServer[0].kind, 12);
    EXPECT_EQ(onServer[0].fromPlayer, m.clients[0]->playerId());
    ASSERT_EQ(onOther.size(), 2u);
    ASSERT_EQ(onSender.size(), 1u) << "not its own event back, only the host's";
    EXPECT_EQ(onSender[0].kind, 13);
    EXPECT_EQ(onSender[0].fromPlayer, 0);
}

TEST(NetSession, LeavingIsSeenByEveryone) {
    Match m(2);
    m.run(0.5);
    std::vector<std::pair<int, bool>> seen;
    m.clients[1]->onPlayer = [&](uint8_t id, bool j) { seen.push_back({ id, j }); };
    const uint8_t leaver = m.clients[0]->playerId();
    m.clients[0]->disconnect();
    m.run(0.3);
    EXPECT_EQ(m.server.clientCount(), 1u);
    ASSERT_FALSE(seen.empty());
    EXPECT_EQ(seen.back().first, leaver);
    EXPECT_FALSE(seen.back().second);
    EXPECT_EQ(m.clients[1]->players(m.now).size(), 1u); // only the host left
}

#if KKE_ENABLE_NET
// The real thing on this machine's UDP: two hosts on different ports (two
// games on one PC), a client finds both by LAN discovery, joins one,
// and states flow.
TEST(NetEnet, TwoHostsOnOnePcDiscoveredAndJoined) {
    auto now = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    EnetTransport t1, t2, tc;
    NetServer s1(t1), s2(t2);
    std::string err;
    uint16_t p1 = 0, p2 = 0;
    for (uint16_t p = 38400; p < 38420 && !p1; ++p) if (s1.start(p, "Alpha", "", &err)) p1 = p;
    for (uint16_t p = static_cast<uint16_t>(p1 + 1); p < 38420 && !p2; ++p) if (s2.start(p, "Beta", "", &err)) p2 = p;
    if (!p1 || !p2) GTEST_SKIP() << "no free UDP ports here: " << err;
    t1.setDiscoveryInfo("Alpha");
    t2.setDiscoveryInfo("Beta");
    ASSERT_TRUE(tc.discover(38400, 38420));
    std::vector<NetEvent> none;
    for (int i = 0; i < 100 && tc.lanGames().size() < 2; ++i) {
        s1.update(now());
        s2.update(now());
        tc.poll(none);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_GE(tc.lanGames().size(), 2u) << "both hosts answered";
    std::vector<std::string> infos;
    for (const auto& g : tc.lanGames()) infos.push_back(g.info);
    EXPECT_NE(std::find(infos.begin(), infos.end(), "Alpha"), infos.end());
    EXPECT_NE(std::find(infos.begin(), infos.end(), "Beta"), infos.end());

    NetClient client(tc);
    ASSERT_TRUE(client.connect("127.0.0.1", p2, "Joiner", "", &err)) << err;
    NetPlayerState hostState;
    hostState.position = glm::vec3(3, 0, 4);
    s2.setLocalState(hostState);
    for (int i = 0; i < 300; ++i) {
        const double t = now();
        s1.update(t);
        s2.update(t);
        client.update(t);
        if (client.status() == NetClient::Status::Connected && !client.players(t).empty() && client.players(t)[0].hasState) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_EQ(client.status(), NetClient::Status::Connected) << client.statusText();
    EXPECT_EQ(s2.clientCount(), 1u);
    EXPECT_EQ(s1.clientCount(), 0u);
    const auto ps = client.players(now());
    ASSERT_FALSE(ps.empty());
    EXPECT_EQ(ps[0].name, "Beta");
    ASSERT_TRUE(ps[0].hasState);
    EXPECT_NEAR(ps[0].state.position.x, 3.0f, 0.01f);
}
#endif

// ---------------------------------------------------------------- networking v2 (#28)

namespace {
// What a client saw of spawns and breaks.
struct Seen {
    std::map<uint16_t, SpawnMsg> spawns;
    std::vector<uint16_t> despawns;
    std::map<uint16_t, std::set<std::pair<uint16_t, uint16_t>>> borders;
    size_t breakMessages = 0;
    void watch(NetClient& c) {
        c.onSpawn = [this](const SpawnMsg& m) { spawns[m.id] = m; };
        c.onDespawn = [this](uint16_t id) { despawns.push_back(id); spawns.erase(id); };
        c.onBreak = [this](const BreakMsg& m) {
            ++breakMessages;
            for (const auto& b : m.borders) borders[m.id].insert(b);
        };
    }
};
} // namespace

TEST(NetSession, SpawnsReachEveryoneAndLateJoinersGetTheOnesStillThere) {
    Match m(1);
    Seen early;
    early.watch(*m.clients[0]);
    m.run(0.5);
    m.server.spawn({ 0x8000, 0x4C01, { 1, 2, 3 } });           // a script's crate
    m.server.spawn({ 0x8001, 0x4C03, { 9 } }, false);          // a thrown ball: not for late joiners
    m.server.spawn({ 0x8002, 0x4C01, { 4 } });
    m.run(0.3);
    ASSERT_EQ(early.spawns.size(), 3u);
    EXPECT_EQ(early.spawns[0x8000].desc, (std::vector<uint8_t>{ 1, 2, 3 }));
    m.server.despawn(0x8002);
    m.run(0.3);
    EXPECT_EQ(early.despawns, std::vector<uint16_t>{ 0x8002 });
    EXPECT_EQ(m.server.spawnCount(), 1u);

    Seen late;
    late.watch(m.addClient());
    m.run(0.5);
    ASSERT_EQ(m.clients[1]->status(), NetClient::Status::Connected);
    ASSERT_EQ(late.spawns.size(), 1u) << "the crate, not the old throw nor the removed one";
    EXPECT_TRUE(late.spawns.count(0x8000));
}

TEST(NetSession, BreaksAreSentOnceAndReplayedToLateJoiners) {
    Match m(1);
    Seen early;
    early.watch(*m.clients[0]);
    m.run(0.5);
    m.server.breakBorders(4, 99, { { 1, 2 }, { 3, 2 } });
    m.run(0.2);
    // The whole set again plus one new border: only the new one goes out.
    m.server.breakBorders(4, 99, { { 1, 2 }, { 2, 3 }, { 5, 6 } });
    m.run(0.2);
    EXPECT_EQ(early.breakMessages, 2u);
    const std::set<std::pair<uint16_t, uint16_t>> want{ { 1, 2 }, { 2, 3 }, { 5, 6 } };
    EXPECT_EQ(early.borders[4], want);
    // A big break is split over several messages.
    std::vector<std::pair<uint16_t, uint16_t>> many;
    for (uint16_t i = 0; i < 2500; ++i) many.push_back({ i, static_cast<uint16_t>(i + 1) });
    m.server.breakBorders(9, 5, many);
    m.run(0.3);
    EXPECT_EQ(early.borders[9].size(), 2500u);

    Seen late;
    late.watch(m.addClient());
    m.run(0.5);
    EXPECT_EQ(late.borders[4], want);
    EXPECT_EQ(late.borders[9].size(), 2500u);
    // Forgotten (the breakable was rebuilt): the next joiner hears nothing of it.
    m.server.forgetBreaks(9);
    Seen later;
    later.watch(m.addClient());
    m.run(0.5);
    EXPECT_EQ(later.borders.count(9), 0u);
    EXPECT_EQ(later.borders[4], want);
}

TEST(NetSession, ClientsCantSpawnOrBreak) {
    Match m(1);
    m.run(0.5);
    // A client that said hello properly, then sends server-only messages.
    LoopbackTransport rogue(m.net);
    const PeerId toServer = rogue.connect("localhost", 5000);
    std::vector<NetEvent> ev;
    for (int i = 0; i < 5; ++i) { m.step(); rogue.poll(ev); }
    HelloMsg h{ kProtocolVersion, "kke", "Rogue", "" };
    rogue.send(toServer, Channel::Reliable, encode(MessageType::Hello, h));
    for (int i = 0; i < 5; ++i) { m.step(); rogue.poll(ev); }
    ASSERT_EQ(m.server.clientCount(), 2u);
    SpawnMsg sp{ 0x8000, 1, {} };
    BreakMsg br{ 1, 1, { { 1, 2 } } };
    for (int i = 0; i < 15; ++i) {
        rogue.send(toServer, Channel::Reliable, encode(MessageType::Spawn, sp));
        rogue.send(toServer, Channel::Reliable, encode(MessageType::Break, br));
        m.step();
        rogue.poll(ev);
    }
    EXPECT_GE(m.server.badPackets(), 20u);
    EXPECT_EQ(m.server.clientCount(), 1u) << "kicked for sending server-only messages";
    EXPECT_EQ(m.server.spawnCount(), 0u);
}

TEST(NetSession, TheGamesMoveCheckRefusesAndCorrects) {
    Match m(1);
    // "No going past x = 5" (a wall, in a real game: WorldMoveCheck).
    std::vector<double> dts;
    m.server.checkMove = [&](uint8_t, const NetPlayerState&, const NetPlayerState& to, double dt) {
        if (to.position.x >= 5.0f) return false;
        dts.push_back(dt); // between accepted states: one state interval
        return true;
    };
    glm::vec3 corrected(-1.0f);
    m.clients[0]->onCorrection = [&](const glm::vec3& p) { corrected = p; };
    m.run(0.5);
    NetPlayerState s;
    for (int i = 0; i <= 40; ++i) { // 6 m/s, well inside the speed limits
        s.position = glm::vec3(1.0f + 0.1f * static_cast<float>(i), 0, 0);
        m.clients[0]->setLocalState(s);
        m.step();
    }
    m.run(0.3);
    EXPECT_GE(m.server.refusedMoves(), 1u);
    EXPECT_GE(m.server.corrections(), 1u);
    EXPECT_LT(corrected.x, 5.0f);
    EXPECT_GT(corrected.x, 4.0f);
    ASSERT_FALSE(dts.empty());
    for (double dt : dts) EXPECT_NEAR(dt, 1.0 / 30.0, 0.02);
    const auto onHost = m.server.players(m.now);
    ASSERT_EQ(onHost.size(), 1u);
    EXPECT_LT(onHost[0].state.position.x, 5.0f);
}
