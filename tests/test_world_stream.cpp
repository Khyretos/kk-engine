// World and level streaming (kke/WorldStream.h, ENG-F64 / ENG-F78).
#include "kke/WorldStream.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>

using namespace kke;

namespace {

struct Fake : StreamBackend {
    std::vector<std::string> log;
    std::map<std::string, int> pendingPolls;  // polls that say "not yet" before Ready
    std::map<std::string, int> beginFails;    // times begin() refuses
    std::map<std::string, int> loadFails;     // times poll() says Failed
    std::set<std::string> never;              // pending for ever
    std::map<std::string, int> polled;

    bool begin(const StreamCell& c, std::string* error) override {
        log.push_back("begin:" + c.name);
        if (beginFails[c.name] > 0) {
            --beginFails[c.name];
            if (error) *error = "disk error";
            return false;
        }
        polled[c.name] = 0;
        return true;
    }
    CellLoad poll(const StreamCell& c, std::string* error) override {
        if (never.count(c.name)) return CellLoad::Pending;
        if (loadFails[c.name] > 0) {
            --loadFails[c.name];
            if (error) *error = "corrupt";
            return CellLoad::Failed;
        }
        return polled[c.name]++ < pendingPolls[c.name] ? CellLoad::Pending : CellLoad::Ready;
    }
    void activate(const StreamCell& c) override { log.push_back("activate:" + c.name); }
    void unload(const StreamCell& c) override { log.push_back("unload:" + c.name); }

    size_t count(const std::string& what) const {
        return static_cast<size_t>(std::count_if(log.begin(), log.end(), [&](const std::string& l) { return l == what; }));
    }
    std::vector<std::string> only(const std::string& prefix) const {
        std::vector<std::string> out;
        for (const std::string& l : log)
            if (l.rfind(prefix, 0) == 0) out.push_back(l.substr(prefix.size()));
        return out;
    }
};

// ten cells in a row along x, each 10 m wide: "row_0_0" ... "row_9_0"
struct Row {
    Fake backend;
    WorldStreamer streamer;
    Row() : streamer(&backend) {
        streamer.config().loadRadius = 25.0f;
        streamer.config().unloadRadius = 35.0f;
        streamer.config().maxStartsPerUpdate = 1;
        streamer.config().maxLoading = 2;
        std::string error;
        EXPECT_EQ(streamer.addGrid("row", 10.0f, 0, 0, 9, 0, &error), 10u) << error;
    }
    void run(int updates, float dt = 0.1f) {
        for (int i = 0; i < updates; ++i) streamer.update(dt);
    }
    std::vector<std::string> loadedNames() const {
        auto l = streamer.loaded();
        std::sort(l.begin(), l.end());
        return l;
    }
};

std::string cellName(int x) { return "row_" + std::to_string(x) + "_0"; }

} // namespace

TEST(WorldStreamer, CellsNeedANameAnAreaAndAUniqueName) {
    WorldStreamer s;
    std::string error;
    StreamCell c;
    EXPECT_FALSE(s.addCell(c, &error));
    EXPECT_NE(error.find("needs a name"), std::string::npos);
    c.name = "a";
    EXPECT_FALSE(s.addCell(c, &error));
    EXPECT_NE(error.find("no area"), std::string::npos) << error;
    c.max = { 10, 10 };
    EXPECT_TRUE(s.addCell(c, &error)) << error;
    EXPECT_FALSE(s.addCell(c, &error));
    EXPECT_NE(error.find("'a' already"), std::string::npos) << error;
    EXPECT_EQ(s.cellCount(), 1u);
    EXPECT_TRUE(s.hasCell("a"));
    EXPECT_FALSE(s.hasCell("b"));
    ASSERT_NE(s.cell("a"), nullptr);
    EXPECT_EQ(s.cell("b"), nullptr);
    StreamCell inverted;
    inverted.name = "inv";
    inverted.min = { 5, 5 };
    inverted.max = { 5, 9 };
    EXPECT_FALSE(s.addCell(inverted, &error)) << "no width";
}

TEST(WorldStreamer, AGridIsNamedAndPlaced) {
    WorldStreamer s;
    std::string error;
    EXPECT_EQ(s.addGrid("tile", 64.0f, -1, -2, 1, 0, &error), 9u) << error;
    const StreamCell* c = s.cell("tile_-1_-2");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->min, glm::vec2(-64, -128));
    EXPECT_EQ(c->max, glm::vec2(0, -64));
    EXPECT_EQ(c->data, "tile_-1_-2");
    EXPECT_EQ(s.addGrid("tile", 64.0f, 0, 0, 0, 0, &error), 0u) << "a name that is taken";
    EXPECT_NE(error.find("already"), std::string::npos);
    EXPECT_EQ(s.addGrid("x", 0.0f, 0, 0, 1, 1, &error), 0u);
    EXPECT_NE(error.find("size"), std::string::npos);
    EXPECT_EQ(s.addGrid("y", 10.0f, 3, 0, 2, 0, &error), 0u);
    EXPECT_NE(error.find("before"), std::string::npos);
}

TEST(WorldStreamer, TheNearestCellsLoadFirstAFewAtATime) {
    Row r;
    r.streamer.setFocus("player", glm::vec3(5, 0, 5)); // inside cell 0; cells 0-3 are within 25 m
    r.run(1);
    EXPECT_EQ(r.backend.only("begin:"), (std::vector<std::string>{ cellName(0) })) << "one start per update";
    r.run(3);
    EXPECT_EQ(r.backend.only("begin:"), (std::vector<std::string>{ cellName(0), cellName(1), cellName(2), cellName(3) })) << "nearest first";
    EXPECT_EQ(r.loadedNames(), (std::vector<std::string>{ cellName(0), cellName(1), cellName(2), cellName(3) }));
    r.run(5);
    EXPECT_EQ(r.backend.count("begin:" + cellName(4)), 0u) << "35 m is out of reach";
    EXPECT_EQ(r.streamer.stats().loads, 4u);
    EXPECT_EQ(r.streamer.stats().loadedNow, 4);
    // each cell is activated after it began
    const auto first = std::find(r.backend.log.begin(), r.backend.log.end(), "begin:" + cellName(0));
    const auto act = std::find(r.backend.log.begin(), r.backend.log.end(), "activate:" + cellName(0));
    EXPECT_LT(first, act);
}

TEST(WorldStreamer, StandingOnABorderDoesNotLoadAndUnloadEveryFrame) {
    Row r;
    r.streamer.setFocus("p", glm::vec3(5, 0, 5));
    r.run(8);
    ASSERT_EQ(r.streamer.loaded().size(), 4u);
    r.streamer.takeEvents();
    r.backend.log.clear();
    // cell 3 is 25 m away: at the load edge. Wobbling between 25 and 33 m (inside the unload radius) changes nothing.
    for (int i = 0; i < 40; ++i) {
        r.streamer.setFocus("p", glm::vec3(i % 2 ? 5.0f : -3.0f, 0, 5));
        r.run(1);
    }
    EXPECT_TRUE(r.backend.log.empty()) << "nothing loaded or unloaded: " << r.backend.log.size() << " calls";
    EXPECT_TRUE(r.streamer.takeEvents().empty());
    // well beyond the unload radius: it goes
    r.streamer.setFocus("p", glm::vec3(-20, 0, 5)); // cell 3 is now 50 m away, cell 1 is 30
    r.run(1);
    EXPECT_EQ(r.backend.count("unload:" + cellName(3)), 1u);
    EXPECT_EQ(r.backend.count("unload:" + cellName(2)), 1u) << "40 m";
    EXPECT_EQ(r.backend.count("unload:" + cellName(1)), 0u) << "30 m: still inside the unload radius";
}

TEST(WorldStreamer, TheFarthestCellsUnloadFirstWithinTheBudget) {
    Row r;
    r.streamer.config().maxUnloadsPerUpdate = 2;
    r.streamer.setFocus("p", glm::vec3(5, 0, 5));
    r.run(8);
    ASSERT_EQ(r.streamer.loaded().size(), 4u);
    r.backend.log.clear();
    r.streamer.setFocus("p", glm::vec3(95, 0, 5)); // far away: the first four are 55 to 85 m away
    r.streamer.config().maxStartsPerUpdate = 0 + 1;
    r.run(1);
    EXPECT_EQ(r.backend.only("unload:"), (std::vector<std::string>{ cellName(0), cellName(1) })) << "the farthest two";
    r.run(1);
    EXPECT_EQ(r.backend.count("unload:" + cellName(2)), 1u);
    EXPECT_EQ(r.backend.count("unload:" + cellName(3)), 1u);
}

TEST(WorldStreamer, ALoadInTheBackgroundIsWaitedFor) {
    Row r;
    r.backend.pendingPolls[cellName(0)] = 3;
    r.streamer.setFocus("p", glm::vec3(5, 0, 5));
    r.run(1);
    EXPECT_EQ(r.streamer.state(cellName(0)), CellState::Loading);
    EXPECT_EQ(r.streamer.loading(), (std::vector<std::string>{ cellName(0) }));
    EXPECT_EQ(r.backend.count("activate:" + cellName(0)), 0u);
    r.run(3);
    EXPECT_EQ(r.streamer.state(cellName(0)), CellState::Loaded);
    EXPECT_EQ(r.backend.count("activate:" + cellName(0)), 1u);
    const auto events = r.streamer.takeEvents();
    const auto it = std::find_if(events.begin(), events.end(), [&](const StreamEvent& e) { return e.kind == StreamEvent::Kind::Loaded && e.name == cellName(0); });
    EXPECT_NE(it, events.end()) << "it is announced when it is in";
}

TEST(WorldStreamer, NoMoreLoadsInFlightThanTheLimit) {
    Row r;
    for (int i = 0; i < 10; ++i) r.backend.never.insert(cellName(i));
    r.streamer.config().maxStartsPerUpdate = 5;
    r.streamer.config().maxLoading = 2;
    r.streamer.setFocus("p", glm::vec3(5, 0, 5));
    r.run(20);
    EXPECT_EQ(r.streamer.loading().size(), 2u);
    EXPECT_EQ(r.backend.only("begin:").size(), 2u);
}

TEST(WorldStreamer, ALoadThatIsNoLongerWantedIsCancelled) {
    Row r;
    r.backend.never.insert(cellName(0));
    r.streamer.setFocus("p", glm::vec3(5, 0, 5));
    r.run(1);
    ASSERT_EQ(r.streamer.state(cellName(0)), CellState::Loading);
    r.streamer.setFocus("p", glm::vec3(95, 0, 5)); // gone before it finished
    r.run(1);
    EXPECT_EQ(r.streamer.state(cellName(0)), CellState::Unloaded);
    EXPECT_EQ(r.backend.count("unload:" + cellName(0)), 1u) << "the backend is told, to stop the work";
    EXPECT_EQ(r.backend.count("activate:" + cellName(0)), 0u);
    EXPECT_EQ(r.streamer.stats().cancelled, 1u);
    for (const StreamEvent& e : r.streamer.takeEvents()) EXPECT_NE(e.kind, StreamEvent::Kind::Unloaded) << "it never loaded: no unload event";
}

TEST(WorldStreamer, AFailedCellIsTriedAgainAfterItsDelayAFewTimes) {
    Row r;
    r.streamer.config().retryDelay = 2.0f;
    r.streamer.config().maxRetries = 2;
    r.backend.beginFails[cellName(0)] = 2; // refuses twice, then works
    r.streamer.setFocus("p", glm::vec3(5, 0, 5));
    r.run(1);
    EXPECT_EQ(r.streamer.state(cellName(0)), CellState::Failed);
    auto events = r.streamer.takeEvents();
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events[0].kind, StreamEvent::Kind::Failed);
    EXPECT_EQ(events[0].reason, "disk error");
    r.run(10); // 1 s: before the delay (cell 1..3 load meanwhile)
    EXPECT_EQ(r.backend.count("begin:" + cellName(0)), 1u) << "not before the retry delay";
    r.run(15); // 2.5 s
    EXPECT_EQ(r.backend.count("begin:" + cellName(0)), 2u);
    r.run(30);
    EXPECT_EQ(r.streamer.state(cellName(0)), CellState::Loaded) << "the third try works";
    EXPECT_EQ(r.streamer.stats().retries, 2u);
    EXPECT_EQ(r.streamer.stats().failures, 2u);
}

TEST(WorldStreamer, ACellThatKeepsFailingIsGivenUpOnAndDoesNotHoldUpReady) {
    Row r;
    r.streamer.config().retryDelay = 0.5f;
    r.streamer.config().maxRetries = 2;
    r.streamer.config().requiredRadius = 15.0f;
    r.backend.loadFails[cellName(1)] = 100; // never works
    r.streamer.setFocus("p", glm::vec3(5, 0, 5));
    r.run(5);
    EXPECT_FALSE(r.streamer.ready()) << "failed once and waiting to try again";
    r.run(140);
    EXPECT_EQ(r.streamer.state(cellName(1)), CellState::Failed);
    EXPECT_EQ(r.backend.count("begin:" + cellName(1)), 3u) << "the first try and two retries";
    EXPECT_TRUE(r.streamer.ready()) << "a cell that will not come is not waited for";
    const size_t begins = r.backend.count("begin:" + cellName(1));
    r.run(100);
    EXPECT_EQ(r.backend.count("begin:" + cellName(1)), begins) << "and not tried again";
}

TEST(WorldStreamer, ALoadThatTakesForeverTimesOut) {
    Row r;
    r.streamer.config().loadTimeout = 3.0f;
    r.backend.never.insert(cellName(0));
    r.streamer.setFocus("p", glm::vec3(5, 0, 5));
    r.run(20); // 2 s
    EXPECT_EQ(r.streamer.state(cellName(0)), CellState::Loading);
    r.run(15); // 3.5 s
    EXPECT_EQ(r.streamer.state(cellName(0)), CellState::Failed);
    EXPECT_EQ(r.streamer.stats().timeouts, 1u);
    EXPECT_EQ(r.backend.count("unload:" + cellName(0)), 1u) << "the backend is told to stop";
    const auto events = r.streamer.takeEvents();
    const auto failed = std::find_if(events.begin(), events.end(), [](const StreamEvent& e) { return e.kind == StreamEvent::Kind::Failed; });
    ASSERT_NE(failed, events.end());
    EXPECT_NE(failed->reason.find("timed out"), std::string::npos);
}

TEST(WorldStreamer, PersistentCellsLoadFirstAndStay) {
    Row r;
    StreamCell hub;
    hub.name = "hub";
    hub.min = { 500, 500 };
    hub.max = { 520, 520 };
    hub.persistent = true;
    ASSERT_TRUE(r.streamer.addCell(hub));
    r.run(1); // no focus at all
    EXPECT_EQ(r.streamer.state("hub"), CellState::Loaded) << "persistent cells need no one near";
    r.streamer.setFocus("p", glm::vec3(5, 0, 5));
    r.run(1);
    EXPECT_EQ(r.backend.only("begin:").front(), "hub");
    r.streamer.setFocus("p", glm::vec3(-5000, 0, 5));
    r.run(20);
    EXPECT_EQ(r.streamer.state("hub"), CellState::Loaded) << "and never unload";
    EXPECT_EQ(r.backend.count("unload:hub"), 0u);
}

TEST(WorldStreamer, PinnedCellsStayAndForcedCellsLoadFromAnywhere) {
    Row r;
    r.streamer.setFocus("p", glm::vec3(5, 0, 5));
    r.run(8);
    ASSERT_TRUE(r.streamer.pin(cellName(2), true));
    EXPECT_FALSE(r.streamer.pin("nowhere", true));
    r.streamer.setFocus("p", glm::vec3(95, 0, 5));
    r.run(10);
    EXPECT_EQ(r.streamer.state(cellName(2)), CellState::Loaded) << "pinned: kept when far";
    EXPECT_EQ(r.streamer.state(cellName(1)), CellState::Unloaded);
    ASSERT_TRUE(r.streamer.pin(cellName(2), false));
    r.run(2);
    EXPECT_EQ(r.streamer.state(cellName(2)), CellState::Unloaded) << "unpinned: it goes";
    // forced: far away, still loaded
    ASSERT_TRUE(r.streamer.force(cellName(9) , false));
    ASSERT_TRUE(r.streamer.force(cellName(5), true));
    r.streamer.setFocus("p", glm::vec3(-500, 0, 5));
    r.run(5);
    EXPECT_EQ(r.streamer.state(cellName(5)), CellState::Loaded);
    ASSERT_TRUE(r.streamer.force(cellName(5), false));
    r.run(2);
    EXPECT_EQ(r.streamer.state(cellName(5)), CellState::Unloaded);
    EXPECT_FALSE(r.streamer.force("nowhere", true));
}

TEST(WorldStreamer, ProgressAndReadyFollowTheCellsThatMatter) {
    Row r;
    r.streamer.config().requiredRadius = 15.0f; // cells 0, 1 and 2 (5 m, 0 and 5 m... see below)
    r.streamer.setFocus("p", glm::vec3(5, 0, 5)); // cell 0: 0 m; cell 1: 5 m; cell 2: 15 m; cell 3: 25 m
    EXPECT_FLOAT_EQ(r.streamer.progress(), 0.0f);
    EXPECT_FALSE(r.streamer.ready());
    r.run(1);
    EXPECT_NEAR(r.streamer.progress(), 1.0f / 3.0f, 1e-5f);
    r.run(1);
    EXPECT_NEAR(r.streamer.progress(), 2.0f / 3.0f, 1e-5f);
    EXPECT_FALSE(r.streamer.ready());
    r.run(1);
    EXPECT_FLOAT_EQ(r.streamer.progress(), 1.0f);
    EXPECT_TRUE(r.streamer.ready()) << "the three inside 15 m are in; cell 3 is still to come and does not matter";
    EXPECT_EQ(r.streamer.state(cellName(3)), CellState::Unloaded);
    // no cells at all, or none required: ready at once
    WorldStreamer empty;
    EXPECT_TRUE(empty.ready());
    EXPECT_FLOAT_EQ(empty.progress(), 1.0f);
}

TEST(WorldStreamer, SeveralFociKeepTheirOwnCellsLoaded) {
    Row r;
    r.streamer.config().maxStartsPerUpdate = 4;
    r.streamer.setFocus("one", glm::vec3(5, 0, 5));
    r.streamer.setFocus("two", glm::vec3(95, 0, 5));
    EXPECT_EQ(r.streamer.focusCount(), 2u);
    r.run(4);
    const auto names = r.loadedNames();
    EXPECT_NE(std::find(names.begin(), names.end(), cellName(0)), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), cellName(9)), names.end());
    EXPECT_EQ(std::find(names.begin(), names.end(), cellName(5)), names.end()) << "nobody is near the middle";
    EXPECT_TRUE(r.streamer.removeFocus("two"));
    EXPECT_FALSE(r.streamer.removeFocus("two"));
    r.run(6);
    EXPECT_EQ(r.streamer.state(cellName(9)), CellState::Unloaded) << "the cells only the second one wanted go";
    EXPECT_EQ(r.streamer.state(cellName(0)), CellState::Loaded);
}

TEST(WorldStreamer, TheNumberOfFociIsLimited) {
    WorldStreamer s;
    for (size_t i = 0; i < WorldStreamer::kMaxFocus; ++i) EXPECT_TRUE(s.setFocus("f" + std::to_string(i), glm::vec3(0)));
    EXPECT_FALSE(s.setFocus("one more", glm::vec3(0)));
    EXPECT_TRUE(s.setFocus("f0", glm::vec3(1))) << "moving one that exists is fine";
    EXPECT_FALSE(s.setFocus("f0", glm::vec3(NAN, 0, 0))) << "a position that is not a number";
}

TEST(WorldStreamer, BiasAndPerCellRadii) {
    Fake backend;
    WorldStreamer s(&backend);
    s.config().loadRadius = 50.0f;
    s.config().unloadRadius = 60.0f;
    s.config().maxStartsPerUpdate = 1;
    auto add = [&](const char* name, float x, float bias, float load, float unload) {
        StreamCell c;
        c.name = name;
        c.min = { x, 0 };
        c.max = { x + 10, 10 };
        c.bias = bias;
        c.loadRadius = load;
        c.unloadRadius = unload;
        ASSERT_TRUE(s.addCell(c));
    };
    add("near", 10.0f, 0.0f, -1, -1);       // 10 m
    add("important", 30.0f, 25.0f, -1, -1); // 30 m but worth 25 m of distance: 5, so before "near"
    add("shy", 20.0f, 0.0f, 5.0f, -1);      // 20 m away but only loads within 5 m
    s.setFocus("p", glm::vec3(0, 0, 5));
    for (int i = 0; i < 6; ++i) s.update(0.1f);
    EXPECT_EQ(backend.only("begin:"), (std::vector<std::string>{ "important", "near" }));
    EXPECT_EQ(s.state("shy"), CellState::Unloaded);
    s.setFocus("p", glm::vec3(16, 0, 5)); // 4 m from "shy"
    s.update(0.1f);
    EXPECT_EQ(s.state("shy"), CellState::Loaded);
    // an unload radius smaller than the load radius would load and unload every frame: it is lifted to the load radius
    StreamCell tight;
    tight.name = "tight";
    tight.min = { 100, 0 };
    tight.max = { 110, 10 };
    tight.loadRadius = 5.0f;
    tight.unloadRadius = 2.0f;
    ASSERT_TRUE(s.addCell(tight));
    s.setFocus("p", glm::vec3(96, 0, 5)); // 4 m away
    s.update(0.1f);
    EXPECT_EQ(s.state("tight"), CellState::Loaded);
    s.setFocus("p", glm::vec3(95.5f, 0, 5)); // 4.5 m: beyond its unload radius of 2 but inside its load radius of 5
    s.update(0.1f);
    EXPECT_EQ(s.state("tight"), CellState::Loaded) << "the unload radius is never closer than the load radius";
    s.setFocus("p", glm::vec3(94.0f, 0, 5)); // 6 m
    s.update(0.1f);
    EXPECT_EQ(s.state("tight"), CellState::Unloaded);
}

TEST(WorldStreamer, ClearUnloadsEverythingAndForgetsTheCells) {
    Row r;
    r.streamer.setFocus("p", glm::vec3(5, 0, 5));
    r.run(8);
    r.backend.log.clear();
    r.streamer.clear();
    EXPECT_EQ(r.backend.only("unload:").size(), 4u);
    EXPECT_EQ(r.streamer.cellCount(), 0u);
    EXPECT_TRUE(r.streamer.loaded().empty());
    r.run(3);
    EXPECT_TRUE(r.backend.only("begin:").empty());
}

TEST(WorldStreamer, WithoutABackendOrAFocusNothingHappens) {
    WorldStreamer s;
    std::string error;
    ASSERT_EQ(s.addGrid("g", 10.0f, 0, 0, 2, 2, &error), 9u);
    s.setFocus("p", glm::vec3(5, 0, 5));
    s.update(0.1f); // no backend
    EXPECT_TRUE(s.loaded().empty());
    EXPECT_EQ(s.state("nowhere"), CellState::Unloaded);
    EXPECT_GT(s.distance("nowhere"), 1e20f);
    Fake backend;
    WorldStreamer t(&backend);
    t.addGrid("g", 10.0f, 0, 0, 2, 2);
    t.update(0.1f); // no focus
    EXPECT_TRUE(backend.log.empty());
    EXPECT_GT(t.distance("g_0_0"), 1e20f);
    t.setFocus("p", glm::vec3(15, 0, 5));
    EXPECT_NEAR(t.distance("g_0_0"), 5.0f, 1e-5f) << "from the focus to the cell's edge";
    EXPECT_NEAR(t.distance("g_1_0"), 0.0f, 1e-5f) << "inside";
    EXPECT_NEAR(t.distance("g_2_2"), std::sqrt(5.0f * 5.0f + 15.0f * 15.0f), 1e-4f) << "across a corner: 5 m in x, 15 m in z";
}

TEST(WorldStreamer, EventsComeInOrderAndAreTakenOnce) {
    Row r;
    r.streamer.setFocus("p", glm::vec3(5, 0, 5));
    r.run(2);
    r.streamer.setFocus("p", glm::vec3(95, 0, 5));
    r.run(3);
    const auto events = r.streamer.takeEvents();
    std::vector<std::string> shape;
    for (const StreamEvent& e : events) shape.push_back(std::string(e.kind == StreamEvent::Kind::Loaded ? "+" : e.kind == StreamEvent::Kind::Unloaded ? "-" : "?") + e.name);
    ASSERT_GE(shape.size(), 4u);
    EXPECT_EQ(shape[0], "+" + cellName(0));
    EXPECT_EQ(shape[1], "+" + cellName(1));
    EXPECT_TRUE(r.streamer.takeEvents().empty());
}

TEST(OriginShifter, TheWorldIsMovedBackInWholeStepsInsteadOfThePlayerTravellingFar) {
    OriginShifter o(2000.0, 1000.0);
    EXPECT_EQ(o.update(glm::dvec3(500, 0, 0)), glm::dvec3(0.0));
    EXPECT_EQ(o.update(glm::dvec3(2000, 0, 0)), glm::dvec3(0.0)) << "at the threshold, not past it";
    EXPECT_EQ(o.shifts(), 0);
    const glm::dvec3 shift = o.update(glm::dvec3(2400, 10, -50));
    EXPECT_EQ(shift, glm::dvec3(2000, 0, 0)) << "to the nearest whole step";
    EXPECT_EQ(o.origin(), glm::dvec3(2000, 0, 0));
    EXPECT_EQ(o.shifts(), 1);
    EXPECT_EQ(o.update(glm::dvec3(2500, 0, 0)), glm::dvec3(0.0));
    // the local position of the focus after the shift is small
    EXPECT_EQ(o.toLocal(glm::dvec3(2400, 10, -50)), glm::vec3(400, 10, -50));
    EXPECT_EQ(o.toWorld(glm::vec3(400, 10, -50)), glm::dvec3(2400, 10, -50));
}

TEST(OriginShifter, ALongJourneyKeepsLocalNumbersSmallAndWorldNumbersExact) {
    OriginShifter o(2000.0, 1000.0);
    glm::dvec3 world(0.0);
    float worstLocal = 0.0f;
    for (int i = 0; i < 100000; ++i) {
        world += glm::dvec3(3.7, 0.0, 1.1); // 100 km on, a step at a time
        o.update(world);
        worstLocal = std::max(worstLocal, glm::length(o.toLocal(world)));
    }
    EXPECT_GT(world.x, 300000.0);
    EXPECT_LT(worstLocal, 3600.0f) << "the player is never far from the local zero";
    const glm::dvec3 back = o.toWorld(o.toLocal(world));
    EXPECT_NEAR(back.x, world.x, 0.01) << "and the world position is recovered to a centimetre";
    EXPECT_NEAR(std::fmod(o.origin().x, 1000.0), 0.0, 0.0) << "the origin only ever moves in whole steps: no rounding";
    EXPECT_GT(o.shifts(), 100);
}

TEST(OriginShifter, BadSettingsFallBackToSomethingSensible) {
    OriginShifter o(-5.0, 0.0);
    EXPECT_EQ(o.update(glm::dvec3(1500, 0, 0)), glm::dvec3(0.0)) << "the default threshold (2000) is used";
    EXPECT_NE(o.update(glm::dvec3(2600, 0, 0)), glm::dvec3(0.0));
}
