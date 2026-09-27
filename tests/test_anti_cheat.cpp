// The anti-cheat building blocks (docs/ANTI_CHEAT.md): per-role server
// authority, input sanity checks, sealed game data, shipping builds.

#include "kke/DevTools.h"
#include "kke/InputSanity.h"
#include "kke/PackSeal.h"
#include "kke/net/Authority.h"
#include "kke/net/NetSession.h"
#include "kke/net/Transport.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <algorithm>
#include <system_error>

using namespace kke::net;
using kke::InputSanity;
namespace seal = kke::seal;

// ------------------------------------------------------------ authority

TEST(AuthorityPolicy, DefaultsAreTodaysBehaviour) {
    AuthorityPolicy p;
    EXPECT_EQ(p.authority(3, Action::Move), Authority::Checked);
    EXPECT_EQ(p.authority(3, Action::Event), Authority::Checked);
    EXPECT_EQ(p.roleOf(3), "player");
    EXPECT_EQ(p.roleCount(), 1u);
}

TEST(AuthorityPolicy, PresetsRolesAndAssignment) {
    AuthorityPolicy coop = AuthorityPolicy::coop();
    EXPECT_EQ(coop.authority(1, Action::Event), Authority::Client);
    AuthorityPolicy p = AuthorityPolicy::competitive();
    EXPECT_EQ(p.authority(1, Action::Event), Authority::Checked);
    ASSERT_TRUE(p.assign(1, "spectator"));
    EXPECT_EQ(p.authority(1, Action::Move), Authority::Server);
    EXPECT_EQ(p.authority(1, Action::Event), Authority::Server);
    EXPECT_EQ(p.authority(2, Action::Move), Authority::Checked) << "only that player";
    EXPECT_FALSE(p.assign(2, "admin")) << "unknown roles are refused";
    EXPECT_EQ(p.roleOf(2), "player");
    EXPECT_TRUE(p.setRole("builder", { Authority::Checked, Authority::Client }));
    EXPECT_TRUE(p.assign(2, "builder"));
    EXPECT_EQ(p.authority(2, Action::Event), Authority::Client);
    EXPECT_FALSE(p.setRole("", {}));
    EXPECT_FALSE(p.setRole("has space", {}));
    EXPECT_FALSE(p.setRole(std::string(33, 'x'), {}));
    p.forget(1);
    EXPECT_EQ(p.roleOf(1), "player");
    EXPECT_TRUE(p.assign(2, "player"));
    EXPECT_EQ(p.roleOf(2), "player");
    EXPECT_STREQ(toString(Authority::Server), "server");
}

namespace {

struct Match {
    LoopbackNetwork net;
    LoopbackTransport serverT{ net };
    NetServer server{ serverT };
    std::vector<std::unique_ptr<LoopbackTransport>> clientT;
    std::vector<std::unique_ptr<NetClient>> clients;
    double now = 0.0;

    explicit Match(size_t n) {
        EXPECT_TRUE(server.start(5000, "Host", ""));
        for (size_t i = 0; i < n; ++i) {
            clientT.push_back(std::make_unique<LoopbackTransport>(net));
            clients.push_back(std::make_unique<NetClient>(*clientT.back()));
            EXPECT_TRUE(clients.back()->connect("localhost", 5000, "P" + std::to_string(i), ""));
        }
    }
    void step() {
        now += 1.0 / 60.0;
        net.advance(1.0 / 60.0);
        server.update(now);
        for (auto& c : clients) c->update(now);
    }
    void run(double seconds) {
        for (double t = 0; t < seconds; t += 1.0 / 60.0) step();
    }
    NetPlayerState hostSees(uint8_t id) {
        for (const RemotePlayer& p : server.players(now))
            if (p.id == id) return p.state;
        ADD_FAILURE() << "player " << int(id) << " not seen";
        return {};
    }
};

NetPlayerState at(float x) {
    NetPlayerState s;
    s.position = glm::vec3(x, 0, 0);
    return s;
}

} // namespace

TEST(AuthorityInNetServer, ServerAuthorityDropsAClientsMovesAndEvents) {
    Match m(2);
    m.server.authority = AuthorityPolicy::competitive();
    m.run(0.5);
    const uint8_t spectator = m.clients[1]->playerId();
    ASSERT_TRUE(m.server.authority.assign(spectator, "spectator"));
    std::vector<GameEventMsg> seen;
    m.server.onEvent = [&](const GameEventMsg& e) { seen.push_back(e); };
    m.clients[0]->setLocalState(at(1));
    m.clients[1]->setLocalState(at(2));
    m.clients[0]->sendEvent(7, { 1 });
    m.clients[1]->sendEvent(7, { 2 });
    m.run(0.5);
    ASSERT_EQ(seen.size(), 1u) << "the spectator's event is dropped";
    EXPECT_EQ(seen[0].fromPlayer, m.clients[0]->playerId());
    EXPECT_EQ(m.server.refusedEvents(), 1u);
    bool spectatorSeen = false;
    for (const RemotePlayer& p : m.server.players(m.now)) spectatorSeen |= p.id == spectator && p.hasState;
    EXPECT_FALSE(spectatorSeen) << "a spectator's own moves never become a player";
}

TEST(AuthorityInNetServer, CheckedEventsGoThroughTheGamesCheck) {
    Match m(1);
    m.run(0.5);
    std::vector<uint16_t> seen;
    m.server.onEvent = [&](const GameEventMsg& e) { seen.push_back(e.kind); };
    // "Damage" (kind 1) only ever comes from the server in this game.
    m.server.checkEvent = [](uint8_t, const GameEventMsg& e) { return e.kind != 1; };
    m.clients[0]->sendEvent(1, { 99 });
    m.clients[0]->sendEvent(2, {});
    m.run(0.3);
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0], 2);
    EXPECT_EQ(m.server.refusedEvents(), 1u);
    // A role whose events are Client authority skips the check.
    m.server.authority.setRole("trusted", { Authority::Checked, Authority::Client });
    ASSERT_TRUE(m.server.authority.assign(m.clients[0]->playerId(), "trusted"));
    m.clients[0]->sendEvent(1, {});
    m.run(0.3);
    EXPECT_EQ(seen.size(), 2u);
}

TEST(AuthorityInNetServer, ClientAuthorityMovesSkipTheChecks) {
    Match m(1);
    m.run(0.5);
    const uint8_t id = m.clients[0]->playerId();
    m.clients[0]->setLocalState(at(1));
    m.run(0.5);
    // Checked (the default): 100 m in a step is refused.
    m.clients[0]->setLocalState(at(101));
    m.run(0.5);
    EXPECT_NEAR(m.hostSees(id).position.x, 1.0f, 0.01f);
    // Client authority (a level editor role, say): taken as is.
    m.server.authority.setRole("editor", { Authority::Client, Authority::Client });
    ASSERT_TRUE(m.server.authority.assign(id, "editor"));
    m.clients[0]->setLocalState(at(301));
    m.run(0.5);
    EXPECT_NEAR(m.hostSees(id).position.x, 301.0f, 0.01f);
}

TEST(AuthorityInNetServer, RolesAreForgottenWhenAPlayerLeaves) {
    Match m(1);
    m.run(0.5);
    const uint8_t id = m.clients[0]->playerId();
    m.server.authority = AuthorityPolicy::competitive();
    ASSERT_TRUE(m.server.authority.assign(id, "spectator"));
    m.clients[0]->disconnect();
    m.run(0.5);
    EXPECT_EQ(m.server.authority.roleOf(id), "player") << "the next player with this id starts fresh";
}

// ------------------------------------------------------------ input sanity

namespace {

// A person mashing one button at ~`rate` a second: 10-30 ms of drift.
void humanMash(InputSanity& s, uint32_t control, double rate, int presses, double& t, std::mt19937& rng) {
    std::normal_distribution<double> drift(0.0, 0.018);
    for (int i = 0; i < presses; ++i) {
        s.button(control, true, t);
        s.button(control, false, t + 0.03);
        t += std::max(0.045, 1.0 / rate + drift(rng));
    }
}

} // namespace

TEST(InputSanity, HumansPass) {
    InputSanity s;
    std::mt19937 rng(7);
    double t = 1.0;
    humanMash(s, 1, 12.0, 80, t, rng);
    // Walking about: a few keys held and released at human times.
    std::uniform_real_distribution<double> gap(0.08, 0.6);
    for (int i = 0; i < 200; ++i) {
        const uint32_t key = 10 + static_cast<uint32_t>(i % 4);
        s.button(key, true, t);
        t += gap(rng);
        s.button(key, false, t);
        t += gap(rng);
    }
    // A thumb on a stick: noisy even when "still".
    std::normal_distribution<double> noise(0.0, 0.003);
    for (int i = 0; i < 600; ++i) {
        s.axis(100, static_cast<float>(0.5 + noise(rng)), t);
        t += 0.008;
        s.update(t);
    }
    // Reactions: 180-300 ms, with the odd lucky fast one.
    std::uniform_real_distribution<double> react(0.18, 0.3);
    for (int i = 0; i < 30; ++i) s.reaction(i % 10 == 0 ? 0.09 : react(rng), t + i);
    EXPECT_EQ(s.total(), 0u);
    for (const auto& f : s.findings()) ADD_FAILURE() << toString(f.kind) << ": " << f.detail;
    EXPECT_FLOAT_EQ(s.suspicion(), 0.0f);
}

TEST(InputSanity, TurboAndImpossibleRatesAreFlagged) {
    InputSanity s;
    std::vector<InputSanity::Finding> live;
    s.onFinding = [&](const InputSanity::Finding& f) { live.push_back(f); };
    double t = 0.0;
    for (int i = 0; i < 60; ++i) { // a rapid-fire mod: 15 Hz, dead regular
        s.button(5, true, t);
        s.button(5, false, t + 0.02);
        t += 1.0 / 15.0;
    }
    EXPECT_EQ(s.count(InputSanity::Kind::Turbo), 1u) << "once per cooldown, not per press";
    ASSERT_FALSE(live.empty());
    EXPECT_EQ(live[0].control, 5u);
    for (int i = 0; i < 60; ++i) { // 40 presses a second: nobody
        s.button(6, true, t);
        s.button(6, false, t + 0.01);
        t += 1.0 / 40.0 + (i % 2 ? 0.004 : -0.004);
    }
    EXPECT_EQ(s.count(InputSanity::Kind::ImpossibleRate), 1u);
    EXPECT_GT(s.suspicion(), 0.5f);
}

TEST(InputSanity, ReplayedMacrosAreFlagged) {
    InputSanity s;
    double t = 0.0;
    // A fighting-game combo on a macro key: down, down-forward, forward, punch.
    const uint32_t seq[] = { 20, 21, 22, 23 };
    const double gaps[] = { 0.05, 0.033, 0.041, 0.07 };
    for (int rep = 0; rep < 4; ++rep) {
        for (int i = 0; i < 4; ++i) {
            s.button(seq[i], true, t);
            t += 0.016;
            s.button(seq[i], false, t);
            t += gaps[i];
        }
        t += 0.4;
    }
    EXPECT_GE(s.count(InputSanity::Kind::Macro), 1u);

    // The same combo by hand: close, but people are 10+ ms off each time.
    InputSanity human;
    std::mt19937 rng(3);
    std::normal_distribution<double> jitter(0.0, 0.012);
    t = 0.0;
    for (int rep = 0; rep < 10; ++rep) {
        for (int i = 0; i < 4; ++i) {
            human.button(seq[i], true, t);
            t += std::max(0.005, 0.016 + jitter(rng));
            human.button(seq[i], false, t);
            t += std::max(0.005, gaps[i] + jitter(rng));
        }
        t += 0.4;
    }
    EXPECT_EQ(human.count(InputSanity::Kind::Macro), 0u);
}

TEST(InputSanity, NoiselessSticksReactionBotsAndForgedValues) {
    InputSanity s;
    s.axis(9, 0.25f, 1.0); // anti-recoil script: exactly a quarter down, then nothing
    s.update(2.0);
    EXPECT_EQ(s.count(InputSanity::Kind::SteadyAxis), 0u);
    s.update(3.5);
    EXPECT_EQ(s.count(InputSanity::Kind::SteadyAxis), 1u);
    s.update(9.0);
    EXPECT_EQ(s.count(InputSanity::Kind::SteadyAxis), 1u) << "one finding per steady run";
    s.axis(10, 0.0f, 1.0); // at rest, or pushed to the edge: normal
    s.axis(11, 1.0f, 1.0);
    s.update(20.0);
    EXPECT_EQ(s.count(InputSanity::Kind::SteadyAxis), 1u);

    for (int i = 0; i < 10; ++i) s.reaction(0.04, 30.0 + i);
    EXPECT_EQ(s.count(InputSanity::Kind::InhumanReaction), 1u);
    s.reaction(-0.2, 50.0); // a guess before the cue: ignored

    s.axis(12, std::nanf(""), 60.0);
    s.axis(13, 3.0f, 70.0);
    s.button(14, true, 80.0);
    s.button(14, false, 79.0); // time going backwards
    EXPECT_EQ(s.count(InputSanity::Kind::BadValue), 3u);
    s.reset();
    EXPECT_EQ(s.total(), 0u);
    EXPECT_TRUE(s.findings().empty());
}

// ------------------------------------------------------------ pack seal

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() / ("kke_seal_test_" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(path / "scripts");
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    void write(const std::string& rel, const std::string& text) const {
        std::ofstream f(path / rel, std::ios::binary | std::ios::trunc);
        f << text;
    }
};

seal::KeyPair testKeys(uint8_t fill) {
    std::array<uint8_t, 32> seed{};
    seed.fill(fill);
    return seal::keyPairFromSeed(seed);
}

} // namespace

TEST(PackSeal, KnownHashAndHexRoundTrip) {
    // BLAKE2b-256 of "abc" (RFC 7693 test vectors use 512; this is the 256-bit one).
    const seal::Hash h = seal::hashBytes("abc", 3);
    EXPECT_EQ(seal::toHex(h), "bddd813c634239723171ef3fee98579b94964e3bb1cb3e427262c8c068d52319");
    seal::Hash back{};
    ASSERT_TRUE(seal::fromHex(seal::toHex(h), back));
    EXPECT_EQ(back, h);
    EXPECT_FALSE(seal::fromHex("abc", back));
    EXPECT_FALSE(seal::fromHex(std::string(64, 'g'), back));
    const seal::KeyPair a = testKeys(1), b = testKeys(1);
    EXPECT_EQ(a.publicKey, b.publicKey) << "same seed, same keys";
    seal::KeyPair fresh;
    std::string error;
    ASSERT_TRUE(seal::generateKeyPair(fresh, &error)) << error;
    EXPECT_NE(fresh.publicKey, a.publicKey);
}

TEST(PackSeal, SealedFolderVerifiesAndEveryKindOfTamperingShows) {
    TempDir dir;
    dir.write("level.json", "{\"walls\": 12}");
    dir.write("scripts/weapon.lua", "damage = 10");
    dir.write("scripts/ui.lua", "print('hi')");
    const seal::KeyPair keys = testKeys(7);

    seal::Manifest m;
    std::string error;
    ASSERT_TRUE(seal::build(dir.path, m, &error)) << error;
    EXPECT_EQ(m.files.size(), 3u);
    EXPECT_TRUE(m.files.count("scripts/weapon.lua"));
    seal::sign(m, keys.secret);
    ASSERT_TRUE(seal::save(m, dir.path / seal::kSealFileName, &error)) << error;

    seal::Report r = seal::verify(dir.path, keys.publicKey);
    EXPECT_TRUE(r.ok()) << r.summary();
    EXPECT_EQ(r.summary(), "seal ok");

    // Someone else's key: the signature doesn't hold.
    r = seal::verify(dir.path, testKeys(8).publicKey);
    EXPECT_FALSE(r.signatureOk);
    EXPECT_FALSE(r.ok());

    // A cheat edits a script, deletes a level file, drops in a new one.
    dir.write("scripts/weapon.lua", "damage = 9999");
    std::filesystem::remove(dir.path / "level.json");
    dir.write("scripts/aimbot.lua", "--");
    r = seal::verify(dir.path, keys.publicKey);
    EXPECT_TRUE(r.signatureOk);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.modified, std::vector<std::string>{ "scripts/weapon.lua" });
    EXPECT_EQ(r.missing, std::vector<std::string>{ "level.json" });
    EXPECT_EQ(r.added, std::vector<std::string>{ "scripts/aimbot.lua" });

    // Rewriting the seal to match doesn't help without the private key.
    seal::Manifest forged;
    ASSERT_TRUE(seal::build(dir.path, forged, &error));
    forged.signature = m.signature;
    ASSERT_TRUE(seal::save(forged, dir.path / seal::kSealFileName, &error));
    r = seal::verify(dir.path, keys.publicKey);
    EXPECT_FALSE(r.signatureOk);
    EXPECT_NE(r.summary().find("INVALID"), std::string::npos);
}

TEST(PackSeal, DigestAndBadSealsAreHandled) {
    seal::Manifest a, b;
    a.files["x"] = seal::hashBytes("1", 1);
    b.files["x"] = seal::hashBytes("2", 1);
    EXPECT_NE(seal::digest(a), seal::digest(b));
    EXPECT_EQ(seal::digest(a), seal::digest(a));

    seal::Manifest out;
    std::string error;
    EXPECT_FALSE(seal::fromJson("not json", out, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(seal::fromJson(R"({"format":"other","files":{}})", out, &error));
    EXPECT_FALSE(seal::fromJson(R"({"format":"kke-seal-1","files":{"a":"zz"}})", out, &error));
    EXPECT_FALSE(seal::fromJson(R"({"format":"kke-seal-1","files":{},"signature":"12"})", out, &error));
    ASSERT_TRUE(seal::fromJson(seal::toJson(a), out, &error)) << error;
    EXPECT_EQ(out.files, a.files);
    EXPECT_FALSE(out.signature.has_value());

    TempDir dir;
    seal::Report r = seal::verify(dir.path, testKeys(1).publicKey);
    EXPECT_FALSE(r.sealFound);
    EXPECT_FALSE(r.ok());
    dir.write(seal::kSealFileName, "{broken");
    r = seal::verify(dir.path, testKeys(1).publicKey);
    EXPECT_TRUE(r.sealFound);
    EXPECT_FALSE(r.error.empty());
    EXPECT_FALSE(seal::build(dir.path / "nope", out, &error));
}

// ------------------------------------------------------------ shipping builds

TEST(DevTools, DebugSwitchesFollowTheBuild) {
#if defined(_WIN32)
    _putenv_s("KKE_TEST_DEV_SWITCH", "1");
#else
    setenv("KKE_TEST_DEV_SWITCH", "1", 1);
#endif
    if constexpr (kke::dev::kEnabled) {
        ASSERT_NE(kke::dev::env("KKE_TEST_DEV_SWITCH"), nullptr);
        EXPECT_TRUE(kke::dev::flag("KKE_TEST_DEV_SWITCH"));
    } else {
        EXPECT_EQ(kke::dev::env("KKE_TEST_DEV_SWITCH"), nullptr) << "shipping builds ignore debug switches";
        EXPECT_FALSE(kke::dev::flag("KKE_TEST_DEV_SWITCH"));
    }
    EXPECT_FALSE(kke::dev::flag("KKE_TEST_DEV_SWITCH_UNSET"));
    EXPECT_EQ(kke::dev::env(nullptr), nullptr);
}
