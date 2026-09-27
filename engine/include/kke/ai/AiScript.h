#pragma once

#include "kke/LuaApi.h"
#include "kke/ai/AiWorld.h"

#include <glm/glm.hpp>

#include <functional>
#include <vector>

namespace kke {
class ScriptVM;
}

namespace kke::ai {

// The AI core in Lua, as `ai.*` (docs/AI.md "Lua"), documented so every
// binding is also a node in the node graph (kke/NodeGraph.h) and so the
// Simple palette's animal recipes are graphs of real blocks
// (docs/PLAY_TO_MAKE.md). Ids are the game's thing ids.
//
//   ai.add(thing, species) -> bool         ai.remove(thing)
//   ai.goTo(thing, pos [, run])            ai.follow(thing, leader [, distance])
//   ai.attack(thing, target)               ai.hold(thing [, pos])
//   ai.flee(thing, from)                   ai.fetch(thing, target)
//   ai.free(thing)                         ai.noise(pos [, loudness [, by]])
//   ai.setMood(thing, trait, value)        ai.setNeed(thing, need, value)
//   ai.need(thing, need) -> n              ai.doing(thing) -> action name
//   ai.knows(thing, other) -> bool         ai.setTeam(thing, team)
//   ai.feel(thing, other, attitude)        ai.setInput(thing, name, value)
//   ai.teach(thing, action) -> bool        ai.learn(species) -> share right
//   ai.unlearn(species)
//   ai.species() -> { "sheep", ... }       ai.defineSpecies{ id = "wolf", ... }
//
// Events (hook.Add(name, id, function(e) ... end)):
//   "Spotted"     { who, what }          who noticed what
//   "LostSight"   { who, what }
//   "Heard"       { who, where, by }
//   "Scared"      { who, of }            started running away
//   "Calmed"      { who }
//   "Attacks"     { who, target }        in reach: the game deals the damage
//   "Arrived"     { who, at, thing }     reached where it was sent (thing: fetch target)
//   "StartsDoing" { who, action }        "graze", "flee", "order_follow", ...
std::vector<ApiFunction> aiApiFunctions();
std::vector<ApiEvent> aiApiEvents();

#if KKE_ENABLE_LUA
// Where a thing is, for ai.add (the game knows its things). False = no such thing.
using LocateFn = std::function<bool(uint32_t thing, glm::vec3& position)>;

// Binds ai.* over `world`; `world` must outlive `vm`.
void bindAi(ScriptVM& vm, AiWorld& world, LocateFn locate);
// Runs the Lua hooks for a batch of AiWorld::takeEvents().
void fireAiEvents(ScriptVM& vm, const std::vector<AiEvent>& events);
#endif

} // namespace kke::ai
