#pragma once

#include "kke/Orders.h"
#include "kke/ai/AiWorld.h"

#include <functional>
#include <map>
#include <vector>

namespace kke {

// The player's orders (kke/Orders.h) carried out by the AI core
// (kke/ai/AiWorld.h): one order system, two halves. The OrderBoard says
// what each unit was told (selection, formation slots, queues, done or
// failed); AiWorld decides how (paths, steering, fighting, its own wants
// when it has no order). This bridge turns each new board order into an
// ai::Order and each ai Arrived event back into done:
//
//   Move, Regroup   MoveTo the unit's formation slot; done on arrival (it then holds there)
//   Follow          Follow the target at followDistance
//   Stay            Hold the spot (still defends it)
//   Sit             Hold where it is; the game shows it sitting
//   Attack, Focus   Attack the target; the game ends it with OrderBoard::targetGone
//   Fetch           Interact with the thing; pickUp; Interact with the issuer; deliver; done
//   Pet             Interact with the issuer; petStart (the game ends it when the pat is over)
//   Drop            drop; done at once
//   Free            clearOrder: back to its own wants
//
// Units that aren't agents in the AiWorld are left alone (a scripted unit
// answers with order.done). docs/COMMANDS.md, tested in tests/test_orders.cpp.
class AiOrderBridge {
public:
    AiOrderBridge(OrderBoard& board, ai::AiWorld& world);
    ~AiOrderBridge();
    AiOrderBridge(const AiOrderBridge&) = delete;
    AiOrderBridge& operator=(const AiOrderBridge&) = delete;

    // The steps only the game can do. Unset: they just work.
    std::function<bool(uint32_t unit, uint32_t thing)> pickUp;                 // false: can't (the fetch fails)
    std::function<void(uint32_t unit, uint32_t thing, uint32_t to)> deliver;   // put it down in front of `to`
    std::function<void(uint32_t unit)> drop;
    std::function<void(uint32_t unit, uint32_t by)> petStart;                  // unset: done on arrival

    float followDistance = 2.0f;
    float runBeyond = 8.0f;       // Move, Regroup, Pet: run when it is farther than this
    float fetchReach = 0.5f;      // Interact distance for the thing to fetch

    // Every frame after AiWorld::update, with the events it gave (the game
    // may read them too: take them once, pass them here).
    void handle(const std::vector<ai::AiEvent>& events);
    // What `unit` is carrying for a fetch (0 = nothing).
    uint32_t carrying(uint32_t unit) const;

private:
    void apply(uint32_t unit, const UnitOrder& o);

    OrderBoard& m_board;
    ai::AiWorld& m_world;
    int m_listener = 0;
    struct Fetch { uint32_t thing = 0, issuer = 0; bool carrying = false; };
    std::map<uint32_t, Fetch> m_fetch;
};

} // namespace kke
