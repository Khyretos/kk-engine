#pragma once

#include "kke/LuaApi.h"
#include "kke/Orders.h"

#include <cstdint>
#include <string>
#include <vector>

namespace kke {

class ScriptVM;

// Orders as play blocks (docs/COMMANDS.md): the same orders the Simple
// buttons, the radial wheel and a right click give, as Lua functions and
// so as node-graph nodes (kke/NodeGraph.h builds its library from the
// documented bindings).
//
//   order.move(units, pos [, formation])   order.follow(units [, leader])
//   order.stay(units [, pos])              order.attack(units, target)
//   order.focus(units, target)             order.fetch(units, thing)
//   order.drop(units)   order.sit(units)   order.pet(units)
//   order.regroup(units [, pos])           order.free(units)
//   order.current(unit) -> "move"/"sit"/... or nil
//   order.target(unit) -> thing or nil     order.done(unit [, ok])
//   order.player() -> thing                order.selected() -> { thing, ... }
//
// `units` is one thing or a table of things; left out in a node, it is
// whoever the player has selected (or `me` in a thing's own graph). The
// player gives every order made from Lua (Fetch brings things to them).
// order.done is for units whose brain is a script: it ends the current
// order the way the AI core does for its own units.
//
// Events (hook.Add(name, id, function(e) ... end)):
//   "Ordered"   { unit, order, target, point, by }  a unit got a new order
//   "OrderDone" { unit, order, ok }                  a unit finished (or failed) one
class IOrderHost {
public:
    virtual ~IOrderHost() = default;
    virtual uint32_t player() const = 0;                  // who gives orders from Lua
    virtual std::vector<uint32_t> selected() const = 0;   // the player's current selection
};

std::vector<ApiFunction> orderApiFunctions();
std::vector<ApiEvent> orderApiEvents();

#if KKE_ENABLE_LUA
// Binds order.* on `vm` over `board` and listens to the board. Events are
// queued and fired by fireEvents() (once a frame, outside any script), so
// an order given from a hook never re-enters the script that gave it.
// `vm`, `board` and `host` must outlive this object.
class OrderScript {
public:
    OrderScript(ScriptVM& vm, OrderBoard& board, IOrderHost& host);
    ~OrderScript();
    OrderScript(const OrderScript&) = delete;
    OrderScript& operator=(const OrderScript&) = delete;

    void fireEvents();
    size_t pending() const { return m_events.size(); }

private:
    struct Event {
        bool done = false; // false: Ordered, true: OrderDone
        uint32_t unit = 0, target = 0, by = 0;
        std::string order;
        glm::vec3 point{0.0f};
        bool hasPoint = false, ok = true;
    };
    ScriptVM& m_vm;
    OrderBoard& m_board;
    IOrderHost& m_host;
    int m_listener = 0;
    std::vector<Event> m_events;
};
#endif

} // namespace kke
