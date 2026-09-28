// The party game's rules that don't need a window (games/party): the show
// (places and points), glass shattering, and what goes over the wire.

#include "NetParty.h"
#include "Shatter.h"
#include "Show.h"

#include "kke/net/Protocol.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>

using namespace party;

TEST(PartyShow, RacesRankFinishersThenStandersThenTheLastOut) {
    std::vector<RoundResult> r(5);
    r[0].finished = true;
    r[0].finishOrder = 1;
    r[1].finished = true;
    r[1].finishOrder = 0;
    r[2].score = 12.0f;           // still going, further
    r[3].score = 4.0f;
    r[4].out = true;
    r[4].outOrder = 0;
    const std::vector<int> places = placesOf(r);
    EXPECT_EQ(places, (std::vector<int>{ 1, 0, 2, 3, 4 }));
}

TEST(PartyShow, SurvivalRanksTheLastOutBestAndTiesSharePoints) {
    std::vector<RoundResult> r(4);
    r[0].out = true;
    r[0].outOrder = 0;
    r[1].out = true;
    r[1].outOrder = 2;
    r[2].score = 1.0f;            // two survivors share the win
    r[3].score = 1.0f;
    const std::vector<int> places = placesOf(r);
    EXPECT_EQ(places[2], 0);
    EXPECT_EQ(places[3], 0);
    EXPECT_LT(places[1], places[0]);
    EXPECT_EQ(pointsFor(places[2], 4), pointsFor(places[3], 4));
    EXPECT_GT(pointsFor(0, 4), pointsFor(1, 4));
    EXPECT_GE(pointsFor(11, 12), 0);
}

TEST(PartyShow, SameSeedSamePlaylistAndPointsAddUp) {
    const std::vector<std::string> all{ "a", "b", "c", "d", "e", "f", "g", "h" };
    Show one, two;
    one.start(all, 5, 3, 1234);
    two.start(all, 5, 3, 1234);
    EXPECT_EQ(one.playlist, two.playlist);
    std::set<std::string> seen;
    for (int k = 0; k < 5; ++k) {
        EXPECT_TRUE(seen.insert(one.current()).second) << "a game repeats before they've all been played";
        std::vector<RoundResult> r(3);
        r[static_cast<size_t>(k % 3)].finished = true;
        r[static_cast<size_t>(k % 3)].finishOrder = 0;
        one.score(r);
        EXPECT_FALSE(one.over());
        one.next();
    }
    EXPECT_TRUE(one.over());
    int total = 0;
    for (int p : one.points) total += p;
    EXPECT_GT(total, 0);
    const std::vector<int> order = one.standings();
    ASSERT_EQ(order.size(), 3u);
    EXPECT_GE(one.points[static_cast<size_t>(order[0])], one.points[static_cast<size_t>(order[2])]);
}

TEST(PartyShatter, ShardsCoverThePaneExactly) {
    ShardDesc d;
    d.halfSize = { 1.0f, 1.0f };
    d.impact = { 0.3f, -0.2f };
    d.pieces = 16;
    d.seed = 7;
    const std::vector<Polygon2> shards = shatterPane(d);
    ASSERT_GE(shards.size(), 8u);
    float area = 0.0f;
    for (const Polygon2& s : shards) {
        ASSERT_GE(s.size(), 3u);
        const float a = polygonArea(s);
        EXPECT_GT(a, 0.0f) << "every shard is counter-clockwise and not empty";
        area += a;
        for (const glm::vec2& p : s) {
            EXPECT_LE(std::abs(p.x), 1.0f + 1e-4f);
            EXPECT_LE(std::abs(p.y), 1.0f + 1e-4f);
        }
    }
    EXPECT_NEAR(area, 4.0f, 1e-3f);
    // The same break everywhere (it's an event: every machine shatters it).
    const std::vector<Polygon2> again = shatterPane(d);
    ASSERT_EQ(again.size(), shards.size());
    for (size_t i = 0; i < shards.size(); ++i) EXPECT_EQ(again[i], shards[i]);
}

TEST(PartyNet, RoundWithEverySeatFitsOneEventAndRoundTrips) {
    netparty::Round r;
    r.round = 42;
    r.index = 3;
    r.rounds = 8;
    r.seed = 0xdeadbeef;
    r.game = "glass_bridge";
    for (int k = 0; k < 12; ++k) {
        netparty::Seat s;
        s.player = static_cast<uint8_t>(k);
        s.cpu = k % 3 == 0;
        s.name = "Sixteen letters!" ;
        s.look = BeanLook{ k % 12, k % 6, k % 5, k % 9 };
        s.points = k * 7;
        r.seats.push_back(s);
    }
    const std::vector<uint8_t> bytes = netparty::encode(r);
    EXPECT_LE(bytes.size(), kke::net::kMaxEventBytes);
    const auto back = netparty::decodeRound(bytes);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->round, 42u);
    EXPECT_EQ(back->index, 3);
    EXPECT_EQ(back->rounds, 8);
    EXPECT_EQ(back->seed, 0xdeadbeefu);
    EXPECT_EQ(back->game, "glass_bridge");
    ASSERT_EQ(back->seats.size(), 12u);
    EXPECT_EQ(back->seats[5].name, "Sixteen letters!");
    EXPECT_EQ(back->seats[5].look.hat, 5);
    EXPECT_EQ(back->seats[6].cpu, true);
    EXPECT_EQ(back->seats[11].points, 77);
}

TEST(PartyNet, EventsAndPosesRoundTrip) {
    const auto phase = netparty::decodePhase(netparty::encode(netparty::Phase{ 9, 4 }));
    ASSERT_TRUE(phase.has_value());
    EXPECT_EQ(phase->round, 9u);
    EXPECT_EQ(phase->phase, 4);

    const auto result = netparty::decodeResult(netparty::encode(netparty::Result{ 9, 3, netparty::kResultOut, 5 }));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->player, 3);
    EXPECT_EQ(result->kind, netparty::kResultOut);
    EXPECT_EQ(result->order, 5);

    const auto game = netparty::decodeGame(netparty::encode(netparty::Game{ 9, 1, -3, 123456 }));
    ASSERT_TRUE(game.has_value());
    EXPECT_EQ(game->a, -3);
    EXPECT_EQ(game->b, 123456);

    netparty::Pose p;
    p.feet = { 1.5f, -2.0f, -40.25f };
    p.yaw = 90.0f;
    p.look = BeanLook{ 3, 2, 1, 7 };
    p.score = 17.0f;
    p.stunned = true;
    const netparty::Pose q = netparty::fromState(netparty::toState(p));
    EXPECT_NEAR(q.feet.z, p.feet.z, 0.02f);
    EXPECT_EQ(q.look.colour, 3);
    EXPECT_EQ(q.look.hat, 7);
    EXPECT_NEAR(q.score, 17.0f, 0.01f);
    EXPECT_TRUE(q.stunned);

    // Garbage never decodes into something.
    EXPECT_FALSE(netparty::decodeRound({ 1, 2, 3 }).has_value());
}
