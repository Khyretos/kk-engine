// Climb Race's mountains (games/climb_race/mountains/*.yaml): every shipped
// one loads cleanly, can be climbed, and goes over the network exactly.
#include "Ghost.h"
#include "Mountains.h"
#include "NetRace.h"
#include "Progress.h"

#include "kke/ClimbWall.h"
#include "kke/Climber.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using climb_race::Mountain;

constexpr float kDt = 1.0f / 60.0f;

std::vector<Mountain> shipped() {
    std::vector<std::string> problems;
    std::vector<Mountain> m = climb_race::loadMountains(KKE_CLIMB_MOUNTAINS_DIR, problems);
    for (const std::string& p : problems) ADD_FAILURE() << p;
    return m;
}

TEST(ClimbMountains, TheTourLoadsInOrderWithoutProblems) {
    const std::vector<Mountain> tour = shipped();
    ASSERT_GE(tour.size(), 6u);
    EXPECT_EQ(tour.front().id, "pebble_hill");
    for (size_t i = 0; i < tour.size(); ++i) {
        const Mountain& m = tour[i];
        EXPECT_FALSE(m.name.empty()) << m.id;
        EXPECT_FALSE(m.about.empty()) << m.id;
        EXPECT_FALSE(m.mood.empty()) << m.id;
        EXPECT_GT(m.medals[0], 0.0f) << m.id;
        EXPECT_LE(m.medals[0], m.medals[1]) << m.id;
        EXPECT_LE(m.medals[1], m.medals[2]) << m.id;
        if (i > 0) {
            EXPECT_LT(tour[i - 1].order, m.order) << m.id;
        }
    }
}

TEST(ClimbMountains, EveryMountainCanBeClimbedByTheBot) {
    for (const Mountain& m : shipped()) {
        const kke::ClimbWall w = kke::ClimbWall::generate(m.desc);
        ASSERT_FALSE(w.route(w.desc().routeStep + 0.01f).empty()) << m.id;
        EXPECT_EQ(static_cast<int>(w.ledges().size()), m.desc.ledges) << m.id;
        kke::Climber::Settings s;
        s.maxStamina = 1e6f; // the route and the moves, not the rests
        kke::Climber c(w, s);
        kke::ClimbBot bot(w.line());
        const glm::vec3 p = w.holds()[static_cast<size_t>(bot.route().front())].position;
        ASSERT_TRUE(c.start(glm::vec3(p.x, 0.0f, w.surfaceZ(p.x, 1.0f) + 0.45f))) << m.id;
        float t = 0.0f;
        for (; t < 300.0f && c.state() != kke::Climber::State::Topped && c.state() != kke::Climber::State::Fell; t += kDt)
            c.update(bot.think(c, kDt), kDt);
        EXPECT_EQ(c.state(), kke::Climber::State::Topped) << m.id << " stuck at y " << c.hips().y << " after " << t << " s";
    }
}

TEST(ClimbMountains, MoreJugsMeansMoreJugs) {
    kke::ClimbWallDesc d;
    d.seed = 5;
    auto jugs = [](const kke::ClimbWall& w) {
        int n = 0;
        for (const kke::ClimbHold& h : w.holds()) n += h.kind == kke::ClimbHold::Kind::Jug ? 1 : 0;
        return n;
    };
    const int usual = jugs(kke::ClimbWall::generate(d));
    d.jugBias = 0.8f;
    EXPECT_GT(jugs(kke::ClimbWall::generate(d)), usual + 10);
}

TEST(ClimbMountains, MistakesAreReportedNotFatal) {
    const nlohmann::json j = { { "name", "Typo Tor" }, { "hieght", 20 }, { "height", 500 }, { "medals", { 50, 40, 60 } } };
    Mountain m;
    std::vector<std::string> problems;
    ASSERT_TRUE(climb_race::mountainFromJson(j, m, problems));
    EXPECT_EQ(m.name, "Typo Tor");
    EXPECT_FLOAT_EQ(m.desc.height, 80.0f); // clamped
    ASSERT_EQ(problems.size(), 3u);        // the unknown key, the height, the medal order
    std::vector<std::string> none;
    EXPECT_FALSE(climb_race::mountainFromJson(nlohmann::json::array(), m, none));
}

TEST(ClimbMountains, TheRaceSetupCarriesTheMountainExactly) {
    const std::vector<Mountain> tour = shipped();
    ASSERT_FALSE(tour.empty());
    climb_race::netrace::Setup s;
    s.mountain = tour.back();
    s.mountain.desc.height = 33.3333f; // not a round number
    s.round = 4;
    s.mode = 2; // Elimination
    s.seats.push_back({ 2, 1, false, "Pip", glm::vec3(0.2f, 0.4f, 0.6f) });
    const auto back = climb_race::netrace::decodeSetup(climb_race::netrace::encode(s));
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->mountain.id, s.mountain.id);
    EXPECT_EQ(back->mountain.name, s.mountain.name);
    EXPECT_EQ(back->mountain.mood, s.mountain.mood);
    EXPECT_EQ(back->mountain.desc.seed, s.mountain.desc.seed);
    EXPECT_EQ(back->mountain.desc.ledges, s.mountain.desc.ledges);
    EXPECT_EQ(back->mountain.desc.height, s.mountain.desc.height); // bit for bit
    EXPECT_EQ(back->mountain.desc.crimpBias, s.mountain.desc.crimpBias);
    EXPECT_EQ(back->mountain.medals[2], s.mountain.medals[2]);
    EXPECT_EQ(back->round, 4u);
    EXPECT_EQ(back->mode, 2);
    ASSERT_EQ(back->seats.size(), 1u);
    EXPECT_EQ(back->seats[0].name, "Pip");
}

TEST(ClimbMountains, MedalsGoByTime) {
    Mountain m;
    m.medals[0] = 30.0f;
    m.medals[1] = 40.0f;
    m.medals[2] = 60.0f;
    EXPECT_EQ(climb_race::medalFor(m, 29.0f), 0);
    EXPECT_EQ(climb_race::medalFor(m, 35.0f), 1);
    EXPECT_EQ(climb_race::medalFor(m, 60.0f), 2);
    EXPECT_EQ(climb_race::medalFor(m, 61.0f), -1);
}

TEST(ClimbProgress, FinishingAMountainOpensTheNext) {
    const std::vector<Mountain> tour = shipped();
    ASSERT_GE(tour.size(), 3u);
    climb_race::Progress p;
    EXPECT_TRUE(p.isOpen(tour, 0));
    EXPECT_FALSE(p.isOpen(tour, 1));
    EXPECT_FALSE(p.isOpen(tour, 2));
    // A slow first finish: no medal, but the next mountain opens.
    auto r = p.finish(tour, tour[0], tour[0].medals[2] + 10.0f);
    EXPECT_EQ(r.medal, -1);
    EXPECT_TRUE(r.newBest);
    EXPECT_EQ(r.opened, tour[1].id);
    EXPECT_TRUE(p.isOpen(tour, 1));
    EXPECT_FALSE(p.isOpen(tour, 2));
    // Faster: silver, a new best; nothing new opens.
    r = p.finish(tour, tour[0], tour[0].medals[1] - 0.5f);
    EXPECT_EQ(r.medal, 1);
    EXPECT_TRUE(r.betterMedal);
    EXPECT_TRUE(r.newBest);
    EXPECT_TRUE(r.opened.empty());
    // Slower again: the best and the medal stay.
    r = p.finish(tour, tour[0], tour[0].medals[2] - 0.5f);
    EXPECT_EQ(r.medal, 2);
    EXPECT_FALSE(r.betterMedal);
    EXPECT_FALSE(r.newBest);
    ASSERT_NE(p.record(tour[0].id), nullptr);
    EXPECT_EQ(p.record(tour[0].id)->medal, 1);
    EXPECT_FLOAT_EQ(p.record(tour[0].id)->best, tour[0].medals[1] - 0.5f);
    EXPECT_EQ(p.record(tour[0].id)->finishes, 3);
}

TEST(ClimbProgress, SavesAndLoadsAndRandomHasNoRecords) {
    const std::vector<Mountain> tour = shipped();
    ASSERT_GE(tour.size(), 2u);
    climb_race::Progress p;
    p.finish(tour, tour[0], tour[0].medals[0] - 1.0f);
    EXPECT_TRUE(p.finish(tour, climb_race::randomMountain(3), 12.0f).opened.empty());
    EXPECT_EQ(p.record("random"), nullptr);
    climb_race::Progress q;
    q.load(p.save());
    ASSERT_NE(q.record(tour[0].id), nullptr);
    EXPECT_EQ(q.record(tour[0].id)->medal, 0);
    EXPECT_FLOAT_EQ(q.record(tour[0].id)->best, tour[0].medals[0] - 1.0f);
    EXPECT_TRUE(q.isOpen(tour, 1));
    // A broken file starts the tour again rather than crashing.
    climb_race::Progress broken;
    broken.load(nlohmann::json{ { "mountains", { { "pebble_hill", "fast" } } } });
    EXPECT_EQ(broken.record("pebble_hill"), nullptr);
    broken.openAll = true;
    EXPECT_TRUE(broken.isOpen(tour, tour.size() - 1));
}

TEST(ClimbGhost, RecordsSavesAndPlaysBack) {
    climb_race::Ghost g;
    g.name = "Pip";
    // Up the rock at 1 m/s, a hand on a hold 0.8 m above the feet.
    for (float t = 0.0f; t <= 3.0f; t += 1.0f / 60.0f) {
        climb_race::netrace::Pose p;
        p.feet = glm::vec3(0.5f, t, 0.3f);
        p.climbing = true;
        p.grip[0] = p.feet + glm::vec3(-0.2f, 0.8f, 0.0f);
        p.grip[1] = p.feet + glm::vec3(0.2f, 0.7f, 0.0f);
        p.foot[0] = p.foot[1] = p.feet;
        p.hips = p.feet + glm::vec3(0.0f, 0.9f, 0.1f);
        g.record(t, p);
    }
    g.setTime(3.0f);
    const auto back = climb_race::Ghost::decode(g.encode());
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->name, "Pip");
    EXPECT_NEAR(back->time(), 3.0f, 0.001f);
    const climb_race::netrace::Pose mid = back->at(1.525f); // between two samples
    EXPECT_NEAR(mid.feet.y, 1.525f, 0.03f); // a sample is taken on the first frame past its time
    EXPECT_TRUE(mid.climbing);
    EXPECT_NEAR(mid.grip[0].y - mid.feet.y, 0.8f, 0.01f);
    EXPECT_NEAR(back->at(99.0f).feet.y, 3.0f, 0.06f); // after the end: the last pose
    EXPECT_FALSE(climb_race::Ghost::decode({ 1, 2, 3 }).has_value());
}

} // namespace
