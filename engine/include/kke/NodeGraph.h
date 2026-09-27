#pragma once

#include "kke/LuaApi.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace kke {

// The Intermediate door of play-to-make (docs/PLAY_TO_MAKE.md): a node
// graph, "blueprints", where "when this happens" is wired to "do that".
//
// It is not a second engine. The nodes are the documented Lua bindings
// and events (kke/LuaApi.h: NodeLibrary::fromApi builds the library from
// what ScriptVM has bound, so a new binding is a new node), and a graph
// runs by being turned into the Lua it stands for (compileGraph) and
// loaded into the same ScriptVM as hand-written scripts. "Show Lua" is
// that text; errors come back as Lua errors and are mapped to the node
// that made the line.
//
// Pure logic, no UI: tested in tests/test_node_graph.cpp. The editor is
// in the sandbox (games/sandbox/GraphEditor.*), in RmlUi.

// ---------------------------------------------------------------- library

struct PinDef {
    std::string name;     // "target"; flow pins: ">" (in / out), "then", "else", "each", "done"
    std::string type;     // api_type::..., "any", or "flow"
    std::string label;
    std::string fallback; // Lua source used when nothing is linked and no value was typed; "" = none
    bool flow() const { return type == "flow"; }
};

struct NodeDef {
    enum class Kind {
        Event,  // starts a chain: a hook, "When the game starts", "Every few seconds"
        Action, // flow in, flow out: a binding call, "Wait"
        Flow,   // flow in, several flow outs: "If", "Do N times"
        Value,  // no flow: a getter or a calculation, worked out where it's used
    };
    std::string type;     // "event:Hit", "call:play.ragdoll", "flow:if", "value:random"
    std::string title;    // "Knock over"
    std::string category; // library section
    std::string doc;
    Kind kind = Kind::Action;
    std::vector<PinDef> inputs, outputs;
    // Event nodes: the hook name and the field naming what it's about.
    std::string event, subject;
    // Binding nodes: "play.ragdoll".
    std::string function;

    const PinDef* input(const std::string& name) const;
    const PinDef* output(const std::string& name) const;
};

class NodeLibrary {
public:
    // Every documented binding with a label becomes a node (pure ones as
    // value nodes), every documented event an event node, plus the
    // built-in flow and value nodes (start, every, wait, if, repeat,
    // random, compare, add, place, me).
    static NodeLibrary fromApi(const std::vector<ApiFunction>& functions, const std::vector<ApiEvent>& events);

    const std::vector<NodeDef>& nodes() const { return m_nodes; }
    const NodeDef* find(const std::string& type) const;
    // Section names in the order the editor lists them.
    std::vector<std::string> categories() const;

    // Whether a value of type `from` may be linked into a pin of type `to`.
    static bool compatible(const std::string& from, const std::string& to);

private:
    std::vector<NodeDef> m_nodes;
};

// ---------------------------------------------------------------- graph

struct GraphNode {
    int id = 0;
    std::string type;
    glm::vec2 pos{0.0f};                       // canvas position (editor units)
    std::map<std::string, std::string> values; // typed values of unlinked inputs, as the user wrote them
};

struct GraphLink {
    int fromNode = 0;
    std::string fromPin;
    int toNode = 0;
    std::string toPin;
    bool operator==(const GraphLink&) const = default;
};

struct NodeGraph {
    std::string name; // shown in the editor: "Bat", "Level", "Person 3"
    std::vector<GraphNode> nodes;
    std::vector<GraphLink> links;
    int nextId = 1;

    bool empty() const { return nodes.empty(); }
    GraphNode* find(int id);
    const GraphNode* find(int id) const;
    int add(const std::string& type, glm::vec2 pos);   // returns the new node's id
    void remove(int nodeId);                           // and its links
    // Links an output to an input. A value input takes one link, and so
    // does a flow output (a new link replaces the old one); a value output
    // feeds any number of inputs, and a flow input can be reached from
    // several places. False if the pins don't exist in `library`,
    // don't fit together, or it would link a node to itself.
    bool connect(const NodeLibrary& library, const GraphLink& link);
    void disconnect(const GraphLink& link);
    const GraphLink* linkInto(int node, const std::string& pin) const;

    // kke.graph JSON (also embedded in kke.scene, kke/SceneFile.h):
    //   { "name": "Bat", "nodes": [ { "id": 1, "type": "event:Hit", "pos": [0, 0],
    //       "values": { "name": "wood" } } ],
    //     "links": [ { "from": [1, "target"], "to": [2, "thing"] } ] }
    // Unknown node types are kept (a graph from a newer build loses
    // nothing); compileGraph reports them.
    std::string toJson() const;
    // Throws std::runtime_error naming `sourceName` and the problem.
    static NodeGraph fromJson(const std::string& json, const std::string& sourceName = "graph");
};

// ---------------------------------------------------------------- compiling to Lua

struct CompileOptions {
    std::string source;  // the VM script name: hook and timer names start with it
    // Attached to one placed thing: `me` is it, and events about a thing
    // only fire for it.
    uint32_t owner = 0;
    // A palette block's recipe ("Look inside"): events about a thing fire
    // for every thing of this block, with `me` = that thing. For a tool
    // (`toolBlock` true), events carrying "by" fire when the tool did it.
    std::string block;
    bool toolBlock = false;
    // When set ("graph.ran"), every step calls it with its node id as it
    // runs, on the step's own line, so the editor can light up what just
    // happened. Lines are the same with and without it: "Show Lua" shows
    // the version without, errors map with either.
    std::string trace;
};

struct GraphIssue {
    int node = 0;         // 0: the graph as a whole
    bool error = true;    // false: a hint (a node not connected yet)
    std::string message;
};

struct CompiledGraph {
    std::string lua;
    std::vector<int> lineNode;       // per line (index = line - 1): the node that line came from, 0 = none
    std::vector<GraphIssue> issues;
    bool ok() const;                 // no errors (hints allowed)
    // The node behind a Lua error message from this script ("source:12: ..."), 0 if none.
    int nodeForError(const std::string& message, const std::string& source) const;
};

CompiledGraph compileGraph(const NodeGraph& graph, const NodeLibrary& library, const CompileOptions& options);

// A typed value as Lua source ("3" -> 3, "hi" -> "hi", "1 2 3" -> Vec(1, 2, 3)).
// False when `text` isn't a value of `type` (a number that isn't one).
bool valueToLua(const std::string& type, const std::string& text, std::string& lua);

// ---------------------------------------------------------------- recipes

// The graph a Simple palette block is made of ("Look inside"): the bat is
// "when it hits someone -> knock them over, play a wooden bonk"; an animal
// is "when put down -> be a sheep" (ai.add, so the graph needs the ai.*
// nodes: kke::ai::bindAi before NodeLibrary::fromApi). Empty for
// blocks with no behaviour of their own yet (people, props): their recipe
// starts empty and whatever is added applies to every one of them.
NodeGraph playBlockRecipe(const std::string& blockId);

} // namespace kke
