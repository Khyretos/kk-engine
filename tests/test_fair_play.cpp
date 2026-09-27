// Fog of war, game rules and Kreative DRM licences (docs/ANTI_CHEAT.md,
// docs/DRM.md).

#include "kke/GameRules.h"
#include "kke/License.h"
#include "kke/net/NetSession.h"
#include "kke/net/Transport.h"
#include "kke/net/Visibility.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <random>
#include <system_error>

using namespace kke::net;

// ------------------------------------------------------------ fog of war

namespace {

// A wall along x = 0 from z = -10 to 10, 3 m tall: the only thing in the level.
bool clearOfWall(const glm::vec3& a, const glm::vec3& b) {
    if ((a.x < 0) == (b.x < 0)) return true;
    const float t = a.x / (a.x - b.x);
    const glm::vec3 p = a + (b - a) * t;
    return !(p.z > -10.0f && p.z < 10.0f && p.y < 3.0f);
}

Visibility::Player player(uint8_t id, glm::vec3 feet, glm::vec3 velocity = glm::vec3(0.0f)) { return { id, feet, velocity }; }

} // namespace

TEST(Visibility, WallsHideDistanceHearsAndCornersAreSentEarly) {
    Visibility v(clearOfWall);
    // 1 and 2 on either side of the wall, 40 m apart: hidden from each other.
    v.update(0.0, { player(1, { -20, 0, 0 }), player(2, { 20, 0, 0 }), player(3, { -20, 0, 30 }) });
    EXPECT_FALSE(v.visible(1, 2));
    EXPECT_FALSE(v.visible(2, 1));
    EXPECT_TRUE(v.visible(1, 3)) << "same side, in the open";
    EXPECT_TRUE(v.visible(2, 3)) << "3 is past the wall's end";
    EXPECT_GT(v.raysLastUpdate, 0u);
    // Close enough to hear through the wall: sent.
    v.update(1.0, { player(1, { -5, 0, 0 }), player(2, { 5, 0, 0 }) });
    EXPECT_TRUE(v.visible(1, 2));
    // 2 runs toward the wall's end at 8 m/s, still just behind it: not in
    // sight yet, but sent a little early (lead), so it never pops in late.
    v.settings.hearingRadius = 1.0f;
    v.settings.lead = 0.0;
    v.update(2.0, { player(1, { -20, 0, 11 }), player(2, { 20, 0, 8.0f }, { 0, 0, 8 }) });
    EXPECT_FALSE(v.visible(1, 2)) << "without the lead: behind the wall";
    v.forget(1);
    v.settings.lead = 0.15;
    v.update(2.0, { player(1, { -20, 0, 11 }), player(2, { 20, 0, 8.0f }, { 0, 0, 8 }) });
    EXPECT_TRUE(v.visible(1, 2)) << "about to clear the wall's end";
    v.update(2.05, { player(1, { -20, 0, 11 }), player(2, { 20, 0, -5.0f }) }); // too soon to re-trace: unchanged
    EXPECT_TRUE(v.visible(1, 2));
    v.update(2.2, { player(1, { -20, 0, 11 }), player(2, { 20, 0, -5.0f }) });
    EXPECT_TRUE(v.visible(1, 2)) << "kept for keepVisible after losing sight: no flicker";
    v.update(3.0, { player(1, { -20, 0, 11 }), player(2, { 20, 0, -5.0f }) });
    EXPECT_FALSE(v.visible(1, 2));
    // Far away: never, without casting a ray.
    v.update(4.0, { player(1, { -20, 0, 20 }), player(2, { 500, 0, 20 }) });
    EXPECT_FALSE(v.visible(1, 2));
    EXPECT_EQ(v.tracedPairs, 0u);
    // Leaving forgets; broken states never reveal.
    v.update(5.0, { player(1, { -20, 0, 20 }), player(2, { NAN, 0, 0 }) });
    EXPECT_FALSE(v.visible(1, 2));
    v.update(6.0, { player(1, { -20, 0, 20 }) });
    EXPECT_FALSE(v.visible(2, 1));
}

TEST(Visibility, CostStaysBoundedWithManyPlayers) {
    Visibility v(clearOfWall);
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> pos(-100.0f, 100.0f);
    std::vector<Visibility::Player> players;
    for (uint8_t i = 1; i <= 64; ++i) players.push_back(player(i, { pos(rng), 0, pos(rng) }));
    v.update(0.0, players);
    const size_t first = v.raysLastUpdate;
    EXPECT_LE(first, 64u * 63u * 24u) << "at most 24 rays a pair";
    v.update(1.0 / 30.0, players); // next snapshot: nothing is due for a re-trace
    EXPECT_EQ(v.raysLastUpdate, 0u);
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
    void run(double seconds) {
        for (double t = 0; t < seconds; t += 1.0 / 60.0) {
            now += 1.0 / 60.0;
            net.advance(1.0 / 60.0);
            server.update(now);
            for (auto& c : clients) c->update(now);
        }
    }
    bool sees(size_t client, uint8_t id) {
        for (const RemotePlayer& p : clients[client]->players(now))
            if (p.id == id) return p.hasState;
        return false;
    }
};

} // namespace

TEST(FogOfWarInNetServer, HiddenPlayersAreNotSentAndComeBackFresh) {
    Match m(2);
    NetPlayerState s;
    s.position = glm::vec3(1, 0, 0);
    m.clients[0]->setLocalState(s);
    s.position = glm::vec3(2, 0, 0);
    m.clients[1]->setLocalState(s);
    m.server.setLocalState(s);
    m.run(1.0);
    const uint8_t a = m.clients[0]->playerId(), b = m.clients[1]->playerId();
    ASSERT_TRUE(m.sees(0, b));
    bool hideB = true;
    m.server.sendPlayer = [&](uint8_t viewer, uint8_t subject) { return !(hideB && viewer == a && subject == b); };
    m.run(0.5);
    EXPECT_FALSE(m.sees(0, b)) << "left out of snapshots: gone at once, not frozen where it was";
    EXPECT_TRUE(m.sees(0, 0)) << "the host is still sent";
    EXPECT_TRUE(m.sees(1, a)) << "only that one viewer";
    // B moves while hidden, then comes into view: shown where it is now.
    s.position = glm::vec3(8, 0, 0);
    m.clients[1]->setLocalState(s);
    m.run(0.5);
    hideB = false;
    m.run(0.5);
    ASSERT_TRUE(m.sees(0, b));
    for (const RemotePlayer& p : m.clients[0]->players(m.now)) {
        if (p.id == b) {
            EXPECT_NEAR(p.state.position.x, 8.0f, 0.01f);
        }
    }
}

// ------------------------------------------------------------ game rules

TEST(GameRules, FlyingNeedsAPlaneWithFuelAndDamageHasACeiling) {
    kke::GameRules rules;
    std::string error;
    ASSERT_TRUE(rules.rule("flying needs a plane with fuel", { "flying == 1" }, { "in_plane == 1", "fuel > 0" }, &error)) << error;
    ASSERT_TRUE(rules.rule("no hit above the best buffed attack", {}, { "damage <= max_damage" }, &error)) << error;
    ASSERT_TRUE(rules.limit("gold in range", "gold", 0, 1e6));

    EXPECT_TRUE(rules.check({ { "flying", 0 }, { "damage", 50 }, { "max_damage", 900 } })) << "walking: the flying rule doesn't apply";
    EXPECT_TRUE(rules.check({ { "flying", 1 }, { "in_plane", 1 }, { "fuel", 3 }, { "max_damage", 900 } }));

    std::vector<kke::RuleViolation> v;
    EXPECT_FALSE(rules.check({ { "flying", 1 }, { "in_plane", 1 }, { "fuel", 0 } }, &v));
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].rule, "flying needs a plane with fuel");
    EXPECT_EQ(v[0].detail, "needs fuel > 0 (fuel = 0)");

    v.clear();
    EXPECT_FALSE(rules.check({ { "damage", 1000000 }, { "max_damage", 900 }, { "gold", -5 } }, &v));
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0].detail, "needs damage <= max_damage (damage = 1e+06, max_damage = 900)");
    // A rare multiplier: the game raises max_damage for that hit.
    EXPECT_TRUE(rules.check({ { "damage", 2500 }, { "max_damage", 3000 } }));
    // NaN never passes.
    EXPECT_FALSE(rules.check({ { "damage", std::nan("") }, { "max_damage", 900 } }));
    EXPECT_EQ(rules.violations("flying needs a plane with fuel"), 1u);
    EXPECT_EQ(rules.checks(), 6u);
}

TEST(GameRules, RulesAreDataAndBadOnesAreRefused) {
    kke::GameRules rules;
    ASSERT_TRUE(rules.rule("fly", { "flying == 1" }, { "fuel > 0.5" }));
    ASSERT_TRUE(rules.limit("speed", "speed", 0, 12));
    kke::GameRules copy;
    std::string error;
    ASSERT_TRUE(copy.loadJson(rules.toJson(), &error)) << error;
    ASSERT_EQ(copy.rules().size(), 2u);
    EXPECT_EQ(copy.rules()[0].require[0].toString(), "fuel > 0.5");
    EXPECT_EQ(copy.toJson(), rules.toJson());

    kke::Condition c;
    EXPECT_TRUE(kke::Condition::parse("hp>=0", c));
    EXPECT_EQ(c.toString(), "hp >= 0");
    for (const char* bad : { "", "1 > 2", "hp", "hp => 3", "hp > ", "hp > 3 4", "hp > 3x", "hp > -" })
        EXPECT_FALSE(kke::Condition::parse(bad, c, &error)) << bad;
    EXPECT_FALSE(rules.rule("", {}, { "hp > 0" }));
    EXPECT_FALSE(rules.rule("x", {}, {}));
    EXPECT_FALSE(rules.limit("backwards", "hp", 10, 0));
    const size_t before = copy.rules().size();
    EXPECT_FALSE(copy.loadJson(R"({"rules":[{"name":"a","require":["hp >"]}]})", &error));
    EXPECT_FALSE(copy.loadJson("[]", &error));
    EXPECT_FALSE(copy.loadJson(R"({"rules":[{"name":"a","require":[3]}]})", &error));
    EXPECT_EQ(copy.rules().size(), before) << "a bad file leaves the rules as they were";
}

// ------------------------------------------------------------ licences

namespace {

kke::seal::KeyPair devKeys(uint8_t fill) {
    std::array<uint8_t, 32> seed{};
    seed.fill(fill);
    return kke::seal::keyPairFromSeed(seed);
}

} // namespace

TEST(KreativeDrm, SignedLicencesCheckGameExpiryAndMachine) {
    namespace lic = kke::license;
    const auto keys = devKeys(3);
    lic::License l;
    l.game = "com.example.mygame";
    l.licenseId = "KEY-1234";
    l.issued = 1'000;
    l.expires = 5'000;
    l.machines = { "m1", "m2" };
    l.extra["edition"] = "deluxe";
    lic::sign(l, keys.secret);

    EXPECT_TRUE(lic::verify(l, keys.publicKey, "com.example.mygame", "m2", 2'000).ok());
    EXPECT_EQ(lic::verify(l, keys.publicKey, "com.example.mygame", "m3", 2'000).status, lic::Status::NewMachine)
        << "the game decides: ask 'is this you?', activate, or refuse";
    EXPECT_EQ(lic::verify(l, keys.publicKey, "com.example.other", "m1", 2'000).status, lic::Status::WrongGame);
    EXPECT_EQ(lic::verify(l, keys.publicKey, "com.example.mygame", "m1", 5'000).status, lic::Status::Expired);
    EXPECT_EQ(lic::verify(l, devKeys(4).publicKey, "com.example.mygame", "m1", 2'000).status, lic::Status::BadSignature);

    // Editing any field breaks the signature.
    lic::License edited = l;
    edited.machines.push_back("m3");
    EXPECT_EQ(lic::verify(edited, keys.publicKey, "com.example.mygame", "m3", 2'000).status, lic::Status::BadSignature);
    edited = l;
    edited.expires = 0;
    EXPECT_EQ(lic::verify(edited, keys.publicKey, "com.example.mygame", "m1", 9'000).status, lic::Status::BadSignature);

    // Round trip through the file format.
    lic::License back;
    std::string error;
    ASSERT_TRUE(lic::fromJson(lic::toJson(l), back, &error)) << error;
    EXPECT_TRUE(lic::verify(back, keys.publicKey, "com.example.mygame", "m1", 2'000).ok());
    EXPECT_EQ(back.extra.at("edition"), "deluxe");
    EXPECT_FALSE(lic::fromJson("{}", back, &error));
    EXPECT_FALSE(lic::fromJson(R"({"format":"kke-license-1","game":"g","license":"k","machines":"m1"})", back, &error));
    EXPECT_FALSE(lic::fromJson(R"({"format":"kke-license-1","game":"g","license":"k","signature":"00"})", back, &error));
    lic::License unsigned_;
    unsigned_.game = "g";
    EXPECT_EQ(lic::verify(unsigned_, keys.publicKey, "g", "m1", 0).status, lic::Status::BadSignature);
}

TEST(KreativeDrm, TheDeveloperCanUnlockEveryCopy) {
    namespace lic = kke::license;
    const auto keys = devKeys(9);
    lic::License unlock;
    unlock.game = "com.example.mygame";
    unlock.licenseId = "UNLOCK";
    unlock.unlockAll = true;
    lic::sign(unlock, keys.secret);
    EXPECT_TRUE(lic::verify(unlock, keys.publicKey, "com.example.mygame", "any machine", 1'900'000'000).ok());
    EXPECT_FALSE(lic::verify(unlock, keys.publicKey, "com.example.other", "any", 0).ok()) << "only for that game";
}

TEST(KreativeDrm, MachineIdsDifferPerGameAndFilesAreChecked) {
    namespace lic = kke::license;
    const std::string a = lic::machineId("game.a"), b = lic::machineId("game.b");
    if (!a.empty()) {
        EXPECT_EQ(a.size(), 32u);
        EXPECT_NE(a, b) << "two games can't match players up";
        EXPECT_EQ(a, lic::machineId("game.a"));
    }
    const auto keys = devKeys(1);
    const std::filesystem::path file = std::filesystem::temp_directory_path() / ("kke_license_" + std::to_string(std::random_device{}()) + ".json");
    EXPECT_EQ(lic::verifyFile(file, keys.publicKey, "game.a").status, lic::Status::Malformed);
    lic::License l;
    l.game = "game.a";
    l.licenseId = "K";
    if (!a.empty()) l.machines = { a };
    lic::sign(l, keys.secret);
    std::string error;
    ASSERT_TRUE(lic::save(l, file, &error)) << error;
    EXPECT_TRUE(lic::verifyFile(file, keys.publicKey, "game.a").ok());
    std::error_code ec;
    std::filesystem::remove(file, ec);
}
