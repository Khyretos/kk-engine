// The node graph (Intermediate play-to-make, kke/NodeGraph.h): the library
// comes from the documented Lua bindings, graphs compile to Lua that runs
// in the same ScriptVM as scripts, and errors map back to nodes.

#include "kke/NodeGraph.h"
#include "kke/PlayScript.h"
#include "kke/SceneFile.h"
#if KKE_ENABLE_LUA
#include "kke/ScriptVM.h"

#include <lua.h>
#endif

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>

using namespace kke;

namespace {

CompileOptions opts(std::string source, uint32_t owner = 0, std::string block = {}, bool tool = false) {
    CompileOptions o;
    o.source = std::move(source);
    o.owner = owner;
    o.block = std::move(block);
    o.toolBlock = tool;
    return o;
}

NodeLibrary playLibrary() { return NodeLibrary::fromApi(playApiFunctions(), playApiEvents()); }

bool hasIssue(const CompiledGraph& c, int node, bool error) {
    return std::any_of(c.issues.begin(), c.issues.end(), [&](const GraphIssue& i) { return i.node == node && i.error == error; });
}

} // namespace

TEST(NodeGraph, LibraryComesFromTheDocumentedBindings) {
    const NodeLibrary lib = playLibrary();
    for (const ApiFunction& f : playApiFunctions()) {
        const NodeDef* d = lib.find("call:" + f.qualifiedName());
        if (f.label.empty()) {
            EXPECT_EQ(d, nullptr) << f.qualifiedName() << " has no label, so no node";
            continue;
        }
        ASSERT_NE(d, nullptr) << f.qualifiedName();
        EXPECT_EQ(d->title, f.label);
        EXPECT_EQ(d->kind, f.pure ? NodeDef::Kind::Value : NodeDef::Kind::Action);
        for (const ApiParam& p : f.params) EXPECT_NE(d->input(p.name), nullptr) << f.qualifiedName() << " " << p.name;
    }
    for (const ApiEvent& e : playApiEvents()) {
        const NodeDef* d = lib.find("event:" + e.name);
        ASSERT_NE(d, nullptr) << e.name;
        EXPECT_EQ(d->kind, NodeDef::Kind::Event);
        for (const ApiParam& f : e.fields) EXPECT_NE(d->output(f.name), nullptr) << e.name << " " << f.name;
    }
    // Built-ins and sections, "When" first.
    for (const char* t : { "flow:start", "flow:every", "flow:wait", "flow:if", "flow:repeat", "value:random", "value:me" })
        EXPECT_NE(lib.find(t), nullptr) << t;
    ASSERT_FALSE(lib.categories().empty());
    EXPECT_EQ(lib.categories().front(), "When");
}

TEST(NodeGraph, PinTypesDecideWhatConnects) {
    EXPECT_TRUE(NodeLibrary::compatible("thing", "thing"));
    EXPECT_TRUE(NodeLibrary::compatible("any", "vec"));
    EXPECT_TRUE(NodeLibrary::compatible("block", "text"));
    EXPECT_TRUE(NodeLibrary::compatible("number", "text")); // anything can be said
    EXPECT_FALSE(NodeLibrary::compatible("vec", "thing"));
    EXPECT_FALSE(NodeLibrary::compatible("flow", "thing"));
    EXPECT_FALSE(NodeLibrary::compatible("number", "flow"));
}

TEST(NodeGraph, ConnectReplacesAndRefuses) {
    const NodeLibrary lib = playLibrary();
    NodeGraph g;
    const int hit = g.add("event:Hit", { 0, 0 });
    const int a = g.add("call:play.ragdoll", { 300, 0 });
    const int b = g.add("call:play.say", { 300, 200 });
    EXPECT_TRUE(g.connect(lib, { hit, ">", a, ">" }));
    EXPECT_TRUE(g.connect(lib, { hit, ">", b, ">" })); // a flow output leads one way: replaces
    ASSERT_EQ(g.links.size(), 1u);
    EXPECT_EQ(g.links[0].toNode, b);
    EXPECT_TRUE(g.connect(lib, { hit, "target", a, "thing" }));
    EXPECT_TRUE(g.connect(lib, { hit, "block", b, "text" }));
    EXPECT_TRUE(g.connect(lib, { hit, "target", b, "text" })); // an input takes one: replaces
    EXPECT_EQ(g.linkInto(b, "text")->fromPin, "target");
    EXPECT_FALSE(g.connect(lib, { hit, "point", a, "thing" })); // a place isn't a thing
    EXPECT_FALSE(g.connect(lib, { a, ">", a, ">" }));           // not to itself
    EXPECT_FALSE(g.connect(lib, { hit, "nope", a, "thing" }));
    g.remove(hit);
    EXPECT_TRUE(g.links.empty());
}

TEST(NodeGraph, JsonRoundTrips) {
    NodeGraph g = playBlockRecipe("bat");
    g.find(g.nodes[0].id)->pos = { 12.0f, -40.0f };
    const NodeGraph back = NodeGraph::fromJson(g.toJson());
    EXPECT_EQ(back.name, "Bat");
    ASSERT_EQ(back.nodes.size(), g.nodes.size());
    for (size_t i = 0; i < g.nodes.size(); ++i) {
        EXPECT_EQ(back.nodes[i].id, g.nodes[i].id);
        EXPECT_EQ(back.nodes[i].type, g.nodes[i].type);
        EXPECT_EQ(back.nodes[i].pos, g.nodes[i].pos);
        EXPECT_EQ(back.nodes[i].values, g.nodes[i].values);
    }
    EXPECT_EQ(back.links, g.links);
    EXPECT_EQ(back.nextId, g.nextId);
    EXPECT_EQ(back.toJson(), g.toJson());
    EXPECT_THROW(NodeGraph::fromJson("{\"nodes\": [{\"id\": 1, \"type\": \"x\"}, {\"id\": 1, \"type\": \"y\"}]}"), std::runtime_error);
    EXPECT_THROW(NodeGraph::fromJson("{\"nodes\": [], \"links\": [{\"from\": [1, \">\"], \"to\": [2, \">\"]}]}"), std::runtime_error);
    EXPECT_THROW(NodeGraph::fromJson("not json"), std::runtime_error);
    // Unknown node types survive a load (and are reported when compiled).
    const NodeGraph future = NodeGraph::fromJson("{\"nodes\": [{\"id\": 4, \"type\": \"call:play.teleport\"}]}");
    ASSERT_EQ(future.nodes.size(), 1u);
    EXPECT_TRUE(hasIssue(compileGraph(future, playLibrary(), opts("g")), 4, true));
}

TEST(NodeGraph, TypedValuesBecomeLua) {
    std::string lua;
    ASSERT_TRUE(valueToLua("number", " 2.5 ", lua));
    EXPECT_EQ(lua, "2.5");
    EXPECT_FALSE(valueToLua("number", "two", lua));
    EXPECT_FALSE(valueToLua("number", "inf", lua));
    ASSERT_TRUE(valueToLua("vec", "1, 2 3", lua));
    EXPECT_EQ(lua, "Vec(1, 2, 3)");
    EXPECT_FALSE(valueToLua("vec", "1 2", lua));
    ASSERT_TRUE(valueToLua("text", "say \"hi\"\n", lua));
    EXPECT_EQ(lua, "\"say \\\"hi\\\"\\n\"");
    ASSERT_TRUE(valueToLua("bool", "no", lua));
    EXPECT_EQ(lua, "false");
    EXPECT_FALSE(valueToLua("thing", "3", lua));
}

TEST(NodeGraph, BatRecipeIsTheLuaItStandsFor) {
    const CompiledGraph c = compileGraph(playBlockRecipe("bat"), playLibrary(), opts("graph:bat", 0, "bat", true));
    EXPECT_TRUE(c.ok()) << c.lua;
    EXPECT_NE(c.lua.find("hook.Add(\"Hit\", \"graph:bat#1\", function(e1)"), std::string::npos) << c.lua;
    EXPECT_NE(c.lua.find("if e1.by ~= \"bat\" then return end"), std::string::npos) << c.lua;
    EXPECT_NE(c.lua.find("play.ragdoll(e1.target, e1.push)"), std::string::npos) << c.lua;
    EXPECT_NE(c.lua.find("play.sound(\"wood\", e1.point)"), std::string::npos) << c.lua;
    EXPECT_EQ(std::count(c.lua.begin(), c.lua.end(), '\n'), std::ptrdiff_t(c.lineNode.size()));
    EXPECT_TRUE(playBlockRecipe("person").empty());
}

TEST(NodeGraph, ProblemsAreReportedOnTheirNode) {
    const NodeLibrary lib = playLibrary();
    NodeGraph g;
    const int start = g.add("flow:start", { 0, 0 });
    const int knock = g.add("call:play.ragdoll", { 300, 0 }); // who? nothing connected, no "me" in a level graph
    const int lonely = g.add("call:play.say", { 300, 300 });  // never reached
    const int hit = g.add("event:Hit", { 0, 500 });
    const int say = g.add("call:play.say", { 300, 500 });
    const int spawn = g.add("call:play.spawn", { 600, 0 });
    const int badNumber = g.add("call:play.addScore", { 600, 500 });
    ASSERT_TRUE(g.connect(lib, { start, ">", knock, ">" }));
    ASSERT_TRUE(g.connect(lib, { hit, ">", say, ">" }));
    ASSERT_TRUE(g.connect(lib, { knock, ">", spawn, ">" }));
    ASSERT_TRUE(g.connect(lib, { hit, "block", spawn, "block" })); // from another "When"
    ASSERT_TRUE(g.connect(lib, { say, ">", badNumber, ">" }));
    g.find(badNumber)->values["points"] = "lots";
    const CompiledGraph c = compileGraph(g, lib, opts("level"));
    EXPECT_FALSE(c.ok());
    EXPECT_TRUE(hasIssue(c, knock, true));
    EXPECT_TRUE(hasIssue(c, lonely, false));
    EXPECT_FALSE(hasIssue(c, lonely, true));
    EXPECT_TRUE(hasIssue(c, spawn, true));
    EXPECT_TRUE(hasIssue(c, badNumber, true));
    EXPECT_FALSE(hasIssue(c, say, true));

    // A thing's own graph: an unconnected "Who" is the thing itself.
    NodeGraph mine;
    const int s = mine.add("flow:start", { 0, 0 });
    const int k = mine.add("call:play.ragdoll", { 300, 0 });
    ASSERT_TRUE(mine.connect(lib, { s, ">", k, ">" }));
    const CompiledGraph m = compileGraph(mine, lib, opts("thing", 7));
    EXPECT_TRUE(m.ok()) << m.lua;
    EXPECT_NE(m.lua.find("local me = 7"), std::string::npos);
    EXPECT_NE(m.lua.find("play.ragdoll(me, Vec(0, 1, 0))"), std::string::npos) << m.lua;
}

TEST(NodeGraph, CirclesAreCaught) {
    const NodeLibrary lib = playLibrary();
    NodeGraph g;
    const int start = g.add("flow:start", { 0, 0 });
    const int a = g.add("call:play.say", { 300, 0 });
    const int b = g.add("call:play.addScore", { 600, 0 });
    ASSERT_TRUE(g.connect(lib, { start, ">", a, ">" }));
    ASSERT_TRUE(g.connect(lib, { a, ">", b, ">" }));
    ASSERT_TRUE(g.connect(lib, { b, ">", a, ">" }));
    const CompiledGraph c = compileGraph(g, lib, opts("loop"));
    EXPECT_FALSE(c.ok());
    EXPECT_TRUE(hasIssue(c, a, true));
    // Values in a circle: Add fed by itself through another Add.
    NodeGraph v;
    const int s = v.add("flow:start", { 0, 0 });
    const int say = v.add("call:play.say", { 300, 0 });
    const int x = v.add("value:add", { 0, 200 });
    const int y = v.add("value:add", { 0, 400 });
    ASSERT_TRUE(v.connect(lib, { s, ">", say, ">" }));
    ASSERT_TRUE(v.connect(lib, { x, "sum", say, "text" }));
    ASSERT_TRUE(v.connect(lib, { y, "sum", x, "a" }));
    ASSERT_TRUE(v.connect(lib, { x, "sum", y, "a" }));
    EXPECT_FALSE(compileGraph(v, lib, opts("values")).ok());
}

#if KKE_ENABLE_LUA

namespace {

// The world the play blocks act on, as a list.
class FakeWorld : public IPlayWorld {
public:
    struct Thing { std::string block, owner; glm::vec3 pos{0.0f}; bool down = false; };
    std::map<uint32_t, Thing> things;
    uint32_t next = 1;
    std::vector<std::pair<uint32_t, glm::vec3>> ragdolls;
    std::vector<std::string> sounds, said, removedOwners;
    double points = 0.0;

    uint32_t add(const std::string& block, glm::vec3 pos = glm::vec3(0.0f)) {
        things[next] = { block, "", pos, false };
        return next++;
    }

    std::vector<BlockInfo> blocks() const override { return { { "person", "Person", "character" }, { "crate", "Box", "prop" } }; }
    uint32_t spawn(const std::string& block, const glm::vec3& p, float, const std::string& owner) override {
        if (block != "person" && block != "crate") return 0;
        things[next] = { block, owner, p, false };
        return next++;
    }
    bool remove(uint32_t t) override { return things.erase(t) > 0; }
    bool exists(uint32_t t) const override { return things.count(t) > 0; }
    bool ragdoll(uint32_t t, const glm::vec3& push) override {
        auto it = things.find(t);
        if (it == things.end() || it->second.block != "person" || it->second.down) return false;
        it->second.down = true;
        ragdolls.emplace_back(t, push);
        return true;
    }
    bool standUp(uint32_t t) override {
        auto it = things.find(t);
        if (it == things.end() || !it->second.down) return false;
        it->second.down = false;
        return true;
    }
    bool isDown(uint32_t t) const override { return things.count(t) && things.at(t).down; }
    bool swingAt(uint32_t t) override { return exists(t); }
    void sound(const std::string& name, const glm::vec3&) override { sounds.push_back(name); }
    void say(const std::string& text) override { said.push_back(text); }
    double addScore(double p) override { return points += p; }
    double score() const override { return points; }
    glm::vec3 position(uint32_t t) const override { return things.count(t) ? things.at(t).pos : glm::vec3(0.0f); }
    std::string blockOf(uint32_t t) const override { return things.count(t) ? things.at(t).block : std::string(); }
    void removeOwnedBy(const std::string& owner) override {
        removedOwners.push_back(owner);
        std::erase_if(things, [&](const auto& kv) { return kv.second.owner == owner; });
    }
};

struct Loaded {
    CompiledGraph compiled;
    bool ran = false;
};

Loaded load(ScriptVM& vm, const NodeGraph& g, const CompileOptions& opt) {
    Loaded l;
    l.compiled = compileGraph(g, NodeLibrary::fromApi(vm.apiFunctions(), vm.apiEvents()), opt);
    l.ran = vm.reloadString(l.compiled.lua, opt.source);
    return l;
}

} // namespace

TEST(NodeGraphLua, EveryPlayFunctionIsDocumented) {
    ScriptVM vm;
    FakeWorld world;
    bindPlayBlocks(vm, world);
    // Walk the real play table: nothing bound without a description.
    lua_State* L = vm.state();
    lua_getglobal(L, "play");
    ASSERT_TRUE(lua_istable(L, -1));
    int count = 0;
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        const std::string name = lua_tostring(L, -2);
        EXPECT_TRUE(std::any_of(vm.apiFunctions().begin(), vm.apiFunctions().end(),
                                [&](const ApiFunction& f) { return f.table == "play" && f.name == name; }))
            << "play." << name << " is bound but not documented";
        ++count;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    EXPECT_EQ(size_t(count), playApiFunctions().size());
    EXPECT_EQ(vm.apiEvents().size(), playApiEvents().size());
}

TEST(NodeGraphLua, ShoveAndLookAtWorkWithAnyWorld) {
    // A world without joint motors or look-at (the defaults): a shove
    // knocks them over, looking does nothing, and neither is an error.
    ScriptVM vm;
    FakeWorld world;
    bindPlayBlocks(vm, world);
    const uint32_t person = world.add("person");
    ASSERT_TRUE(vm.reloadString("play.say(tostring(play.stagger(1, Vec(2, 0, 0))))\n"
                                "play.say(tostring(play.lookAt(1)))\n"
                                "play.say(tostring(play.lookAt(1, Vec(0, 1, 5))))\n"
                                "play.say(tostring(play.lookAway(1)))\n",
                                "test"))
        << vm.errors().back().message;
    EXPECT_EQ(world.said, (std::vector<std::string>{ "true", "false", "false", "false" }));
    ASSERT_EQ(world.ragdolls.size(), 1u);
    EXPECT_EQ(world.ragdolls[0].first, person);
    EXPECT_EQ(world.ragdolls[0].second, glm::vec3(2, 0, 0));
    // They are nodes too.
    const NodeLibrary lib = NodeLibrary::fromApi(vm.apiFunctions(), vm.apiEvents());
    for (const char* node : { "call:play.stagger", "call:play.lookAt", "call:play.lookAway" })
        EXPECT_NE(lib.find(node), nullptr) << node;
}

TEST(NodeGraphLua, BatRecipeKnocksOverWhoTheBatHits) {
    ScriptVM vm;
    FakeWorld world;
    bindPlayBlocks(vm, world);
    const uint32_t person = world.add("person");
    const Loaded l = load(vm, playBlockRecipe("bat"), opts("graph:bat", 0, "bat", true));
    ASSERT_TRUE(l.ran) << (vm.errors().empty() ? l.compiled.lua : vm.errors().back().message);
    firePlayHit(vm, { person, "ball", glm::vec3(0.0f), glm::vec3(1.0f), "person" }); // not the bat: not its recipe
    EXPECT_TRUE(world.ragdolls.empty());
    firePlayHit(vm, { person, "bat", glm::vec3(1, 1, 1), glm::vec3(4, 1, 0), "person" });
    ASSERT_EQ(world.ragdolls.size(), 1u);
    EXPECT_EQ(world.ragdolls[0].first, person);
    EXPECT_EQ(world.ragdolls[0].second, glm::vec3(4, 1, 0));
    ASSERT_EQ(world.sounds.size(), 1u);
    EXPECT_EQ(world.sounds[0], "wood");
    EXPECT_TRUE(vm.errors().empty());
}

TEST(NodeGraphLua, ThingGraphOnlyHearsItsOwnEvents) {
    ScriptVM vm;
    FakeWorld world;
    bindPlayBlocks(vm, world);
    const NodeLibrary lib = NodeLibrary::fromApi(vm.apiFunctions(), vm.apiEvents());
    const uint32_t me = world.add("person"), other = world.add("person");
    NodeGraph g;
    const int tap = g.add("event:Clicked", { 0, 0 });
    const int knock = g.add("call:play.ragdoll", { 300, 0 });
    const int points = g.add("call:play.addScore", { 600, 0 });
    const int say = g.add("call:play.say", { 900, 0 });
    ASSERT_TRUE(g.connect(lib, { tap, ">", knock, ">" }));
    ASSERT_TRUE(g.connect(lib, { knock, ">", points, ">" }));
    ASSERT_TRUE(g.connect(lib, { points, ">", say, ">" }));
    ASSERT_TRUE(g.connect(lib, { points, "score", say, "text" })); // an action's result, later in its chain
    g.find(points)->values["points"] = "10";
    const Loaded l = load(vm, g, opts("graph:thing", me));
    ASSERT_TRUE(l.compiled.ok()) << l.compiled.lua;
    ASSERT_TRUE(l.ran);
    firePlayClicked(vm, other, glm::vec3(0.0f), "person");
    EXPECT_TRUE(world.ragdolls.empty());
    firePlayClicked(vm, me, glm::vec3(0.0f), "person");
    ASSERT_EQ(world.ragdolls.size(), 1u);
    EXPECT_EQ(world.ragdolls[0].first, me);
    EXPECT_EQ(world.points, 10.0);
    ASSERT_EQ(world.said.size(), 1u);
    EXPECT_EQ(world.said[0], "10.0"); // Lua's own tostring of a float score
}

TEST(NodeGraphLua, BlockRecipeAppliesToEveryThingOfThatBlock) {
    ScriptVM vm;
    FakeWorld world;
    bindPlayBlocks(vm, world);
    const NodeLibrary lib = NodeLibrary::fromApi(vm.apiFunctions(), vm.apiEvents());
    const uint32_t a = world.add("person"), b = world.add("person"), box = world.add("crate");
    NodeGraph g;
    const int tap = g.add("event:Clicked", { 0, 0 });
    const int knock = g.add("call:play.ragdoll", { 300, 0 }); // "Who" left empty: me, the one tapped
    ASSERT_TRUE(g.connect(lib, { tap, ">", knock, ">" }));
    ASSERT_TRUE(load(vm, g, opts("graph:person", 0, "person")).ran);
    firePlayClicked(vm, box, glm::vec3(0.0f), "crate");
    firePlayClicked(vm, b, glm::vec3(0.0f), "person");
    firePlayClicked(vm, a, glm::vec3(0.0f), "person");
    ASSERT_EQ(world.ragdolls.size(), 2u);
    EXPECT_EQ(world.ragdolls[0].first, b);
    EXPECT_EQ(world.ragdolls[1].first, a);
    EXPECT_TRUE(vm.errors().empty()) << vm.errors().front().message;
}

TEST(NodeGraphLua, TimersWaitsAndLoops) {
    ScriptVM vm;
    FakeWorld world;
    bindPlayBlocks(vm, world);
    const NodeLibrary lib = NodeLibrary::fromApi(vm.apiFunctions(), vm.apiEvents());
    NodeGraph g;
    // Start: bring out 3 people in a row, wait 1 s, say "go".
    const int start = g.add("flow:start", { 0, 0 });
    const int rep = g.add("flow:repeat", { 300, 0 });
    const int place = g.add("value:place", { 300, 200 });
    const int spawn = g.add("call:play.spawn", { 600, 0 });
    const int wait = g.add("flow:wait", { 600, 300 });
    const int say = g.add("call:play.say", { 900, 300 });
    // Every 2 s: a point.
    const int every = g.add("flow:every", { 0, 600 });
    const int point = g.add("call:play.addScore", { 300, 600 });
    ASSERT_TRUE(g.connect(lib, { start, ">", rep, ">" }));
    ASSERT_TRUE(g.connect(lib, { rep, "each", spawn, ">" }));
    ASSERT_TRUE(g.connect(lib, { rep, "count", place, "x" }));
    ASSERT_TRUE(g.connect(lib, { place, "pos", spawn, "pos" }));
    ASSERT_TRUE(g.connect(lib, { rep, "done", wait, ">" }));
    ASSERT_TRUE(g.connect(lib, { wait, ">", say, ">" }));
    ASSERT_TRUE(g.connect(lib, { every, ">", point, ">" }));
    g.find(say)->values["text"] = "go";
    const Loaded l = load(vm, g, opts("graph:level"));
    ASSERT_TRUE(l.compiled.ok()) << l.compiled.lua;
    ASSERT_TRUE(l.ran) << vm.errors().back().message << "\n" << l.compiled.lua;
    ASSERT_EQ(world.things.size(), 3u);
    EXPECT_EQ(world.things.rbegin()->second.pos, glm::vec3(3, 0, 0));
    EXPECT_TRUE(world.said.empty());
    vm.updateTimers(0.5);
    EXPECT_TRUE(world.said.empty());
    vm.updateTimers(1.1);
    ASSERT_EQ(world.said.size(), 1u);
    EXPECT_EQ(world.said[0], "go");
    vm.updateTimers(2.2);
    vm.updateTimers(4.3);
    EXPECT_EQ(world.points, 2.0);

    // Changing the graph (a reload) takes back what it brought out and
    // stops its timers; the new version starts fresh.
    g.remove(every);
    ASSERT_TRUE(load(vm, g, opts("graph:level")).ran);
    EXPECT_NE(std::find(world.removedOwners.begin(), world.removedOwners.end(), "graph:level"), world.removedOwners.end());
    EXPECT_EQ(world.things.size(), 3u);
    vm.updateTimers(10.0);
    EXPECT_EQ(world.points, 2.0);
}

TEST(NodeGraphLua, RuntimeErrorsPointAtTheirNode) {
    ScriptVM vm;
    FakeWorld world;
    bindPlayBlocks(vm, world);
    const NodeLibrary lib = NodeLibrary::fromApi(vm.apiFunctions(), vm.apiEvents());
    NodeGraph g;
    const int start = g.add("flow:start", { 0, 0 });
    const int say = g.add("call:play.say", { 300, 0 });
    const int knock = g.add("call:play.ragdoll", { 600, 0 });
    const int add = g.add("value:add", { 600, 200 });
    ASSERT_TRUE(g.connect(lib, { start, ">", say, ">" }));
    ASSERT_TRUE(g.connect(lib, { say, ">", knock, ">" }));
    ASSERT_TRUE(g.connect(lib, { add, "sum", knock, "push" }));
    g.find(add)->values["a"] = "hello"; // words + a number: a Lua error when it runs
    g.find(knock)->values["thing"] = "";
    NodeGraph mine = g; // on a thing, so "Who" is fine
    const Loaded l = load(vm, mine, opts("graph:thing:1", 1));
    ASSERT_TRUE(l.compiled.ok()) << l.compiled.lua;
    EXPECT_FALSE(l.ran);
    ASSERT_FALSE(vm.errors().empty());
    EXPECT_EQ(l.compiled.nodeForError(vm.errors().back().message, "graph:thing:1"), knock) << vm.errors().back().message;
    EXPECT_EQ(world.said.size(), 1u); // what ran before the error still happened
}

#endif

#if KKE_ENABLE_LUA
TEST(NodeGraphLua, TraceLightsUpWhatRanWithoutMovingLines) {
    ScriptVM vm;
    FakeWorld world;
    bindPlayBlocks(vm, world);
    std::vector<int> ran;
    vm.registerFunction("graph", "ran", [&](lua_State* L) {
        ran.push_back(int(lua_tointeger(L, 1)));
        return 0;
    });
    const uint32_t person = world.add("person");
    CompileOptions plain = opts("graph:bat", 0, "bat", true);
    CompileOptions traced = plain;
    traced.trace = "graph.ran";
    const NodeGraph bat = playBlockRecipe("bat");
    const NodeLibrary lib = NodeLibrary::fromApi(vm.apiFunctions(), vm.apiEvents());
    const CompiledGraph a = compileGraph(bat, lib, plain), b = compileGraph(bat, lib, traced);
    EXPECT_EQ(a.lineNode, b.lineNode);
    EXPECT_EQ(a.lua.find("graph.ran"), std::string::npos);
    ASSERT_TRUE(vm.reloadString(b.lua, "graph:bat")) << b.lua;
    firePlayHit(vm, { person, "ball", glm::vec3(0.0f), glm::vec3(0.0f), "person" });
    EXPECT_TRUE(ran.empty()); // filtered out: nothing lit
    firePlayHit(vm, { person, "bat", glm::vec3(0.0f), glm::vec3(1.0f), "person" });
    EXPECT_EQ(ran, (std::vector<int>{ 1, 2, 3 }));
}
#endif

TEST(NodeGraph, SavedWithTheScene) {
    SceneFile scene;
    SceneObject person;
    person.asset = "SM_Chr_Kid_01";
    person.graph.name = "Person";
    person.graph.add("event:Clicked", { 0, 0 });
    scene.objects.push_back(person);
    scene.objects.push_back(SceneObject{});
    scene.objects.back().asset = "SM_Prop_Crate_01";
    scene.graph.name = "Level";
    scene.graph.add("flow:start", { 10, 20 });
    scene.recipes["bat"] = playBlockRecipe("bat");
    const SceneFile back = SceneFile::parse(scene.toJson());
    ASSERT_EQ(back.objects.size(), 2u);
    EXPECT_EQ(back.objects[0].graph.toJson(), person.graph.toJson());
    EXPECT_TRUE(back.objects[1].graph.empty());
    EXPECT_EQ(back.graph.toJson(), scene.graph.toJson());
    ASSERT_EQ(back.recipes.count("bat"), 1u);
    EXPECT_EQ(back.recipes.at("bat").toJson(), scene.recipes.at("bat").toJson());
    EXPECT_EQ(back.toJson(), scene.toJson());
    // A scene without graphs doesn't grow the keys.
    SceneFile plain;
    plain.objects.push_back(scene.objects[1]);
    EXPECT_EQ(plain.toJson().find("graph"), std::string::npos);
}
