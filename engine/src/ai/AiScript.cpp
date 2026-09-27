#include "kke/ai/AiScript.h"

#if KKE_ENABLE_LUA
#include "kke/ScriptVM.h"

#include <lauxlib.h>
#include <lua.h>
#include <nlohmann/json.hpp>
#endif

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace kke::ai {

namespace {

ApiParam param(std::string name, std::string type, std::string label, std::string fallback = {}) {
    return ApiParam{ std::move(name), std::move(type), std::move(label), std::move(fallback) };
}

ApiFunction fn(std::string name, std::string label, std::string category, std::string doc, std::vector<ApiParam> params,
               ApiParam result = {}, bool pure = false) {
    ApiFunction f;
    f.table = "ai";
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

std::vector<ApiFunction> aiApiFunctions() {
    using namespace api_type;
    return {
        fn("add", "Bring to life as", "Minds", "Gives a thing a mind: it sees, hears, gets hungry and decides for itself",
           { param("thing", Thing, "Who"), param("species", Text, "As", "\"sheep\"") }, param("ok", Bool, "Worked")),
        fn("remove", "Stop thinking", "Minds", "Takes a thing's mind away again (it stands still)", { param("thing", Thing, "Who") }),
        fn("goTo", "Go to", "Orders", "Walks (or runs) somewhere and stays there",
           { param("thing", Thing, "Who"), param("pos", Vec, "Where", "Vec(0, 0, 0)"), param("run", Bool, "Run", "false") }),
        fn("follow", "Follow", "Orders", "Stays close to someone wherever they go",
           { param("thing", Thing, "Who"), param("leader", Thing, "Whom"), param("distance", Number, "How close", "2.5") }),
        fn("attack", "Attack", "Orders", "Goes for someone until told otherwise",
           { param("thing", Thing, "Who"), param("target", Thing, "Whom") }),
        fn("hold", "Stay", "Orders", "Stays put (here, or at a place) and faces trouble",
           { param("thing", Thing, "Who"), param("pos", Vec, "Where", "nil") }),
        fn("flee", "Run away from", "Orders", "Runs away from someone until far enough",
           { param("thing", Thing, "Who"), param("from", Thing, "From whom") }),
        fn("fetch", "Go and get", "Orders", "Goes to a thing; \"Arrived\" happens when it gets there",
           { param("thing", Thing, "Who"), param("target", Thing, "What"), param("run", Bool, "Run", "true") }),
        fn("free", "Do as it likes", "Orders", "Forgets its orders and goes back to its own life", { param("thing", Thing, "Who") }),
        fn("noise", "Make a noise", "Senses", "A sound everyone near enough hears (a bark is 25, footsteps 2)",
           { param("pos", Vec, "Where", "Vec(0, 0, 0)"), param("loudness", Number, "How loud", "20"), param("by", Thing, "Made by", "nil") }),
        fn("setMood", "Change mood", "Minds", "Sets boldness, curiosity, aggression or sociability (0 to 1)",
           { param("thing", Thing, "Who"), param("trait", Text, "Mood", "\"boldness\""), param("value", Number, "How much", "0.5") }),
        fn("setNeed", "Set need", "Minds", "Sets hunger, thirst or tiredness (0 = fine, 1 = desperate)",
           { param("thing", Thing, "Who"), param("need", Text, "Need", "\"hunger\""), param("value", Number, "How much", "1") }),
        fn("need", "How much it needs", "Minds", "Hunger, thirst or tiredness now (0 to 1)",
           { param("thing", Thing, "Who"), param("need", Text, "Need", "\"hunger\"") }, param("value", Number, "How much"), true),
        fn("doing", "What is it doing", "Minds", "The action it chose: \"graze\", \"flee\", \"investigate\", ...",
           { param("thing", Thing, "Who") }, param("action", Text, "Doing"), true),
        fn("knows", "Knows about", "Senses", "Whether it has noticed someone (seen, heard or smelled)",
           { param("thing", Thing, "Who"), param("other", Thing, "Whom") }, param("knows", Bool, "Knows"), true),
        fn("setTeam", "Put on team", "Minds", "Same team: friends. Different teams: enemies. 0: no team",
           { param("thing", Thing, "Who"), param("team", Number, "Team", "1") }),
        fn("feel", "Feel about", "Minds", "How it feels about someone: friendly, curious, fear, hostile, hunt, ignore",
           { param("thing", Thing, "Who"), param("other", Thing, "About whom"), param("attitude", Text, "Feeling", "\"friendly\"") }),
        fn("setInput", "Set a feeling", "Minds", "A number its own decisions can use (\"health\", \"loyalty\", ...)",
           { param("thing", Thing, "Who"), param("name", Text, "Name", "\"health\""), param("value", Number, "Value", "1") }),
        fn("teach", "Show what to do", "Learning", "\"In a moment like this, do that\": one example for its kind (graze, flee, rest, ...)",
           { param("thing", Thing, "Who"), param("action", Text, "Do", "\"graze\"") }, param("ok", Bool, "Worked")),
        fn("learn", "Learn from what it was shown", "Learning",
           "Its kind learns from every example so far and from now on leans towards it. How many it gets right (0 to 1)",
           { param("species", Text, "Kind", "\"sheep\"") }, param("right", Number, "Gets right")),
        fn("unlearn", "Forget what it learned", "Learning", "Back to its instincts (the examples are kept)",
           { param("species", Text, "Kind", "\"sheep\"") }),
        // Lists and tables aren't something a node can hold: Lua only.
        fn("species", "", "", "Every species id: { \"sheep\", \"cow\", ... }", {}),
        fn("defineSpecies", "", "", "Adds or changes a species from a table (the same keys as a species file, docs/AI.md)", {}),
    };
}

std::vector<ApiEvent> aiApiEvents() {
    using namespace api_type;
    return {
        { "Spotted", "When it notices someone", "It saw, heard or smelled someone clearly enough",
          { param("who", Thing, "Who"), param("what", Thing, "Noticed") }, "who" },
        { "LostSight", "When it loses track", "It hasn't sensed someone for a while",
          { param("who", Thing, "Who"), param("what", Thing, "Lost") }, "who" },
        { "Heard", "When it hears a noise", "A noise was loud enough for it",
          { param("who", Thing, "Who"), param("where", Vec, "Where"), param("by", Thing, "Made by") }, "who" },
        { "Scared", "When it gets scared", "It started running away", { param("who", Thing, "Who"), param("of", Thing, "Of") },
          "who" },
        { "Calmed", "When it calms down", "It stopped running away", { param("who", Thing, "Who") }, "who" },
        { "Attacks", "When it attacks", "It is close enough to bite, peck or hit someone",
          { param("who", Thing, "Who"), param("target", Thing, "Whom") }, "who" },
        { "Arrived", "When it gets there", "It reached the place or thing it was sent to",
          { param("who", Thing, "Who"), param("at", Vec, "Where"), param("thing", Thing, "Thing") }, "who" },
        { "StartsDoing", "When it starts something", "It chose something new to do",
          { param("who", Thing, "Who"), param("action", Text, "Doing") }, "who" },
    };
}

#if KKE_ENABLE_LUA

namespace {

uint32_t thingArg(lua_State* L, int index) {
    if (lua_isnoneornil(L, index)) return 0;
    const lua_Integer id = luaL_checkinteger(L, index);
    return id > 0 && id <= lua_Integer(UINT32_MAX) ? uint32_t(id) : 0;
}

bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

float number01(lua_State* L, int index, double fallback) {
    const double v = luaL_optnumber(L, index, fallback);
    return std::isfinite(v) ? float(v) : 0.0f;
}

// A Lua table as JSON (for ai.defineSpecies): arrays for 1..n sequences,
// objects otherwise; depth-limited like ScriptVM::encodeValue.
bool toJson(lua_State* L, int index, nlohmann::json& out, int depth, std::string& error) {
    if (depth > 8) {
        error = "table nested too deep";
        return false;
    }
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
    case LUA_TNIL: out = nullptr; return true;
    case LUA_TBOOLEAN: out = bool(lua_toboolean(L, index)); return true;
    case LUA_TNUMBER:
        if (lua_isinteger(L, index)) out = int64_t(lua_tointeger(L, index));
        else out = lua_tonumber(L, index);
        return true;
    case LUA_TSTRING: out = std::string(lua_tostring(L, index)); return true;
    case LUA_TTABLE: {
        const lua_Integer n = lua_Integer(lua_rawlen(L, index));
        if (n > 0) {
            out = nlohmann::json::array();
            for (lua_Integer i = 1; i <= n; ++i) {
                lua_rawgeti(L, index, i);
                nlohmann::json v;
                const bool ok = toJson(L, -1, v, depth + 1, error);
                lua_pop(L, 1);
                if (!ok) return false;
                out.push_back(std::move(v));
            }
            return true;
        }
        out = nlohmann::json::object();
        lua_pushnil(L);
        while (lua_next(L, index)) {
            if (lua_type(L, -2) != LUA_TSTRING) {
                lua_pop(L, 2);
                error = "table keys must be names";
                return false;
            }
            const std::string key = lua_tostring(L, -2);
            nlohmann::json v;
            if (!toJson(L, -1, v, depth + 1, error)) {
                lua_pop(L, 2);
                return false;
            }
            out[key] = std::move(v);
            lua_pop(L, 1);
        }
        return true;
    }
    default: error = "only numbers, text, true/false and tables"; return false;
    }
}

} // namespace

void bindAi(ScriptVM& vm, AiWorld& world, LocateFn locate) {
    AiWorld* w = &world;
    auto loc = std::make_shared<LocateFn>(std::move(locate));
    for (const ApiFunction& api : aiApiFunctions()) {
        ScriptVM::Fn f;
        const std::string& n = api.name;
        if (n == "add") {
            f = [w, loc](lua_State* L) {
                const uint32_t t = thingArg(L, 1);
                const std::string species = luaL_optstring(L, 2, "sheep");
                glm::vec3 pos(0.0f);
                bool ok = t && *loc && (*loc)(t, pos) && finite(pos);
                if (ok && w->has(t)) w->remove(t); // re-adding changes the species
                ok = ok && w->addAgent(t, species, pos);
                lua_pushboolean(L, ok);
                return 1;
            };
        } else if (n == "remove") {
            f = [w](lua_State* L) { lua_pushboolean(L, w->remove(thingArg(L, 1))); return 1; };
        } else if (n == "goTo") {
            f = [w](lua_State* L) {
                const glm::vec3 pos = ScriptVM::toVec3(L, 2);
                if (!finite(pos)) return luaL_error(L, "ai.goTo: the position must be real numbers");
                Order o;
                o.kind = Order::Kind::MoveTo;
                o.position = pos;
                o.distance = 1.0f;
                o.run = lua_toboolean(L, 3);
                w->order(thingArg(L, 1), o);
                return 0;
            };
        } else if (n == "follow") {
            f = [w](lua_State* L) {
                Order o;
                o.kind = Order::Kind::Follow;
                o.target = thingArg(L, 2);
                o.distance = std::clamp(number01(L, 3, 2.5), 0.5f, 50.0f);
                w->order(thingArg(L, 1), o);
                return 0;
            };
        } else if (n == "attack") {
            f = [w](lua_State* L) {
                Order o;
                o.kind = Order::Kind::Attack;
                o.target = thingArg(L, 2);
                w->order(thingArg(L, 1), o);
                return 0;
            };
        } else if (n == "hold") {
            f = [w](lua_State* L) {
                const uint32_t t = thingArg(L, 1);
                const Agent* a = w->agent(t);
                if (!a) return 0;
                Order o;
                o.kind = Order::Kind::Hold;
                o.position = lua_isnoneornil(L, 2) ? a->position : ScriptVM::toVec3(L, 2);
                if (!finite(o.position)) return luaL_error(L, "ai.hold: the position must be real numbers");
                w->order(t, o);
                return 0;
            };
        } else if (n == "flee") {
            f = [w](lua_State* L) {
                Order o;
                o.kind = Order::Kind::Flee;
                o.target = thingArg(L, 2);
                w->order(thingArg(L, 1), o);
                return 0;
            };
        } else if (n == "fetch") {
            f = [w](lua_State* L) {
                Order o;
                o.kind = Order::Kind::Interact;
                o.target = thingArg(L, 2);
                o.run = lua_isnoneornil(L, 3) ? true : bool(lua_toboolean(L, 3));
                w->order(thingArg(L, 1), o);
                return 0;
            };
        } else if (n == "free") {
            f = [w](lua_State* L) { w->clearOrder(thingArg(L, 1)); return 0; };
        } else if (n == "noise") {
            f = [w](lua_State* L) {
                Noise noise;
                noise.position = ScriptVM::toVec3(L, 1);
                noise.loudness = std::clamp(number01(L, 2, 20.0), 0.0f, 200.0f);
                noise.source = thingArg(L, 3);
                w->makeNoise(noise);
                return 0;
            };
        } else if (n == "setMood") {
            f = [w](lua_State* L) {
                const uint32_t t = thingArg(L, 1);
                const std::string trait = luaL_checkstring(L, 2);
                const float v = number01(L, 3, 0.5);
                const Agent* a = w->agent(t);
                if (!a) return 0;
                Temperament m = a->mood;
                if (trait == "boldness") m.boldness = v;
                else if (trait == "curiosity") m.curiosity = v;
                else if (trait == "aggression") m.aggression = v;
                else if (trait == "sociability") m.sociability = v;
                else return luaL_error(L, "ai.setMood: \"%s\" isn't a mood (boldness, curiosity, aggression, sociability)", trait.c_str());
                w->setMood(t, m);
                return 0;
            };
        } else if (n == "setNeed") {
            f = [w](lua_State* L) {
                lua_pushboolean(L, w->setNeed(thingArg(L, 1), luaL_checkstring(L, 2), number01(L, 3, 1.0)));
                return 1;
            };
        } else if (n == "need") {
            f = [w](lua_State* L) {
                const float v = w->need(thingArg(L, 1), luaL_checkstring(L, 2));
                if (v < 0.0f) lua_pushnil(L);
                else lua_pushnumber(L, v);
                return 1;
            };
        } else if (n == "doing") {
            f = [w](lua_State* L) {
                const uint32_t t = thingArg(L, 1);
                if (!w->has(t)) lua_pushnil(L);
                else lua_pushstring(L, w->actionName(t).c_str());
                return 1;
            };
        } else if (n == "knows") {
            f = [w](lua_State* L) {
                const Agent* a = w->agent(thingArg(L, 1));
                const uint32_t other = thingArg(L, 2);
                bool knows = false;
                if (a)
                    for (const Awareness& m : a->memory) knows = knows || (m.id == other && m.spotted);
                lua_pushboolean(L, knows);
                return 1;
            };
        } else if (n == "setTeam") {
            f = [w](lua_State* L) {
                const lua_Integer team = luaL_optinteger(L, 2, 1);
                w->setTeam(thingArg(L, 1), team > 0 && team <= lua_Integer(UINT32_MAX) ? uint32_t(team) : 0u);
                return 0;
            };
        } else if (n == "feel") {
            f = [w](lua_State* L) {
                Attitude a;
                const std::string name = luaL_optstring(L, 3, "friendly");
                if (!attitudeFromName(name, a))
                    return luaL_error(L, "ai.feel: \"%s\" isn't a feeling (friendly, curious, fear, hostile, hunt, ignore)", name.c_str());
                w->setAttitude(thingArg(L, 1), thingArg(L, 2), a);
                return 0;
            };
        } else if (n == "setInput") {
            f = [w](lua_State* L) {
                const double v = luaL_optnumber(L, 3, 1.0);
                lua_pushboolean(L, w->setInput(thingArg(L, 1), luaL_checkstring(L, 2), std::isfinite(v) ? float(v) : 0.0f));
                return 1;
            };
        } else if (n == "teach") {
            f = [w](lua_State* L) {
                lua_pushboolean(L, w->teach(thingArg(L, 1), luaL_checkstring(L, 2)));
                return 1;
            };
        } else if (n == "learn") {
            f = [w](lua_State* L) {
                lua_pushnumber(L, double(w->learn(luaL_checkstring(L, 1)).accuracy));
                return 1;
            };
        } else if (n == "unlearn") {
            f = [w](lua_State* L) {
                w->unlearn(luaL_checkstring(L, 1));
                return 0;
            };
        } else if (n == "species") {
            f = [w](lua_State* L) {
                const std::vector<std::string> ids = w->speciesIds();
                lua_createtable(L, int(ids.size()), 0);
                for (size_t i = 0; i < ids.size(); ++i) {
                    lua_pushstring(L, ids[i].c_str());
                    lua_rawseti(L, -2, lua_Integer(i + 1));
                }
                return 1;
            };
        } else if (n == "defineSpecies") {
            f = [w](lua_State* L) {
                luaL_checktype(L, 1, LUA_TTABLE);
                nlohmann::json j;
                std::string error;
                if (!toJson(L, 1, j, 0, error)) return luaL_error(L, "ai.defineSpecies: %s", error.c_str());
                // Starts from the species of the same id, if there is one.
                Species s;
                if (j.is_object() && j.contains("id") && j["id"].is_string())
                    if (const Species* base = w->species(j["id"].get<std::string>())) {
                        s = *base;
                        s.actions.clear();
                    }
                if (!speciesFromJson(j, s, &error)) return luaL_error(L, "ai.defineSpecies: %s", error.c_str());
                w->defineSpecies(std::move(s));
                return 0;
            };
        }
        if (f) vm.registerFunction(api, std::move(f));
    }
    for (const ApiEvent& e : aiApiEvents()) vm.describeEvent(e);
}

namespace {

void setThing(lua_State* L, const char* key, uint32_t thing) {
    if (thing) lua_pushinteger(L, lua_Integer(thing));
    else lua_pushnil(L);
    lua_setfield(L, -2, key);
}

} // namespace

void fireAiEvents(ScriptVM& vm, const std::vector<AiEvent>& events) {
    for (const AiEvent& e : events) {
        const char* name = nullptr;
        switch (e.kind) {
        case AiEvent::Kind::Spotted: name = "Spotted"; break;
        case AiEvent::Kind::Lost: name = "LostSight"; break;
        case AiEvent::Kind::Heard: name = "Heard"; break;
        case AiEvent::Kind::Scared: name = "Scared"; break;
        case AiEvent::Kind::Calmed: name = "Calmed"; break;
        case AiEvent::Kind::Attack: name = "Attacks"; break;
        case AiEvent::Kind::Arrived: name = "Arrived"; break;
        case AiEvent::Kind::ActionChanged: name = "StartsDoing"; break;
        }
        if (!name || vm.hookCount(name) == 0) continue;
        vm.callHookWith(name, [&](lua_State* L) {
            lua_createtable(L, 0, 3);
            setThing(L, "who", e.who);
            switch (e.kind) {
            case AiEvent::Kind::Spotted:
            case AiEvent::Kind::Lost: setThing(L, "what", e.other); break;
            case AiEvent::Kind::Heard:
                ScriptVM::pushVec3(L, e.position);
                lua_setfield(L, -2, "where");
                setThing(L, "by", e.other);
                break;
            case AiEvent::Kind::Scared: setThing(L, "of", e.other); break;
            case AiEvent::Kind::Calmed: break;
            case AiEvent::Kind::Attack: setThing(L, "target", e.other); break;
            case AiEvent::Kind::Arrived:
                ScriptVM::pushVec3(L, e.position);
                lua_setfield(L, -2, "at");
                setThing(L, "thing", e.other);
                break;
            case AiEvent::Kind::ActionChanged:
                lua_pushstring(L, e.action.c_str());
                lua_setfield(L, -2, "action");
                break;
            }
            return 1;
        });
    }
}

#endif

} // namespace kke::ai
