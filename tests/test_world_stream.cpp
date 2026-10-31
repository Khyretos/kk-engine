#include "kke/WorldStream.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

using kke::CellState;
using kke::StreamAction;
using kke::StreamCell;
using kke::StreamConfig;
using kke::WorldStreamer;

namespace {

StreamCell cellAt(const char* id, float x, float z, float size = 10.0f) {
    StreamCell c;
    c.id = id;
    c.box = { { x, -1000.0f, z }, { x + size, 1000.0f, z + size } };
    return c;
}

// Does what the streamer says, loading at once.
std::vector<StreamAction> step(WorldStreamer& w, float dt = 0.1f) {
    const auto actions = w.update(dt);
    for (const StreamAction& a : actions)
        if (a.kind == StreamAction::Kind::Load) w.loaded(a.id);
    return actions;
}

std::vector<std::string> ids(const std::vector<StreamAction>& actions, StreamAction::Kind kind) {
    std::vector<std::string> out;
    for (const auto& a : actions)
        if (a.kind == kind) out.push_back(a.id);
    return out;
}

} // namespace

TEST(WorldStream, BoxDistanceIsZeroInsideAndEuclideanOutside) {
    const kke::StreamBox b{ { 0, 0, 0 }, { 10, 10, 10 } };
    EXPECT_FLOAT_EQ(b.distanceTo({ 5, 5, 5 }), 0.0f);
    EXPECT_FLOAT_EQ(b.distanceTo({ 13, 5, 5 }), 3.0f);
    EXPECT_FLOAT_EQ(b.distanceTo({ 13, 14, 5 }), 5.0f);
    EXPECT_FLOAT_EQ(b.distanceTo({ -3, 5, 5 }), 3.0f);
}

TEST(WorldStream, ACellLoadsWhenAnObserverComesWithinTheRadiusAndStaysUntilTheyAreFurther) {
    WorldStreamer w;
    StreamConfig c;
    c.loadRadius = 50.0f;
    c.unloadRadius = 80.0f;
    w.setConfig(c);
    ASSERT_TRUE(w.addCell(cellAt("hamlet", 100, 0)));
    EXPECT_FALSE(w.addCell(cellAt("hamlet", 0, 0))) << "an id is one cell";
    w.setObservers({ { 0, 0, 0 } }); // 100 m away
    EXPECT_TRUE(step(w).empty());
    EXPECT_EQ(w.state("hamlet"), CellState::Unloaded);
    w.setObservers({ { 60, 0, 0 } }); // 40 m from the box: within 50
    const auto a = step(w);
    ASSERT_EQ(a.size(), 1u);
    EXPECT_EQ(a[0].kind, StreamAction::Kind::Load);
    EXPECT_TRUE(w.isLoaded("hamlet"));
    w.setObservers({ { 30, 0, 0 } }); // 70 m: past the load radius, inside the unload radius: it stays
    EXPECT_TRUE(step(w).empty());
    EXPECT_TRUE(w.isLoaded("hamlet")) << "hysteresis: no load/unload flicker on the edge";
    w.setObservers({ { 0, 0, 0 } }); // 100 m: gone
    const auto b = step(w);
    ASSERT_EQ(b.size(), 1u);
    EXPECT_EQ(b[0].kind, StreamAction::Kind::Unload);
    EXPECT_EQ(w.state("hamlet"), CellState::Unloaded);
}

TEST(WorldStream, LoadsAreSpreadOutNearestFirstAndNeverAskedTwice) {
    WorldStreamer w;
    StreamConfig c;
    c.loadRadius = 100.0f;
    c.maxLoadsPerUpdate = 1;
    c.maxInFlight = 1;
    w.setConfig(c);
    w.addCell(cellAt("far", 60, 0));
    w.addCell(cellAt("near", 12, 0));
    w.addCell(cellAt("mid", 30, 0));
    w.setObservers({ { 0, 0, 0 } });
    const auto first = w.update(0.1f); // nothing confirmed yet
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0].id, "near");
    EXPECT_EQ(w.state("near"), CellState::Loading);
    EXPECT_TRUE(w.update(0.1f).empty()) << "one in flight is the limit; and 'near' is not asked for again";
    w.loaded("near");
    EXPECT_EQ(step(w)[0].id, "mid");
    EXPECT_EQ(step(w)[0].id, "far");
    EXPECT_TRUE(step(w).empty());
    EXPECT_EQ(w.stats().loaded, 3u);
}

TEST(WorldStream, AGridMakesCellsAroundTheObserverAndForgetsThemBehindIt) {
    WorldStreamer w;
    StreamConfig c;
    c.gridSize = 100.0f;
    c.loadRadius = 120.0f;
    c.unloadRadius = 160.0f;
    c.maxLoadsPerUpdate = 16;
    c.maxInFlight = 16;
    c.maxUnloadsPerUpdate = 16;
    w.setConfig(c);
    w.setObservers({ { 50, 0, 50 } }); // in cell 0,0
    step(w);
    EXPECT_TRUE(w.isLoaded("0,0"));
    EXPECT_TRUE(w.isLoaded("1,0")) << "20 m from its edge";
    EXPECT_TRUE(w.isLoaded("-1,0"));
    EXPECT_TRUE(w.isLoaded("1,1"));
    EXPECT_FALSE(w.isLoaded("2,0")) << "150 m from the observer";
    EXPECT_EQ(w.gridCellAt({ 50, 0, 50 }), "0,0");
    EXPECT_EQ(w.gridCellAt({ -1, 0, 250 }), "-1,2");
    // Walk 400 m east: the old cells unload and are forgotten, new ones appear.
    w.setObservers({ { 450, 0, 50 } });
    const auto actions = step(w);
    const auto unloaded = ids(actions, StreamAction::Kind::Unload);
    EXPECT_NE(std::find(unloaded.begin(), unloaded.end(), "0,0"), unloaded.end());
    EXPECT_TRUE(w.isLoaded("4,0"));
    EXPECT_EQ(w.state("0,0"), CellState::Unloaded);
    step(w);
    EXPECT_EQ(w.state("0,0"), CellState::Unknown) << "an unloaded cell out of reach is forgotten at the next update: the map stays small";
    EXPECT_LT(w.stats().cells, 20u);
}

TEST(WorldStream, PersistentCellsAreLoadedWithoutAnyoneAndNeverUnloaded) {
    WorldStreamer w;
    StreamConfig c;
    c.maxResident = 2;
    c.maxLoadsPerUpdate = 8;
    c.maxInFlight = 8;
    w.setConfig(c);
    StreamCell level = cellAt("persistent", 5000, 5000);
    level.always = true;
    w.addCell(level);
    EXPECT_EQ(step(w).size(), 1u) << "no observers at all";
    EXPECT_TRUE(w.isLoaded("persistent"));
    w.setObservers({ { 0, 0, 0 } });
    step(w);
    w.setObservers({ { 99999, 0, 99999 } });
    step(w);
    EXPECT_TRUE(w.isLoaded("persistent"));
}

TEST(WorldStream, ACapOnResidentCellsUnloadsTheFarthestForANearerOne) {
    WorldStreamer w;
    StreamConfig c;
    c.loadRadius = 100.0f;
    c.unloadRadius = 400.0f; // far enough that only the cap decides
    c.maxResident = 2;
    c.maxLoadsPerUpdate = 4;
    c.maxInFlight = 4;
    w.setConfig(c);
    w.addCell(cellAt("a", 10, 0));
    w.addCell(cellAt("b", 70, 0));
    w.setObservers({ { 0, 0, 0 } });
    step(w);
    EXPECT_TRUE(w.isLoaded("a"));
    EXPECT_TRUE(w.isLoaded("b"));
    w.addCell(cellAt("c", -25, 0)); // nearer than b
    const auto actions = step(w);
    ASSERT_EQ(actions.size(), 2u);
    EXPECT_EQ(actions[0].kind, StreamAction::Kind::Unload) << "the room is made first";
    EXPECT_EQ(actions[0].id, "b");
    EXPECT_EQ(actions[1].kind, StreamAction::Kind::Load);
    EXPECT_EQ(actions[1].id, "c");
    EXPECT_LE(w.resident().size(), 2u);
    // A cell that is farther than everything resident does not push anything out.
    w.addCell(cellAt("d", 90, 0));
    EXPECT_TRUE(step(w).empty());
    EXPECT_FALSE(w.isLoaded("d"));
}

TEST(WorldStream, PriorityLoadsFirstAndIsTheLastToGoForRoom) {
    WorldStreamer w;
    StreamConfig c;
    c.loadRadius = 200.0f;
    c.maxResident = 1;
    w.setConfig(c);
    StreamCell boss = cellAt("boss", 150, 0);
    boss.priority = 5;
    w.addCell(cellAt("closer", 10, 0));
    w.addCell(boss);
    w.setObservers({ { 0, 0, 0 } });
    EXPECT_EQ(step(w)[0].id, "boss") << "priority beats distance";
    EXPECT_TRUE(step(w).empty()) << "and the cap of one is held by the more important cell";
    EXPECT_TRUE(w.isLoaded("boss"));
    EXPECT_FALSE(w.isLoaded("closer"));
}

TEST(WorldStream, AFailedLoadIsRetriedAfterTheDelay) {
    WorldStreamer w;
    StreamConfig c;
    c.retryAfter = 2.0f;
    w.setConfig(c);
    w.addCell(cellAt("broken", 5, 0));
    w.setObservers({ { 0, 0, 0 } });
    ASSERT_EQ(w.update(0.1f).size(), 1u);
    w.failed("broken");
    EXPECT_EQ(w.state("broken"), CellState::Failed);
    EXPECT_TRUE(w.update(1.0f).empty());
    EXPECT_TRUE(w.update(0.5f).empty());
    const auto again = w.update(1.0f);
    ASSERT_EQ(again.size(), 1u) << "tried again after 2.5 s";
    EXPECT_EQ(w.stats().failures, 1u);
    w.loaded("broken");
    EXPECT_TRUE(w.isLoaded("broken"));
}

TEST(WorldStream, SeveralObserversKeepEveryoneSurroundingsLoaded) {
    WorldStreamer w;
    StreamConfig c;
    c.loadRadius = 40.0f;
    c.unloadRadius = 60.0f;
    c.maxLoadsPerUpdate = 8;
    c.maxInFlight = 8;
    w.setConfig(c);
    w.addCell(cellAt("west", -500, 0));
    w.addCell(cellAt("east", 500, 0));
    w.setObservers({ { -490, 0, 5 }, { 505, 0, 5 } }); // two players, split screen or online
    step(w);
    EXPECT_TRUE(w.isLoaded("west"));
    EXPECT_TRUE(w.isLoaded("east"));
    w.setObservers({ { 505, 0, 5 } }); // the first leaves
    step(w);
    EXPECT_FALSE(w.isLoaded("west"));
    EXPECT_TRUE(w.isLoaded("east"));
}

TEST(WorldStream, RemovingACellSaysWhetherItWasResidentAndALevelVolumeIsABox) {
    WorldStreamer w;
    StreamConfig c;
    c.loadRadius = 0.0f; // a level volume: loaded while the player is inside it
    c.unloadRadius = 5.0f;
    w.setConfig(c);
    StreamCell vol;
    vol.id = "cellar";
    vol.box = { { 0, -10, 0 }, { 20, 0, 20 } };
    w.addCell(vol);
    w.setObservers({ { 10, 5, 10 } }); // above it
    EXPECT_TRUE(step(w).empty()) << "5 m above the volume: outside";
    w.setObservers({ { 10, -2, 10 } });
    ASSERT_EQ(step(w).size(), 1u);
    EXPECT_TRUE(w.isLoaded("cellar"));
    EXPECT_TRUE(w.removeCell("cellar"));
    EXPECT_FALSE(w.removeCell("cellar"));
    EXPECT_EQ(w.state("cellar"), CellState::Unknown);
}
