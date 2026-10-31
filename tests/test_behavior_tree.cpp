// Behaviour trees (kke/ai/BehaviorTree.h, ENG-F71, docs/AI.md "Behaviour trees").
#include "kke/ai/BehaviorTree.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

using namespace kke::ai;
using nlohmann::json;

namespace {

// An action that plays back a script of answers (the last one repeats),
// counting how often it ran and how often it was aborted.
struct Script {
    std::vector<BtStatus> answers;
    int ran = 0;
    int aborted = 0;
};

std::unique_ptr<BtNode> scripted(const std::shared_ptr<Script>& s, const std::string& name = "step") {
    return btAction(
        [s](BtContext&) {
            const size_t i = std::min(static_cast<size_t>(s->ran), s->answers.size() - 1);
            ++s->ran;
            return s->answers[i];
        },
        name, [s](BtContext&) { ++s->aborted; });
}

std::shared_ptr<Script> script(std::vector<BtStatus> a) { return std::make_shared<Script>(Script{ std::move(a) }); }

constexpr BtStatus S = BtStatus::Success, F = BtStatus::Failure, R = BtStatus::Running;

template <typename... Nodes>
BtChildren kids(Nodes&&... n) {
    BtChildren c;
    (c.push_back(std::forward<Nodes>(n)), ...);
    return c;
}

BehaviorTree tree(std::unique_ptr<BtNode> root) { return BehaviorTree(std::move(root), 7); }

} // namespace

TEST(Blackboard, StoresTypedValuesAndConverts) {
    Blackboard b;
    b.set("hp", 80);
    b.set("alive", true);
    b.set("name", "orc");
    b.set("home", glm::vec3(1, 2, 3));
    EXPECT_DOUBLE_EQ(b.getNumber("hp"), 80.0);
    EXPECT_TRUE(b.getBool("hp")); // a number is true when not 0
    EXPECT_DOUBLE_EQ(b.getNumber("alive"), 1.0);
    EXPECT_EQ(b.getString("name"), "orc");
    EXPECT_EQ(b.getString("hp"), "80");
    EXPECT_EQ(b.getVec3("home"), glm::vec3(1, 2, 3));
    EXPECT_EQ(b.getString("home"), "(1, 2, 3)");
    EXPECT_FALSE(b.has("nothing"));
    EXPECT_DOUBLE_EQ(b.getNumber("nothing", 5.0), 5.0);
    EXPECT_EQ(b.getVec3("hp", glm::vec3(9)), glm::vec3(9)); // a number is no vector
    b.set("hp", 0);
    EXPECT_FALSE(b.getBool("hp"));
    b.erase("hp");
    EXPECT_FALSE(b.has("hp"));
    EXPECT_EQ(b.size(), 3u);
}

TEST(Blackboard, VersionCountsRealChangesOnly) {
    Blackboard b;
    const uint64_t v0 = b.version();
    b.set("x", 1);
    const uint64_t v1 = b.version();
    EXPECT_GT(v1, v0);
    b.set("x", 1); // the same value
    EXPECT_EQ(b.version(), v1);
    b.set("x", 2);
    EXPECT_GT(b.version(), v1);
    const uint64_t v2 = b.version();
    b.erase("missing");
    EXPECT_EQ(b.version(), v2);
    b.clear();
    EXPECT_GT(b.version(), v2);
}

TEST(Blackboard, CompareHandlesTypes) {
    using V = Blackboard::Value;
    EXPECT_TRUE(btCompare(V(3.0), BtOp::Less, V(4.0)));
    EXPECT_TRUE(btCompare(V(4.0), BtOp::GreaterEqual, V(4.0)));
    EXPECT_FALSE(btCompare(V(4.0), BtOp::Greater, V(4.0)));
    EXPECT_TRUE(btCompare(V(true), BtOp::Equal, V(1.0))); // a bool counts as 0 or 1
    EXPECT_TRUE(btCompare(V(std::string("a")), BtOp::Less, V(std::string("b"))));
    EXPECT_TRUE(btCompare(V(glm::vec3(1)), BtOp::Equal, V(glm::vec3(1))));
    EXPECT_FALSE(btCompare(V(glm::vec3(1)), BtOp::Less, V(glm::vec3(2))));
    EXPECT_FALSE(btCompare(V(std::string("1")), BtOp::Equal, V(1.0))); // different kinds are never equal
    EXPECT_TRUE(btCompare(V(std::string("1")), BtOp::NotEqual, V(1.0)));
    BtOp op;
    EXPECT_TRUE(btOpFromName(">=", op));
    EXPECT_EQ(op, BtOp::GreaterEqual);
    EXPECT_FALSE(btOpFromName("=>", op));
}

TEST(BehaviorTree, SequenceResumesAtTheRunningChild) {
    auto a = script({ S }), b = script({ R, R, S }), c = script({ S });
    auto t = tree(btSequence(kids(scripted(a), scripted(b), scripted(c))));
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(a->ran, 1) << "the child before the running one is not run again";
    EXPECT_EQ(t.tick(0.1f), S);
    EXPECT_EQ(b->ran, 3);
    EXPECT_EQ(c->ran, 1);
    // finished: the next tick starts from the top (b has run out of answers and now says success)
    EXPECT_EQ(t.tick(0.1f), S);
    EXPECT_EQ(a->ran, 2);
}

TEST(BehaviorTree, SequenceFailsAtTheFirstFailureAndRestarts) {
    auto a = script({ S }), b = script({ F }), c = script({ S });
    auto t = tree(btSequence(kids(scripted(a), scripted(b), scripted(c))));
    EXPECT_EQ(t.tick(0.1f), F);
    EXPECT_EQ(c->ran, 0);
    EXPECT_EQ(t.tick(0.1f), F);
    EXPECT_EQ(a->ran, 2); // started over
    EXPECT_EQ(btSequence({})->type(), "sequence");
    auto empty = tree(btSequence({}));
    EXPECT_EQ(empty.tick(0.1f), S) << "an empty sequence has nothing to fail at";
}

TEST(BehaviorTree, SelectorTakesTheFirstThatSucceeds) {
    auto a = script({ F }), b = script({ R, S }), c = script({ S });
    auto t = tree(btSelector(kids(scripted(a), scripted(b), scripted(c))));
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(t.tick(0.1f), S);
    EXPECT_EQ(a->ran, 1) << "a failed branch is not retried while a later one runs";
    EXPECT_EQ(c->ran, 0);
    auto none = tree(btSelector(kids(scripted(script({ F })), scripted(script({ F })))));
    EXPECT_EQ(none.tick(0.1f), F);
    auto empty = tree(btSelector({}));
    EXPECT_EQ(empty.tick(0.1f), F);
}

TEST(BehaviorTree, ReactiveSequenceRechecksItsConditionsAndAbortsWhatRuns) {
    auto work = script({ R });
    auto guard = std::make_shared<bool>(true);
    auto t = tree(btReactiveSequence(kids(btCondition([guard](BtContext&) { return *guard; }, "safe"), scripted(work))));
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(work->ran, 2);
    *guard = false;
    EXPECT_EQ(t.tick(0.1f), F);
    EXPECT_EQ(work->aborted, 1) << "the running branch was dropped";
    EXPECT_EQ(work->ran, 2);
    *guard = true;
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(work->aborted, 1);
}

TEST(BehaviorTree, ReactiveSelectorLetsAStrongerBranchPreempt) {
    auto flee = script({ R }), fight = script({ R });
    auto danger = std::make_shared<bool>(false);
    auto t = tree(btReactiveSelector(kids(btReactiveSequence(kids(btCondition([danger](BtContext&) { return *danger; }, "danger"), scripted(flee, "flee"))),
                                          scripted(fight, "fight"))));
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(fight->ran, 1);
    *danger = true;
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(flee->ran, 1);
    EXPECT_EQ(fight->aborted, 1) << "the lower branch was aborted when the stronger one took over";
    *danger = false;
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(flee->aborted, 1);
    EXPECT_EQ(fight->ran, 2);
}

TEST(BehaviorTree, SelectorWithMemoryDoesNotPreempt) {
    auto fight = script({ R }), flee = script({ R });
    auto danger = std::make_shared<bool>(false);
    auto t = tree(btSelector(kids(btSequence(kids(btCondition([danger](BtContext&) { return *danger; }), scripted(flee))), scripted(fight))));
    EXPECT_EQ(t.tick(0.1f), R);
    *danger = true;
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(flee->ran, 0) << "a plain selector stays with the running branch";
    EXPECT_EQ(fight->ran, 2);
}

TEST(BehaviorTree, ParallelPolicies) {
    {
        auto a = script({ S }), b = script({ R, R, S });
        auto t = tree(btParallel(kids(scripted(a), scripted(b)))); // all must succeed, one failure fails
        EXPECT_EQ(t.tick(0.1f), R);
        EXPECT_EQ(t.tick(0.1f), R);
        EXPECT_EQ(a->ran, 1) << "a finished child is not run again";
        EXPECT_EQ(t.tick(0.1f), S);
    }
    {
        auto a = script({ F }), b = script({ R });
        auto t = tree(btParallel(kids(scripted(a), scripted(b))));
        EXPECT_EQ(t.tick(0.1f), F);
        EXPECT_EQ(b->aborted, 1) << "what was still running is aborted when it fails";
    }
    {
        auto a = script({ S }), b = script({ R });
        auto t = tree(btParallel(kids(scripted(a), scripted(b)), 1, 0)); // one success is enough
        EXPECT_EQ(t.tick(0.1f), S);
        EXPECT_EQ(b->aborted, 1);
    }
    {
        auto a = script({ F }), b = script({ S });
        auto t = tree(btParallel(kids(scripted(a), scripted(b)), 0, 0)); // all must succeed; fail only if all fail
        EXPECT_EQ(t.tick(0.1f), F) << "all finished and neither goal reached: failure";
    }
    EXPECT_EQ(tree(btParallel({})).tick(0.1f), S);
}

TEST(BehaviorTree, RandomOrderIsSeededAndWeighted) {
    auto order = [](uint32_t seed, std::vector<float> weights) {
        auto seen = std::make_shared<std::vector<int>>();
        BtChildren c;
        for (int i = 0; i < 3; ++i)
            c.push_back(btAction([seen, i](BtContext&) {
                seen->push_back(i);
                return F; // a selector goes on to the next child
            }));
        BehaviorTree t(btRandomSelector(std::move(c), std::move(weights)), seed);
        t.tick(0.1f);
        return *seen;
    };
    EXPECT_EQ(order(5, {}), order(5, {})) << "the same seed gives the same order";
    EXPECT_EQ(order(5, {}).size(), 3u);
    bool varies = false;
    for (uint32_t s = 1; s < 30 && !varies; ++s) varies = order(s, {}) != order(1, {});
    EXPECT_TRUE(varies);
    // a heavy weight goes first most of the time
    int first = 0;
    for (uint32_t s = 1; s <= 200; ++s) first += order(s, { 0.05f, 0.05f, 10.0f })[0] == 2;
    EXPECT_GT(first, 180);
}

TEST(BehaviorTree, RandomSequenceRunsEveryChildOnce) {
    auto count = std::make_shared<int>(0);
    BtChildren c;
    for (int i = 0; i < 4; ++i)
        c.push_back(btAction([count](BtContext&) {
            ++*count;
            return S;
        }));
    BehaviorTree t(btRandomSequence(std::move(c)), 3);
    EXPECT_EQ(t.tick(0.1f), S);
    EXPECT_EQ(*count, 4);
}

TEST(BehaviorTree, InverterSucceederFailer) {
    EXPECT_EQ(tree(btInverter(btConstant(S))).tick(0.1f), F);
    EXPECT_EQ(tree(btInverter(btConstant(F))).tick(0.1f), S);
    auto run = script({ R });
    auto t = tree(btInverter(scripted(run)));
    EXPECT_EQ(t.tick(0.1f), R) << "running passes through";
    EXPECT_EQ(tree(btSucceeder(btConstant(F))).tick(0.1f), S);
    EXPECT_EQ(tree(btFailer(btConstant(S))).tick(0.1f), F);
    auto t2 = tree(btSucceeder(scripted(script({ R }))));
    EXPECT_EQ(t2.tick(0.1f), R);
}

TEST(BehaviorTree, RepeatCountsIterationsOnePerTick) {
    auto step = script({ S });
    auto t = tree(btRepeat(scripted(step), 3));
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(t.tick(0.1f), S);
    EXPECT_EQ(step->ran, 3);
    EXPECT_EQ(t.tick(0.1f), R) << "starts again after finishing";
    // for ever
    auto forever = script({ S });
    auto t2 = tree(btRepeat(scripted(forever), 0));
    for (int i = 0; i < 50; ++i) EXPECT_EQ(t2.tick(0.1f), R);
    EXPECT_EQ(forever->ran, 50);
    // a failing child ends it
    auto flaky = script({ S, S, F });
    auto t3 = tree(btRepeat(scripted(flaky), 10));
    EXPECT_EQ(t3.tick(0.1f), R);
    EXPECT_EQ(t3.tick(0.1f), R);
    EXPECT_EQ(t3.tick(0.1f), F);
}

TEST(BehaviorTree, RetryTriesUntilItSucceedsOrRunsOut) {
    auto flaky = script({ F, F, S });
    auto t = tree(btRetry(scripted(flaky), 5));
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(t.tick(0.1f), S);
    auto never = script({ F });
    auto t2 = tree(btRetry(scripted(never), 3));
    EXPECT_EQ(t2.tick(0.1f), R);
    EXPECT_EQ(t2.tick(0.1f), R);
    EXPECT_EQ(t2.tick(0.1f), F);
    EXPECT_EQ(never->ran, 3);
}

TEST(BehaviorTree, UntilFailEndsWhenTheChildFails) {
    auto step = script({ S, S, F });
    auto t = tree(btUntilFail(scripted(step)));
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(t.tick(0.1f), S);
}

TEST(BehaviorTree, CooldownRefusesUntilItsTimeIsUp) {
    auto fire = script({ S });
    auto t = tree(btCooldown(scripted(fire), 1.0f));
    EXPECT_EQ(t.tick(0.1f), S);
    EXPECT_EQ(fire->ran, 1);
    for (int i = 0; i < 8; ++i) EXPECT_EQ(t.tick(0.1f), F) << i;
    EXPECT_EQ(fire->ran, 1) << "the child is not run while cooling down";
    for (int i = 0; i < 3; ++i) t.tick(0.1f);
    EXPECT_EQ(fire->ran, 2) << "and runs again once the second has passed";
}

TEST(BehaviorTree, TimeoutAbortsALongChild) {
    auto slow = script({ R });
    auto t = tree(btTimeout(scripted(slow), 1.0f));
    BtStatus last = R;
    int ticks = 0;
    while (last == R && ticks < 100) {
        last = t.tick(0.1f);
        ++ticks;
    }
    EXPECT_EQ(last, F);
    EXPECT_NEAR(ticks * 0.1f, 1.0f, 0.25f);
    EXPECT_EQ(slow->aborted, 1);
    auto fast = script({ R, S });
    auto t2 = tree(btTimeout(scripted(fast), 1.0f));
    EXPECT_EQ(t2.tick(0.1f), R);
    EXPECT_EQ(t2.tick(0.1f), S);
}

TEST(BehaviorTree, DelayWaitsBeforeTheChildStarts) {
    auto go = script({ S });
    auto t = tree(btDelay(scripted(go), 0.5f));
    int ticks = 0;
    BtStatus s = R;
    while (s == R && ticks < 100) {
        s = t.tick(0.1f);
        ++ticks;
    }
    EXPECT_EQ(s, S);
    EXPECT_EQ(go->ran, 1);
    EXPECT_NEAR(ticks * 0.1f, 0.5f, 0.15f);
}

TEST(BehaviorTree, WaitSetCheckAndChance) {
    auto t = tree(btSequence(kids(btWait(0.3f), btSet("ready", true), btCheckTruthy("ready"), btCheck("n", BtOp::Equal, 0.0))));
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(t.tick(0.1f), F) << "n is not set yet: a compare on a missing key fails";
    t.blackboard().set("n", 0);
    EXPECT_EQ(t.tick(0.5f), S);
    EXPECT_TRUE(t.blackboard().getBool("ready"));
    // check against another key
    auto cmp = tree(btCheck("hp", BtOp::Less, std::string("limit"), true));
    cmp.blackboard().set("hp", 10);
    cmp.blackboard().set("limit", 30);
    EXPECT_EQ(cmp.tick(0.1f), S);
    cmp.blackboard().set("hp", 50);
    EXPECT_EQ(cmp.tick(0.1f), F);
    // an expected-false truthy check also takes a missing key
    EXPECT_EQ(tree(btCheckTruthy("nothing", false)).tick(0.1f), S);
    // chance: seeded, and about right
    int hits = 0;
    BehaviorTree c(btChance(0.25f), 11);
    for (int i = 0; i < 2000; ++i) hits += c.tick(0.01f) == S;
    EXPECT_GT(hits, 400);
    EXPECT_LT(hits, 600);
    EXPECT_EQ(tree(btChance(0.0f)).tick(0.1f), F);
    EXPECT_EQ(tree(btChance(1.0f)).tick(0.1f), S);
}

TEST(BehaviorTree, ResetAbortsWhatIsRunning) {
    auto work = script({ R });
    auto t = tree(btSequence(kids(btConstant(S), scripted(work))));
    EXPECT_EQ(t.tick(0.1f), R);
    t.reset();
    EXPECT_EQ(work->aborted, 1);
    EXPECT_EQ(t.tick(0.1f), R);
    EXPECT_EQ(work->ran, 2);
    EXPECT_EQ(BehaviorTree().tick(0.1f), F) << "an empty tree fails";
}

TEST(BehaviorTree, BadTimeStepsAreIgnored) {
    BehaviorTree t(btWait(1.0f));
    t.tick(-5.0f);
    t.tick(std::nanf(""));
    EXPECT_DOUBLE_EQ(t.time(), 0.0);
    EXPECT_EQ(t.tick(1.0f), S);
}

TEST(BehaviorTree, DescribeShowsTheTreeAndWhatRan) {
    auto work = script({ R });
    auto t = tree(btSelector(kids(btSequence(kids(btCheck("hp", BtOp::Less, 30.0), scripted(script({ S }), "enrage")), "boss phase 2"),
                                  scripted(work, "melee"))));
    t.tick(0.1f);
    const std::string d = t.describe();
    EXPECT_NE(d.find("selector"), std::string::npos);
    EXPECT_NE(d.find("sequence \"boss phase 2\""), std::string::npos);
    EXPECT_NE(d.find("check \"hp < 30\" [failure]"), std::string::npos);
    EXPECT_NE(d.find("action \"melee\" [running]"), std::string::npos);
    EXPECT_NE(d.find("action \"enrage\" [idle]"), std::string::npos) << "a node that never ran is idle";
    EXPECT_EQ(BehaviorTree().describe(), "(empty tree)\n");
}

// The scenario the layer exists for: a boss with phases.
TEST(BehaviorTree, ABossFightWithPhases) {
    auto log = std::make_shared<std::vector<std::string>>();
    auto say = [log](const std::string& what, BtStatus s) {
        return [log, what, s](BtContext&) {
            if (log->empty() || log->back() != what) log->push_back(what);
            return s;
        };
    };
    BtFactory f;
    f.addAction("charge", say("charge", R), [log](BtContext&) { log->push_back("charge cancelled"); });
    f.addAction("slam", say("slam", S));
    f.addAction("summon", say("summon", S));
    f.addAction("rage", say("rage", R));
    f.addCondition("playerNear", [](BtContext& c) { return c.blackboard.getBool("playerNear"); });
    const json spec = json::parse(R"({
        "type": "reactive_selector", "label": "boss",
        "children": [
            { "type": "reactive_sequence", "label": "phase 3", "children": [
                { "type": "check", "key": "hp", "op": "<", "value": 30 },
                { "type": "action", "name": "rage" } ] },
            { "type": "reactive_sequence", "label": "phase 2", "children": [
                { "type": "check", "key": "hp", "op": "<", "value": 60 },
                { "type": "cooldown", "seconds": 5, "child": { "type": "action", "name": "summon" } },
                "slam" ] },
            { "type": "reactive_sequence", "label": "phase 1", "children": [ "playerNear", "charge" ] }
        ] })");
    std::string error;
    auto root = f.build(spec, error);
    ASSERT_TRUE(root) << error;
    BehaviorTree t(std::move(root), 1);
    t.blackboard().set("hp", 100);
    t.blackboard().set("playerNear", true);
    t.tick(0.1f);
    t.tick(0.1f);
    EXPECT_EQ(log->back(), "charge");
    t.blackboard().set("hp", 50); // phase 2 pre-empts the charge (which is aborted after phase 2's first tick)
    t.tick(0.1f);
    EXPECT_EQ(*log, (std::vector<std::string>{ "charge", "summon", "slam", "charge cancelled" }));
    t.blackboard().set("hp", 20); // and phase 3 pre-empts that
    t.tick(0.1f);
    EXPECT_EQ(log->back(), "rage");
    t.blackboard().set("hp", 100); // healed: back to the opening
    t.tick(0.1f);
    EXPECT_EQ(log->back(), "charge");
}

TEST(BtFactory, BuildsEveryNodeType) {
    BtFactory f;
    f.addAction("go", [](BtContext&) { return S; });
    f.addCondition("ok", [](BtContext&) { return true; });
    const json spec = json::parse(R"({ "type": "sequence", "children": [
        "go", "ok", { "type": "success" }, { "type": "failure" },
        { "type": "inverter", "child": "ok" }, { "type": "succeeder", "child": "ok" }, { "type": "failer", "child": "ok" },
        { "type": "repeat", "count": 2, "child": "go" }, { "type": "retry", "attempts": 2, "child": "go" },
        { "type": "until_fail", "child": "go" }, { "type": "cooldown", "seconds": 1, "child": "go" },
        { "type": "timeout", "seconds": 1, "child": "go" }, { "type": "delay", "seconds": 1, "child": "go" },
        { "type": "check", "key": "k" }, { "type": "check", "key": "k", "expected": false },
        { "type": "check", "key": "a", "op": ">=", "valueKey": "b" }, { "type": "check", "key": "v", "op": "==", "value": [1, 2, 3] },
        { "type": "set", "key": "s", "value": "text" }, { "type": "wait", "seconds": 0.5 }, { "type": "chance", "probability": 0.5 },
        { "type": "parallel", "success": 1, "failure": "all", "children": [ "go" ] },
        { "type": "random_selector", "weights": [1, 2], "children": [ "go", "ok" ] },
        { "type": "random_sequence", "children": [ "go" ] },
        { "type": "reactive_sequence", "children": [ "go" ] }, { "type": "reactive_selector", "children": [ "go" ] },
        { "type": "selector", "children": [ { "type": "action", "name": "go" }, { "type": "condition", "name": "ok" } ] } ] })");
    std::string error;
    auto root = f.build(spec, error);
    ASSERT_TRUE(root) << error;
    EXPECT_EQ(root->children().size(), 26u);
    EXPECT_EQ(root->children()[0]->type(), "action");
    EXPECT_EQ(root->children()[1]->type(), "condition");
}

TEST(BtFactory, SubtreesAreReusedWithTheirOwnState) {
    BtFactory f;
    auto runs = std::make_shared<int>(0);
    f.addAction("work", [runs](BtContext&) {
        ++*runs;
        return S;
    });
    f.addTree("twice", json::parse(R"({ "type": "repeat", "count": 2, "child": "work" })"));
    std::string error;
    auto root = f.build(json::parse(R"({ "type": "sequence", "children": [
        { "type": "subtree", "name": "twice" }, { "type": "subtree", "name": "twice", "label": "again" } ] })"), error);
    ASSERT_TRUE(root) << error;
    EXPECT_EQ(root->children()[0]->name(), "twice");
    EXPECT_EQ(root->children()[1]->name(), "again");
    BehaviorTree t(std::move(root));
    BtStatus s = R;
    for (int i = 0; i < 10 && s == R; ++i) s = t.tick(0.1f);
    EXPECT_EQ(s, S);
    EXPECT_EQ(*runs, 4);
}

TEST(BtFactory, ExplainsMistakesWithThePathToThem) {
    BtFactory f;
    f.addAction("go", [](BtContext&) { return S; });
    f.addTree("loop", json::parse(R"({ "type": "subtree", "name": "loop" })"));
    auto fails = [&](const char* text, const std::string& expect) {
        std::string error;
        auto n = f.build(json::parse(text), error);
        EXPECT_EQ(n, nullptr) << text;
        EXPECT_NE(error.find(expect), std::string::npos) << "'" << error << "' should mention '" << expect << "'";
    };
    fails(R"({ "type": "sequnce", "children": [] })", "root: unknown type 'sequnce'");
    fails(R"({ "children": [] })", "root: has no \"type\"");
    fails(R"({ "type": "sequence" })", "sequence needs \"children\"");
    fails(R"({ "type": "sequence", "children": [ "go", "jump" ] })", "root.children[1]: no action or condition called 'jump'");
    fails(R"({ "type": "sequence", "children": [ { "type": "inverter" } ] })", "root.children[0]: inverter needs one \"child\"");
    fails(R"({ "type": "sequence", "children": [ 7 ] })", "root.children[0]: a node is an object");
    fails(R"({ "type": "wait" })", "needs \"seconds\"");
    fails(R"({ "type": "wait", "seconds": "long" })", "\"seconds\" must be a number");
    fails(R"({ "type": "cooldown", "seconds": -1, "child": "go" })", "can't be negative");
    fails(R"({ "type": "chance", "probability": 2 })", "0 to 1");
    fails(R"({ "type": "check", "key": "x", "op": "=>", "value": 1 })", "\"op\" is one of");
    fails(R"({ "type": "check", "key": "x", "op": "<" })", "needs \"value\"");
    fails(R"({ "type": "check", "op": "<", "value": 1 })", "needs \"key\"");
    fails(R"({ "type": "set", "key": "x", "value": {} })", "set needs \"value\"");
    fails(R"({ "type": "action", "name": "fly" })", "no action called 'fly'");
    fails(R"({ "type": "condition", "name": "go" })", "no condition called 'go'");
    fails(R"({ "type": "subtree", "name": "nowhere" })", "no tree called 'nowhere'");
    fails(R"({ "type": "subtree", "name": "loop" })", "a subtree that contains itself");
    fails(R"({ "type": "parallel", "success": "most", "children": [] })", "\"success\" is \"all\"");
    fails(R"({ "type": "random_selector", "weights": [1], "children": [ "go", "go" ] })", "1 weights for 2 children");
    fails(R"({ "type": "random_selector", "weights": "heavy", "children": [] })", "list of numbers");
    // too many nodes
    json big = { { "type", "sequence" }, { "children", json::array() } };
    for (size_t i = 0; i < BtFactory::kMaxNodes + 5; ++i) big["children"].push_back("go");
    fails(big.dump().c_str(), "more than");
}
