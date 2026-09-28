// The Flying demo's flight model and course (games/flying_demo/Flight.h,
// Course.h): planes fly, stall and land the way the README says, and
// the CPU pilots get round every island's rings.
#include "Course.h"
#include "Flight.h"
#include "FlyNet.h"
#include "Stunts.h"

#include <gtest/gtest.h>

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
    const net::Plane q = net::fromState(net::toState(p));
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
    ASSERT_EQ(t->seats.size(), 2u);
    EXPECT_EQ(t->seats[1].name, "Ace");
    EXPECT_TRUE(t->seats[1].cpu);
    EXPECT_EQ(t->seats[1].skill, 3);
    EXPECT_EQ(t->seats[0].livery, 2);
    EXPECT_FALSE(net::decodeSetup({ 1, 2 }).has_value());
    EXPECT_EQ(net::tintFromText(net::tintText(glm::vec3(1.0f, 0.5f, 0.0f)), glm::vec3(0.0f)).g, 128.0f / 255.0f);
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
