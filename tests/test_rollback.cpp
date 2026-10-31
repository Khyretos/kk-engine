// Rollback netcode (kke/Rollback.h, ENG-F55).
#include "kke/Rollback.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <memory>
#include <random>

using namespace kke;

namespace {

// A deterministic toy fighting game: two players with a position and a
// velocity, an input of a push (-2..2) and a button, and a generator that
// advances every frame (so a wrongly replayed frame changes the future).
struct Toy {
    struct State {
        int32_t frame = 0;
        int32_t pos[2] = { 0, 0 };
        int32_t vel[2] = { 0, 0 };
        uint32_t rng = 12345;
        int32_t hits = 0;
    } s;
    int cheatAt = -1; // makes this copy disagree with the other from that frame on

    void step(const std::vector<Bytes>& in) {
        for (size_t p = 0; p < 2; ++p) {
            const int push = static_cast<int8_t>(in[p][0]);
            s.vel[p] = std::clamp(s.vel[p] + push, -10, 10);
            s.pos[p] += s.vel[p];
            if (in[p][1] & 1) s.vel[p] = 0; // the brake
        }
        s.rng = s.rng * 1664525u + 1013904223u;
        if (s.pos[0] == s.pos[1]) { // the players touch: the generator decides who is thrown
            s.hits += 1;
            s.pos[(s.rng >> 16) & 1] += 3;
        }
        ++s.frame;
        if (s.frame == cheatAt) s.pos[0] += 1;
    }
    Bytes save() const {
        Bytes b(sizeof s);
        std::memcpy(b.data(), &s, sizeof s);
        return b;
    }
    void load(const Bytes& b) { std::memcpy(&s, b.data(), sizeof s); }
    RollbackGame game() {
        RollbackGame g;
        g.save = [this] { return save(); };
        g.load = [this](const Bytes& b) { load(b); };
        g.advance = [this](const std::vector<Bytes>& in) { step(in); };
        return g;
    }
};

// What player p presses on frame f (a pattern that changes often enough to make guesses wrong).
Bytes pressed(int player, int frame) {
    const int push = ((frame * 7 + player * 13 + frame / 5) % 5) - 2;
    const int button = ((frame / 9 + player) % 3 == 0) ? 1 : 0;
    return { static_cast<uint8_t>(static_cast<int8_t>(push)), static_cast<uint8_t>(button) };
}

// The game run straight through, every input known, the way it should come out.
Toy::State reference(int frames, int delay) {
    Toy t;
    for (int f = 0; f < frames; ++f) {
        std::vector<Bytes> in;
        for (int p = 0; p < 2; ++p) in.push_back(f >= delay ? pressed(p, f - delay) : Bytes{ 0, 0 });
        t.step(in);
    }
    return t.s;
}

// A one-way link that loses, doubles, delays and reorders packets.
struct Link {
    struct Msg {
        int at;
        Bytes data;
    };
    std::vector<Msg> queue;
    std::mt19937 rng{ 99 };
    int latency = 0, jitter = 0;
    double loss = 0.0, duplicate = 0.0;
    uint64_t sent = 0, dropped = 0;
    void send(const Bytes& b, int now) {
        ++sent;
        std::uniform_real_distribution<double> u(0.0, 1.0);
        if (u(rng) < loss) {
            ++dropped;
            return;
        }
        auto when = [&] { return now + latency + (jitter > 0 ? static_cast<int>(rng() % static_cast<unsigned>(jitter + 1)) : 0); };
        queue.push_back({ when(), b });
        if (u(rng) < duplicate) queue.push_back({ when() + 1, b });
    }
    void deliver(int now, RollbackSession& to) {
        std::stable_sort(queue.begin(), queue.end(), [](const Msg& a, const Msg& b) { return a.at < b.at; }); // late ones arrive late: reordering
        size_t i = 0;
        for (; i < queue.size() && queue[i].at <= now; ++i) to.receive(queue[i].data);
        queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(i));
    }
};

struct Match {
    Toy toy[2];
    RollbackSession session[2];
    std::unique_ptr<RollbackDriver> driver[2];
    Link toB, toA; // A -> B, B -> A
    int now = 0;
    int stalls[2] = { 0, 0 };

    explicit Match(const RollbackConfig& cfg) : session{ RollbackSession(cfg), RollbackSession(cfg) } {
        session[0].setLocalPlayer(0);
        session[1].setLocalPlayer(1);
        for (int i = 0; i < 2; ++i) driver[i] = std::make_unique<RollbackDriver>(session[i], toy[i].game());
    }
    void setNetwork(int latency, int jitter, double loss, double duplicate) {
        for (Link* l : { &toB, &toA }) {
            l->latency = latency;
            l->jitter = jitter;
            l->loss = loss;
            l->duplicate = duplicate;
        }
        toB.rng.seed(1);
        toA.rng.seed(2);
    }
    // One tick of the world: each machine plays a frame if it is not ahead of the limit.
    void tick(int frames) {
        for (int i = 0; i < 2; ++i) {
            if (session[i].frame() < frames) {
                if (driver[i]->tick(pressed(i, session[i].frame())) == RollbackSession::Status::PredictionLimit) ++stalls[i];
            }
            (i == 0 ? toB : toA).send(session[i].makePacket(), now);
        }
        ++now;
        toB.deliver(now, session[1]);
        toA.deliver(now, session[0]);
    }
    // Plays both machines to `frames` frames, then lets the network settle.
    void play(int frames) {
        int guard = 0;
        while ((session[0].frame() < frames || session[1].frame() < frames) && guard++ < frames * 60) tick(frames);
        // a clean line to finish: everything arrives, every correction is made
        for (int i = 0; i < 200; ++i) {
            for (int m = 0; m < 2; ++m) {
                const Bytes p = session[m].makePacket();
                session[1 - m].receive(p);
            }
            driver[0]->settle();
            driver[1]->settle();
            if (session[0].confirmedFrame() + 1 >= frames && session[1].confirmedFrame() + 1 >= frames) {
                driver[0]->settle();
                driver[1]->settle();
                break;
            }
        }
    }
};

bool same(const Toy::State& a, const Toy::State& b) { return std::memcmp(&a, &b, sizeof a) == 0; }

} // namespace

TEST(Rollback, TheFirstTickSavesThenAdvancesWithAGuessForTheOtherPlayer) {
    RollbackConfig cfg;
    cfg.inputSize = 2;
    RollbackSession s(cfg);
    ASSERT_TRUE(s.setLocalPlayer(0));
    EXPECT_FALSE(s.setLocalPlayer(1)) << "only once";
    ASSERT_TRUE(s.addLocalInput({ 3, 0 }));
    std::vector<RollbackRequest> out;
    ASSERT_EQ(s.advance(out), RollbackSession::Status::Ok);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].kind, RollbackRequest::Kind::Save);
    EXPECT_EQ(out[0].frame, 0);
    EXPECT_EQ(out[1].kind, RollbackRequest::Kind::Advance);
    EXPECT_EQ(out[1].frame, 0);
    ASSERT_EQ(out[1].inputs.size(), 2u);
    EXPECT_FALSE(out[1].predicted[0]) << "our own: the delay frames are known";
    EXPECT_TRUE(out[1].predicted[1]) << "theirs: a guess";
    EXPECT_EQ(out[1].inputs[1], (Bytes{ 0, 0 })) << "nothing seen yet: nothing pressed";
    EXPECT_EQ(s.frame(), 1);
    Bytes b;
    bool predicted = false;
    ASSERT_TRUE(s.inputFor(1, 0, b, &predicted));
    EXPECT_TRUE(predicted);
}

TEST(Rollback, AGuessThatWasRightCostsNothingAndOneThatWasWrongIsReplayed) {
    RollbackConfig cfg;
    cfg.inputSize = 1;
    cfg.inputDelay = 0;
    RollbackSession a(cfg), b(cfg);
    a.setLocalPlayer(0);
    b.setLocalPlayer(1);
    std::vector<RollbackRequest> out;
    // A plays 3 frames alone; B has pressed 5 on frame 0 and then nothing new
    for (int i = 0; i < 3; ++i) {
        a.addLocalInput({ 1 });
        out.clear();
        ASSERT_EQ(a.advance(out), RollbackSession::Status::Ok);
    }
    b.addLocalInput({ 5 });
    out.clear();
    b.advance(out); // B's frame 0 input is 5
    ASSERT_TRUE(a.receive(b.makePacket()));
    // frame 0: A guessed 0 for B, the truth is 5: a mistake
    a.addLocalInput({ 1 });
    out.clear();
    ASSERT_EQ(a.advance(out), RollbackSession::Status::Ok);
    ASSERT_GE(out.size(), 3u);
    EXPECT_EQ(out[0].kind, RollbackRequest::Kind::Load);
    EXPECT_EQ(out[0].frame, 0) << "back to before the mistake";
    // frames 0, 1, 2 re-run (Advance 0 right after the load, a Save before each of the others), then the new frame 3
    std::vector<std::pair<char, int>> shape;
    for (const auto& r : out) shape.emplace_back("SLA"[static_cast<int>(r.kind)], r.frame);
    const std::vector<std::pair<char, int>> expected = { { 'L', 0 }, { 'A', 0 }, { 'S', 1 }, { 'A', 1 }, { 'S', 2 }, { 'A', 2 }, { 'S', 3 }, { 'A', 3 } };
    EXPECT_EQ(shape, expected);
    EXPECT_EQ(out[1].inputs[1], (Bytes{ 5 })) << "the re-run of frame 0 has the real input";
    EXPECT_FALSE(out[1].predicted[1]);
    EXPECT_TRUE(out[3].predicted[1]) << "frame 1 is still a guess (we only know frame 0): their last, 5";
    EXPECT_EQ(out[3].inputs[1], (Bytes{ 5 }));
    EXPECT_EQ(a.stats().rollbacks, 1u);
    EXPECT_EQ(a.stats().framesResimulated, 3u);
    EXPECT_EQ(a.stats().mispredictions, 1u);
    // now B presses 5 for frame 1 too: A's guess for frame 1 was 5, so nothing to redo
    b.addLocalInput({ 5 });
    out.clear();
    b.advance(out);
    ASSERT_TRUE(a.receive(b.makePacket()));
    a.addLocalInput({ 1 });
    out.clear();
    ASSERT_EQ(a.advance(out), RollbackSession::Status::Ok);
    EXPECT_EQ(out.size(), 2u) << "a guess that was right: just the new frame";
    EXPECT_EQ(a.stats().rollbacks, 1u);
}

TEST(Rollback, BothMachinesEndUpWithTheStateASequentialRunHas) {
    struct Case {
        const char* name;
        int latency, jitter, delay, prediction;
        double loss, dup;
    };
    for (const Case& c : { Case{ "same tick", 0, 0, 2, 8, 0.0, 0.0 }, Case{ "lag", 5, 0, 2, 12, 0.0, 0.0 }, Case{ "lag and jitter", 5, 4, 2, 16, 0.0, 0.0 },
                           Case{ "loss", 3, 2, 2, 16, 0.25, 0.0 }, Case{ "loss, doubles and reordering", 6, 5, 2, 20, 0.2, 0.2 },
                           Case{ "no input delay", 4, 2, 0, 12, 0.05, 0.05 }, Case{ "a lot of delay", 4, 2, 6, 8, 0.1, 0.0 } }) {
        RollbackConfig cfg;
        cfg.inputSize = 2;
        cfg.inputDelay = c.delay;
        cfg.maxPrediction = c.prediction;
        cfg.checksumInterval = 5;
        Match m(cfg);
        m.setNetwork(c.latency, c.jitter, c.loss, c.dup);
        m.play(150);
        ASSERT_EQ(m.session[0].frame(), 150) << c.name;
        ASSERT_EQ(m.session[1].frame(), 150) << c.name;
        const Toy::State want = reference(150, c.delay);
        EXPECT_TRUE(same(m.toy[0].s, want)) << c.name << ": A, frame " << m.toy[0].s.frame << " pos " << m.toy[0].s.pos[0] << "," << m.toy[0].s.pos[1];
        EXPECT_TRUE(same(m.toy[1].s, want)) << c.name << ": B";
        for (int i = 0; i < 2; ++i)
            for (const RollbackEvent& e : m.session[i].takeEvents()) EXPECT_NE(e.kind, RollbackEvent::Kind::Desync) << c.name << ": a false desync at frame " << e.frame;
    }
}

TEST(Rollback, ALaggyLineRollsBackAndAGoodOneBarelyDoes) {
    auto rollbacks = [](int latency) {
        RollbackConfig cfg;
        cfg.inputDelay = 2;
        cfg.maxPrediction = 16;
        Match m(cfg);
        m.setNetwork(latency, 0, 0.0, 0.0);
        m.play(200);
        return m.session[0].stats().rollbacks + m.session[1].stats().rollbacks;
    };
    const uint64_t good = rollbacks(1), bad = rollbacks(8);
    EXPECT_GT(bad, good) << "more lag, more corrections";
    EXPECT_GT(bad, 10u);
}

TEST(Rollback, TheSimulationWaitsWhenItIsTooFarAheadOfTheOtherPlayersInputs) {
    RollbackConfig cfg;
    cfg.inputDelay = 1;
    cfg.maxPrediction = 4;
    RollbackSession s(cfg);
    s.setLocalPlayer(0);
    int ok = 0, stalled = 0;
    std::vector<RollbackRequest> out;
    for (int i = 0; i < 20; ++i) { // nothing ever arrives from player 1
        s.addLocalInput({ 1, 0 });
        out.clear();
        const auto status = s.advance(out);
        if (status == RollbackSession::Status::Ok) ++ok;
        else {
            ++stalled;
            EXPECT_TRUE(out.empty()) << "a stalled tick asks for nothing";
        }
    }
    EXPECT_EQ(ok, 4) << "four frames of guessing, then it waits";
    EXPECT_EQ(stalled, 16);
    EXPECT_EQ(s.frame(), 4);
    EXPECT_EQ(s.stats().stalls, 16u);
}

TEST(Rollback, ALongStallDoesNotLoseTheInputs) {
    RollbackConfig cfg;
    cfg.maxPrediction = 4;
    Match m(cfg);
    m.setNetwork(30, 0, 0.0, 0.0); // a thirty tick line: both must stall
    m.play(60);
    EXPECT_GT(m.stalls[0] + m.stalls[1], 0);
    const Toy::State want = reference(60, cfg.inputDelay);
    EXPECT_TRUE(same(m.toy[0].s, want));
    EXPECT_TRUE(same(m.toy[1].s, want));
}

TEST(Rollback, AMachineThatDisagreesIsCaught) {
    RollbackConfig cfg;
    cfg.checksumInterval = 5;
    Match m(cfg);
    m.toy[1].cheatAt = 42; // B's game is not deterministic from frame 42
    m.setNetwork(2, 1, 0.0, 0.0);
    m.play(120);
    int desyncs = 0;
    int firstFrame = 1 << 30;
    for (int i = 0; i < 2; ++i)
        for (const RollbackEvent& e : m.session[i].takeEvents())
            if (e.kind == RollbackEvent::Kind::Desync) {
                ++desyncs;
                firstFrame = std::min(firstFrame, e.frame);
                EXPECT_NE(e.local, e.remote);
            }
    EXPECT_GT(desyncs, 0);
    EXPECT_GE(firstFrame, 42) << "not before it happened";
    EXPECT_LE(firstFrame, 42 + 10 + 5) << "and soon after (the checksum interval and the lag)";
}

TEST(Rollback, TheFasterMachineIsToldToWait) {
    RollbackConfig cfg;
    cfg.maxPrediction = 30;
    Match m(cfg);
    m.setNetwork(2, 0, 0.0, 0.0);
    // A runs two ticks for every one of B's
    int waitA = 0, waitB = 0;
    for (int t = 0; t < 120; ++t) {
        for (int k = 0; k < 2; ++k) {
            m.driver[0]->tick(pressed(0, m.session[0].frame()));
            m.toB.send(m.session[0].makePacket(), m.now);
        }
        m.driver[1]->tick(pressed(1, m.session[1].frame()));
        m.toA.send(m.session[1].makePacket(), m.now);
        ++m.now;
        m.toB.deliver(m.now, m.session[1]);
        m.toA.deliver(m.now, m.session[0]);
        waitA = std::max(waitA, m.session[0].recommendedWait());
        waitB = std::max(waitB, m.session[1].recommendedWait());
    }
    EXPECT_GT(waitA, 0) << "A is ahead: it should wait";
    EXPECT_EQ(waitB, 0) << "B is behind: it never waits";
    EXPECT_LE(waitA, 8);
}

TEST(Rollback, ADisconnectedPlayerPlaysZeroesAndTheGameCarriesOn) {
    RollbackConfig cfg;
    cfg.inputDelay = 1;
    cfg.maxPrediction = 4;
    RollbackSession s(cfg);
    s.setLocalPlayer(0);
    std::vector<RollbackRequest> out;
    for (int i = 0; i < 4; ++i) {
        s.addLocalInput({ 1, 0 });
        out.clear();
        s.advance(out);
    }
    s.addLocalInput({ 1, 0 });
    out.clear();
    EXPECT_EQ(s.advance(out), RollbackSession::Status::PredictionLimit);
    EXPECT_FALSE(s.disconnectPlayer(0)) << "not ourselves";
    EXPECT_FALSE(s.disconnectPlayer(5));
    ASSERT_TRUE(s.disconnectPlayer(1, 0));
    EXPECT_TRUE(s.disconnected(1));
    EXPECT_FALSE(s.disconnectPlayer(1)) << "once";
    const auto events = s.takeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, RollbackEvent::Kind::Disconnected);
    EXPECT_EQ(events[0].player, 1);
    for (int i = 0; i < 6; ++i) {
        s.addLocalInput({ 1, 0 });
        out.clear();
        ASSERT_EQ(s.advance(out), RollbackSession::Status::Ok) << "no more waiting for the one who left";
    }
    Bytes b;
    bool predicted = true;
    ASSERT_TRUE(s.inputFor(1, s.frame() - 1, b, &predicted));
    EXPECT_EQ(b, (Bytes{ 0, 0 }));
    EXPECT_FALSE(predicted) << "zeroes are settled, not guessed";
    EXPECT_EQ(s.confirmedFrame(), s.frame() + cfg.inputDelay - 1);
}

TEST(Rollback, PacketsThatAreNotValidAreRefusedAndChangeNothing) {
    RollbackConfig cfg;
    cfg.inputSize = 2;
    RollbackSession a(cfg), b(cfg);
    a.setLocalPlayer(0);
    b.setLocalPlayer(1);
    std::vector<RollbackRequest> out;
    for (int i = 0; i < 5; ++i) {
        b.addLocalInput({ 1, 0 });
        out.clear();
        b.advance(out);
    }
    const Bytes good = b.makePacket();
    ASSERT_TRUE(a.receive(good));
    EXPECT_EQ(a.stats().packetsReceived, 1u);
    const int confirmedBefore = a.confirmedFrame();
    auto refused = [&](Bytes p, const char* what) {
        const uint64_t before = a.stats().packetsRefused;
        EXPECT_FALSE(a.receive(p)) << what;
        EXPECT_EQ(a.stats().packetsRefused, before + 1) << what;
    };
    refused({}, "empty");
    refused({ 'R' }, "one byte");
    Bytes wrongMagic = good;
    wrongMagic[0] = 'X';
    refused(wrongMagic, "magic");
    Bytes wrongVersion = good;
    wrongVersion[1] = 9;
    refused(wrongVersion, "version");
    Bytes fromSelf = good;
    fromSelf[2] = 0; // says it is from player 0: us
    refused(fromSelf, "from ourselves");
    Bytes fromNobody = good;
    fromNobody[2] = 7;
    refused(fromNobody, "no such player");
    Bytes wrongPlayers = good;
    wrongPlayers[12] = 5; // the player count
    refused(wrongPlayers, "player count");
    Bytes truncated(good.begin(), good.end() - 3);
    refused(truncated, "cut short");
    Bytes extra = good;
    extra.push_back(0);
    refused(extra, "extra bytes");
    refused(Bytes(RollbackSession::kMaxPacket + 1, 0), "too big");
    // an absurd frame range
    Bytes far = good;
    const size_t startAt = 4 + 4 + 4 + 1 + 4 * 2;
    far[startAt] = 0xFF;
    far[startAt + 1] = 0xFF;
    far[startAt + 2] = 0xFF;
    far[startAt + 3] = 0x7F;
    refused(far, "a frame far in the future");
    EXPECT_EQ(a.confirmedFrame(), confirmedBefore) << "nothing of the refused ones was used";
    // and it still takes good ones
    EXPECT_TRUE(a.receive(good));
    // an unconfigured session takes nothing
    RollbackSession none(cfg);
    EXPECT_FALSE(none.receive(good));
    EXPECT_TRUE(none.makePacket().empty());
}

TEST(Rollback, RandomAndDamagedPacketsNeverCrash) {
    RollbackConfig cfg;
    RollbackSession a(cfg), b(cfg);
    a.setLocalPlayer(0);
    b.setLocalPlayer(1);
    std::vector<RollbackRequest> out;
    for (int i = 0; i < 20; ++i) {
        b.addLocalInput({ static_cast<uint8_t>(i), 0 });
        out.clear();
        b.advance(out);
    }
    const Bytes good = b.makePacket();
    std::mt19937 rng(5);
    for (int i = 0; i < 3000; ++i) {
        Bytes p = good;
        const int hits = 1 + static_cast<int>(rng() % 6);
        for (int k = 0; k < hits; ++k) p[rng() % p.size()] = static_cast<uint8_t>(rng());
        if (rng() % 4 == 0) p.resize(rng() % p.size());
        a.receive(p);
        if (i % 50 == 0) {
            a.addLocalInput({ 1, 0 });
            out.clear();
            a.advance(out);
        }
    }
    Bytes noise(300);
    for (int i = 0; i < 1000; ++i) {
        for (uint8_t& x : noise) x = static_cast<uint8_t>(rng());
        a.receive(noise);
    }
    SUCCEED();
}

TEST(Rollback, ConfigurationIsKeptInRange) {
    RollbackConfig cfg;
    cfg.players = 99;
    cfg.inputSize = 0;
    cfg.inputDelay = 100;
    cfg.maxPrediction = 0;
    cfg.checksumInterval = -5;
    RollbackSession s(cfg);
    EXPECT_EQ(s.config().players, 8);
    EXPECT_EQ(s.config().inputSize, 1);
    EXPECT_EQ(s.config().inputDelay, 16);
    EXPECT_EQ(s.config().maxPrediction, 1);
    EXPECT_EQ(s.config().checksumInterval, 0);
    EXPECT_EQ(s.snapshotSlots(), 3);
    EXPECT_FALSE(s.setLocalPlayer(8));
    EXPECT_FALSE(s.setLocalPlayer(-1));
    std::vector<RollbackRequest> out;
    EXPECT_EQ(s.advance(out), RollbackSession::Status::NoLocalPlayer);
    ASSERT_TRUE(s.setLocalPlayer(3));
    EXPECT_FALSE(s.addLocalInput({ 1, 2 })) << "the wrong size";
    EXPECT_TRUE(s.addLocalInput({ 1 }));
}

TEST(Rollback, MorePlayersThanTwoWork) {
    RollbackConfig cfg;
    cfg.players = 3;
    cfg.inputSize = 1;
    cfg.inputDelay = 1;
    std::vector<std::unique_ptr<RollbackSession>> s;
    for (int i = 0; i < 3; ++i) {
        s.push_back(std::make_unique<RollbackSession>(cfg));
        s[static_cast<size_t>(i)]->setLocalPlayer(i);
    }
    std::vector<std::vector<RollbackRequest>> out(3);
    auto run = [&](int ticks) {
        for (int t = 0; t < ticks; ++t) {
            for (int i = 0; i < 3; ++i) {
                s[static_cast<size_t>(i)]->addLocalInput({ static_cast<uint8_t>((t + i) % 7) });
                out[static_cast<size_t>(i)].clear();
                s[static_cast<size_t>(i)]->advance(out[static_cast<size_t>(i)]);
            }
            for (int i = 0; i < 3; ++i) {
                const Bytes p = s[static_cast<size_t>(i)]->makePacket();
                for (int j = 0; j < 3; ++j)
                    if (j != i) s[static_cast<size_t>(j)]->receive(p);
            }
        }
    };
    run(30);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(s[static_cast<size_t>(i)]->frame(), 30);
        EXPECT_GE(s[static_cast<size_t>(i)]->confirmedFrame(), 25);
    }
    // every machine used the same input for every player on a frame everyone has confirmed
    for (int f = 16; f < 23; ++f) // (the oldest frames are forgotten: only the last 16 are kept)
        for (int p = 0; p < 3; ++p) {
            Bytes a, b, c;
            ASSERT_TRUE(s[0]->inputFor(p, f, a));
            ASSERT_TRUE(s[1]->inputFor(p, f, b));
            ASSERT_TRUE(s[2]->inputFor(p, f, c));
            EXPECT_EQ(a, b);
            EXPECT_EQ(b, c);
        }
}

TEST(RollbackSyncTest, ADeterministicGamePasses) {
    Toy t;
    std::string report;
    EXPECT_TRUE(rollbackSyncTest(
        t.game(), 2, 120, 4, [](int f) { return std::vector<Bytes>{ pressed(0, f), pressed(1, f) }; }, &report))
        << report;
    EXPECT_EQ(t.s.frame, 120 + 0) << "and it ends where a plain run does";
}

TEST(RollbackSyncTest, AGameThatReadsSomethingOutsideItsStateIsCaught) {
    // the frame counter lives outside the saved state: a replay sees a different value
    Toy t;
    int outside = 0;
    RollbackGame g = t.game();
    g.advance = [&](const std::vector<Bytes>& in) {
        t.step(in);
        t.s.pos[0] += (++outside) % 2 == 0 ? 1 : 0; // odd number of re-runs shifts the parity: a replay differs
    };
    std::string report;
    EXPECT_FALSE(rollbackSyncTest(g, 2, 60, 3, [](int f) { return std::vector<Bytes>{ pressed(0, f), pressed(1, f) }; }, &report));
    EXPECT_NE(report.find("the state at frame"), std::string::npos) << report;
    EXPECT_NE(report.find("differs after loading and re-running"), std::string::npos) << report;
}

TEST(RollbackSyncTest, AStateThatChangesSizeIsReported) {
    Bytes blob(4, 0);
    int frame = 0;
    RollbackGame g;
    g.save = [&] { return blob; };
    g.load = [&](const Bytes& b) { blob = b; };
    g.advance = [&](const std::vector<Bytes>&) {
        blob.push_back(static_cast<uint8_t>(frame++)); // grows by one a frame, and the replay grows it again
    };
    std::string report;
    EXPECT_FALSE(rollbackSyncTest(g, 1, 20, 2, [](int) { return std::vector<Bytes>{ Bytes{ 0 } }; }, &report));
    EXPECT_NE(report.find("differs"), std::string::npos) << report;
}

TEST(RollbackDriver, KeepsOnlyAsManySnapshotsAsItNeedsAndChecksumsEverySave) {
    RollbackConfig cfg;
    cfg.maxPrediction = 4;
    cfg.inputDelay = 1;
    Toy t;
    RollbackSession s(cfg);
    s.setLocalPlayer(0);
    uint64_t checks = 0;
    RollbackGame g = t.game();
    g.checksum = [&](const Bytes& b) {
        ++checks;
        return fnv1a(b);
    };
    RollbackDriver d(s, g);
    int ok = 0;
    for (int i = 0; i < 12; ++i) ok += d.tick({ 1, 0 }) == RollbackSession::Status::Ok;
    EXPECT_EQ(ok, 4);
    EXPECT_EQ(checks, 4u) << "one checksum per Save";
    EXPECT_EQ(fnv1a({}), 1469598103934665603ull);
    EXPECT_NE(fnv1a({ 1 }), fnv1a({ 2 }));
}


