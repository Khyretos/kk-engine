// The AI core (kke/ai/*, docs/AI.md): steering, senses, utility
// decisions, navmesh paths and whole-world scenarios on the farm species.
#include "kke/ai/AiWorld.h"
#include "kke/ai/Clips.h"
#include "kke/ai/Learning.h"
#include "kke/ai/NavMesh.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <algorithm>

using namespace kke::ai;

namespace {

float flatDist(const glm::vec3& a, const glm::vec3& b) { return std::hypot(a.x - b.x, a.z - b.z); }

void run(AiWorld& w, float seconds, float dt = 1.0f / 30.0f) {
    for (float t = 0.0f; t < seconds; t += dt) w.update(dt);
}

bool hasEvent(const std::vector<AiEvent>& events, AiEvent::Kind kind, AgentId who, AgentId other = 0) {
    for (const AiEvent& e : events)
        if (e.kind == kind && e.who == who && (other == 0 || e.other == other)) return true;
    return false;
}

// A flat square floor, 2 triangles, plus an optional wall box.
void addQuad(std::vector<glm::vec3>& v, std::vector<uint32_t>& idx, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
    const uint32_t base = uint32_t(v.size());
    v.insert(v.end(), { a, b, c, d });
    idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
}
void addFloor(std::vector<glm::vec3>& v, std::vector<uint32_t>& idx, float half) {
    // Counter-clockwise seen from above (+y): Recast's walkable side.
    addQuad(v, idx, { -half, 0, -half }, { -half, 0, half }, { half, 0, half }, { half, 0, -half });
}
void addBox(std::vector<glm::vec3>& v, std::vector<uint32_t>& idx, glm::vec3 mn, glm::vec3 mx) {
    const glm::vec3 p[8] = { { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z }, { mx.x, mn.y, mx.z }, { mn.x, mn.y, mx.z },
                             { mn.x, mx.y, mn.z }, { mx.x, mx.y, mn.z }, { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z } };
    addQuad(v, idx, p[4], p[7], p[6], p[5]); // top
    addQuad(v, idx, p[0], p[1], p[5], p[4]);
    addQuad(v, idx, p[1], p[2], p[6], p[5]);
    addQuad(v, idx, p[2], p[3], p[7], p[6]);
    addQuad(v, idx, p[3], p[0], p[4], p[7]);
}

} // namespace

// ---- Utility

TEST(AiUtility, CurvesHaveTheirShapes) {
    EXPECT_FLOAT_EQ(Curve::linear().evaluate(0.3f), 0.3f);
    EXPECT_FLOAT_EQ(Curve::inverse().evaluate(0.3f), 0.7f);
    EXPECT_FLOAT_EQ(Curve::step(0.5f).evaluate(0.49f), 0.0f);
    EXPECT_FLOAT_EQ(Curve::step(0.5f).evaluate(0.5f), 1.0f);
    EXPECT_NEAR(Curve::logistic(0.5f, 10.0f).evaluate(0.5f), 0.5f, 1e-4f);
    EXPECT_LT(Curve::logistic(0.5f, 10.0f).evaluate(0.1f), 0.05f);
    EXPECT_GT(Curve::logistic(0.5f, 10.0f).evaluate(0.9f), 0.95f);
    EXPECT_FLOAT_EQ(Curve::quadratic().evaluate(0.5f), 0.25f);
    // Inputs outside [0, 1] and NaN are clamped, never propagated.
    EXPECT_FLOAT_EQ(Curve::linear().evaluate(7.0f), 1.0f);
    EXPECT_FLOAT_EQ(Curve::linear().evaluate(std::nanf("")), 0.0f);
}

TEST(AiUtility, HighestScoreWinsAndMomentumKeepsTheCurrentOne) {
    std::vector<UtilityAction> actions(2);
    actions[0].name = "eat";
    actions[0].considerations = { { "hunger", Curve::linear() } };
    actions[1].name = "sleep";
    actions[1].considerations = { { "tired", Curve::linear() } };
    float hunger = 0.6f, tired = 0.5f;
    InputFn in = [&](const std::string& n) { return n == "hunger" ? hunger : n == "tired" ? tired : 0.0f; };
    EXPECT_EQ(chooseAction(actions, in, -1).index, 0);
    hunger = 0.5f;
    tired = 0.55f;
    EXPECT_EQ(chooseAction(actions, in, -1).index, 1);
    // Already eating: a slightly better alternative doesn't flip it back and forth.
    EXPECT_EQ(chooseAction(actions, in, 0).index, 0);
    // A blocked (cooling down) action is skipped.
    EXPECT_EQ(chooseAction(actions, in, -1, [](int i) { return i == 1; }).index, 0);
}

TEST(AiUtility, CompensationDoesNotPunishMoreConsiderations) {
    UtilityAction one, three;
    one.considerations = { { "x", Curve::linear() } };
    three.considerations = { { "x", Curve::linear() }, { "x", Curve::linear() }, { "x", Curve::linear() } };
    InputFn in = [](const std::string&) { return 0.8f; };
    const float s1 = scoreAction(one, in), s3 = scoreAction(three, in);
    EXPECT_FLOAT_EQ(s1, 0.8f);
    EXPECT_GT(s3, 0.8f * 0.8f * 0.8f); // raw product 0.512
    EXPECT_LT(s3, s1 + 1e-4f);
}

// ---- Steering

TEST(AiSteering, SeekFleeArrive) {
    Mover m;
    m.position = { 0, 0, 0 };
    const glm::vec3 s = seek(m, { 10, 0, 0 });
    EXPECT_GT(s.x, 0.0f);
    EXPECT_LT(flee(m, { 10, 0, 0 }).x, 0.0f);
    EXPECT_EQ(flee(m, { 10, 0, 0 }, 5.0f), glm::vec3(0.0f)); // out of panic range
    // Arriving from rest ends up on the target and stopped.
    for (int i = 0; i < 600; ++i) integrate(m, arrive(m, { 5, 0, 0 }, 2.0f), 1.0f / 60.0f);
    EXPECT_NEAR(m.position.x, 5.0f, 0.1f);
    EXPECT_LT(glm::length(m.velocity), 0.2f);
}

TEST(AiSteering, SeparationPushesApartAndGridFindsNeighbours) {
    Mover m;
    m.position = { 0, 0, 0 };
    Neighbour n{ { 0.3f, 0, 0 }, glm::vec3(0.0f), 0.4f };
    EXPECT_LT(separation(m, std::span<const Neighbour>(&n, 1)).x, 0.0f);

    SpatialGrid grid(4.0f);
    std::vector<glm::vec3> pos{ { 0, 0, 0 }, { 3, 0, 0 }, { 9, 0, 0 }, { -3.5f, 0, 1 } };
    for (uint32_t i = 0; i < pos.size(); ++i) grid.insert(i, pos[i]);
    std::vector<uint32_t> found;
    grid.query({ 0, 0, 0 }, 4.0f, [&](uint32_t id) { return pos[id]; }, found);
    std::sort(found.begin(), found.end());
    EXPECT_EQ(found, (std::vector<uint32_t>{ 0, 1, 3 }));
}

TEST(AiSteering, AvoidsAnObstacleAhead) {
    Mover m;
    m.position = { 0, 0, 0 };
    m.velocity = { 0, 0, 2 };
    CircleObstacle post{ { 0.1f, 0, 1.5f }, 0.5f };
    const glm::vec3 f = avoidObstacles(m, std::span<const CircleObstacle>(&post, 1));
    EXPECT_LT(f.x, 0.0f); // steers left, away from the post slightly to the right
    CircleObstacle behind{ { 0, 0, -3 }, 0.5f };
    EXPECT_EQ(avoidObstacles(m, std::span<const CircleObstacle>(&behind, 1)), glm::vec3(0.0f));
}

// ---- Senses

TEST(AiPerception, SightHasRangeAndCone) {
    Senses s;
    s.sightRange = 20.0f;
    s.fovDegrees = 120.0f;
    const glm::vec3 eye(0, 1, 0), fwd(0, 0, 1);
    EXPECT_FLOAT_EQ(sightStrength(eye, fwd, s, { 0, 1, 5 }), 1.0f);
    EXPECT_GT(sightStrength(eye, fwd, s, { 0, 1, 15 }), 0.0f);
    EXPECT_LT(sightStrength(eye, fwd, s, { 0, 1, 15 }), 1.0f);
    EXPECT_EQ(sightStrength(eye, fwd, s, { 0, 1, 25 }), 0.0f);
    EXPECT_EQ(sightStrength(eye, fwd, s, { 0, 1, -5 }), 0.0f); // behind
    // Crouching (lower visibility) is seen from less far.
    EXPECT_EQ(sightStrength(eye, fwd, s, { 0, 1, 15 }, Conspicuity{ 0.5f, 1.0f }), 0.0f);
}

TEST(AiPerception, AwarenessBuildsUpAndFades) {
    Senses s;
    Awareness a;
    int spottedAt = -1;
    for (int i = 0; i < 100 && spottedAt < 0; ++i)
        if (updateAwareness(a, 0.4f, 0.1f, float(i) * 0.1f, s) > 0) spottedAt = i;
    ASSERT_GT(spottedAt, 0); // a faint stimulus takes a while
    Awareness b;
    EXPECT_EQ(updateAwareness(b, 1.0f, 0.1f, 0.0f, s), 1); // a strong one is instant
    int lostAt = -1;
    for (int i = 0; i < 200 && lostAt < 0; ++i)
        if (updateAwareness(b, 0.0f, 0.1f, float(i) * 0.1f, s) < 0) lostAt = i;
    EXPECT_GT(lostAt, 0);
}

TEST(AiPerception, HearingAndScentTrails) {
    Senses s;
    EXPECT_FLOAT_EQ(hearingStrength({ 0, 0, 0 }, s, Noise{ { 3, 0, 0 }, 25.0f }), 1.0f);
    EXPECT_EQ(hearingStrength({ 0, 0, 0 }, s, Noise{ { 30, 0, 0 }, 25.0f }), 0.0f);

    ScentField field;
    for (int i = 0; i < 10; ++i) field.emit(7, { float(i), 0, 0 });
    EXPECT_EQ(field.size(), 10u);
    std::vector<ScentField::Smelled> out;
    field.smell({ 9, 0, 3 }, s, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].source, 7u);
    // Downwind of the trail, smelled from further away.
    std::vector<ScentField::Smelled> none, downwind;
    field.smell({ 9, 0, 9 }, s, none);
    EXPECT_TRUE(none.empty());
    field.wind = { 0, 0, 3 };
    field.smell({ 9, 0, 9 }, s, downwind);
    EXPECT_FALSE(downwind.empty());
    // Trails fade away.
    for (int i = 0; i < 200; ++i) field.update(0.1f);
    EXPECT_EQ(field.size(), 0u);
}

// ---- Navmesh

TEST(AiNavMesh, PathGoesAroundAWall) {
    std::vector<glm::vec3> v;
    std::vector<uint32_t> idx;
    addFloor(v, idx, 10.0f);
    addBox(v, idx, { -6, 0, -0.5f }, { 6, 2, 0.5f }); // a wall across the middle
    NavMesh nav;
    std::string error;
    ASSERT_TRUE(nav.build(v, idx, {}, &error)) << error;
    EXPECT_GT(nav.polygonCount(), 0u);

    NavMesh::Path p;
    ASSERT_TRUE(nav.findPath({ 0, 0, -5 }, { 0, 0, 5 }, p));
    EXPECT_FALSE(p.partial);
    ASSERT_GE(p.points.size(), 3u); // start, round an end of the wall, goal
    bool rounded = false;
    for (const glm::vec3& c : p.points) rounded = rounded || std::abs(c.x) >= 6.0f;
    EXPECT_TRUE(rounded);
    EXPECT_NEAR(p.points.back().z, 5.0f, 0.3f);

    EXPECT_FALSE(nav.walkable({ 0, 0, -5 }, { 0, 0, 5 }));
    EXPECT_TRUE(nav.walkable({ -8, 0, -5 }, { -8, 0, 5 }));
    // Sliding into the wall stops at it.
    const glm::vec3 slid = nav.moveAlongSurface({ 0, 0, -3 }, { 0, 0, 3 });
    EXPECT_LT(slid.z, -0.5f);

    // Saved and loaded, it answers the same.
    NavMesh copy;
    ASSERT_TRUE(copy.load(nav.save(), &error)) << error;
    NavMesh::Path p2;
    ASSERT_TRUE(copy.findPath({ 0, 0, -5 }, { 0, 0, 5 }, p2));
    EXPECT_EQ(p2.points.size(), p.points.size());

    float seq = 0.1f;
    glm::vec3 r;
    ASSERT_TRUE(nav.randomPointNear({ 5, 0, 5 }, 3.0f, [&] { return seq = std::fmod(seq + 0.37f, 1.0f); }, r));
    EXPECT_LT(std::abs(r.x), 10.0f);
}

TEST(AiNavMesh, RejectsNothingWalkable) {
    NavMesh nav;
    std::string error;
    std::vector<glm::vec3> v;
    std::vector<uint32_t> idx;
    EXPECT_FALSE(nav.build(v, idx, {}, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(nav.valid());
    EXPECT_FALSE(nav.load(std::vector<uint8_t>(10, 0), &error));
}

// ---- The world: farm scenarios

TEST(AiWorld, SheepRunFromTheDogAndCalmDown) {
    AiWorld w(42);
    // Facing the gate the dog comes through (sheep see almost all round,
    // but not straight behind).
    for (uint32_t i = 0; i < 6; ++i) ASSERT_TRUE(w.addAgent(10 + i, "sheep", { float(i % 3) * 1.5f, 0, float(i / 3) * 1.5f }, 180.0f));
    ASSERT_TRUE(w.addActor(1, "dog", { 1.5f, 0, -14 }));
    std::vector<AiEvent> events;
    auto step = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            w.update(1.0f / 30.0f);
            for (AiEvent& e : w.takeEvents()) events.push_back(e);
        }
    };
    step(30);
    // The dog runs at them and stops in the middle of where they stood.
    for (int i = 0; i < 70; ++i) {
        w.setTransform(1, { 1.5f, 0, std::min(-14.0f + float(i) * 0.2f, 0.75f) }, { 0, 0, 6 }, 0.0f);
        step(1);
    }
    step(60);
    const glm::vec3 dog = w.agent(1)->position;
    for (uint32_t i = 0; i < 6; ++i) {
        EXPECT_TRUE(hasEvent(events, AiEvent::Kind::Scared, 10 + i, 1)) << w.describe(10 + i);
        EXPECT_GT(flatDist(w.agent(10 + i)->position, dog), 7.0f) << w.describe(10 + i);
    }
    // The dog leaves; after a while they calm down.
    w.setTransform(1, { 200, 0, -200 }, glm::vec3(0.0f), 0.0f);
    run(w, 25.0f);
    for (uint32_t i = 0; i < 6; ++i) EXPECT_NE(w.actionName(10 + i), "flee") << w.describe(10 + i);
    // And they stay together as a flock.
    const glm::vec3 c = w.agent(10)->position;
    for (uint32_t i = 1; i < 6; ++i) EXPECT_LT(flatDist(w.agent(10 + i)->position, c), 12.0f) << w.describe(10 + i);
}

TEST(AiWorld, CuriousCowComesToLookButKeepsItsDistance) {
    AiWorld w(3);
    ASSERT_TRUE(w.addAgent(20, "cow", { 0, 0, 0 }));
    w.setNeed(20, "hunger", 0.0f);
    w.setNeed(20, "thirst", 0.0f);
    ASSERT_TRUE(w.addActor(1, "dog", { 0, 0, 8 }));
    run(w, 6.0f);
    EXPECT_EQ(w.actionName(20), "investigate") << w.describe(20);
    const float d = flatDist(w.agent(20)->position, w.agent(1)->position);
    EXPECT_LT(d, 6.0f);
    EXPECT_GT(d, 2.0f);
}

TEST(AiWorld, GooseAttacksTheDog) {
    AiWorld w(5);
    ASSERT_TRUE(w.addAgent(30, "goose", { 0, 0, 0 }));
    ASSERT_TRUE(w.addActor(1, "dog", { 0, 0, 4 }));
    std::vector<AiEvent> events;
    for (int i = 0; i < 150; ++i) {
        w.update(1.0f / 30.0f);
        for (AiEvent& e : w.takeEvents()) events.push_back(e);
    }
    EXPECT_EQ(w.attitude(30, 1), Attitude::Hostile);
    EXPECT_TRUE(hasEvent(events, AiEvent::Kind::Attack, 30, 1)) << w.describe(30);
}

TEST(AiWorld, FoxHuntsChickensAndFleesTheFarmer) {
    AiWorld w(9);
    ASSERT_TRUE(w.addAgent(40, "fox", { 0, 0, 0 }));
    w.setNeed(40, "hunger", 0.9f);
    // The chicken faces the fox (a fox creeping up from behind gets it).
    ASSERT_TRUE(w.addAgent(41, "chicken", { 0, 0, 8 }, 180.0f));
    run(w, 1.0f);
    EXPECT_EQ(w.actionName(40), "hunt") << w.describe(40);
    EXPECT_EQ(w.actionName(41), "flee") << w.describe(41);
    // The farmer comes striding towards the fox.
    ASSERT_TRUE(w.addActor(2, "farmer", { 0, 0, 16 }));
    for (int i = 0; i < 45; ++i) {
        w.setTransform(2, { 0, 0, 16.0f - float(i) * 0.05f }, { 0, 0, -1.4f }, 180.0f);
        w.update(1.0f / 30.0f);
    }
    EXPECT_EQ(w.actionName(40), "flee") << w.describe(40);
}

TEST(AiWorld, OrdersWinAndReportArrival) {
    AiWorld w(11);
    ASSERT_TRUE(w.addAgent(50, "dog", { 0, 0, 0 }));
    ASSERT_TRUE(w.addActor(1, "farmer", { 0, 0, 0 }));
    ASSERT_TRUE(w.addAgent(60, "sheep", { 30, 0, 30 })); // something to fetch
    // Go there: arrives, then holds that spot.
    w.order(50, Order{ Order::Kind::MoveTo, 0, { 8, 0, 0 }, 1.0f, false });
    std::vector<AiEvent> events;
    for (int i = 0; i < 300; ++i) {
        w.update(1.0f / 30.0f);
        for (AiEvent& e : w.takeEvents()) events.push_back(e);
    }
    EXPECT_TRUE(hasEvent(events, AiEvent::Kind::Arrived, 50));
    EXPECT_LT(flatDist(w.agent(50)->position, { 8, 0, 0 }), 1.2f);
    EXPECT_EQ(w.agent(50)->order.kind, Order::Kind::Hold);
    // Follow: keeps near the farmer as they walk off.
    w.order(50, Order{ Order::Kind::Follow, 1, glm::vec3(0.0f), 2.5f, false });
    for (int i = 0; i < 300; ++i) {
        w.setTransform(1, { 0, 0, float(i) * 0.04f }, { 0, 0, 1.2f }, 0.0f);
        w.update(1.0f / 30.0f);
    }
    EXPECT_LT(flatDist(w.agent(50)->position, w.agent(1)->position), 4.0f) << w.describe(50);
    EXPECT_EQ(w.actionName(50), "order_follow");
    // Interact (fetch): goes to the thing, fires Arrived with it, order ends.
    w.order(50, Order{ Order::Kind::Interact, 60, glm::vec3(0.0f), 2.0f, true });
    events.clear();
    for (int i = 0; i < 400 && !hasEvent(events, AiEvent::Kind::Arrived, 50, 60); ++i) {
        w.update(1.0f / 30.0f);
        for (AiEvent& e : w.takeEvents()) events.push_back(e);
    }
    EXPECT_TRUE(hasEvent(events, AiEvent::Kind::Arrived, 50, 60)) << w.describe(50);
    EXPECT_EQ(w.agent(50)->order.kind, Order::Kind::None);
}

TEST(AiWorld, TeamsMakeFriendsAndEnemies) {
    AiWorld w;
    ASSERT_TRUE(w.addAgent(1, "farmer", { 0, 0, 0 }));
    ASSERT_TRUE(w.addAgent(2, "farmer", { 5, 0, 0 }));
    ASSERT_TRUE(w.addAgent(3, "farmer", { 10, 0, 0 }));
    w.setTeam(1, 1);
    w.setTeam(2, 1);
    w.setTeam(3, 2);
    EXPECT_EQ(w.attitude(1, 2), Attitude::Friendly);
    EXPECT_EQ(w.attitude(1, 3), Attitude::Hostile);
    w.setAttitude(1, 3, Attitude::Ignore);
    EXPECT_EQ(w.attitude(1, 3), Attitude::Ignore);
}

TEST(AiWorld, NoisesAreHeardAndRemovedAgentsForgotten) {
    AiWorld w;
    ASSERT_TRUE(w.addAgent(10, "sheep", { 0, 0, 0 }));
    ASSERT_TRUE(w.addActor(1, "dog", { 0, 0, -30 })); // out of sight
    run(w, 0.5f);
    w.takeEvents();
    w.makeNoise(Noise{ { 0, 0, -20 }, 25.0f, 1, 0 }); // a bark
    run(w, 0.3f);
    const std::vector<AiEvent> events = w.takeEvents();
    EXPECT_TRUE(hasEvent(events, AiEvent::Kind::Heard, 10, 1));
    EXPECT_FALSE(w.agent(10)->memory.empty());
    ASSERT_TRUE(w.remove(1));
    EXPECT_TRUE(w.agent(10)->memory.empty());
    EXPECT_FALSE(w.has(1));
}

TEST(AiWorld, WalksAroundWallsOnTheNavMesh) {
    std::vector<glm::vec3> v;
    std::vector<uint32_t> idx;
    addFloor(v, idx, 12.0f);
    addBox(v, idx, { -7, 0, -0.5f }, { 7, 2, 0.5f });
    NavMesh nav;
    ASSERT_TRUE(nav.build(v, idx));
    AiWorld w;
    w.setNavMesh(&nav);
    ASSERT_TRUE(w.addAgent(50, "dog", { 0, 0, -6 }));
    w.order(50, Order{ Order::Kind::MoveTo, 0, { 0, 0, 6 }, 0.5f, true });
    for (int i = 0; i < 900 && w.agent(50)->order.kind == Order::Kind::MoveTo; ++i) {
        w.update(1.0f / 30.0f);
        EXPECT_FALSE(std::abs(w.agent(50)->position.z) < 0.4f && std::abs(w.agent(50)->position.x) < 6.8f) << "walked through the wall";
    }
    EXPECT_LT(flatDist(w.agent(50)->position, { 0, 0, 6 }), 1.2f) << w.describe(50);
}

TEST(AiWorld, SameSeedSameStory) {
    auto story = [](uint64_t seed) {
        AiWorld w(seed);
        for (uint32_t i = 0; i < 8; ++i) w.addAgent(10 + i, i % 2 ? "chicken" : "sheep", { float(i), 0, float(i % 3) });
        w.addAgent(2, "fox", { 10, 0, 10 });
        run(w, 10.0f);
        std::vector<float> out;
        for (const Agent& a : w.agents()) out.insert(out.end(), { a.position.x, a.position.z, float(a.action) });
        return out;
    };
    EXPECT_EQ(story(7), story(7));
}

TEST(AiSpecies, JsonAndYamlRoundTrip) {
    for (const Species& s : builtinSpecies()) {
        Species copy;
        std::string error;
        Species withActions = s;
        withActions.actions = defaultActions(s);
        ASSERT_TRUE(speciesFromJson(speciesToJson(withActions), copy, &error)) << error;
        EXPECT_EQ(speciesToJson(copy), speciesToJson(withActions));
    }
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "kke_ai_species_test";
    std::filesystem::create_directories(dir);
    {
        std::ofstream f(dir / "wolf.yml");
        f << "species:\n"
             "  - id: wolf\n"
             "    runSpeed: 9\n"
             "    hunts: [sheep, chicken]\n"
             "    temperament: { boldness: 0.7, aggression: 0.5 }\n"
             "  - id: sheep\n"
             "    walkSpeed: 0.5\n";
    }
    AiWorld w;
    std::string error;
    EXPECT_EQ(w.loadSpecies(dir / "wolf.yml", &error), 2) << error;
    ASSERT_NE(w.species("wolf"), nullptr);
    EXPECT_FLOAT_EQ(w.species("wolf")->runSpeed, 9.0f);
    EXPECT_FLOAT_EQ(w.species("wolf")->temperament.boldness, 0.7f);
    // A partial entry tweaks the built-in, keeping everything else.
    EXPECT_FLOAT_EQ(w.species("sheep")->walkSpeed, 0.5f);
    EXPECT_TRUE(w.species("sheep")->flocks);
    // Sheep already fear wolves.
    w.addAgent(1, "sheep", { 0, 0, 0 });
    w.addAgent(2, "wolf", { 0, 0, 5 });
    EXPECT_EQ(w.attitude(1, 2), Attitude::Fear);
    EXPECT_EQ(w.attitude(2, 1), Attitude::Hunt);
    {
        std::ofstream f(dir / "bad.json");
        f << R"({ "id": "x", "actions": [ { "name": "a", "considerations": [ { "input": "fear", "curve": "wobbly" } ] } ] })";
    }
    EXPECT_EQ(w.loadSpecies(dir / "bad.json", &error), 0);
    EXPECT_NE(error.find("wobbly"), std::string::npos);
    std::filesystem::remove_all(dir);
}

// ---- Lua and the node graph

#if KKE_ENABLE_LUA
#include "kke/NodeGraph.h"
#include "kke/ScriptVM.h"
#include "kke/ai/AiScript.h"

#include <lua.h>

namespace {

struct LuaFarm {
    kke::ScriptVM vm;
    AiWorld world{ 21 };
    std::unordered_map<uint32_t, glm::vec3> things;
    LuaFarm() {
        bindAi(vm, world, [this](uint32_t t, glm::vec3& p) {
            auto it = things.find(t);
            if (it == things.end()) return false;
            p = it->second;
            return true;
        });
    }
    void step(float seconds) {
        for (float t = 0.0f; t < seconds; t += 1.0f / 30.0f) {
            world.update(1.0f / 30.0f);
            fireAiEvents(vm, world.takeEvents());
        }
    }
};

} // namespace

TEST(AiLua, EveryAiFunctionIsDocumented) {
    LuaFarm f;
    lua_State* L = f.vm.state();
    lua_getglobal(L, "ai");
    ASSERT_TRUE(lua_istable(L, -1));
    int count = 0;
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        ++count;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    EXPECT_EQ(size_t(count), aiApiFunctions().size());
    EXPECT_EQ(f.vm.apiEvents().size(), aiApiEvents().size());
}

TEST(AiLua, ScriptsTeachAndLearn) {
    LuaFarm f;
    f.things[5] = { 0, 0, 0 };
    ASSERT_TRUE(f.vm.runString(R"(
        assert(ai.add(5, "sheep"))
        assert(not ai.teach(5, "fly"))
        for i = 1, 12 do
            ai.setNeed(5, "tiredness", i % 2)
            assert(ai.teach(5, (i % 2 == 1) and "rest" or "graze"))
        end
        local right = ai.learn("sheep")
        assert(right >= 0 and right <= 1, "share right " .. tostring(right))
        ai.unlearn("sheep")
    )", "teach")) << f.vm.errors().back().message;
    ASSERT_NE(f.world.policy("sheep"), nullptr);
    EXPECT_EQ(f.world.policy("sheep")->exampleCount(), 12u);
    EXPECT_FALSE(f.world.policy("sheep")->trained());
}

TEST(AiLua, ScriptsGiveMindsOrdersAndHearEvents) {
    LuaFarm f;
    f.things[5] = { 0, 0, 0 };
    f.things[6] = { 0, 0, 6 };
    ASSERT_TRUE(f.vm.runString(R"(
        assert(ai.add(5, "sheep"))
        assert(ai.add(6, "dog"))
        assert(not ai.add(99, "sheep"))         -- no such thing
        assert(not ai.add(5, "dragon") == true) -- no such species (re-add fails, 5 is gone)
        assert(ai.add(5, "sheep"))
        ai.setNeed(5, "hunger", 0.9)
        assert(math.abs(ai.need(5, "hunger") - 0.9) < 1e-6)
        ai.setMood(5, "boldness", 0)
        ai.goTo(6, Vec(0, 0, 1), true)
        -- (Each script has its own globals: the hooks note what happened on the agents.)
        hook.Add("Scared", "t", function(e) if e.who == 5 and e.of == 6 then ai.setInput(5, "was_scared", 1) end end)
        hook.Add("Arrived", "t", function(e) ai.setInput(e.who, "arrived", 1) end)
        ai.defineSpecies{ id = "wolf", runSpeed = 9, hunts = { "sheep" } }
        local found = false
        for _, s in ipairs(ai.species()) do found = found or s == "wolf" end
        assert(found)
    )", "farm")) << f.vm.errors().back().message;
    f.step(3.0f);
    EXPECT_EQ(f.world.input(5, "was_scared"), 1.0f) << f.world.describe(5);
    EXPECT_EQ(f.world.input(6, "arrived"), 1.0f) << f.world.describe(6);
    ASSERT_TRUE(f.vm.runString(R"(
        assert(ai.doing(6) == "order_hold")
        assert(ai.knows(5, 6))
        ai.free(6)
        ai.feel(5, 6, "friendly")
    )", "check")) << f.vm.errors().back().message;
    f.step(15.0f);
    EXPECT_NE(f.world.actionName(5), "flee") << f.world.describe(5);
    EXPECT_FALSE(f.vm.runString("ai.feel(5, 6, 'grumpy')", "bad"));
}

TEST(AiLua, NodeGraphWhenScaredMakeANoise) {
    LuaFarm f;
    f.things[5] = { 0, 0, 0 };
    f.things[6] = { 0, 0, 4 };
    ASSERT_TRUE(f.vm.runString("ai.add(5, 'sheep'); ai.setMood(5, 'boldness', 0)", "setup"));
    const kke::NodeLibrary lib = kke::NodeLibrary::fromApi(f.vm.apiFunctions(), f.vm.apiEvents());
    ASSERT_NE(lib.find("event:Scared"), nullptr);
    ASSERT_NE(lib.find("call:ai.follow"), nullptr);
    kke::NodeGraph g;
    const int when = g.add("event:Scared", { 0, 0 });
    const int bleat = g.add("call:ai.noise", { 300, 0 });
    ASSERT_TRUE(g.connect(lib, { when, ">", bleat, ">" }));
    kke::CompileOptions opt;
    opt.source = "graph:sheep";
    const kke::CompiledGraph c = kke::compileGraph(g, lib, opt);
    ASSERT_TRUE(c.ok()) << c.lua;
    ASSERT_TRUE(f.vm.reloadString(c.lua, opt.source)) << f.vm.errors().back().message;
    // A dog turns up: the sheep bolts and the graph makes a noise, which
    // the dog (an agent here) hears.
    f.world.addAgent(6, "dog", f.things[6]);
    std::vector<AiEvent> heard;
    for (int i = 0; i < 60; ++i) {
        f.world.update(1.0f / 30.0f);
        std::vector<AiEvent> ev = f.world.takeEvents();
        for (const AiEvent& e : ev)
            if (e.kind == AiEvent::Kind::Heard && e.who == 6) heard.push_back(e);
        fireAiEvents(f.vm, ev);
    }
    EXPECT_FALSE(heard.empty());
    EXPECT_TRUE(f.vm.errors().empty()) << f.vm.errors().back().message;
}
#endif

// ---------------------------------------------------------------- clips

namespace {
kke::ModelData withClips(std::initializer_list<const char*> names) {
    kke::ModelData d;
    for (const char* n : names) d.animations.push_back({ n, 1.0f, 30.0f, {} });
    return d;
}
} // namespace

TEST(AiClips, PicksAModelsClipForWhatTheAnimalDoes) {
    // Quaternius' cow: a slow walk for walking, the run for running.
    const kke::ModelData cow = withClips({ "Armature|Death", "Armature|Idle", "Armature|Jump", "Armature|Run", "Armature|Walk", "Armature|WalkSlow" });
    EXPECT_EQ(clipForAnim(cow, "idle").clip, 1);
    EXPECT_EQ(clipForAnim(cow, "walk").clip, 5);
    EXPECT_EQ(clipForAnim(cow, "run").clip, 3);
    EXPECT_EQ(clipForAnim(cow, "eat").clip, 1);   // no eat clip: stands
    EXPECT_EQ(clipForAnim(cow, "graze").clip, 1); // unknown name: idle
    // Quaternius' sheep only has Idle and Jump: it hops, slower when walking.
    const kke::ModelData sheep = withClips({ "Armature|Idle", "Armature|Jump" });
    EXPECT_EQ(clipForAnim(sheep, "walk").clip, 1);
    EXPECT_FLOAT_EQ(clipForAnim(sheep, "walk").speed, 0.7f);
    EXPECT_EQ(clipForAnim(sheep, "run").clip, 1);
    EXPECT_FLOAT_EQ(clipForAnim(sheep, "run").speed, 1.0f);
    // Exact names win (clips appended under the AI's own names).
    const kke::ModelData dog = withClips({ "rest", "idle", "walk", "run", "eat" });
    EXPECT_EQ(clipForAnim(dog, "idle").clip, 1);
    EXPECT_EQ(clipForAnim(dog, "eat").clip, 4);
    EXPECT_EQ(clipForAnim(withClips({}), "idle").clip, -1);
}

// ---------------------------------------------------------------- teaching by example

namespace {
// x and y in [0, 1): "b" when x > y, else "a".
LearnedPolicy taughtXY(uint64_t seed) {
    LearnedPolicy p;
    p.setup({ "x", "y" }, { "a", "b" });
    uint64_t s = 42;
    auto r = [&] {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return float((s >> 33) % 1000) / 1000.0f;
    };
    for (int i = 0; i < 80; ++i) {
        const float x = r(), y = r();
        if (std::abs(x - y) < 0.05f) continue; // no coin flips in the lesson
        p.addExample({ x, y }, x > y ? 1 : 0);
    }
    p.train(400, 0.5f, seed);
    return p;
}
} // namespace

TEST(AiLearning, LearnsWhatItIsShown) {
    const LearnedPolicy p = taughtXY(7);
    ASSERT_TRUE(p.trained());
    EXPECT_EQ(p.best({ 0.9f, 0.1f }), "b");
    EXPECT_EQ(p.best({ 0.1f, 0.8f }), "a");
    const std::vector<float> probs = p.predict({ 0.8f, 0.2f });
    ASSERT_EQ(probs.size(), 2u);
    EXPECT_NEAR(probs[0] + probs[1], 1.0f, 1e-5f);
    EXPECT_GT(probs[1], 0.7f);
    EXPECT_TRUE(p.predict({ 0.5f }).empty()); // wrong size
    EXPECT_TRUE(LearnedPolicy().predict({ 0.5f, 0.5f }).empty()); // untrained
}

TEST(AiLearning, SameExamplesAndSeedSameNetwork) {
    EXPECT_EQ(taughtXY(3).toJson(), taughtXY(3).toJson());
    EXPECT_NE(taughtXY(3).toJson()["weights"], taughtXY(4).toJson()["weights"]);
}

TEST(AiLearning, SavesAndLoadsAsJsonOrYaml) {
    const LearnedPolicy p = taughtXY(5);
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "kke_ai_learning";
    std::filesystem::create_directories(dir);
    for (const char* name : { "policy.json", "policy.yml" }) {
        std::string error;
        ASSERT_TRUE(p.save(dir / name, &error)) << error;
        LearnedPolicy back;
        ASSERT_TRUE(back.load(dir / name, &error)) << error;
        EXPECT_EQ(back.exampleCount(), p.exampleCount());
        EXPECT_EQ(back.predict({ 0.7f, 0.3f }), p.predict({ 0.7f, 0.3f })) << name;
    }
    LearnedPolicy bad;
    std::string error;
    EXPECT_FALSE(bad.fromJson(nlohmann::json{ { "format", "kke.ai.policy" }, { "features", { "x" } }, { "actions", { "a" } }, { "weights", { 1.0 } } }, &error));
    EXPECT_NE(error.find("weights"), std::string::npos) << error;
    std::filesystem::remove_all(dir);
}

TEST(AiLearning, ATaughtAnimalLeansTowardsWhatItWasShown) {
    // Two things it could do: "left" when x is high, "right" when y is.
    // It is taught the opposite, and after learning it does as taught.
    AiWorld w(9);
    Species s;
    s.id = "pupil";
    UtilityAction left{ "left", "idle", 1.0f, { { "x", Curve::linear() } } };
    UtilityAction right{ "right", "idle", 1.0f, { { "y", Curve::linear() } } };
    s.actions = { left, right };
    w.defineSpecies(s);
    ASSERT_TRUE(w.addAgent(1, "pupil", glm::vec3(0.0f)));
    auto situation = [&](float x, float y) {
        w.setInput(1, "x", x);
        w.setInput(1, "y", y);
        for (int i = 0; i < 8; ++i) w.update(0.1f);
        return w.actionName(1);
    };
    EXPECT_EQ(situation(0.8f, 0.3f), "left");
    EXPECT_FALSE(w.teach(1, "fly")); // not one of its actions
    EXPECT_FALSE(w.teach(99, "left"));
    for (int i = 0; i < 40; ++i) {
        const float x = 0.2f + 0.6f * float(i % 10) / 10.0f, y = 0.8f - 0.6f * float(i % 10) / 10.0f + (i < 20 ? 0.05f : -0.05f);
        if (std::abs(x - y) < 0.05f) continue;
        w.setInput(1, "x", x);
        w.setInput(1, "y", y);
        ASSERT_TRUE(w.teach(1, x > y ? "right" : "left"));
    }
    const LearnedPolicy::Result r = w.learn("pupil");
    EXPECT_GE(r.accuracy, 0.9f);
    ASSERT_NE(w.policy("pupil"), nullptr);
    EXPECT_EQ(w.policy("pupil")->features(), (std::vector<std::string>{ "x", "y" }));
    EXPECT_EQ(situation(0.8f, 0.3f), "right");
    EXPECT_EQ(situation(0.3f, 0.8f), "left");
    w.unlearn("pupil");
    EXPECT_EQ(situation(0.8f, 0.3f), "left"); // instincts again
    EXPECT_GT(w.policy("pupil")->exampleCount(), 30u); // what it was shown is kept
}
