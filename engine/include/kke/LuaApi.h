#pragma once

#include <string>
#include <vector>

namespace kke {

// What a Lua binding takes, gives back and does, written next to the
// binding itself (ScriptVM::registerFunction(ApiFunction, fn)), so tools
// built on the bindings can't fall behind them. The node graph builds its
// whole library from these (kke/NodeGraph.h, docs/PLAY_TO_MAKE.md): a new
// documented binding is a new node, with no second list to update.

// Value types a node pin can carry. Plain strings so a binding in any
// file can use them; the node editor knows how to edit each one.
namespace api_type {
inline constexpr const char* Thing = "thing";   // a placed thing's id (integer)
inline constexpr const char* Block = "block";   // a play block id: "person", "crate", ...
inline constexpr const char* Number = "number";
inline constexpr const char* Text = "text";
inline constexpr const char* Vec = "vec";       // Vec(x, y, z)
inline constexpr const char* Bool = "bool";
inline constexpr const char* Sound = "sound";   // a sound name: "bonk", "wood", "metal", ...
} // namespace api_type

struct ApiParam {
    std::string name;     // Lua name: "target"
    std::string type;     // api_type::...
    std::string label;    // what a node shows: "Who"
    std::string fallback; // default as Lua source ("1", "\"bonk\"", "Vec(0, 0, 0)"); "" = required
};

struct ApiFunction {
    std::string table, name; // play.spawn -> "play", "spawn"
    std::string label;       // node title, a short verb phrase: "Knock over"; "" = no node
    std::string doc;         // one line, shown as the node's tooltip
    std::string category;    // node library section: "Things", "Sound", ...
    std::vector<ApiParam> params;
    ApiParam result;         // type "" = returns nothing
    // No side effects (a getter): drawn without flow pins and evaluated
    // where its value is used.
    bool pure = false;

    std::string qualifiedName() const { return table + "." + name; }
};

// An event scripts hook with hook.Add(name, id, function(e) ... end).
// Every engine event passes one table; `fields` are its keys.
struct ApiEvent {
    std::string name;    // "Hit"
    std::string label;   // "When something is hit"
    std::string doc;
    std::vector<ApiParam> fields;
    // The field naming the thing the event is about ("target" for Hit),
    // "" if none. A graph attached to one thing only reacts to its own.
    std::string subject;
};

} // namespace kke
