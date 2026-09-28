// Tennis (games/tennis): the score, the umpire, the ball's flight, the
// shot that puts a ball on a spot, and the CPU player's footwork. All pure
// logic; the FEMFX ball itself is checked by KKE_TENNIS_BALLTEST=1. And
// every online message, written and read back (NetTennis.h).
#include "Bot.h"
#include "Court.h"
#include "NetTennis.h"
#include "Rules.h"
#include "Shot.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace {

using namespace tennis;

void winGame(Score& s, int team) {
    for (int i = 0; i < 4; ++i) s.pointTo(team);
}

TEST(TennisScore, LoveGameAndTheCalls) {
    Score s;
    EXPECT_EQ(s.callText(), "Love all");
    s.pointTo(0);
    EXPECT_EQ(s.pointText(0), "15");
    s.pointTo(0);
    s.pointTo(1);
    EXPECT_EQ(s.pointText(0), "30");
    EXPECT_EQ(s.pointText(1), "15");
    s.pointTo(0);
    EXPECT_TRUE(s.pointTo(0)); // the game
    EXPECT_EQ(s.games(0), 1);
    EXPECT_EQ(s.points(0), 0);
}

TEST(TennisScore, DeuceAdvantageAndBackToDeuce) {
    Score s;
    for (int i = 0; i < 3; ++i) {
        s.pointTo(0);
        s.pointTo(1);
    }
    EXPECT_EQ(s.callText(), "Deuce");
    EXPECT_FALSE(s.pointTo(0));
    EXPECT_EQ(s.pointText(0), "AD");
    EXPECT_FALSE(s.pointTo(1));
    EXPECT_EQ(s.callText(), "Deuce");
    s.pointTo(1);
    EXPECT_TRUE(s.pointTo(1));
    EXPECT_EQ(s.games(1), 1);
}

TEST(TennisScore, NoAdDecidesAtDeuce) {
    MatchRules r;
    r.noAd = true;
    Score s(r);
    for (int i = 0; i < 3; ++i) {
        s.pointTo(0);
        s.pointTo(1);
    }
    EXPECT_TRUE(s.pointTo(1));
    EXPECT_EQ(s.games(1), 1);
}

TEST(TennisScore, ServeAlternatesAndEndsSwitchOnOddGames) {
    Score s;
    const int first = s.servingTeam();
    const int side0 = s.sideOf(0);
    winGame(s, 0);
    EXPECT_NE(s.servingTeam(), first);
    EXPECT_TRUE(s.endsJustSwitched());
    EXPECT_EQ(s.sideOf(0), -side0);
    winGame(s, 0);
    EXPECT_EQ(s.servingTeam(), first);
    EXPECT_EQ(s.sideOf(0), -side0); // no switch after the second game
}

TEST(TennisScore, TiebreakAtSixAllWinsTheSet) {
    Score s;
    for (int g = 0; g < 6; ++g) {
        winGame(s, 0);
        winGame(s, 1);
    }
    EXPECT_TRUE(s.inTiebreak());
    for (int i = 0; i < 6; ++i) {
        s.pointTo(0);
        s.pointTo(1);
    }
    EXPECT_FALSE(s.over()); // 6-6 in the tiebreak: two clear needed
    s.pointTo(0);
    s.pointTo(0);
    EXPECT_TRUE(s.over());
    EXPECT_EQ(s.winner(), 0);
    EXPECT_EQ(s.setGames(0, 0), 7);
    EXPECT_EQ(s.setGames(0, 1), 6);
}

TEST(TennisScore, BestOfThree) {
    MatchRules r;
    r.gamesPerSet = 2;
    r.setsToWin = 2;
    Score s(r);
    for (int set = 0; set < 2; ++set) {
        winGame(s, 1);
        winGame(s, 1);
    }
    EXPECT_TRUE(s.over());
    EXPECT_EQ(s.winner(), 1);
    EXPECT_EQ(s.sets(1), 2);
}

// A serve from +z: the box is on -z, diagonally across from the server.
struct ServeBoxes {
    float x;
    float z;
};
ServeBoxes deuceBox() {
    const glm::vec3 server = servePosition(1, true, false);
    return { -std::copysign(2.0f, server.x), -4.0f };
}

TEST(TennisRally, TheServeGoesDiagonallyIntoTheBox) {
    const ServeBoxes b = deuceBox();
    EXPECT_TRUE(inServiceBox(b.x, b.z, 1, true));
    EXPECT_FALSE(inServiceBox(-b.x, b.z, 1, true));     // the other box
    EXPECT_FALSE(inServiceBox(b.x, -kServiceLine - 0.5f, 1, true)); // long
    EXPECT_FALSE(inServiceBox(b.x, 4.0f, 1, true));     // the server's own half
}

TEST(TennisRally, FaultThenDoubleFault) {
    Rally r;
    r.newPoint(0, 1, true, false);
    EXPECT_TRUE(r.mayHit(0));
    EXPECT_FALSE(r.mayHit(1));
    r.onHit(0);
    EXPECT_EQ(r.onBounce(0.0f, -9.0f), Rally::Result::Fault); // long
    r.serveAgain();
    EXPECT_TRUE(r.secondServeNow());
    r.onHit(0);
    EXPECT_EQ(r.onOut(), Rally::Result::PointTo1); // into the fence: double fault
    EXPECT_EQ(r.call(), "Double fault");
}

TEST(TennisRally, NetCordServeIsALet) {
    Rally r;
    r.newPoint(0, 1, true, false);
    const ServeBoxes b = deuceBox();
    r.onHit(0);
    r.onNet();
    EXPECT_EQ(r.onBounce(b.x, b.z), Rally::Result::Let);
    r.serveAgain();
    EXPECT_FALSE(r.secondServeNow()); // a let doesn't use up the first serve
}

TEST(TennisRally, ReturnWaitsForTheBounceThenTwoBouncesWin) {
    Rally r;
    r.newPoint(0, 1, true, false);
    const ServeBoxes b = deuceBox();
    r.onHit(0);
    EXPECT_FALSE(r.mayHit(1)); // not out of the air
    EXPECT_EQ(r.onBounce(b.x, b.z), Rally::Result::None);
    EXPECT_TRUE(r.mayHit(1));
    EXPECT_TRUE(r.bouncedOnce());
    EXPECT_EQ(r.onHit(1), Rally::Result::None);
    EXPECT_FALSE(r.mayHit(1)); // not twice
    EXPECT_TRUE(r.mayHit(0));  // the server may volley it
    EXPECT_EQ(r.onBounce(1.0f, 8.0f), Rally::Result::None);
    EXPECT_EQ(r.onBounce(1.0f, 12.5f), Rally::Result::PointTo1); // it got past the server
}

TEST(TennisRally, ARallyShotLandingOutLoses) {
    Rally r;
    r.newPoint(0, 1, true, false);
    const ServeBoxes b = deuceBox();
    r.onHit(0);
    r.onBounce(b.x, b.z);
    r.onHit(1);
    EXPECT_EQ(r.onBounce(0.0f, 13.0f), Rally::Result::PointTo0); // past the baseline
    EXPECT_EQ(r.call(), "Out");
}

TEST(TennisCourt, NetSagsToTheMiddle) {
    EXPECT_NEAR(netHeight(0.0f), kNetHeightCentre, 1e-4f);
    EXPECT_NEAR(netHeight(kPostX), kNetHeightPost, 1e-3f);
    EXPECT_GT(netHeight(3.0f), netHeight(0.0f));
}

TEST(TennisCourt, TheTenCourtsDontOverlap) {
    SportCenter c;
    for (int i = 0; i < SportCenter::kCourts; ++i) {
        EXPECT_EQ(c.courtAt(c.courts[static_cast<size_t>(i)].origin), i);
        // A fence corner of one court is inside no other.
        const glm::vec3 corner = c.courts[static_cast<size_t>(i)].toWorld({ kFenceHalfX, 0.0f, kFenceHalfZ });
        for (int j = 0; j < SportCenter::kCourts; ++j) {
            if (i != j) {
                EXPECT_FALSE(c.courts[static_cast<size_t>(j)].contains(corner));
            }
        }
    }
}

TEST(TennisFlight, ClosedFormMatchesSmallSteps) {
    const Flight f{ { 0.0f, 1.0f, 11.0f }, { 1.5f, 3.0f, -24.0f }, kGravity + 4.0f };
    glm::vec3 p = f.pos, v = f.vel;
    const float h = 1e-4f;
    for (int i = 0; i < 10000; ++i) { // 1 s
        v += (glm::vec3(0.0f, -f.gravity, 0.0f) - f.drag * v) * h;
        p += v * h;
    }
    EXPECT_LT(glm::length(p - f.at(1.0f)), 0.01f);
    EXPECT_LT(glm::length(v - f.velocityAt(1.0f)), 0.01f);
    const float down = f.timeDownTo(kBallRadius);
    EXPECT_GT(down, 0.0f);
    EXPECT_NEAR(f.at(down).y, kBallRadius, 1e-3f);
    const float tn = f.timeAtNet();
    EXPECT_NEAR(f.at(tn).z, 0.0f, 1e-3f);
}

TEST(TennisShot, EveryShotLandsWhereItWasAimedAndClearsTheNet) {
    const glm::vec3 from(1.0f, 1.0f, 12.0f);
    for (ShotKind k : { ShotKind::Flat, ShotKind::Topspin, ShotKind::Slice, ShotKind::Lob, ShotKind::Drop }) {
        for (const glm::vec3& target : { glm::vec3(-3.0f, 0.0f, -9.0f), glm::vec3(3.5f, 0.0f, -6.0f), glm::vec3(0.0f, 0.0f, -2.5f) }) {
            const ShotPlan plan = planShot(from, target, 22.0f, k, 0.3f);
            ASSERT_TRUE(plan.clearsNet);
            const Flight f{ from, plan.velocity, plan.gravity };
            const float t = f.timeDownTo(kBallRadius);
            const glm::vec3 land = f.at(t);
            EXPECT_NEAR(land.x, target.x, 0.02f);
            EXPECT_NEAR(land.z, target.z, 0.02f);
            const glm::vec3 atNet = f.at(f.timeAtNet());
            EXPECT_GE(atNet.y - kBallRadius, netHeight(atNet.x) + 0.29f);
        }
    }
}

TEST(TennisShot, AServeFromTheLineLandsInTheBox) {
    const glm::vec3 server = servePosition(1, true, false);
    const ServeBoxes b = deuceBox();
    const glm::vec3 contact = server + glm::vec3(0.2f, 2.7f, -0.3f);
    const ShotPlan plan = planShot(contact, { b.x, 0.0f, b.z }, 40.0f, ShotKind::Serve, 0.1f);
    ASSERT_TRUE(plan.clearsNet);
    const Flight f{ contact, plan.velocity, plan.gravity };
    const glm::vec3 land = f.at(f.timeDownTo(kBallRadius));
    EXPECT_TRUE(inServiceBox(land.x, land.z, 1, true));
}

TEST(TennisShot, MeetPointStaysReachable) {
    // A deep, high-bouncing ball is taken before it gets to the fence.
    const Flight f{ { 0.0f, 1.0f, -12.0f }, planShot({ 0.0f, 1.0f, -12.0f }, { 0.0f, 0.0f, 10.5f }, 26.0f, ShotKind::Flat).velocity };
    glm::vec3 where;
    float when = 0.0f;
    ASSERT_TRUE(meetPoint(f, 1, BounceModel{}, where, when));
    EXPECT_LE(where.z, kHalfLength + 3.0f + 0.01f);
    EXPECT_GT(where.y, kBallRadius);
    EXPECT_FALSE(meetPoint(f, -1, BounceModel{}, where, when)); // not the hitter's own side
}

// The CPU's legs: shots from the far baseline to anywhere in its court; a
// Hard bot running at its speed toward where it says gets within reach
// of the ball, between knee and head, before the second bounce.
TEST(TennisBot, AHardBotReachesAlmostEveryBall) {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> ux(-kSinglesHalfWidth + 0.3f, kSinglesHalfWidth - 0.3f), uz(4.0f, kHalfLength - 0.5f);
    int reached = 0;
    constexpr int kShots = 60;
    constexpr float dt = 1.0f / 60.0f;
    for (int n = 0; n < kShots; ++n) {
        Bot bot(static_cast<uint32_t>(n + 1), 2);
        const glm::vec3 from(ux(rng) * 0.5f, 1.0f, -12.5f);
        const ShotKind kind = n % 3 == 0 ? ShotKind::Flat : n % 3 == 1 ? ShotKind::Topspin : ShotKind::Slice;
        const ShotPlan plan = planShot(from, { ux(rng), 0.0f, uz(rng) }, 24.0f, kind, 0.4f);
        Flight ball{ from, plan.velocity, plan.gravity };
        glm::vec3 feet(0.0f, 0.0f, kHalfLength + 0.6f);
        bot.onOpponentHit();
        bool bounced = false, hit = false;
        for (float t = 0.0f; t < 4.0f && !hit; t += dt) {
            Bot::View v;
            v.ball = ball;
            v.ballInPlay = true;
            v.mayHit = true;
            v.bounced = bounced;
            v.side = 1;
            v.feet = feet;
            v.opponent = { 0.0f, 0.0f, -12.5f };
            const Bot::Decision d = bot.think(v, dt);
            glm::vec3 to = d.moveTo - feet;
            to.y = 0.0f;
            const float dist = glm::length(to);
            if (dist > 1e-3f) feet += to / dist * std::min(bot.skill().speed * d.urgency * dt, dist);
            // The ball: one step, with its bounces.
            Flight next{ ball.at(dt), ball.velocityAt(dt), ball.gravity };
            if (next.pos.y < kBallRadius) {
                if (bounced) break; // twice: missed
                const float tb = ball.timeDownTo(kBallRadius);
                next = bounce(ball, tb, BounceModel{}, kGravity + (ball.gravity - kGravity) * kPullAfterBounce);
                bounced = true;
            }
            ball = next;
            const glm::vec2 flat(ball.pos.x - feet.x, ball.pos.z - feet.z);
            if (bounced && glm::length(flat) < 1.5f && ball.pos.y > 0.3f && ball.pos.y < 2.4f) hit = true;
        }
        if (hit) ++reached;
    }
    EXPECT_GE(reached, kShots * 9 / 10);
}

// A lob from well behind the baseline stays under the roof (a slow one
// would need a moon ball: the air takes its speed) and still lands.
TEST(TennisShot, DeepLobStaysUnderTheRoof) {
    const glm::vec3 from(-4.3f, 1.4f, -14.6f), target(1.0f, 0.0f, 10.4f);
    const ShotPlan plan = planShot(from, target, 10.0f, ShotKind::Lob, 1.5f);
    const Flight f{ from, plan.velocity, plan.gravity };
    float top = 0.0f;
    for (float t = 0.0f; t < plan.time; t += 0.01f) top = std::max(top, f.at(t).y);
    EXPECT_LT(top, kLidHeight - 2.0f);
    EXPECT_TRUE(plan.clearsNet);
    const glm::vec3 land = f.at(plan.time);
    EXPECT_NEAR(land.x, target.x, 0.05f);
    EXPECT_NEAR(land.z, target.z, 0.05f);
}

// The crowd's spots: none inside a fence, on the players' bench, or
// closer to another spot (this court's or the next one's) than two people
// standing side by side.
TEST(TennisCourt, SpectatorSeatsHaveRoom) {
    const SportCenter c;
    std::vector<glm::vec3> all;
    for (int i = 0; i < SportCenter::kCourts; ++i)
        for (const SportCenter::Seat& s : c.seats(i)) {
            EXPECT_EQ(c.courtAt(s.pos), -1);
            const glm::vec3 l = c.courts[static_cast<size_t>(i)].toLocal(s.pos);
            // The players' bench: 0.53 to 0.97 m out from the long fences, a person is 0.3 m round.
            if (std::abs(l.z) < 11.3f) {
                EXPECT_TRUE(std::abs(l.x) < kFenceHalfX || std::abs(l.x) - kFenceHalfX > 0.97f + 0.3f) << l.x;
            }
            all.push_back(s.pos);
        }
    for (size_t a = 0; a < all.size(); ++a)
        for (size_t b = a + 1; b < all.size(); ++b) EXPECT_GT(glm::length(all[a] - all[b]), 0.65f);
}

// Online: every message reads back as written (to the wire's rounding),
// and a damaged one is refused rather than half read.
TEST(TennisNet, MessagesRoundTrip) {
    net::Setup s;
    s.match = 7;
    s.court = 3;
    s.teamSize = 2;
    s.gamesPerSet = 6;
    s.setsToWin = 2;
    s.seats.push_back({ 0, 0, 0, false, "Juno", { 0.35f, 0.6f, 1.25f } });
    s.seats.push_back({ 4, 1, 1, true, "Ace (CPU)", { 1.0f, 0.5f, 0.2f } });
    const auto s2 = net::decodeSetup(net::encode(s));
    ASSERT_TRUE(s2.has_value());
    EXPECT_EQ(s2->match, 7u);
    EXPECT_EQ(s2->court, 3);
    EXPECT_EQ(s2->teamSize, 2);
    EXPECT_EQ(s2->gamesPerSet, 6);
    EXPECT_EQ(s2->setsToWin, 2);
    ASSERT_EQ(s2->seats.size(), 2u);
    EXPECT_EQ(s2->seats[1].player, 4);
    EXPECT_EQ(s2->seats[1].team, 1);
    EXPECT_EQ(s2->seats[1].slot, 1);
    EXPECT_TRUE(s2->seats[1].cpu);
    EXPECT_EQ(s2->seats[0].name, "Juno");
    EXPECT_NEAR(s2->seats[0].tint.z, 1.25f, 0.01f);

    const auto sv = net::decodeServe(net::encode(net::Serve{ 7, 300, true, true }));
    ASSERT_TRUE(sv.has_value());
    EXPECT_EQ(sv->point, 300);
    EXPECT_TRUE(sv->again && sv->second);

    const auto pt = net::decodePoint(net::encode(net::Point{ 7, 12, static_cast<uint8_t>(Rally::Result::Fault), "Fault" }));
    ASSERT_TRUE(pt.has_value());
    EXPECT_EQ(static_cast<Rally::Result>(pt->result), Rally::Result::Fault);
    EXPECT_EQ(pt->call, "Fault");

    net::Hit h;
    h.match = 7;
    h.point = 12;
    h.player = 2;
    h.shot = 5;
    h.kind = static_cast<uint8_t>(ShotKind::Slice);
    h.at = { -3.2f, 0.9f, 11.4f };
    h.velocity = { 2.5f, 4.0f, -24.0f };
    h.spin = { -50.0f, 0.0f, 3.0f };
    h.pull = -2.5f;
    h.squash = 0.6f;
    const auto h2 = net::decodeHit(net::encode(h));
    ASSERT_TRUE(h2.has_value());
    EXPECT_EQ(h2->shot, 5);
    EXPECT_EQ(h2->kind, h.kind);
    EXPECT_LT(glm::length(h2->at - h.at), 0.002f);
    EXPECT_LT(glm::length(h2->velocity - h.velocity), 0.004f);
    EXPECT_LT(glm::length(h2->spin - h.spin), 0.05f);
    EXPECT_NEAR(h2->pull, h.pull, 0.005f);
    // A hit read back and written again is the same bytes: every machine
    // flies the ball from exactly the same numbers.
    EXPECT_EQ(net::encode(*h2), net::encode(h));

    net::BallState b;
    b.match = 7;
    b.point = 12;
    b.shot = 6;
    b.flight = { { 1.0f, 0.5f, -4.0f }, { 0.5f, 3.0f, -20.0f }, kGravity + 3.0f };
    b.rolling = true;
    const auto b2 = net::decodeBall(net::encode(b));
    ASSERT_TRUE(b2.has_value());
    EXPECT_EQ(b2->shot, 6);
    EXPECT_TRUE(b2->rolling);
    EXPECT_NEAR(b2->flight.gravity, b.flight.gravity, 0.01f);
    EXPECT_FLOAT_EQ(b2->flight.drag, kDrag);

    const auto e2 = net::decodeEnd(net::encode(net::End{ 7, "Juno left the match" }));
    ASSERT_TRUE(e2.has_value());
    EXPECT_EQ(e2->why, "Juno left the match");

    std::vector<uint8_t> cut = net::encode(h);
    cut.resize(cut.size() / 2);
    EXPECT_FALSE(net::decodeHit(cut).has_value());
}

TEST(TennisNet, PoseRoundTrip) {
    net::Pose p;
    p.feet = { 12.0f, 0.0f, -30.0f };
    p.velocity = { 3.0f, 0.0f, -1.0f };
    p.facing = glm::normalize(glm::vec3(1.0f, 0.0f, -1.0f));
    p.swing = SwingPose::Kind::Backhand;
    p.swingT = 0.5f;
    p.contact = { -0.7f, 1.1f, 0.45f };
    p.celebrating = true;
    p.cheer = false;
    const net::Pose q = net::fromState(net::toState(p));
    EXPECT_LT(glm::length(q.feet - p.feet), 0.01f);
    EXPECT_LT(glm::length(q.facing - p.facing), 0.02f);
    EXPECT_EQ(q.swing, SwingPose::Kind::Backhand);
    EXPECT_NEAR(q.swingT, 0.5f, 0.02f);
    EXPECT_LT(glm::length(q.contact - p.contact), 0.005f);
    EXPECT_TRUE(q.celebrating);
    EXPECT_FALSE(q.cheer);
    // Not swinging stays not swinging.
    p.swingT = -2.0f;
    EXPECT_LT(net::fromState(net::toState(p)).swingT, -1.0f);
}

TEST(TennisNet, TintText) {
    EXPECT_EQ(net::tintText({ 1.0f, 0.0f, 0.5f }), "#ff0080");
    const glm::vec3 t = net::tintFromText("#ff0080", glm::vec3(0.0f));
    EXPECT_NEAR(t.z, 128.0f / 255.0f, 1e-4f);
    EXPECT_EQ(net::tintFromText("oops", glm::vec3(0.25f)), glm::vec3(0.25f));
}

} // namespace
