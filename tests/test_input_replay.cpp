// Server-side input replay (kke/net/InputReplay.h, docs/NETWORKING.md
// "Input replay"): the server plays each player's inputs, the client
// predicts and rewinds. Fakes first, then kke::Locomotion on Jolt.

#include "kke/net/InputReplay.h"
#include "kke/net/NetSession.h"
#include "kke/net/Protocol.h"
#include "kke/net/Transport.h"
#if KKE_ENABLE_JOLT
#include "kke/Locomotion.h"
#include "kke/RigidWorld.h"
#include "kke/net/LocomotionReplay.h"
#endif

#include <gtest/gtest.h>

#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <vector>

using namespace kke::net;

namespace {

constexpr float kDt = 1.0f / 60.0f;

// Moves 5 m/s along the input; "up" hops 1 m. A wall at `wallX` (server
// only, in some tests) stops it; `push` is added once at tick `pushTick`.
struct FakeSim : Rewindable {
    glm::vec3 pos{0.0f}, vel{0.0f};
    float wallX = 1e9f;
    uint32_t steps = 0, pushAt = ~0u;
    glm::vec3 push{0.0f};
    uint8_t mode = 0, pushMode = 0; // a movement state (NetPlayerState::state)
    struct S { glm::vec3 pos, vel; uint32_t steps; uint8_t mode; };
    std::vector<S> slots = std::vector<S>(256);

    void step(const InputFrame& in, float dt) override {
        vel = glm::vec3(in.move.x, 0.0f, in.move.y) * 5.0f;
        pos += vel * dt;
        if (in.buttons & kButtonUp) pos.y += 1.0f;
        if (pos.x > wallX) pos.x = wallX;
        if (steps++ == pushAt) {
            pos += push;
            mode = pushMode;
        }
    }
    void save(size_t slot) override { slots[slot % slots.size()] = { pos, vel, steps, mode }; }
    void load(size_t slot) override {
        const S& s = slots[slot % slots.size()];
        pos = s.pos;
        vel = s.vel;
        steps = s.steps;
        mode = s.mode;
    }
    NetPlayerState state() const override {
        NetPlayerState s;
        s.position = pos;
        s.velocity = vel;
        s.state = mode;
        return s;
    }
    void correct(const NetPlayerState& s) override {
        pos = s.position;
        vel = s.velocity;
        mode = s.state;
    }
};

InputFrame input(uint32_t tick, float x, float z = 0.0f, uint8_t buttons = 0) {
    InputFrame f;
    f.tick = tick;
    f.move = glm::vec2(x, z);
    f.buttons = buttons;
    return f;
}

} // namespace

TEST(NetInputQueue, PlaysEachInputOnceInOrderWaitingForLateOnes) {
    InputQueue q;
    InputFrame f;
    EXPECT_FALSE(q.next(f));
    EXPECT_TRUE(q.push(input(0, 1.0f, 0.0f, kButtonUp)));
    EXPECT_FALSE(q.push(input(0, 1.0f))); // the same one resent
    EXPECT_TRUE(q.push(input(2, 0.5f)));  // 1 is late
    q.beginTick();
    ASSERT_TRUE(q.next(f));
    EXPECT_EQ(f.tick, 0u);
    EXPECT_NE(f.buttons & kButtonUp, 0);
    EXPECT_FALSE(q.next(f)); // one per tick
    q.beginTick();
    EXPECT_FALSE(q.next(f)); // 1 isn't here: wait, don't guess
    q.beginTick();
    EXPECT_FALSE(q.next(f));
    EXPECT_TRUE(q.push(input(1, -1.0f))); // late, still in time
    ASSERT_TRUE(q.next(f));
    EXPECT_EQ(f.tick, 1u);
    ASSERT_TRUE(q.next(f)); // the ticks it waited are made up
    EXPECT_EQ(f.tick, 2u);
    EXPECT_EQ(q.lastPlayed(), 2u);
    EXPECT_EQ(q.stalled, 1u);
    // A lost one is waited for gapWait ticks, then skipped.
    q.push(input(4, 1.0f));
    for (size_t i = 0; i < q.gapWait; ++i) {
        q.beginTick();
        EXPECT_FALSE(q.next(f));
    }
    q.beginTick();
    ASSERT_TRUE(q.next(f));
    EXPECT_EQ(f.tick, 4u);
    EXPECT_EQ(q.skipped, 1u);
    EXPECT_FALSE(q.push(input(3, 1.0f))); // too late now
    EXPECT_FALSE(q.push(input(5, std::nanf(""))));
}

TEST(NetInputQueue, AFastClockGetsNoMoreThanOneInputPerTick) {
    // A client claiming twice the ticks: the server still plays one per
    // tick, so the player is exactly as fast as an honest one.
    InputQueue q;
    FakeSim honest, cheat;
    uint32_t tick = 0;
    for (int serverTick = 0; serverTick < 600; ++serverTick) {
        q.push(input(tick++, 1.0f));
        q.push(input(tick++, 1.0f));
        q.beginTick();
        InputFrame f;
        while (q.next(f)) cheat.step(f, kDt);
        honest.step(input(0, 1.0f), kDt);
    }
    EXPECT_LE(cheat.pos.x, honest.pos.x + 1e-3f);
    EXPECT_LE(q.buffered(), q.maxBuffered); // and the backlog is cut, not grown
    EXPECT_GT(q.skipped, 500u);
    // Nor can it save up ticks by going quiet and then sending a burst.
    InputQueue burst;
    FakeSim b;
    uint32_t t = 0;
    for (int serverTick = 0; serverTick < 600; ++serverTick) {
        if (serverTick % 60 == 59)
            for (int k = 0; k < 120; ++k) burst.push(input(t++, 1.0f));
        burst.beginTick();
        InputFrame f;
        while (burst.next(f)) b.step(f, kDt);
    }
    EXPECT_LE(b.pos.x, honest.pos.x + 1e-3f);
}

TEST(NetInputQueue, APressInASkippedInputStillHappens) {
    InputQueue q;
    q.maxBuffered = 3;
    q.push(input(0, 1.0f));
    q.push(input(1, 1.0f, 0.0f, kButtonUp));
    q.push(input(2, 1.0f));
    q.push(input(3, 1.0f)); // four waiting: 0 goes
    q.push(input(4, 1.0f)); // and 1, whose jump moves to 2
    EXPECT_EQ(q.skipped, 2u);
    q.beginTick();
    InputFrame first;
    ASSERT_TRUE(q.next(first));
    EXPECT_EQ(first.tick, 2u);
    EXPECT_NE(first.buttons & kButtonUp, 0);
}

TEST(NetPrediction, AFairClientIsNeverCorrected) {
    FakeSim client, server;
    Prediction p(client);
    std::vector<InputFrame> sent;
    for (int t = 0; t < 300; ++t) {
        sent.push_back(p.tick(input(0, std::sin(t * 0.05f), std::cos(t * 0.03f), t % 50 == 0 ? kButtonUp : 0), kDt));
        // The server is 10 ticks behind (the round trip).
        if (t >= 10) {
            server.step(sent[static_cast<size_t>(t - 10)], kDt);
            p.acknowledge(static_cast<uint32_t>(t - 10), server.state());
        }
    }
    EXPECT_EQ(p.corrections, 0u);
    EXPECT_EQ(p.lastAcknowledged(), 289u);
    EXPECT_EQ(p.unacknowledged(16).size(), 10u);
    EXPECT_EQ(p.unacknowledged(4).front().tick, 296u); // the newest ones
}

TEST(NetPrediction, RewindsToTheServerAndReplaysWhatCameAfter) {
    FakeSim client, server;
    server.pushAt = 30; // something only the server knows (a hit): 2 m sideways
    server.push = glm::vec3(0.0f, 0.0f, 2.0f);
    Prediction p(client);
    std::vector<InputFrame> sent;
    for (int t = 0; t < 120; ++t) {
        sent.push_back(p.tick(input(0, 1.0f), kDt));
        if (t >= 10) {
            server.step(sent[static_cast<size_t>(t - 10)], kDt);
            p.acknowledge(static_cast<uint32_t>(t - 10), server.state());
        }
    }
    EXPECT_EQ(p.corrections, 1u);
    EXPECT_EQ(p.replayedTicks, 10u); // the round trip's worth
    // Where the server will be once it has played our newest inputs.
    for (int t = 110; t < 120; ++t) server.step(sent[static_cast<size_t>(t)], kDt);
    EXPECT_NEAR(glm::length(client.pos - server.pos), 0.0f, 1e-4f);
    // The jump is hidden and fades.
    EXPECT_LT(glm::length(p.visualOffset()), 0.01f);
    // Answers for ticks never made, or older than the last, change nothing.
    EXPECT_FALSE(p.acknowledge(500, server.state()));
    EXPECT_FALSE(p.acknowledge(3, server.state()));
}

TEST(NetPrediction, AnotherMovementStateIsCorrectedEvenInTheSamePlace) {
    // The server says "hanging" where we have "walking", at the same spot.
    FakeSim client, server;
    server.pushAt = 20;
    server.pushMode = 4;
    Prediction p(client);
    std::vector<InputFrame> sent;
    for (int t = 0; t < 60; ++t) {
        sent.push_back(p.tick(input(0, 0.0f), kDt));
        if (t >= 5) {
            server.step(sent[static_cast<size_t>(t - 5)], kDt);
            p.acknowledge(static_cast<uint32_t>(t - 5), server.state());
        }
    }
    EXPECT_EQ(p.corrections, 1u);
    EXPECT_EQ(client.mode, 4);
}

TEST(NetProtocol, InputsRoundTripExactlyAsTheClientSteppedThem) {
    InputMsg m;
    for (uint32_t t = 100; t < 100 + kMaxInputsPerMsg; ++t) {
        InputFrame f = input(t, std::sin(float(t)) * 1.3f, std::cos(float(t)), static_cast<uint8_t>(t * 37));
        f.yaw = float(t) * 23.7f - 400.0f;
        m.frames.push_back(quantize(f));
    }
    const std::vector<uint8_t> bytes = encode(MessageType::Input, m);
    EXPECT_LE(bytes.size(), 80u); // 16 inputs in a small packet (~34 bits each)
    const auto back = decode<InputMsg>(MessageType::Input, bytes.data(), bytes.size());
    ASSERT_TRUE(back);
    ASSERT_EQ(back->frames.size(), m.frames.size());
    for (size_t i = 0; i < m.frames.size(); ++i) {
        const InputFrame& a = m.frames[i];
        const InputFrame b = quantize(back->frames[i]); // what the server plays
        EXPECT_EQ(b.tick, a.tick);
        EXPECT_EQ(b.move, a.move) << i; // bit for bit: both sides step the same numbers
        EXPECT_EQ(b.yaw, a.yaw) << i;
        EXPECT_EQ(b.buttons, a.buttons);
    }
    for (size_t cut = 0; cut < bytes.size(); ++cut) EXPECT_FALSE(decode<InputMsg>(MessageType::Input, bytes.data(), cut));
    InputMsg none;
    std::vector<uint8_t> empty = encode(MessageType::Input, none); // count 0 is out of range
    EXPECT_FALSE(decode<InputMsg>(MessageType::Input, empty.data(), empty.size()));

    InputAckMsg ack{ 1234567u, {} };
    ack.state.position = glm::vec3(1.5f, 2.0f, -3.25f);
    const std::vector<uint8_t> ab = encode(MessageType::InputAck, ack);
    const auto ackBack = decode<InputAckMsg>(MessageType::InputAck, ab.data(), ab.size());
    ASSERT_TRUE(ackBack);
    EXPECT_EQ(ackBack->tick, 1234567u);
    EXPECT_NEAR(ackBack->state.position.z, -3.25f, 1e-3f);
}

namespace {

// A server and clients on one LoopbackNetwork with input replay; each
// client predicts with its own sim, the server moves its copy of each.
template <typename Sim>
struct ReplayMatch {
    LoopbackNetwork net;
    LoopbackTransport serverT{ net };
    NetServer server;
    struct Player {
        std::unique_ptr<LoopbackTransport> transport;
        std::unique_ptr<NetClient> client;
        Sim* local = nullptr;                 // the client's
        std::unique_ptr<Prediction> prediction;
        Sim* remote = nullptr;                // the server's
        std::function<InputFrame(int clientTick)> input;
        int ticksPerStep = 1;                 // > 1: a cheat running its clock fast
    };
    std::deque<Player> players; // join() hands out references: they must stay put
    double now = 0.0;
    int tick = 0;

    static NetConfig config() {
        NetConfig c;
        c.inputReplay = true;
        return c;
    }
    ReplayMatch() : server(serverT, config()) { EXPECT_TRUE(server.start(5000, "Host", "")); }
    Player& join(Sim& local, Sim& remote, std::function<InputFrame(int)> in) {
        Player p;
        p.transport = std::make_unique<LoopbackTransport>(net);
        p.client = std::make_unique<NetClient>(*p.transport, config());
        EXPECT_TRUE(p.client->connect("localhost", 5000, "P" + std::to_string(players.size()), ""));
        p.local = &local;
        p.remote = &remote;
        p.prediction = std::make_unique<Prediction>(local);
        p.input = std::move(in);
        Prediction* pred = p.prediction.get();
        p.client->onInputAck = [pred](uint32_t t, const NetPlayerState& s) { pred->acknowledge(t, s); };
        players.push_back(std::move(p));
        return players.back();
    }
    void step() {
        now += kDt;
        net.advance(kDt);
        server.update(now);
        for (Player& p : players) {
            p.client->update(now);
            if (p.client->status() != NetClient::Status::Connected || !p.client->inputReplay()) continue;
            for (int i = 0; i < p.ticksPerStep; ++i) p.prediction->tick(p.input(static_cast<int>(p.prediction->nextTick())), kDt);
            p.client->sendInputs(p.prediction->unacknowledged(kMaxInputsPerMsg));
        }
        server.inputTick();
        for (Player& p : players) {
            InputFrame in;
            bool moved = false;
            while (server.nextInput(p.client->playerId(), in)) {
                p.remote->step(in, kDt);
                moved = true;
            }
            if (moved) server.setPlayerState(p.client->playerId(), p.remote->state());
        }
        ++tick;
    }
    void run(double seconds) {
        for (double t = 0; t < seconds; t += kDt) step();
    }
};

} // namespace

TEST(NetSession, InputReplayTheServerMovesPlayersFromTheirInputs) {
    ReplayMatch<FakeSim> m;
    m.net.conditions.latencyMs = 60.0f;
    m.net.conditions.jitterMs = 15.0f;
    m.net.conditions.lossPercent = 10.0f;
    FakeSim local, remote, cheatLocal, cheatRemote;
    auto weave = [](int t) { return input(0, t < 240 ? std::sin(t * 0.04f) : 0.0f, t < 240 ? 1.0f : 0.0f); };
    auto& fair = m.join(local, remote, weave);
    auto& cheat = m.join(cheatLocal, cheatRemote, [](int t) { return input(0, 0.0f, t < 240 ? 1.0f : 0.0f); });
    cheat.ticksPerStep = 2; // sends two ticks' inputs every tick
    m.run(6.0);
    ASSERT_TRUE(fair.client->inputReplay()) << fair.client->statusText() << " bad " << m.server.badPackets();
    // Stopped for 2 s: the client ends exactly where the server has it.
    EXPECT_LT(glm::length(local.pos - remote.pos), 0.01f);
    EXPECT_GT(local.pos.z, 15.0f);
    // Loss and jitter now and then make the server repeat an input: a
    // few small corrections, never a fight.
    EXPECT_LT(fair.prediction->corrections, 20u) << "skipped " << m.server.inputs(fair.client->playerId())->skipped;
    // The fast clock bought no distance on the server, which is what
    // everyone sees; the cheat's own screen is put back.
    EXPECT_LE(cheatRemote.pos.z, 240 * kDt * 5.0f + 0.01f);
    EXPECT_LT(glm::length(cheatLocal.pos - cheatRemote.pos), 0.3f);
    // Others see the server's version of each player.
    for (const RemotePlayer& r : fair.client->players(m.now))
        if (r.id == cheat.client->playerId()) {
            EXPECT_NEAR(r.state.position.z, cheatRemote.pos.z, 0.05f);
        }
    // A client that sends a position anyway is ignored and counted.
    NetPlayerState claim;
    claim.position = glm::vec3(0.0f, 0.0f, 500.0f);
    fair.client->setLocalState(claim); // ignored by the client under input replay
    m.run(0.5);
    EXPECT_LT(remote.pos.z, 100.0f);
}

#if KKE_ENABLE_JOLT

using kke::Locomotion;
using kke::RigidWorld;

namespace {

// A floor, a hip-high fence to vault, and optionally a wall only one side has.
struct Level {
    RigidWorld world{ settings() };
    RigidWorld::CharacterId player = 0;
    std::unique_ptr<Locomotion> loco;
    std::unique_ptr<LocomotionReplay> replay;

    static RigidWorld::Settings settings() {
        RigidWorld::Settings s;
        s.threads = 0;
        return s;
    }
    explicit Level(bool fence = true, bool extraWall = false) {
        box({ 0, -0.5f, 0 }, { 50, 0.5f, 50 });
        if (fence) box({ 0, 0.5f, -8 }, { 3, 0.5f, 0.15f }); // 1 m high
        if (extraWall) box({ 0, 1.5f, -16 }, { 3, 1.5f, 0.2f });
        RigidWorld::CharacterDesc cd;
        player = world.addCharacter(cd);
        for (int i = 0; i < 10; ++i) world.step(kDt);
        loco = std::make_unique<Locomotion>(world, player);
        replay = std::make_unique<LocomotionReplay>(world, player, *loco);
    }
    void box(glm::vec3 c, glm::vec3 half) {
        RigidWorld::BodyDesc d;
        d.motion = RigidWorld::Motion::Static;
        d.halfExtents = half;
        d.position = c;
        world.add(d);
    }
    glm::vec3 feet() const { return world.characterPosition(player); }
};

// Adapts a Level to ReplayMatch's Sim.
struct LevelSim : Rewindable {
    Level level;
    bool vaulted = false;
    explicit LevelSim(bool fence = true, bool wall = false) : level(fence, wall) {}
    void step(const InputFrame& in, float dt) override {
        level.replay->step(in, dt);
        vaulted = vaulted || level.loco->state() == Locomotion::State::Vault;
    }
    void save(size_t slot) override { level.replay->save(slot); }
    void load(size_t slot) override { level.replay->load(slot); }
    NetPlayerState state() const override { return level.replay->state(); }
    void correct(const NetPlayerState& s) override { level.replay->correct(s); }
};

// Sprint toward -Z until `stopAt`; "go up" whenever the sensors see
// something to vault (the demo's autopilot), judged on `sim`'s own copy.
InputFrame runAt(int tick, int stopAt, const Level* sim = nullptr) {
    InputFrame f;
    if (tick >= stopAt) return f;
    f.move = glm::vec2(0.0f, -1.0f);
    f.buttons = kButtonFast;
    if (sim && sim->loco->state() == Locomotion::State::Ground &&
        sim->loco->probe({ 0, 0, -1 }, sim->loco->settings().sprintSensor).kind == Locomotion::Obstacle::Kind::Vault)
        f.buttons |= kButtonUp;
    return f;
}

} // namespace

TEST(NetRigidWorld, ACharacterPutBackMovesTheSameAgain) {
    Level a;
    std::vector<glm::vec3> first;
    std::vector<InputFrame> inputs;
    for (int t = 0; t < 150; ++t) {
        if (t == 20) a.replay->save(0);
        inputs.push_back(quantize(runAt(t, 150, &a)));
        a.replay->step(inputs.back(), kDt);
        first.push_back(a.feet());
    }
    // Back to tick 20 (mid-run, before the vault) and the same inputs again.
    a.replay->load(0);
    for (int t = 20; t < 150; ++t) {
        a.replay->step(inputs[static_cast<size_t>(t)], kDt);
        EXPECT_LT(glm::length(a.feet() - first[static_cast<size_t>(t)]), 1e-4f) << "tick " << t;
    }
    EXPECT_LT(first.back().z, -9.0f); // it did go over the fence
    // And step() of the world leaves a replayed character alone.
    const glm::vec3 before = a.feet();
    for (int i = 0; i < 10; ++i) a.world.step(kDt);
    EXPECT_EQ(a.feet(), before);
}

TEST(NetSession, InputReplayWithLocomotionVaultsTheSameOnBothSides) {
    ReplayMatch<LevelSim> m;
    m.net.conditions.latencyMs = 50.0f;
    m.net.conditions.jitterMs = 10.0f;
    m.net.conditions.lossPercent = 5.0f;
    LevelSim local, remote;
    auto& p = m.join(local, remote, [&local](int t) { return runAt(t, 120, &local.level); });
    m.run(5.0);
    EXPECT_TRUE(local.vaulted);
    EXPECT_TRUE(remote.vaulted);
    EXPECT_LT(local.level.feet().z, -9.0f);
    EXPECT_LT(glm::length(local.level.feet() - remote.level.feet()), 0.05f);
    EXPECT_LT(p.prediction->corrections, 10u);
}

TEST(NetSession, InputReplayAWallOnlyTheServerHasStopsTheClient) {
    // A client whose level lacks a wall (a modified map) runs through it
    // on its own screen; the server's copy stops and the client is put back.
    ReplayMatch<LevelSim> m;
    m.net.conditions.latencyMs = 40.0f;
    LevelSim local(false, false), remote(false, true);
    m.join(local, remote, [](int t) { return runAt(t, 150); });
    m.run(6.0);
    EXPECT_GT(remote.level.feet().z, -16.0f); // blocked by the wall
    EXPECT_LT(glm::length(local.level.feet() - remote.level.feet()), 0.05f);
}

#endif
