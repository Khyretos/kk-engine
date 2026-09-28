// Tennis (games/tennis): the score, the umpire, the ball's flight, the
// shot that puts a ball on a spot, and the CPU player's footwork. All pure
// logic; the FEMFX ball itself is checked by KKE_TENNIS_BALLTEST=1.
#include "Bot.h"
#include "Court.h"
#include "Rules.h"
#include "Shot.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <random>

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

} // namespace
