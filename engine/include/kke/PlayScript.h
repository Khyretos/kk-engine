#pragma once

#include "kke/LuaApi.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace kke {

class ScriptVM;

// The play blocks in Lua (docs/PLAY_TO_MAKE.md, "One set of building
// blocks"): the functions and events the Simple palette, the node graph
// and plain Lua scripts all use. The game provides the world
// (IPlayWorld: the sandbox's placed things, ragdolls, sounds); this file
// binds it as `play.*` and documents every binding, which is where the
// node graph's library comes from (kke/NodeGraph.h).
//
//   play.spawn(block, pos [, yaw]) -> thing   play.remove(thing)
//   play.ragdoll(thing [, push])              play.standUp(thing)
//   play.swing(thing)                         play.sound(name [, pos])
//   play.say(text)                            play.addScore(n) -> score
//   play.score() -> n                         play.position(thing) -> Vec
//   play.blockOf(thing) -> block              play.isDown(thing) -> bool
//   play.blocks() -> { {id=, label=, kind=}, ... }
//
// Events (hook.Add(name, id, function(e) ... end), one table each):
//   "Hit"      { target, by, point, push, block }  the bat (or play.swing) hit someone
//   "Clicked"  { thing, point, block }             someone tapped a thing
//   "Placed"   { thing, point, block }             a thing was put in the world
//   "FellOver" { thing, block }                    a person fell over (bat, graph, anything)
//   "StoodUp"  { thing, block }
//
// Things a script spawns belong to it: they go when it is unloaded or
// reloaded (like everything else a script makes), so a graph edited
// while playing doesn't pile up copies.
class IPlayWorld {
public:
    virtual ~IPlayWorld() = default;

    struct BlockInfo { std::string id, label, kind; }; // kind: "character", "prop", "tool"
    virtual std::vector<BlockInfo> blocks() const = 0;
    // 0 when the block is unknown or its assets aren't on disk.
    virtual uint32_t spawn(const std::string& block, const glm::vec3& position, float yawDegrees, const std::string& owner) = 0;
    virtual bool remove(uint32_t thing) = 0;
    virtual bool exists(uint32_t thing) const = 0;
    virtual bool ragdoll(uint32_t thing, const glm::vec3& push) = 0; // people only
    virtual bool standUp(uint32_t thing) = 0;
    virtual bool isDown(uint32_t thing) const = 0;
    virtual bool swingAt(uint32_t thing) = 0;                        // the bat swings through them
    virtual void sound(const std::string& name, const glm::vec3& position) = 0;
    virtual void say(const std::string& text) = 0;
    virtual double addScore(double points) = 0; // returns the new score
    virtual double score() const = 0;
    virtual glm::vec3 position(uint32_t thing) const = 0;
    virtual std::string blockOf(uint32_t thing) const = 0;
    // Everything spawned by `owner` (a script's source) goes.
    virtual void removeOwnedBy(const std::string& owner) = 0;
};

// Sound names play.sound knows (the audio module's impact materials plus
// "bonk"), in the order the node editor lists them.
const std::vector<std::string>& playSoundNames();

// The play.* functions and events, documented (kke/LuaApi.h). Available
// without a VM so tools and tests can list them.
std::vector<ApiFunction> playApiFunctions();
std::vector<ApiEvent> playApiEvents();

#if KKE_ENABLE_LUA
// Binds play.* on `vm` over `world` and documents the play events.
// `world` must outlive vm.
// Unloading a script removes what it spawned (IPlayWorld::removeOwnedBy).
void bindPlayBlocks(ScriptVM& vm, IPlayWorld& world);

// The engine side of the events: the game calls these when they happen.
struct PlayHit { uint32_t target = 0; std::string by = "bat"; glm::vec3 point{0.0f}, push{0.0f}; std::string block; };
void firePlayHit(ScriptVM& vm, const PlayHit& hit);
void firePlayClicked(ScriptVM& vm, uint32_t thing, const glm::vec3& point, const std::string& block);
void firePlayPlaced(ScriptVM& vm, uint32_t thing, const glm::vec3& point, const std::string& block);
void firePlayFellOver(ScriptVM& vm, uint32_t thing, const std::string& block);
void firePlayStoodUp(ScriptVM& vm, uint32_t thing, const std::string& block);
#endif

} // namespace kke
