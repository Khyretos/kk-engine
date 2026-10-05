#include "kke/Orders.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace {

float flatDistance(const glm::vec3& a, const glm::vec3& b) { return glm::length(glm::vec2(a.x - b.x, a.z - b.z)); }

bool segmentsCross(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d) {
    auto orient = [](const glm::vec3& p, const glm::vec3& q, const glm::vec3& r) {
        return (q.x - p.x) * (r.z - p.z) - (q.z - p.z) * (r.x - p.x);
    };
    const float o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
    return o1 * o2 < 0.0f && o3 * o4 < 0.0f;
}

} // namespace

TEST(Orders, NamesRoundTripAndSynonyms) {
    for (kke::OrderKind k : { kke::OrderKind::Move, kke::OrderKind::Follow, kke::OrderKind::Stay, kke::OrderKind::Attack,
                              kke::OrderKind::FocusFire, kke::OrderKind::Fetch, kke::OrderKind::Drop, kke::OrderKind::Sit,
                              kke::OrderKind::Pet, kke::OrderKind::Regroup, kke::OrderKind::Free }) {
        EXPECT_EQ(kke::orderFromName(kke::orderName(k)), k) << kke::orderName(k);
        EXPECT_STRNE(kke::orderLabel(k), "");
    }
    EXPECT_EQ(kke::orderFromName("come"), kke::OrderKind::Follow);
    EXPECT_EQ(kke::orderFromName("hold"), kke::OrderKind::Stay);
    EXPECT_EQ(kke::orderFromName("banana"), kke::OrderKind::None);
    EXPECT_STREQ(kke::orderName(kke::OrderKind::None), "");
}

TEST(Orders, YawMatchesCameraRig) {
    const glm::vec3 f0 = kke::yawForward(0.0f), f90 = kke::yawForward(90.0f);
    EXPECT_NEAR(f0.z, -1.0f, 1e-5f);
    EXPECT_NEAR(f90.x, 1.0f, 1e-5f);
    for (float yaw : { -170.0f, -45.0f, 0.0f, 30.0f, 120.0f })
        EXPECT_NEAR(kke::yawOf(kke::yawForward(yaw)), yaw, 1e-3f);
}

TEST(Orders, FormationsHaveTheRightCountAndSpacing) {
    for (kke::Formation f : { kke::Formation::Line, kke::Formation::Wedge, kke::Formation::Column, kke::Formation::Circle }) {
        for (int n : { 1, 2, 3, 5, 8 }) {
            const auto slots = kke::formationSlots(f, n, glm::vec3(10.0f, 2.0f, -4.0f), 35.0f, 1.5f);
            ASSERT_EQ(int(slots.size()), n) << kke::formationName(f);
            for (size_t i = 0; i < slots.size(); ++i) {
                EXPECT_FLOAT_EQ(slots[i].y, 2.0f);
                for (size_t j = i + 1; j < slots.size(); ++j)
                    EXPECT_GT(flatDistance(slots[i], slots[j]), 1.0f) << kke::formationName(f) << " n=" << n << " " << i << "," << j;
            }
        }
    }
    EXPECT_EQ(kke::formationFromName("file"), kke::Formation::Column);
    EXPECT_EQ(kke::formationFromName("???", kke::Formation::Line), kke::Formation::Line);
}

TEST(Orders, LineIsSideBySideAndWedgePointsForward) {
    // Facing -Z: a line runs along X, centre first.
    const auto line = kke::formationSlots(kke::Formation::Line, 3, glm::vec3(0.0f), 0.0f, 2.0f);
    EXPECT_NEAR(line[0].x, 0.0f, 1e-5f);
    EXPECT_NEAR(line[1].x, 2.0f, 1e-5f); // right first
    EXPECT_NEAR(line[2].x, -2.0f, 1e-5f);
    for (const auto& p : line) EXPECT_NEAR(p.z, 0.0f, 1e-5f);
    // The wedge's point is the order's point; the rest are behind it (+Z).
    const auto wedge = kke::formationSlots(kke::Formation::Wedge, 5, glm::vec3(0.0f), 0.0f, 2.0f);
    EXPECT_NEAR(glm::length(wedge[0]), 0.0f, 1e-5f);
    for (size_t i = 1; i < wedge.size(); ++i) EXPECT_GT(wedge[i].z, 0.5f);
}

TEST(Orders, SlotAssignmentIsOptimalAndPathsDontCross) {
    // Units in a row on the left, slots in a row on the right, reversed:
    // the greedy "nearest first" would cross; the optimum never does.
    std::vector<glm::vec3> units, slots;
    for (int i = 0; i < 6; ++i) {
        units.push_back(glm::vec3(0.0f, 0.0f, float(i) * 2.0f));
        slots.push_back(glm::vec3(10.0f, 0.0f, float(5 - i) * 2.0f + 1.0f));
    }
    const auto pick = kke::assignSlots(units, slots);
    ASSERT_EQ(pick.size(), units.size());
    std::set<int> used(pick.begin(), pick.end());
    EXPECT_EQ(used.size(), units.size());
    for (size_t i = 0; i < units.size(); ++i)
        for (size_t j = i + 1; j < units.size(); ++j)
            EXPECT_FALSE(segmentsCross(units[i], slots[size_t(pick[i])], units[j], slots[size_t(pick[j])])) << i << "," << j;
    // Every other permutation of a small case costs at least as much.
    std::vector<glm::vec3> u{ { 0, 0, 0 }, { 1, 0, 3 }, { 4, 0, 1 }, { -2, 0, 2 } };
    std::vector<glm::vec3> s{ { 3, 0, 3 }, { -1, 0, -1 }, { 0, 0, 4 }, { 2, 0, 0 } };
    const auto best = kke::assignSlots(u, s);
    auto total = [&](const std::vector<int>& p) {
        float t = 0.0f;
        for (size_t i = 0; i < p.size(); ++i) t += glm::length(u[i] - s[size_t(p[i])]);
        return t;
    };
    std::vector<int> perm{ 0, 1, 2, 3 };
    do EXPECT_LE(total(best), total(perm) + 1e-4f);
    while (std::next_permutation(perm.begin(), perm.end()));
    EXPECT_TRUE(kke::assignSlots({}, {}).empty());
}

TEST(Orders, FollowerKeepsItsSideAndStaysOutOfTheWay) {
    kke::FollowSettings s;
    const glm::vec3 leader(0.0f), run(0.0f, 0.0f, -5.0f); // running toward -Z
    // A follower on the left stays on the left, one on the right on the right.
    const glm::vec3 left = kke::followSlot(leader, run, 0.0f, glm::vec3(-1.0f, 0.0f, 0.5f), s);
    const glm::vec3 right = kke::followSlot(leader, run, 0.0f, glm::vec3(1.0f, 0.0f, 0.5f), s);
    EXPECT_LT(left.x, -1.0f);
    EXPECT_GT(right.x, 1.0f);
    EXPECT_FALSE(kke::inLeadersWay(leader, run, left, s));
    EXPECT_FALSE(kke::inLeadersWay(leader, run, right, s));
    // Running puts it further aside than standing.
    const glm::vec3 still = kke::followSlot(leader, glm::vec3(0.0f), 0.0f, glm::vec3(1.0f, 0.0f, 0.5f), s);
    EXPECT_GT(right.x, still.x);
    // Right in front of a running leader is in the way; behind is not.
    EXPECT_TRUE(kke::inLeadersWay(leader, run, glm::vec3(0.2f, 0.0f, -3.0f), s));
    EXPECT_FALSE(kke::inLeadersWay(leader, run, glm::vec3(0.0f, 0.0f, 2.0f), s));
    EXPECT_FALSE(kke::inLeadersWay(leader, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), s));
}

TEST(Orders, SelectionAndGroups) {
    kke::Selection sel;
    sel.select(3);
    sel.add(5);
    sel.add(5);
    sel.toggle(7);
    EXPECT_EQ(sel.ids(), (std::vector<uint32_t>{ 3, 5, 7 }));
    sel.toggle(5);
    EXPECT_EQ(sel.ids(), (std::vector<uint32_t>{ 3, 7 }));
    sel.storeGroup(2);
    sel.select(9);
    EXPECT_TRUE(sel.recallGroup(2));
    EXPECT_EQ(sel.ids(), (std::vector<uint32_t>{ 3, 7 }));
    EXPECT_FALSE(sel.recallGroup(4));
    EXPECT_FALSE(sel.recallGroup(12));
    sel.remove(7);
    EXPECT_EQ(sel.group(2), (std::vector<uint32_t>{ 3 }));
    sel.select(0);
    EXPECT_TRUE(sel.empty());
}

TEST(Orders, ScreenPicking) {
    std::vector<kke::ScreenUnit> units{ { 1, { 10, 10 } }, { 2, { 50, 50 } }, { 3, { 90, 20 } }, { 4, { 30, 30 }, false } };
    EXPECT_EQ(kke::unitsInRect(units, { 60, 60 }, { 0, 0 }), (std::vector<uint32_t>{ 1, 2 }));
    EXPECT_EQ(kke::unitNear(units, { 52, 47 }, 10.0f), 2u);
    EXPECT_EQ(kke::unitNear(units, { 30, 30 }, 5.0f), 0u); // off screen
}

TEST(Orders, ContextOrders) {
    const std::vector<uint32_t> squad{ 1, 2 };
    kke::UnitAbilities soldier, dog;
    dog.attack = false;
    dog.fetch = true;
    kke::PointerTarget enemy{ kke::Relation::Hostile, 40, { 1, 0, 2 } };
    EXPECT_EQ(kke::contextOrder(squad, enemy, soldier).kind, kke::OrderKind::Attack);
    EXPECT_EQ(kke::contextOrder(squad, enemy, soldier, { true, false }).kind, kke::OrderKind::FocusFire);
    EXPECT_EQ(kke::contextOrder(squad, enemy, soldier).target, 40u);
    EXPECT_EQ(kke::contextOrder({ 9 }, enemy, dog).kind, kke::OrderKind::Move);
    kke::PointerTarget ball{ kke::Relation::Item, 50, { 3, 0, 3 } };
    EXPECT_EQ(kke::contextOrder({ 9 }, ball, dog).kind, kke::OrderKind::Fetch);
    EXPECT_EQ(kke::contextOrder(squad, ball, soldier).kind, kke::OrderKind::Move);
    kke::PointerTarget me{ kke::Relation::Self, 100, {} };
    const kke::Order come = kke::contextOrder({ 9 }, me, dog, {}, 100);
    EXPECT_EQ(come.kind, kke::OrderKind::Follow);
    EXPECT_EQ(come.target, 100u);
    kke::PointerTarget ground{ kke::Relation::Ground, 0, { 5, 0, 5 } };
    EXPECT_EQ(kke::contextOrder(squad, ground, soldier).kind, kke::OrderKind::Move);
    EXPECT_EQ(kke::contextOrder(squad, ground, soldier, { true, true }).kind, kke::OrderKind::Stay);
    EXPECT_TRUE(kke::contextOrder(squad, ground, soldier, { false, true }).queue);
    // Pointing at one of the selected units themselves: go there, not "follow yourself".
    kke::PointerTarget own{ kke::Relation::Own, 2, { 7, 0, 7 } };
    EXPECT_EQ(kke::contextOrder(squad, own, soldier).kind, kke::OrderKind::Move);
    EXPECT_EQ(kke::contextOrder({}, ground, soldier).kind, kke::OrderKind::None);
}

TEST(Orders, RadialMenuPicksClockwiseWithHysteresis) {
    kke::RadialMenu wheel(4); // up, right, down, left
    EXPECT_EQ(wheel.updateStick({ 0.0f, -1.0f }), 0);
    EXPECT_EQ(wheel.updateStick({ 1.0f, 0.0f }), 1);
    EXPECT_EQ(wheel.updateStick({ 0.0f, 1.0f }), 2);
    EXPECT_EQ(wheel.updateStick({ -1.0f, 0.0f }), 3);
    // Back to the middle keeps the pick (flick and release).
    EXPECT_EQ(wheel.updateStick({ 0.05f, 0.0f }), 3);
    // Just past the border between left (3) and up (0): still left.
    const float a = glm::radians(-45.0f + 3.0f); // 3 degrees into "up"
    EXPECT_EQ(wheel.updateStick({ std::sin(a), -std::cos(a) }), 3);
    const float b = glm::radians(-45.0f + 12.0f);
    EXPECT_EQ(wheel.updateStick({ std::sin(b), -std::cos(b) }), 0);
    // A pointer brought back to the middle cancels.
    EXPECT_EQ(wheel.updatePointer({ 100.0f, 0.0f }), 1);
    EXPECT_EQ(wheel.updatePointer({ 3.0f, 2.0f }), -1);
    const glm::vec2 d = wheel.direction(1);
    EXPECT_NEAR(d.x, 1.0f, 1e-5f);
    EXPECT_NEAR(d.y, 0.0f, 1e-5f);
}

TEST(Orders, BoardSplitsASquadMoveIntoSlots) {
    std::map<uint32_t, glm::vec3> pos{ { 1, { 0, 0, 0 } }, { 2, { 2, 0, 0 } }, { 3, { 4, 0, 0 } } };
    kke::OrderBoard board([&](uint32_t u) { return pos.count(u) ? pos[u] : glm::vec3(NAN); });
    std::vector<std::pair<uint32_t, kke::OrderKind>> heard;
    board.listen({ [&](uint32_t u, const kke::UnitOrder& o) { heard.emplace_back(u, o.kind); }, {} });
    kke::Order move;
    move.kind = kke::OrderKind::Move;
    move.units = { 1, 2, 3 };
    move.point = { 2, 0, -20 };
    move.hasPoint = true;
    move.formation = kke::Formation::Line;
    move.spacing = 2.0f;
    const uint64_t id = board.issue(move);
    ASSERT_NE(id, 0u);
    EXPECT_EQ(heard.size(), 3u);
    std::set<float> xs;
    for (uint32_t u : { 1u, 2u, 3u }) {
        const kke::UnitOrder* o = board.current(u);
        ASSERT_NE(o, nullptr);
        EXPECT_EQ(o->id, id);
        EXPECT_TRUE(o->hasPoint);
        EXPECT_NEAR(o->point.z, -20.0f, 1e-4f);
        EXPECT_EQ(o->squad.size(), 3u);
        xs.insert(std::round(o->point.x));
    }
    EXPECT_EQ(xs.size(), 3u);
    // Heading straight to -Z in a line: nobody swaps sides.
    EXPECT_LT(board.current(1)->point.x, board.current(2)->point.x);
    EXPECT_LT(board.current(2)->point.x, board.current(3)->point.x);
}

TEST(Orders, BoardRejectsIncompleteOrders) {
    kke::OrderBoard board;
    std::string why;
    kke::Order o;
    EXPECT_EQ(board.issue(o, &why), 0u);
    o.kind = kke::OrderKind::Attack;
    o.units = { 1 };
    EXPECT_EQ(board.issue(o, &why), 0u);
    EXPECT_NE(why.find("target"), std::string::npos);
    o.kind = kke::OrderKind::Move;
    EXPECT_EQ(board.issue(o, &why), 0u);
    o.hasPoint = true;
    o.point = glm::vec3(NAN);
    EXPECT_EQ(board.issue(o, &why), 0u);
    // A unit can't be ordered to attack itself.
    o = {};
    o.kind = kke::OrderKind::Attack;
    o.units = { 4 };
    o.target = 4;
    EXPECT_EQ(board.issue(o, &why), 0u);
    o.kind = kke::OrderKind::Regroup;
    o.target = 0;
    EXPECT_EQ(board.issue(o, &why), 0u); // no place and no issuer
}

TEST(Orders, BoardQueuesCompletesAndCancels) {
    kke::OrderBoard board;
    std::vector<std::string> log;
    board.listen({ [&](uint32_t u, const kke::UnitOrder& o) { log.push_back(std::to_string(u) + " " + kke::orderName(o.kind)); },
                   [&](uint32_t u, const kke::UnitOrder& o, bool ok) {
                       log.push_back(std::to_string(u) + " " + kke::orderName(o.kind) + (ok ? " done" : " failed"));
                   } });
    kke::Order sit;
    sit.kind = kke::OrderKind::Sit;
    sit.units = { 7 };
    board.issue(sit);
    kke::Order fetch;
    fetch.kind = kke::OrderKind::Fetch;
    fetch.units = { 7 };
    fetch.target = 30;
    fetch.queue = true;
    board.issue(fetch);
    EXPECT_EQ(board.currentKind(7), kke::OrderKind::Sit);
    EXPECT_EQ(board.queued(7), 1u);
    board.markRunning(7);
    EXPECT_EQ(board.current(7)->status, kke::UnitOrder::Status::Running);
    board.complete(7);
    EXPECT_EQ(board.currentKind(7), kke::OrderKind::Fetch);
    // The ball went away: the fetch fails, and the dog is free.
    board.targetGone(30);
    EXPECT_EQ(board.currentKind(7), kke::OrderKind::None);
    EXPECT_EQ(log, (std::vector<std::string>{ "7 sit", "7 sit done", "7 fetch", "7 fetch failed" }));
    // A new order replaces the old one silently.
    log.clear();
    board.issue(sit);
    fetch.queue = false;
    board.issue(fetch);
    EXPECT_EQ(log, (std::vector<std::string>{ "7 sit", "7 fetch" }));
    board.cancel(7);
    EXPECT_EQ(board.currentKind(7), kke::OrderKind::None);
    board.forget(7);
    EXPECT_TRUE(board.units().empty());
}

TEST(Orders, FocusFireEndsWhenTheTargetFalls) {
    kke::OrderBoard board;
    int done = 0;
    board.listen({ {}, [&](uint32_t, const kke::UnitOrder& o, bool ok) { done += (o.kind == kke::OrderKind::FocusFire && ok); } });
    kke::Order f;
    f.kind = kke::OrderKind::FocusFire;
    f.units = { 1, 2, 3 };
    f.target = 99;
    board.issue(f);
    EXPECT_EQ(board.current(2)->squad.size(), 3u);
    board.targetGone(99);
    EXPECT_EQ(done, 3);
}

TEST(Orders, StayHoldsWhereEachUnitStands) {
    std::map<uint32_t, glm::vec3> pos{ { 1, { 3, 0, 4 } }, { 2, { -6, 0, 1 } } };
    kke::OrderBoard board([&](uint32_t u) { return pos[u]; });
    kke::Order stay;
    stay.kind = kke::OrderKind::Stay;
    stay.units = { 1, 2 };
    board.issue(stay);
    EXPECT_EQ(board.current(1)->point, pos[1]);
    EXPECT_EQ(board.current(2)->point, pos[2]);
    // Regroup on the issuer: a ring around them.
    pos[50] = glm::vec3(10, 0, 10);
    kke::Order regroup;
    regroup.kind = kke::OrderKind::Regroup;
    regroup.units = { 1, 2 };
    regroup.issuer = 50;
    ASSERT_NE(board.issue(regroup), 0u);
    for (uint32_t u : { 1u, 2u }) EXPECT_LT(flatDistance(board.current(u)->point, pos[50]), 3.0f);
}

TEST(Orders, ListenersMayGiveOrders) {
    // A brain that gets an order and at once gives another must not break the board.
    kke::OrderBoard board;
    board.listen({ [&](uint32_t u, const kke::UnitOrder& o) {
                      if (o.kind == kke::OrderKind::Drop) {
                          kke::Order sit;
                          sit.kind = kke::OrderKind::Sit;
                          sit.units = { u };
                          board.issue(sit);
                      }
                  },
                   {} });
    kke::Order drop;
    drop.kind = kke::OrderKind::Drop;
    drop.units = { 3 };
    board.issue(drop);
    EXPECT_EQ(board.currentKind(3), kke::OrderKind::Sit);
}

TEST(Orders, IntentReadsLookingApproachingAndIdle) {
    kke::IntentReader reader;
    std::vector<kke::IntentCandidate> things{ { 5, { 0, 0, -10 }, kke::Relation::Item }, { 6, { 3, 0, 0 }, kke::Relation::Own } };
    kke::IntentSample p;
    p.view = glm::normalize(glm::vec3(0.0f, -1.2f, -10.0f));
    // Standing, looking at the ball: after the dwell, LookingAt it.
    kke::Intent i;
    for (int f = 0; f < 60; ++f) i = reader.update(p, things, 1.0f / 60.0f);
    EXPECT_EQ(i.kind, kke::Intent::Kind::LookingAt);
    EXPECT_EQ(i.thing, 5u);
    // Walking toward the dog: Approaching it.
    p.view = glm::vec3(1.0f, 0.0f, 0.0f) * -1.0f; // looking away
    p.velocity = glm::vec3(1.5f, 0.0f, 0.0f);
    i = reader.update(p, things, 1.0f / 60.0f);
    EXPECT_EQ(i.kind, kke::Intent::Kind::Approaching);
    EXPECT_EQ(i.thing, 6u);
    // Running away from everything.
    p.velocity = glm::vec3(-6.0f, 0.0f, 0.0f);
    i = reader.update(p, things, 1.0f / 60.0f);
    EXPECT_EQ(i.kind, kke::Intent::Kind::Running);
    EXPECT_NEAR(i.heading.x, -9.0f, 1e-4f);
    // A short stop keeps "running"; a long one is Idle.
    p.velocity = glm::vec3(0.0f);
    i = reader.update(p, things, 0.5f);
    EXPECT_EQ(i.kind, kke::Intent::Kind::Running);
    i = reader.update(p, things, 1.5f);
    EXPECT_EQ(i.kind, kke::Intent::Kind::Idle);
    EXPECT_STREQ(kke::intentName(i.kind), "idle");
}

// ---------------------------------------------------------------- orders carried out by the AI core

#include "kke/OrderBridge.h"

namespace {

// Runs the AI (it moves its agents itself here) until `done` or a limit.
template <typename Done> int run(kke::ai::AiWorld& w, kke::AiOrderBridge& bridge, Done done, int frames = 900, float fps = 30.0f) {
    for (int i = 0; i < frames; ++i) {
        w.update(1.0f / fps);
        bridge.handle(w.takeEvents());
        if (done()) return i;
    }
    return -1;
}

} // namespace

TEST(OrderBridge, FetchGoesThereBringsItBackAndIsDone) {
    kke::ai::AiWorld w(3);
    kke::ai::Species ball;
    ball.id = "ball";
    ball.radius = 0.1f;
    ball.scent = 0.0f;
    w.defineSpecies(ball);
    ASSERT_TRUE(w.addAgent(2, "dog", { 0, 0, 0 }));
    ASSERT_TRUE(w.addActor(1, "farmer", { 0, 0, 2 }));
    ASSERT_TRUE(w.addActor(10, "ball", { 9, 0, -6 }));
    kke::OrderBoard board([&](uint32_t u) { return w.agent(u) ? w.agent(u)->position : glm::vec3(NAN); });
    kke::AiOrderBridge bridge(board, w);
    std::vector<std::string> log;
    bridge.pickUp = [&](uint32_t unit, uint32_t thing) {
        log.push_back("pick " + std::to_string(thing));
        EXPECT_LT(glm::length(w.agent(unit)->position - w.agent(thing)->position), 1.2f);
        return true;
    };
    bridge.deliver = [&](uint32_t unit, uint32_t thing, uint32_t to) {
        log.push_back("deliver " + std::to_string(thing) + " to " + std::to_string(to));
        EXPECT_LT(glm::length(w.agent(unit)->position - w.agent(to)->position), 2.0f);
    };
    bool ok = false;
    board.listen({ {}, [&](uint32_t, const kke::UnitOrder& o, bool good) { ok = o.kind == kke::OrderKind::Fetch && good; } });
    kke::Order fetch;
    fetch.kind = kke::OrderKind::Fetch;
    fetch.units = { 2 };
    fetch.target = 10;
    fetch.issuer = 1;
    ASSERT_NE(board.issue(fetch), 0u);
    EXPECT_EQ(w.agent(2)->order.kind, kke::ai::Order::Kind::Interact);
    ASSERT_GE(run(w, bridge, [&] { return ok; }), 0) << w.describe(2);
    EXPECT_EQ(log, (std::vector<std::string>{ "pick 10", "deliver 10 to 1" }));
    EXPECT_EQ(board.currentKind(2), kke::OrderKind::None);
    EXPECT_EQ(bridge.carrying(2), 0u);
}

TEST(OrderBridge, SquadMoveEndsInFormationAndOrdersMapOntoTheCore) {
    kke::ai::AiWorld w(5);
    for (uint32_t u = 1; u <= 4; ++u) ASSERT_TRUE(w.addAgent(u, "farmer", { float(u) * 1.5f, 0, 0 }));
    ASSERT_TRUE(w.addAgent(9, "farmer", { 30, 0, 30 }));
    for (uint32_t u = 1; u <= 4; ++u) w.setTeam(u, 1);
    w.setTeam(9, 2);
    kke::OrderBoard board([&](uint32_t u) { return w.agent(u)->position; });
    kke::AiOrderBridge bridge(board, w);
    int done = 0;
    board.listen({ {}, [&](uint32_t, const kke::UnitOrder& o, bool good) { done += o.kind == kke::OrderKind::Move && good; } });
    kke::Order move;
    move.kind = kke::OrderKind::Move;
    move.units = { 1, 2, 3, 4 };
    move.point = { 4, 0, -10 };
    move.hasPoint = true;
    move.formation = kke::Formation::Line;
    move.spacing = 2.0f;
    board.issue(move);
    ASSERT_GE(run(w, bridge, [&] { return done == 4; }), 0);
    for (uint32_t a = 1; a <= 4; ++a)
        for (uint32_t b = a + 1; b <= 4; ++b) EXPECT_GT(glm::length(w.agent(a)->position - w.agent(b)->position), 1.0f);
    // Then they hold there, as the core does after MoveTo.
    EXPECT_EQ(w.agent(1)->order.kind, kke::ai::Order::Kind::Hold);
    // Focus fire: everyone on one target.
    kke::Order focus;
    focus.kind = kke::OrderKind::FocusFire;
    focus.units = { 1, 2, 3, 4 };
    focus.target = 9;
    board.issue(focus);
    for (uint32_t u = 1; u <= 4; ++u) {
        EXPECT_EQ(w.agent(u)->order.kind, kke::ai::Order::Kind::Attack);
        EXPECT_EQ(w.agent(u)->order.target, 9u);
    }
    // Sit and stay are holds; at ease clears the order; drop is done at once.
    kke::Order sit;
    sit.kind = kke::OrderKind::Sit;
    sit.units = { 1 };
    board.issue(sit);
    EXPECT_EQ(w.agent(1)->order.kind, kke::ai::Order::Kind::Hold);
    kke::Order easy;
    easy.kind = kke::OrderKind::Free;
    easy.units = { 1 };
    board.issue(easy);
    EXPECT_EQ(w.agent(1)->order.kind, kke::ai::Order::Kind::None);
    int drops = 0;
    bridge.drop = [&](uint32_t) { ++drops; };
    kke::Order drop;
    drop.kind = kke::OrderKind::Drop;
    drop.units = { 2 };
    board.issue(drop);
    EXPECT_EQ(drops, 1);
    EXPECT_EQ(board.currentKind(2), kke::OrderKind::None);
    // A unit the AI doesn't know is left alone.
    kke::Order other = sit;
    other.units = { 77 };
    EXPECT_NE(board.issue(other), 0u);
    EXPECT_EQ(board.currentKind(77), kke::OrderKind::Sit);
}

TEST(OrderBridge, PetWaitsForTheGameToEndThePat) {
    kke::ai::AiWorld w(8);
    ASSERT_TRUE(w.addAgent(2, "dog", { 6, 0, 0 }));
    ASSERT_TRUE(w.addActor(1, "farmer", { 0, 0, 0 }));
    kke::OrderBoard board([&](uint32_t u) { return w.agent(u)->position; });
    kke::AiOrderBridge bridge(board, w);
    bool patting = false;
    bridge.petStart = [&](uint32_t unit, uint32_t by) {
        patting = true;
        EXPECT_EQ(unit, 2u);
        EXPECT_EQ(by, 1u);
    };
    kke::Order pet;
    pet.kind = kke::OrderKind::Pet;
    pet.units = { 2 };
    pet.target = 1;
    pet.issuer = 1;
    board.issue(pet);
    ASSERT_GE(run(w, bridge, [&] { return patting; }), 0) << w.describe(2);
    EXPECT_EQ(board.currentKind(2), kke::OrderKind::Pet); // until the game says the pat is over
    EXPECT_LT(glm::length(w.agent(2)->position - w.agent(1)->position), 1.6f);
    board.complete(2, true);
    EXPECT_EQ(board.currentKind(2), kke::OrderKind::None);
}

// Taking cover: a spot right in front of a crate is reached quickly (at a
// run) and held, not circled because the crate pushes the soldier away.
TEST(OrderBridge, StayAtASpotInFrontOfAnObstacleRunsThereAndSettles) {
    kke::ai::AiWorld w(4);
    ASSERT_TRUE(w.addAgent(1, "farmer", { 0, 0, 12 }));
    w.addObstacle({ { 0, 0, 0 }, 0.8f });
    kke::OrderBoard board([&](uint32_t u) { return w.agent(u)->position; });
    kke::AiOrderBridge bridge(board, w);
    kke::Order cover;
    cover.kind = kke::OrderKind::Stay;
    cover.units = { 1 };
    cover.point = { 0, 0, 1.5f };
    cover.hasPoint = true;
    board.issue(cover);
    EXPECT_TRUE(w.agent(1)->order.run);
    const kke::ai::Species* s = w.species("farmer");
    ASSERT_NE(s, nullptr);
    // At a run, with time to speed up and slow down: well under the walk.
    const float walkTime = 10.5f / s->walkSpeed;
    const int frames = run(w, bridge, [&] { return flatDistance(w.agent(1)->position, cover.point) < 0.6f; });
    ASSERT_GE(frames, 0) << w.describe(1);
    EXPECT_LT(float(frames) / 30.0f, walkTime * 0.8f);
    // And it stays there.
    run(w, bridge, [] { return false; }, 90);
    EXPECT_LT(flatDistance(w.agent(1)->position, cover.point), 0.6f);
    EXPECT_GT(flatDistance(w.agent(1)->position, { 0, 0, 0 }), 0.8f);
}

// Platoon on soucouyant: a soldier sent to the near end of a barrier's
// cover face while still running on from the last order turned onto it so
// slowly that he ran across the barrier's line, to the enemy's side, and
// stuck there at its end.
TEST(OrderBridge, StayAtTheEndOfABarrierFaceArrivesOnThatSide) {
    kke::ai::AiWorld w(4);
    // Platoon's soldier: quick on its feet.
    const kke::ai::Species* farmer = w.species("farmer");
    ASSERT_NE(farmer, nullptr);
    kke::ai::Species soldier = *farmer;
    soldier.id = "soldier";
    soldier.walkSpeed = 2.0f;
    soldier.runSpeed = 5.0f;
    soldier.acceleration = 14.0f;
    soldier.radius = 0.35f;
    w.defineSpecies(soldier);
    ASSERT_TRUE(w.addAgent(1, "soldier", { -3.9f, 0, 3.6f }));
    w.agent(1)->velocity = { 0.3f, 0, -4.2f }; // still running from the last order
    // A barrier along x, as kke games build one: a chain of small circles.
    for (int i = 0; i <= 6; ++i) w.addObstacle({ { -1.2f + 0.4f * float(i), 0, 0 }, 0.4f });
    kke::OrderBoard board([&](uint32_t u) { return w.agent(u)->position; });
    kke::AiOrderBridge bridge(board, w);
    kke::Order cover;
    cover.kind = kke::OrderKind::Stay;
    cover.units = { 1 };
    cover.point = { -1.1f, 0, 1.1f };
    cover.hasPoint = true;
    board.issue(cover);
    // At the 140 fps soucouyant ran it at (seconds of it, not frames).
    const int frames = run(w, bridge, [&] { return flatDistance(w.agent(1)->position, cover.point) < 0.6f; }, 140 * 6, 140.0f);
    ASSERT_GE(frames, 0) << w.describe(1);
    run(w, bridge, [] { return false; }, 140 * 3, 140.0f);
    EXPECT_LT(flatDistance(w.agent(1)->position, cover.point), 0.6f) << w.describe(1);
    EXPECT_GT(w.agent(1)->position.z, 0.4f); // on the cover side
}

#if KKE_ENABLE_LUA
#include "kke/NodeGraph.h"
#include "kke/OrderScript.h"
#include "kke/ScriptVM.h"

#include <lua.h>

namespace {

struct FakeHost : kke::IOrderHost {
    uint32_t me = 100;
    std::vector<uint32_t> sel{ 1, 2 };
    uint32_t player() const override { return me; }
    std::vector<uint32_t> selected() const override { return sel; }
};

} // namespace

TEST(OrderScript, EveryOrderFunctionIsDocumentedAndIsANode) {
    kke::ScriptVM vm;
    kke::OrderBoard board;
    FakeHost host;
    kke::OrderScript script(vm, board, host);
    lua_State* L = vm.state();
    lua_getglobal(L, "order");
    ASSERT_TRUE(lua_istable(L, -1));
    int count = 0;
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        ++count;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    EXPECT_EQ(size_t(count), kke::orderApiFunctions().size());
    const kke::NodeLibrary lib = kke::NodeLibrary::fromApi(vm.apiFunctions(), vm.apiEvents());
    for (const char* type : { "call:order.move", "call:order.sit", "call:order.focus", "event:Ordered", "event:OrderDone" })
        EXPECT_NE(lib.find(type), nullptr) << type;
    EXPECT_EQ(lib.find("call:order.selected"), nullptr); // lists are Lua only
}

TEST(OrderScript, LuaOrdersReachTheBoardAndEventsFireLater) {
    kke::ScriptVM vm;
    std::map<uint32_t, glm::vec3> pos{ { 1, { 0, 0, 0 } }, { 2, { 1, 0, 0 } }, { 3, { 2, 0, 0 } }, { 100, { 0, 0, 5 } } };
    kke::OrderBoard board([&](uint32_t u) { return pos[u]; });
    FakeHost host;
    kke::OrderScript script(vm, board, host);
    ASSERT_TRUE(vm.runString(R"(
        heard = {}
        hook.Add("Ordered", "t", function(e) heard[#heard + 1] = e.unit .. " " .. e.order .. " by " .. tostring(e.by) end)
        hook.Add("OrderDone", "t", function(e) heard[#heard + 1] = e.unit .. " " .. e.order .. (e.ok and " ok" or " failed") end)
        assert(order.move({1, 2, 3}, Vec(0, 0, -10), "line"))
        assert(order.current(2) == "move")
        assert(order.player() == 100)
    )", "test")) << vm.errors().back().message;
    ASSERT_NE(board.current(3), nullptr);
    EXPECT_NEAR(board.current(3)->point.z, -10.0f, 0.5f); // the line faces the way they came
    EXPECT_EQ(script.pending(), 3u);
    script.fireEvents();
    EXPECT_EQ(script.pending(), 0u);
    ASSERT_TRUE(vm.runString(R"(
        assert(#heard == 3, #heard)
        assert(heard[1] == "1 move by 100", heard[1])
        -- No units given: the selection (1 and 2).
        assert(order.sit())
        assert(order.current(1) == "sit" and order.current(2) == "sit" and order.current(3) == "move")
        assert(order.fetch(3, 55))
        assert(order.target(3) == 55)
        assert(order.done(3, false))
        assert(order.current(3) == nil)
        assert(order.follow(3))
        assert(order.target(3) == 100)
    )", "test")) << vm.errors().back().message;
    script.fireEvents();
    ASSERT_TRUE(vm.runString(R"(
        local found = false
        for _, h in ipairs(heard) do if h == "3 fetch failed" then found = true end end
        assert(found)
    )", "test")) << vm.errors().back().message;
    // Mistakes are Lua errors, not crashes.
    EXPECT_FALSE(vm.runString("order.move(1)", "bad1"));
    EXPECT_FALSE(vm.runString("order.attack(1)", "bad2"));
    EXPECT_FALSE(vm.runString("order.sit('dog')", "bad3"));
    EXPECT_FALSE(vm.runString("order.move(1, Vec(0/0, 0, 0))", "bad4"));
}

TEST(OrderScript, AThingsGraphAnswersItsOwnOrders) {
    // A dog's graph: "when given an order -> order done", so a scripted
    // unit finishes whatever it is told, and only its own orders.
    kke::ScriptVM vm;
    kke::OrderBoard board;
    FakeHost host;
    kke::OrderScript script(vm, board, host);
    const kke::NodeLibrary lib = kke::NodeLibrary::fromApi(vm.apiFunctions(), vm.apiEvents());
    kke::NodeGraph g;
    const int when = g.add("event:Ordered", { 0, 0 });
    const int done = g.add("call:order.done", { 300, 0 });
    ASSERT_TRUE(g.connect(lib, { when, ">", done, ">" }));
    kke::CompileOptions opt;
    opt.source = "graph:dog";
    opt.owner = 7;
    const kke::CompiledGraph c = kke::compileGraph(g, lib, opt);
    ASSERT_TRUE(c.ok()) << c.lua;
    ASSERT_TRUE(vm.reloadString(c.lua, opt.source)) << vm.errors().back().message;
    kke::Order sit;
    sit.kind = kke::OrderKind::Sit;
    sit.units = { 7, 8 };
    board.issue(sit);
    script.fireEvents();
    EXPECT_EQ(board.currentKind(7), kke::OrderKind::None);
    EXPECT_EQ(board.currentKind(8), kke::OrderKind::Sit);
    EXPECT_TRUE(vm.errors().empty());
}
#endif
