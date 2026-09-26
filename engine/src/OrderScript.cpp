#include "kke/OrderScript.h"

#if KKE_ENABLE_LUA
#include "kke/ScriptVM.h"

#include <lauxlib.h>
#include <lua.h>
#endif

#include <algorithm>
#include <cmath>
#include <utility>

namespace kke {

namespace {

ApiParam param(std::string name, std::string type, std::string label, std::string fallback = {}) {
    return ApiParam{ std::move(name), std::move(type), std::move(label), std::move(fallback) };
}

ApiFunction fn(std::string name, std::string label, std::string doc, std::vector<ApiParam> params, ApiParam result = {},
               bool pure = false) {
    ApiFunction f;
    f.table = "order";
    f.name = std::move(name);
    f.label = std::move(label);
    f.category = "Orders";
    f.doc = std::move(doc);
    f.params = std::move(params);
    f.result = std::move(result);
    f.pure = pure;
    return f;
}

// Who an order is for: in a node with nothing linked, whoever is selected.
ApiParam units() { return param("units", api_type::Thing, "Who", "order.selected()"); }

} // namespace

std::vector<ApiFunction> orderApiFunctions() {
    using namespace api_type;
    return {
        fn("move", "Send to", "Sends them to a place; a group spreads out there (formation: \"wedge\", \"line\", \"column\", \"circle\")",
           { units(), param("pos", Vec, "Where"), param("formation", Text, "Formation", "\"wedge\"") }),
        fn("follow", "Follow", "They stay with someone (the player if nobody is given), never in their way",
           { units(), param("leader", Thing, "Whom", "order.player()") }),
        fn("stay", "Hold position", "They stay where they are (or go to a place and stay there), and defend it",
           { units(), param("pos", Vec, "Where", "nil") }),
        fn("attack", "Attack", "They go for this target", { units(), param("target", Thing, "Target") }),
        fn("focus", "Focus fire", "Everyone on this one target and nothing else until it is down", { units(), param("target", Thing, "Target") }),
        fn("fetch", "Fetch", "They bring this thing back to the player", { units(), param("thing", Thing, "What") }),
        fn("drop", "Drop it", "They let go of what they carry", { units() }),
        fn("sit", "Sit", "They sit down", { units() }),
        fn("pet", "Pet", "The player pets them: they stand still and enjoy it", { units() }),
        fn("regroup", "Regroup", "They gather around the player (or a place)", { units(), param("pos", Vec, "Where", "nil") }),
        fn("free", "At ease", "No more orders: they do what they like", { units() }),
        fn("current", "Order of", "What someone was told to do: \"move\", \"sit\", ... (nothing if no order)", { param("unit", Thing, "Who") },
           param("order", Text, "Order"), true),
        fn("target", "Order target", "Who or what someone's order is about", { param("unit", Thing, "Who") }, param("target", Thing, "Target"),
           true),
        fn("done", "Order done", "Ends someone's order (for units a script moves): the next queued order starts",
           { param("unit", Thing, "Who"), param("ok", Bool, "Worked", "true") }),
        fn("player", "The player", "The player, who gives the orders", {}, param("player", Thing, "Player"), true),
        // A list isn't something a node can hold: Lua only (and the default "Who").
        fn("selected", "", "Everyone the player has selected: { thing, ... }", {}),
    };
}

std::vector<ApiEvent> orderApiEvents() {
    using namespace api_type;
    return {
        { "Ordered", "When given an order", "Someone got a new order (from the player, a script or a graph)",
          { param("unit", Thing, "Who"), param("order", Text, "Order"), param("target", Thing, "Target"), param("point", Vec, "Where"),
            param("by", Thing, "By") },
          "unit" },
        { "OrderDone", "When an order is done", "Someone finished an order, or couldn't (ok is false)",
          { param("unit", Thing, "Who"), param("order", Text, "Order"), param("ok", Bool, "Worked") }, "unit" },
    };
}

#if KKE_ENABLE_LUA

namespace {

uint32_t thingAt(lua_State* L, int index) {
    if (!lua_isinteger(L, index)) return 0;
    const lua_Integer id = lua_tointeger(L, index);
    return id > 0 && id <= lua_Integer(UINT32_MAX) ? uint32_t(id) : 0;
}

// One thing or a table of things.
std::vector<uint32_t> unitsArg(lua_State* L, int index, const IOrderHost& host) {
    std::vector<uint32_t> out;
    if (lua_isnoneornil(L, index)) return host.selected();
    if (lua_istable(L, index)) {
        const lua_Integer n = luaL_len(L, index);
        for (lua_Integer i = 1; i <= n && i <= 256; ++i) {
            lua_geti(L, index, i);
            if (const uint32_t u = thingAt(L, -1)) out.push_back(u);
            lua_pop(L, 1);
        }
        return out;
    }
    if (const uint32_t u = thingAt(L, index)) out.push_back(u);
    else luaL_argerror(L, index, "a thing or a table of things");
    return out;
}

void pushThing(lua_State* L, uint32_t thing) {
    if (thing) lua_pushinteger(L, lua_Integer(thing));
    else lua_pushnil(L);
}

bool finiteVec(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

} // namespace

OrderScript::OrderScript(ScriptVM& vm, OrderBoard& board, IOrderHost& host) : m_vm(vm), m_board(board), m_host(host) {
    ScriptVM* v = &vm;
    OrderBoard* b = &board;
    IOrderHost* h = &host;
    (void)v;
    // Gives an order built from Lua arguments; returns true if it was valid.
    auto give = [b, h](lua_State* L, Order o) {
        o.issuer = h->player();
        std::string why;
        const bool ok = !o.units.empty() && b->issue(o, &why) != 0;
        lua_pushboolean(L, ok);
        return 1;
    };
    for (const ApiFunction& api : orderApiFunctions()) {
        ScriptVM::Fn f;
        const std::string& n = api.name;
        const OrderKind kind = orderFromName(n);
        if (n == "move" || n == "stay" || n == "regroup") {
            f = [h, give, kind, n](lua_State* L) {
                Order o;
                o.kind = kind;
                o.units = unitsArg(L, 1, *h);
                if (!lua_isnoneornil(L, 2)) {
                    o.point = ScriptVM::toVec3(L, 2, glm::vec3(NAN));
                    if (!finiteVec(o.point)) return luaL_error(L, "order.%s: the place must be real numbers", n.c_str());
                    o.hasPoint = true;
                } else if (kind == OrderKind::Move) {
                    return luaL_error(L, "order.move: where to?");
                }
                if (kind == OrderKind::Move) o.formation = formationFromName(luaL_optstring(L, 3, "wedge"));
                return give(L, o);
            };
        } else if (n == "follow" || n == "attack" || n == "focus" || n == "fetch") {
            f = [h, give, kind, n](lua_State* L) {
                Order o;
                o.kind = kind;
                o.units = unitsArg(L, 1, *h);
                o.target = lua_isnoneornil(L, 2) && kind == OrderKind::Follow ? h->player() : thingAt(L, 2);
                if (!o.target) return luaL_error(L, "order.%s: needs a thing to %s", n.c_str(), kind == OrderKind::Follow ? "follow" : "go for");
                return give(L, o);
            };
        } else if (n == "drop" || n == "sit" || n == "free" || n == "pet") {
            f = [h, give, kind](lua_State* L) {
                Order o;
                o.kind = kind;
                o.units = unitsArg(L, 1, *h);
                if (kind == OrderKind::Pet) o.target = h->player();
                return give(L, o);
            };
        } else if (n == "current") {
            f = [b](lua_State* L) {
                const OrderKind k = b->currentKind(thingAt(L, 1));
                if (k == OrderKind::None) lua_pushnil(L);
                else lua_pushstring(L, orderName(k));
                return 1;
            };
        } else if (n == "target") {
            f = [b](lua_State* L) {
                const UnitOrder* o = b->current(thingAt(L, 1));
                pushThing(L, o ? o->target : 0);
                return 1;
            };
        } else if (n == "done") {
            f = [b](lua_State* L) {
                const uint32_t u = thingAt(L, 1);
                const bool had = b->current(u) != nullptr;
                b->complete(u, lua_isnoneornil(L, 2) ? true : lua_toboolean(L, 2) != 0);
                lua_pushboolean(L, had);
                return 1;
            };
        } else if (n == "player") {
            f = [h](lua_State* L) { pushThing(L, h->player()); return 1; };
        } else if (n == "selected") {
            f = [h](lua_State* L) {
                const std::vector<uint32_t> sel = h->selected();
                lua_createtable(L, int(sel.size()), 0);
                for (size_t i = 0; i < sel.size(); ++i) {
                    lua_pushinteger(L, lua_Integer(sel[i]));
                    lua_rawseti(L, -2, lua_Integer(i + 1));
                }
                return 1;
            };
        }
        if (f) vm.registerFunction(api, std::move(f));
    }
    for (const ApiEvent& e : orderApiEvents()) vm.describeEvent(e);

    OrderBoard::Listener l;
    l.ordered = [this](uint32_t unit, const UnitOrder& o) {
        Event e;
        e.unit = unit;
        e.order = orderName(o.kind);
        e.target = o.target;
        e.by = o.issuer;
        e.point = o.point;
        e.hasPoint = o.hasPoint;
        m_events.push_back(std::move(e));
    };
    l.finished = [this](uint32_t unit, const UnitOrder& o, bool ok) {
        Event e;
        e.done = true;
        e.unit = unit;
        e.order = orderName(o.kind);
        e.ok = ok;
        m_events.push_back(std::move(e));
    };
    m_listener = board.listen(std::move(l));
}

OrderScript::~OrderScript() { m_board.unlisten(m_listener); }

void OrderScript::fireEvents() {
    // Hooks may give orders, which queue more events: those fire next frame.
    std::vector<Event> events;
    events.swap(m_events);
    for (const Event& e : events) {
        m_vm.callHookWith(e.done ? "OrderDone" : "Ordered", [&](lua_State* L) {
            lua_createtable(L, 0, 5);
            lua_pushinteger(L, lua_Integer(e.unit));
            lua_setfield(L, -2, "unit");
            lua_pushstring(L, e.order.c_str());
            lua_setfield(L, -2, "order");
            if (e.done) {
                lua_pushboolean(L, e.ok);
                lua_setfield(L, -2, "ok");
            } else {
                pushThing(L, e.target);
                lua_setfield(L, -2, "target");
                pushThing(L, e.by);
                lua_setfield(L, -2, "by");
                if (e.hasPoint) {
                    ScriptVM::pushVec3(L, e.point);
                    lua_setfield(L, -2, "point");
                }
            }
            return 1;
        });
    }
}

#endif

} // namespace kke
