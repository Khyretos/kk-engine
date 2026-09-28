// The Flying demo's flight model and course (games/flying_demo/Flight.h,
// Course.h): planes fly, stall and land the way the README says, and
// the CPU pilots get round every island's rings. Combat.h: planes and
// bullets hit what they should, and the Dogfight town stands on the land.
#include "Combat.h"
#include "Course.h"
#include "Flight.h"
#include "FlyNet.h"
#include "Stunts.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

using namespace flying;

namespace {
constexpr float kDt = 1.0f / 60.0f;
Ground flatGround(float y = 0.0f) {
    Ground g;
    g.height = [y](float, float) { return y; };
    g.landable = [](float, float) { return true; };
    return g;
}
} // namespace

TEST(Flight, AnglesRoundTrip) {
    const PlaneState s = airborne(glm::vec3(0.0f, 100.0f, 0.0f), 90.0f, 40.0f);
    EXPECT_NEAR(headingOf(s.rotation), 90.0f, 0.01f);
    EXPECT_NEAR(s.forward().x, 1.0f, 1e-4f); // heading 90: +X, a right turn from -Z
    EXPECT_NEAR(pitchOf(s.rotation), 0.0f, 0.01f);
    EXPECT_NEAR(bankOf(s.rotation), 0.0f, 0.01f);
}

TEST(Flight, HandsOffCruiseHoldsItsHeight) {
    FlightSettings f;
    PlaneState s = airborne(glm::vec3(0.0f, 300.0f, 0.0f), 0.0f, 48.0f);
    Controls c;
    c.throttle = 0.6f;
    for (int i = 0; i < 60 * 20; ++i) step(s, c, f, flatGround(), kDt);
    EXPECT_FALSE(s.crashed);
    EXPECT_NEAR(s.position.y, 300.0f, 60.0f) << "a trimmed plane neither dives nor zooms away";
    EXPECT_GT(s.airspeed, f.stallSpeed);
}

TEST(Flight, StickControlsTurnTheRightWay) {
    FlightSettings f;
    Controls c;
    c.pitch = 1.0f;
    PlaneState up = airborne(glm::vec3(0.0f, 300.0f, 0.0f), 0.0f, 50.0f);
    for (int i = 0; i < 30; ++i) step(up, c, f, flatGround(), kDt);
    EXPECT_GT(pitchOf(up.rotation), 20.0f);
    c = Controls{};
    c.roll = 1.0f;
    PlaneState r = airborne(glm::vec3(0.0f, 300.0f, 0.0f), 0.0f, 50.0f);
    for (int i = 0; i < 15; ++i) step(r, c, f, flatGround(), kDt);
    EXPECT_GT(bankOf(r.rotation), 30.0f) << "stick right: right wing down";
    c = Controls{};
    c.yaw = 1.0f;
    PlaneState y = airborne(glm::vec3(0.0f, 300.0f, 0.0f), 0.0f, 50.0f);
    for (int i = 0; i < 60; ++i) step(y, c, f, flatGround(), kDt);
    EXPECT_GT(headingOf(y.rotation), 5.0f) << "right rudder: nose right";
}

TEST(Flight, AFullLoopComesBackAround) {
    FlightSettings f;
    PlaneState s = airborne(glm::vec3(0.0f, 400.0f, 0.0f), 0.0f, 65.0f);
    Controls c;
    c.throttle = 1.0f;
    c.pitch = 1.0f;
    bool inverted = false;
    for (int i = 0; i < 60 * 8 && !(inverted && pitchOf(s.rotation) > -5.0f && s.up().y > 0.9f); ++i) {
        step(s, c, f, flatGround(), kDt);
        inverted = inverted || s.up().y < -0.8f;
    }
    EXPECT_TRUE(inverted) << "went over the top";
    EXPECT_FALSE(s.crashed);
    EXPECT_GT(s.up().y, 0.9f) << "and came back upright";
}

TEST(Flight, StraightUpSlowsAndTipsOver) {
    // The engine is weaker than the plane is heavy: pointed at the sky it
    // slows, and then falls over nose first (a hammerhead), never hangs there.
    FlightSettings f;
    PlaneState s = airborne(glm::vec3(0.0f, 300.0f, 0.0f), 0.0f, 60.0f);
    s.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)) * s.rotation;
    s.velocity = s.forward() * 60.0f;
    ASSERT_GT(s.forward().y, 0.99f);
    Controls c;
    c.throttle = 1.0f;
    float stoppedAt = -1.0f, lowestPitch = 90.0f;
    for (int i = 0; i < 60 * 20; ++i) {
        step(s, c, f, flatGround(), kDt);
        if (stoppedAt < 0.0f && s.velocity.y <= 0.0f) stoppedAt = static_cast<float>(i) * kDt;
        lowestPitch = std::min(lowestPitch, pitchOf(s.rotation));
    }
    EXPECT_GT(stoppedAt, 0.0f) << "it ran out of speed going up";
    EXPECT_LT(stoppedAt, 12.0f);
    EXPECT_LT(lowestPitch, -30.0f) << "and the nose fell through";
    EXPECT_FALSE(s.crashed);
}

TEST(Flight, TooSlowStalls) {
    FlightSettings f;
    PlaneState s = airborne(glm::vec3(0.0f, 500.0f, 0.0f), 0.0f, 30.0f);
    Controls c;
    c.throttle = 0.0f;
    c.pitch = 1.0f;
    bool stalled = false;
    for (int i = 0; i < 60 * 8; ++i) {
        step(s, c, f, flatGround(), kDt);
        stalled = stalled || s.stalled;
    }
    EXPECT_TRUE(stalled);
    EXPECT_LT(s.position.y, 500.0f);
}

TEST(Flight, TakesOffFromTheGroundAndLandsGently) {
    FlightSettings f;
    PlaneState s = parked(glm::vec3(0.0f), 0.0f, f);
    Controls c;
    c.throttle = 1.0f;
    float airborneAt = -1.0f;
    for (int i = 0; i < 60 * 30; ++i) {
        c.pitch = s.airspeed > f.stallSpeed * 1.2f ? 0.35f : 0.0f;
        step(s, c, f, flatGround(), kDt);
        if (!s.onGround && airborneAt < 0.0f) airborneAt = static_cast<float>(i) * kDt;
        if (s.position.y > 40.0f) break;
    }
    EXPECT_FALSE(s.crashed);
    EXPECT_GT(airborneAt, 1.0f) << "it rolls a while first";
    EXPECT_GT(s.position.y, 40.0f);

    // A slow, shallow descent onto the ground is a landing ...
    PlaneState land = airborne(glm::vec3(0.0f, 2.0f, 0.0f), 0.0f, 30.0f);
    land.velocity.y = -2.0f;
    c = Controls{};
    c.throttle = 0.0f;
    for (int i = 0; i < 60 * 3; ++i) step(land, c, f, flatGround(), kDt);
    EXPECT_FALSE(land.crashed);
    EXPECT_TRUE(land.onGround);
    // ... a dive into it is not.
    PlaneState dive = airborne(glm::vec3(0.0f, 30.0f, 0.0f), 0.0f, 60.0f);
    c.pitch = -1.0f;
    for (int i = 0; i < 60 * 5 && !dive.crashed; ++i) step(dive, c, f, flatGround(), kDt);
    EXPECT_TRUE(dive.crashed);
}

TEST(Flight, TakesOffWithJustTheThrottle) {
    // No stick at all: at take-off speed the nose lifts by itself.
    FlightSettings f;
    PlaneState s = parked(glm::vec3(0.0f), 0.0f, f);
    Controls c;
    c.throttle = 1.0f;
    float liftOff = -1.0f;
    for (int i = 0; i < 60 * 20; ++i) {
        step(s, c, f, flatGround(), kDt);
        if (!s.onGround && liftOff < 0.0f) liftOff = -s.position.z;
    }
    EXPECT_FALSE(s.crashed);
    EXPECT_GT(liftOff, 20.0f) << "it rolls a while first";
    EXPECT_LT(liftOff, 400.0f) << "well inside the island's 760 m runway";
    EXPECT_FALSE(s.onGround);
    EXPECT_GT(s.position.y, 8.0f) << "and stays up (pulling back climbs away)";
}

TEST(Course, SameSeedSameIslandAndRings) {
    const Island a(7), b(7), c(8);
    EXPECT_FLOAT_EQ(a.terrain(123.0f, -456.0f), b.terrain(123.0f, -456.0f));
    const std::vector<Ring> ra = a.rings(12, 12.0f), rb = b.rings(12, 12.0f), rc = c.rings(12, 12.0f);
    ASSERT_EQ(ra.size(), 12u);
    for (size_t i = 0; i < ra.size(); ++i) EXPECT_EQ(ra[i].center, rb[i].center);
    EXPECT_NE(ra[0].center, rc[0].center);
    // The runway is flat and landable; the sea is not.
    const Runway& w = a.runway();
    EXPECT_NEAR(a.terrain(w.start.x, w.start.z - 100.0f), w.height(), 0.01f);
    EXPECT_TRUE(a.onRunway(w.start.x, w.start.z - 100.0f));
    EXPECT_FALSE(a.ground().landable(2000.0f, 0.0f));
    EXPECT_FLOAT_EQ(a.surface(2000.0f, 0.0f), Island::kSea);
    for (const Ring& r : ra) EXPECT_GT(r.center.y - r.radius, a.surface(r.center.x, r.center.z) + 20.0f);
}

TEST(Course, ThroughARingOnlyTheRightWay) {
    Ring r;
    r.center = glm::vec3(0.0f, 100.0f, 0.0f);
    r.normal = glm::vec3(0.0f, 0.0f, -1.0f);
    r.radius = 10.0f;
    EXPECT_TRUE(throughRing(r, glm::vec3(3.0f, 100.0f, 2.0f), glm::vec3(3.0f, 100.0f, -2.0f)));
    EXPECT_FALSE(throughRing(r, glm::vec3(3.0f, 100.0f, -2.0f), glm::vec3(3.0f, 100.0f, 2.0f))) << "backwards";
    EXPECT_FALSE(throughRing(r, glm::vec3(13.0f, 100.0f, 2.0f), glm::vec3(13.0f, 100.0f, -2.0f))) << "beside it";
}

// The CPU pilots, at every skill, fly two laps of several islands'
// courses without hitting anything.
TEST(Course, CpuPilotsGetRound) {
    FlightSettings f;
    for (uint32_t seed : { 1u, 2u, 3u, 11u, 42u }) {
        const Island island(seed);
        const std::vector<Ring> rings = island.rings(10, 14.0f);
        const Ground g = island.ground();
        for (float skill : { 0.0f, 0.5f, 1.0f }) {
            const Ring& first = rings[0];
            const glm::vec3 behind = first.center - first.normal * 250.0f;
            PlaneState s = airborne(behind, headingOf(glm::quatLookAt(first.normal, glm::vec3(0, 1, 0))), 45.0f);
            size_t passed = 0;
            float t = 0.0f;
            RingPilot pilot;
            while (passed < rings.size() * 2 && t < 600.0f && !s.crashed) {
                const Ring& next = rings[passed % rings.size()];
                const glm::vec3 from = s.position;
                step(s, steerToward(s, pilot.aim(next, s.position), g, pilot.floor(55.0f), skill), f, g, kDt);
                if (throughRing(next, from, s.position)) {
                    ++passed;
                    pilot.reset();
                }
                t += kDt;
            }
            EXPECT_FALSE(s.crashed) << "seed " << seed << " skill " << skill << " crashed after ring " << passed << " at "
                                    << s.position.x << ", " << s.position.y << ", " << s.position.z;
            EXPECT_EQ(passed, rings.size() * 2) << "seed " << seed << " skill " << skill << " after " << t << " s";
        }
    }
}

TEST(FlyNet, PlanesAndSetupRoundTrip) {
    net::Plane p;
    p.position = glm::vec3(120.5f, 340.25f, -800.0f);
    p.velocity = glm::vec3(50.0f, -3.0f, 12.0f);
    p.rotation = glm::normalize(glm::quat(0.9f, 0.1f, -0.3f, 0.2f));
    p.throttle = 0.75f;
    p.smoke = true;
    p.nextRing = 7;
    p.lap = 1;
    p.finishTime = 123.45f;
    p.score = 4200;
    p.round = 9;
    p.firing = true;
    p.health = 37;
    p.kills = 12;
    p.deaths = 3;
    const net::Plane q = net::fromState(net::toState(p));
    EXPECT_TRUE(q.firing);
    EXPECT_EQ(q.health, 37);
    EXPECT_EQ(q.kills, 12);
    EXPECT_EQ(q.deaths, 3);
    EXPECT_LT(glm::length(q.position - p.position), 0.01f);
    EXPECT_GT(std::abs(glm::dot(q.rotation, p.rotation)), 0.999f);
    EXPECT_NEAR(q.throttle, 0.75f, 0.02f);
    EXPECT_TRUE(q.smoke);
    EXPECT_FALSE(q.crashed);
    EXPECT_EQ(q.nextRing, 7);
    EXPECT_EQ(q.lap, 1);
    EXPECT_NEAR(q.finishTime, 123.45f, 0.011f);
    EXPECT_EQ(q.score, 4200u);
    EXPECT_EQ(q.round, 9);

    net::Setup s;
    s.seed = 42;
    s.round = 3;
    s.mode = 1;
    s.laps = 3;
    s.rings = 12;
    s.ringRadius = 10.0f;
    s.mood = "golden_hour";
    s.killsToWin = 15;
    s.seats.push_back({ 1, 0, false, 1, 2, "Kees", glm::vec3(0.2f, 0.6f, 1.0f) });
    s.seats.push_back({ 5, 1, true, 3, 0, "Ace", glm::vec3(1.0f, 0.3f, 0.2f) });
    const std::optional<net::Setup> t = net::decodeSetup(net::encode(s));
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->seed, 42u);
    EXPECT_EQ(t->mode, 1);
    EXPECT_EQ(t->laps, 3);
    EXPECT_EQ(t->rings, 12);
    EXPECT_FLOAT_EQ(t->ringRadius, 10.0f);
    EXPECT_EQ(t->mood, "golden_hour");
    EXPECT_EQ(t->killsToWin, 15);
    ASSERT_EQ(t->seats.size(), 2u);
    EXPECT_EQ(t->seats[1].name, "Ace");
    EXPECT_TRUE(t->seats[1].cpu);
    EXPECT_EQ(t->seats[1].skill, 3);
    EXPECT_EQ(t->seats[0].livery, 2);
    EXPECT_FALSE(net::decodeSetup({ 1, 2 }).has_value());
    EXPECT_EQ(net::tintFromText(net::tintText(glm::vec3(1.0f, 0.5f, 0.0f)), glm::vec3(0.0f)).g, 128.0f / 255.0f);
}

TEST(FlyNet, DamageDentsAndDownsRoundTrip) {
    net::Damage d;
    d.target = 4;
    d.from = 7;
    d.round = 200;
    d.bump = true;
    d.amount = 42.5f;
    d.speed = 17.25f;
    d.point = glm::vec3(-3.5f, 0.7f, -0.8f);
    d.direction = glm::normalize(glm::vec3(1.0f, -0.2f, 0.1f));
    const std::optional<net::Damage> e = net::decodeDamage(net::encode(d));
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->target, 4);
    EXPECT_EQ(e->from, 7);
    EXPECT_EQ(e->round, 200);
    EXPECT_TRUE(e->bump);
    EXPECT_NEAR(e->amount, 42.5f, 0.13f);
    EXPECT_NEAR(e->speed, 17.25f, 0.13f);
    EXPECT_LT(glm::length(e->point - d.point), 0.03f);
    EXPECT_LT(glm::length(e->direction - d.direction), 0.02f);

    net::Dent dent;
    dent.plane = 2;
    dent.point = glm::vec3(0.0f, 0.5f, 2.4f);
    dent.direction = glm::vec3(0.0f, 0.0f, -1.0f);
    dent.depth = 0.12f;
    const std::optional<net::Dent> f = net::decodeDent(net::encode(dent));
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->plane, 2);
    EXPECT_NEAR(f->depth, 0.12f, 0.005f);

    net::Down down;
    down.plane = 3;
    down.by = 9;
    down.cause = 1;
    const std::optional<net::Down> g = net::decodeDown(net::encode(down));
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(g->by, 9);
    EXPECT_EQ(g->cause, 1);
    EXPECT_FALSE(net::decodeDown({ 1 }).has_value());
}

TEST(Stunts, LoopsAndRollsScore) {
    FlightSettings f;
    StuntTracker t;
    PlaneState s = airborne(glm::vec3(0.0f, 500.0f, 0.0f), 0.0f, 65.0f);
    Controls c;
    c.throttle = 1.0f;
    c.pitch = 1.0f;
    std::string got;
    for (int i = 0; i < 60 * 8 && got.empty(); ++i) {
        step(s, c, f, flatGround(), kDt);
        got = t.update(s, s.position.y, kDt).name;
    }
    EXPECT_EQ(got, "Loop");
    EXPECT_EQ(t.score(), 500);
    c = Controls{};
    c.throttle = 1.0f;
    c.roll = 1.0f;
    got.clear();
    int points = 0;
    for (int i = 0; i < 60 * 4 && got.empty(); ++i) {
        step(s, c, f, flatGround(), kDt);
        const StuntTracker::Trick trick = t.update(s, s.position.y, kDt);
        got = trick.name;
        points = trick.points;
    }
    EXPECT_EQ(got, "Roll");
    EXPECT_EQ(points, 300) << "within 3 s of the loop: x1.5";
    t.crashed();
    EXPECT_EQ(t.score(), 500);
}

TEST(PlaneCombat, HeadOnPlanesMeetEvenBetweenFrames) {
    const glm::quat level = airborne(glm::vec3(0.0f), 0.0f, 1.0f).rotation;
    const glm::quat facing = airborne(glm::vec3(0.0f), 180.0f, 1.0f).rotation;
    // 90 m/s each at 30 frames a second: 6 m a step each, and they pass
    // through each other within one step.
    const PlaneShape shape = planeShape();
    const PlaneContact c = planesTouch(shape, { 0, 100, 3 }, { 0, 100, -3 }, level, { 0, 100, -3 }, { 0, 100, 3 }, facing);
    EXPECT_TRUE(c.hit);
    EXPECT_GT(c.depth, 0.5f);
    // Wing tips 12 m apart: no touch.
    EXPECT_FALSE(planesTouch(shape, { 0, 100, 3 }, { 0, 100, -3 }, level, { 12, 100, -3 }, { 12, 100, 3 }, facing).hit);
    // Side by side, wing tips overlapping: a touch pushing them apart.
    const PlaneContact side = planesTouch(shape, { 0, 100, 0 }, { 0, 100, -1 }, level, { 7.5f, 100, 0 }, { 7.5f, 100, -1 }, level);
    EXPECT_TRUE(side.hit);
    EXPECT_LT(side.normal.x, -0.5f) << "a is pushed away from b";
    // A wider plane (Synty's) reaches further out.
    const PlaneShape wide = planeShape(14.8f);
    EXPECT_GT(wide.bound, 7.4f);
    EXPECT_FALSE(planesTouch(shape, { 0, 100, 0 }, { 0, 100, -1 }, level, { 12.5f, 100, 0 }, { 12.5f, 100, -1 }, level).hit);
    EXPECT_TRUE(planesTouch(wide, { 0, 100, 0 }, { 0, 100, -1 }, level, { 12.5f, 100, 0 }, { 12.5f, 100, -1 }, level).hit);
}

TEST(PlaneCombat, BulletsHitThePlaneNotTheAirBeside) {
    const PlaneState s = airborne(glm::vec3(0.0f, 100.0f, 0.0f), 0.0f, 50.0f);
    const PlaneShape shape = planeShape();
    float t = 0.0f;
    glm::vec3 at;
    EXPECT_TRUE(bulletHits(shape, { -20, 100.6f, 0 }, { 20, 100.6f, 0 }, s.position, s.rotation, t, at)) << "through the cockpit";
    EXPECT_LT(at.x, -1.0f) << "on the side it came from";
    EXPECT_GT(at.x, -4.5f);
    EXPECT_TRUE(bulletHits(shape, { 3.5f, 120, -0.8f }, { 3.5f, 80, -0.8f }, s.position, s.rotation, t, at)) << "down through a wing";
    EXPECT_FALSE(bulletHits(shape, { -20, 104, 0 }, { 20, 104, 0 }, s.position, s.rotation, t, at)) << "over it";
    EXPECT_FALSE(bulletHits(shape, { -20, 100, 0 }, { -8, 100, 0 }, s.position, s.rotation, t, at)) << "short of it";
    // Leading a crossing target: aim ahead of it.
    const glm::vec3 lead = leadPoint({ 0, 0, 0 }, { 0, 0, -450 }, { 40, 0, 0 }, 450.0f);
    EXPECT_NEAR(lead.x, 40.0f, 0.01f);
}

TEST(PlaneCombat, TheTownStandsOnTheLandAndBlocks) {
    for (uint32_t seed : { 1u, 2u, 3u, 11u, 42u }) {
        const Island island(seed);
        const Town airfield(island, false);
        EXPECT_EQ(airfield.buildings().size(), 2u) << "the hangar and the tower";
        const Town town(island, true);
        EXPECT_GT(town.buildings().size(), 40u) << "island " << seed;
        int towers = 0;
        for (const Building& b : town.buildings()) {
            if (b.tower) ++towers;
            const glm::vec3 mid = (b.lo + b.hi) * 0.5f;
            EXPECT_LE(b.lo.y, island.terrain(mid.x, mid.z)) << "no floating buildings";
            EXPECT_FALSE(island.onRunway(mid.x, mid.z, 20.0f)) << "nothing on the runway";
        }
        EXPECT_GT(towers, 5) << "island " << seed << ": something tall to fly between";
        // Flying into a wall touches it; the street beside it doesn't.
        const Building& b = town.buildings().back();
        glm::vec3 n;
        float depth = 0.0f;
        const glm::vec3 wall((b.lo.x + b.hi.x) * 0.5f, (b.lo.y + b.hi.y) * 0.5f, b.lo.z - 0.5f);
        EXPECT_TRUE(town.touches(wall, 1.0f, n, depth));
        EXPECT_LT(n.z, -0.9f) << "out the way it came";
        EXPECT_FALSE(town.touches(wall - glm::vec3(0.0f, 0.0f, 9.0f), 1.0f, n, depth));
        float t = 0.0f;
        EXPECT_TRUE(town.blocks(wall, wall + glm::vec3(0, 0, 1), t));
        EXPECT_NEAR(t, 0.5f, 0.02f);
        EXPECT_GE(town.roof(wall.x, b.lo.z + 1.0f), b.hi.y);
        EXPECT_GT(town.centre().y, town.roof(town.centre().x, town.centre().z));
    }
}
