#include "kke/PlayScript.h"

#if KKE_ENABLE_LUA
#include "kke/ScriptVM.h"

#include <lauxlib.h>
#include <lua.h>
#endif

#include <cmath>
#include <utility>

namespace kke {

const std::vector<std::string>& playSoundNames() {
    static const std::vector<std::string> names{ "bonk", "wood", "stone", "metal", "glass", "rubber", "dirt", "plastic" };
    return names;
}

namespace {

ApiParam param(std::string name, std::string type, std::string label, std::string fallback = {}) {
    return ApiParam{ std::move(name), std::move(type), std::move(label), std::move(fallback) };
}

ApiFunction fn(std::string name, std::string label, std::string category, std::string doc, std::vector<ApiParam> params,
               ApiParam result = {}, bool pure = false) {
    ApiFunction f;
    f.table = "play";
    f.name = std::move(name);
    f.label = std::move(label);
    f.category = std::move(category);
    f.doc = std::move(doc);
    f.params = std::move(params);
    f.result = std::move(result);
    f.pure = pure;
    return f;
}

} // namespace

std::vector<ApiFunction> playApiFunctions() {
    using namespace api_type;
    return {
        fn("spawn", "Bring out", "Things", "Puts a new thing in the world (it goes again when this graph or script is changed)",
           { param("block", Block, "What", "\"person\""), param("pos", Vec, "Where", "Vec(0, 0, 0)"), param("yaw", Number, "Turn", "0") },
           param("thing", Thing, "New thing")),
        fn("remove", "Take away", "Things", "Removes a thing from the world", { param("thing", Thing, "What") }),
        fn("ragdoll", "Knock over", "People", "Makes a person fall over, pushed this way (metres per second)",
           { param("thing", Thing, "Who"), param("push", Vec, "Push", "Vec(0, 1, 0)") }),
        fn("standUp", "Stand up", "People", "Stands a person up again", { param("thing", Thing, "Who") }),
        fn("swing", "Swing the bat at", "People", "The bat swings through someone (they get a Hit)", { param("thing", Thing, "Who") }),
        fn("sound", "Play sound", "Sound", "Plays a sound, at a place if given",
           { param("name", Sound, "Sound", "\"bonk\""), param("pos", Vec, "Where", "nil") }),
        fn("say", "Say", "Show", "Shows words on the screen", { param("text", Text, "Words", "\"Hello!\"") }),
        fn("addScore", "Add points", "Score", "Adds to the score (a minus number takes away)", { param("points", Number, "Points", "1") },
           param("score", Number, "Score")),
        fn("score", "Score", "Score", "The score now", {}, param("score", Number, "Score"), true),
        fn("position", "Where is", "Things", "Where a thing is (the bottom middle)", { param("thing", Thing, "What") },
           param("pos", Vec, "Where"), true),
        fn("blockOf", "What is", "Things", "Which block a thing is: \"person\", \"crate\", ...", { param("thing", Thing, "What") },
           param("block", Block, "Block"), true),
        fn("isDown", "Is down", "People", "Whether a person has fallen over", { param("thing", Thing, "Who") },
           param("down", Bool, "Down"), true),
        // Lists aren't something a node can hold, so no label: Lua only.
        fn("blocks", "", "", "Every block in the palette: { {id=, label=, kind=}, ... }", {}),
    };
}

std::vector<ApiEvent> playApiEvents() {
    using namespace api_type;
    return {
        { "Hit", "When someone is hit", "The bat (or Swing the bat at) hit someone",
          { param("target", Thing, "Who"), param("by", Text, "By"), param("point", Vec, "Where"), param("push", Vec, "Push"),
            param("block", Block, "What") },
          "target" },
        { "Clicked", "When tapped", "Someone tapped or clicked a thing",
          { param("thing", Thing, "What"), param("point", Vec, "Where"), param("block", Block, "Block") }, "thing" },
        { "Placed", "When put down", "A thing was put in the world",
          { param("thing", Thing, "What"), param("point", Vec, "Where"), param("block", Block, "Block") }, "thing" },
        { "FellOver", "When someone falls over", "A person fell over, whatever knocked them",
          { param("thing", Thing, "Who"), param("block", Block, "Block") }, "thing" },
        { "StoodUp", "When someone gets up", "A person stood up again",
          { param("thing", Thing, "Who"), param("block", Block, "Block") }, "thing" },
    };
}

#if KKE_ENABLE_LUA

namespace {

uint32_t thingArg(lua_State* L, int index) {
    const lua_Integer id = luaL_checkinteger(L, index);
    return id > 0 && id <= lua_Integer(UINT32_MAX) ? uint32_t(id) : 0;
}

void pushThing(lua_State* L, uint32_t thing) {
    if (thing) lua_pushinteger(L, lua_Integer(thing));
    else lua_pushnil(L);
}

} // namespace

void bindPlayBlocks(ScriptVM& vm, IPlayWorld& world) {
    ScriptVM* v = &vm;
    IPlayWorld* w = &world;
    for (const ApiFunction& api : playApiFunctions()) {
        ScriptVM::Fn f;
        const std::string& n = api.name;
        if (n == "spawn") {
            f = [v, w](lua_State* L) {
                const std::string block = luaL_checkstring(L, 1);
                const glm::vec3 pos = ScriptVM::toVec3(L, 2);
                const float yaw = float(luaL_optnumber(L, 3, 0.0));
                if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z) || !std::isfinite(yaw))
                    return luaL_error(L, "play.spawn: the position must be real numbers");
                pushThing(L, w->spawn(block, pos, yaw, v->currentSource()));
                return 1;
            };
        } else if (n == "remove") {
            f = [w](lua_State* L) { lua_pushboolean(L, w->remove(thingArg(L, 1))); return 1; };
        } else if (n == "ragdoll") {
            f = [w](lua_State* L) {
                glm::vec3 push = lua_isnoneornil(L, 2) ? glm::vec3(0.0f, 1.0f, 0.0f) : ScriptVM::toVec3(L, 2);
                // The bat caps its push at 9 m/s; scripts get a little more room.
                const float len = glm::length(push);
                if (!std::isfinite(len)) push = glm::vec3(0.0f);
                else if (len > 20.0f) push *= 20.0f / len;
                lua_pushboolean(L, w->ragdoll(thingArg(L, 1), push));
                return 1;
            };
        } else if (n == "standUp") {
            f = [w](lua_State* L) { lua_pushboolean(L, w->standUp(thingArg(L, 1))); return 1; };
        } else if (n == "swing") {
            f = [w](lua_State* L) { lua_pushboolean(L, w->swingAt(thingArg(L, 1))); return 1; };
        } else if (n == "sound") {
            f = [w](lua_State* L) {
                const std::string name = luaL_optstring(L, 1, "bonk");
                const glm::vec3 pos = lua_isnoneornil(L, 2) ? glm::vec3(NAN) : ScriptVM::toVec3(L, 2);
                w->sound(name, pos);
                return 0;
            };
        } else if (n == "say") {
            f = [w](lua_State* L) {
                // Any value: say(3) or say(true) should just work for a child.
                w->say(luaL_tolstring(L, 1, nullptr));
                return 0;
            };
        } else if (n == "addScore") {
            f = [w](lua_State* L) {
                const double p = luaL_optnumber(L, 1, 1.0);
                lua_pushnumber(L, w->addScore(std::isfinite(p) ? p : 0.0));
                return 1;
            };
        } else if (n == "score") {
            f = [w](lua_State* L) { lua_pushnumber(L, w->score()); return 1; };
        } else if (n == "position") {
            f = [w](lua_State* L) {
                const uint32_t t = thingArg(L, 1);
                if (!w->exists(t)) { lua_pushnil(L); return 1; }
                ScriptVM::pushVec3(L, w->position(t));
                return 1;
            };
        } else if (n == "blockOf") {
            f = [w](lua_State* L) {
                const std::string b = w->blockOf(thingArg(L, 1));
                if (b.empty()) lua_pushnil(L);
                else lua_pushstring(L, b.c_str());
                return 1;
            };
        } else if (n == "isDown") {
            f = [w](lua_State* L) { lua_pushboolean(L, w->isDown(thingArg(L, 1))); return 1; };
        } else if (n == "blocks") {
            f = [w](lua_State* L) {
                const std::vector<IPlayWorld::BlockInfo> list = w->blocks();
                lua_createtable(L, int(list.size()), 0);
                for (size_t i = 0; i < list.size(); ++i) {
                    lua_createtable(L, 0, 3);
                    lua_pushstring(L, list[i].id.c_str());
                    lua_setfield(L, -2, "id");
                    lua_pushstring(L, list[i].label.c_str());
                    lua_setfield(L, -2, "label");
                    lua_pushstring(L, list[i].kind.c_str());
                    lua_setfield(L, -2, "kind");
                    lua_rawseti(L, -2, lua_Integer(i + 1));
                }
                return 1;
            };
        }
        if (f) vm.registerFunction(api, std::move(f));
    }
    for (const ApiEvent& e : playApiEvents()) vm.describeEvent(e);
    // What a script spawned goes with it (reload, unload, hard stop).
    auto previous = vm.onUnload;
    vm.onUnload = [w, previous](const std::string& source) {
        if (previous) previous(source);
        w->removeOwnedBy(source);
    };
}

namespace {

void setThing(lua_State* L, const char* key, uint32_t thing) {
    lua_pushinteger(L, lua_Integer(thing));
    lua_setfield(L, -2, key);
}
void setString(lua_State* L, const char* key, const std::string& s) {
    lua_pushstring(L, s.c_str());
    lua_setfield(L, -2, key);
}
void setVec(lua_State* L, const char* key, const glm::vec3& v) {
    ScriptVM::pushVec3(L, v);
    lua_setfield(L, -2, key);
}

} // namespace

void firePlayHit(ScriptVM& vm, const PlayHit& hit) {
    vm.callHookWith("Hit", [&](lua_State* L) {
        lua_createtable(L, 0, 5);
        setThing(L, "target", hit.target);
        setString(L, "by", hit.by);
        setVec(L, "point", hit.point);
        setVec(L, "push", hit.push);
        setString(L, "block", hit.block);
        return 1;
    });
}

void firePlayClicked(ScriptVM& vm, uint32_t thing, const glm::vec3& point, const std::string& block) {
    vm.callHookWith("Clicked", [&](lua_State* L) {
        lua_createtable(L, 0, 3);
        setThing(L, "thing", thing);
        setVec(L, "point", point);
        setString(L, "block", block);
        return 1;
    });
}

void firePlayPlaced(ScriptVM& vm, uint32_t thing, const glm::vec3& point, const std::string& block) {
    vm.callHookWith("Placed", [&](lua_State* L) {
        lua_createtable(L, 0, 3);
        setThing(L, "thing", thing);
        setVec(L, "point", point);
        setString(L, "block", block);
        return 1;
    });
}

void firePlayFellOver(ScriptVM& vm, uint32_t thing, const std::string& block) {
    vm.callHookWith("FellOver", [&](lua_State* L) {
        lua_createtable(L, 0, 2);
        setThing(L, "thing", thing);
        setString(L, "block", block);
        return 1;
    });
}

void firePlayStoodUp(ScriptVM& vm, uint32_t thing, const std::string& block) {
    vm.callHookWith("StoodUp", [&](lua_State* L) {
        lua_createtable(L, 0, 2);
        setThing(L, "thing", thing);
        setString(L, "block", block);
        return 1;
    });
}

#endif

} // namespace kke
