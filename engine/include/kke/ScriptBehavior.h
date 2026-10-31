#pragma once
// ScriptBehavior: the behaviour trees Lua scripts run with bt.* (docs/AI.md "Behaviour trees").
#include "kke/ai/BehaviorTree.h"

#include <nlohmann/json_fwd.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

struct lua_State;

namespace kke {

class ScriptVM;

// bt.create{...} builds a tree from a Lua table whose leaves are Lua
// functions; bt.tick(tree, dt) runs it. The leaf functions are called on
// the calling script's own Lua state (its instruction budget, its errors),
// as function(bb, dt): `bb` reads and writes the tree's blackboard like a
// table (bb.hp = 40; if bb.target then ...).
//
//   local boss = bt.create{ "reactive_selector",
//       { "reactive_sequence", { "check", "hp", "<", 30 }, { "action", function(bb) bb.rage = true return "running" end } },
//       { "sequence", { "condition", function(bb) return bb.playerNear end }, { "action", charge } } }
//   hook.Add("Think", "boss", function() bt.tick(boss, time.delta) end)
//
// A leaf answers "success", "failure" or "running" (true and false work
// too; nothing at all counts as success). Trees live as long as the
// script that made them.
class ScriptBehavior {
public:
    void bind(ScriptVM& vm);
    // Frees the trees, the subtree definitions and the Lua functions `source` made.
    void release(const std::string& source);

    size_t treeCount() const { return m_trees.size(); }
    ai::BehaviorTree* tree(int id);

    static constexpr size_t kMaxTrees = 512;

private:
    struct Tree {
        ai::BehaviorTree tree;
        std::string source;
        int proxyRef = 0; // the bb table scripts see
        bool ticking = false;
    };
    // What a leaf needs while bt.tick runs: the caller's Lua state and the tree.
    struct Ticking {
        lua_State* L = nullptr;
        int id = 0;
        std::string error; // the first leaf error: raised to the caller after the tick
    };

    // A Lua node table as the JSON BtFactory builds from; the functions in
    // it become actions and conditions of `factory`. False with `error`.
    bool convert(lua_State* L, int index, const std::string& path, int depth, ai::BtFactory& factory, nlohmann::json& out,
                 const std::string& source, std::string& error);
    std::string leaf(lua_State* L, int index, bool condition, const std::string& label, ai::BtFactory& factory, const std::string& source,
                     int abortIndex = 0);
    ai::BtStatus callLeaf(int ref, bool condition, const std::string& label, ai::BtContext& c);
    void pushProxy(lua_State* L, int id);
    static int proxyIndex(lua_State* L);
    static int proxyNewIndex(lua_State* L);
    Tree* find(int id);

    ScriptVM* m_vm = nullptr;
    std::map<int, std::unique_ptr<Tree>> m_trees;
    ai::BtFactory m_defs;                         // bt.define'd trees (and the functions they use)
    std::map<std::string, std::vector<int>> m_refs;       // Lua function refs, by script
    struct DefName { bool tree; std::string name; };
    std::map<std::string, std::vector<DefName>> m_defNames; // what bt.define added to m_defs, by script
    Ticking* m_ticking = nullptr;
    int m_nextId = 1;
    int m_nextFn = 1;
};

} // namespace kke
