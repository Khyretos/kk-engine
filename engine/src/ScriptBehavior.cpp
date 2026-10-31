#include "kke/ScriptBehavior.h"

#include "kke/ScriptVM.h"

#include <lauxlib.h>
#include <lua.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <functional>

namespace kke {

using nlohmann::json;
using ai::BtContext;
using ai::BtFactory;
using ai::BtStatus;

namespace {

bool isComposite(const std::string& t) {
    return t == "sequence" || t == "selector" || t == "reactive_sequence" || t == "reactive_selector" || t == "parallel" ||
           t == "random_selector" || t == "random_sequence";
}
bool isPlainDecorator(const std::string& t) { return t == "inverter" || t == "succeeder" || t == "failer" || t == "until_fail"; }

bool readValue(lua_State* L, int index, ai::Blackboard::Value& out) {
    switch (lua_type(L, index)) {
    case LUA_TBOOLEAN: out = lua_toboolean(L, index) != 0; return true;
    case LUA_TNUMBER: {
        const double d = lua_tonumber(L, index);
        if (!std::isfinite(d)) return false;
        out = d;
        return true;
    }
    case LUA_TSTRING: out = std::string(lua_tostring(L, index)); return true;
    case LUA_TTABLE: out = ScriptVM::toVec3(L, index); return true;
    default: return false;
    }
}

void pushValue(lua_State* L, const ai::Blackboard::Value& v) {
    if (const bool* b = std::get_if<bool>(&v)) lua_pushboolean(L, *b);
    else if (const double* d = std::get_if<double>(&v)) lua_pushnumber(L, *d);
    else if (const std::string* s = std::get_if<std::string>(&v)) lua_pushlstring(L, s->data(), s->size());
    else ScriptVM::pushVec3(L, std::get<glm::vec3>(v));
}

// The names a node table may carry besides its positional items.
constexpr const char* kNamed[] = { "label", "weights", "success", "failure", "count", "seconds", "attempts",
                                   "probability", "key", "op", "value", "valueKey", "expected" };

} // namespace

ai::BehaviorTree* ScriptBehavior::tree(int id) {
    Tree* t = find(id);
    return t ? &t->tree : nullptr;
}

ScriptBehavior::Tree* ScriptBehavior::find(int id) {
    const auto it = m_trees.find(id);
    return it == m_trees.end() ? nullptr : it->second.get();
}

// A Lua function as a leaf of `factory`: the returned name is what the JSON refers to.
std::string ScriptBehavior::leaf(lua_State* L, int index, bool condition, const std::string& label, BtFactory& factory,
                                 const std::string& source, int abortIndex) {
    const std::string name = "lua#" + std::to_string(m_nextFn++);
    const int ref = m_vm->ref(L, index);
    m_refs[source].push_back(ref);
    if (condition) {
        factory.addCondition(name, [this, ref, label](BtContext& c) { return callLeaf(ref, true, label, c) == BtStatus::Success; });
        return name;
    }
    ai::BtAbort onAbort;
    if (abortIndex != 0) {
        const int abortRef = m_vm->ref(L, abortIndex);
        m_refs[source].push_back(abortRef);
        onAbort = [this, abortRef, label](BtContext& c) {
            // Same call, answer ignored. (Calling it as a leaf would also check the result.)
            Ticking* t = m_ticking;
            if (!t || !t->error.empty()) return;
            lua_State* L2 = t->L;
            lua_rawgeti(L2, LUA_REGISTRYINDEX, abortRef);
            if (!lua_isfunction(L2, -1)) {
                lua_pop(L2, 1);
                return;
            }
            if (Tree* tr = find(t->id)) lua_rawgeti(L2, LUA_REGISTRYINDEX, tr->proxyRef);
            else lua_pushnil(L2);
            lua_pushnumber(L2, static_cast<double>(c.dt));
            if (lua_pcall(L2, 2, 0, 0) != LUA_OK) {
                const char* m = lua_tostring(L2, -1);
                t->error = label + " (on abort): " + (m ? m : "error");
                lua_pop(L2, 1);
            }
        };
    }
    factory.addAction(name, [this, ref, label](BtContext& c) { return callLeaf(ref, false, label, c); }, std::move(onAbort));
    return name;
}

ai::BtStatus ScriptBehavior::callLeaf(int ref, bool condition, const std::string& label, BtContext& c) {
    Ticking* t = m_ticking;
    if (!t || !t->error.empty()) return BtStatus::Failure; // not inside bt.tick, or an earlier leaf already failed this tick
    lua_State* L = t->L;
    if (!lua_checkstack(L, 6)) {
        t->error = label + ": out of Lua stack";
        return BtStatus::Failure;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        t->error = "the function of '" + label + "' is gone (the script that made it was unloaded)";
        return BtStatus::Failure;
    }
    Tree* tr = find(t->id);
    if (tr) lua_rawgeti(L, LUA_REGISTRYINDEX, tr->proxyRef);
    else lua_pushnil(L);
    lua_pushnumber(L, static_cast<double>(c.dt));
    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        const char* m = lua_tostring(L, -1);
        t->error = label + ": " + (m ? m : "error");
        lua_pop(L, 1);
        return BtStatus::Failure;
    }
    BtStatus s = BtStatus::Success;
    if (condition) {
        s = lua_toboolean(L, -1) ? BtStatus::Success : BtStatus::Failure;
    } else {
        switch (lua_type(L, -1)) {
        case LUA_TNIL: break; // a function with no answer ran: success
        case LUA_TBOOLEAN: s = lua_toboolean(L, -1) ? BtStatus::Success : BtStatus::Failure; break;
        case LUA_TSTRING: {
            const std::string a = lua_tostring(L, -1);
            if (a == "success") s = BtStatus::Success;
            else if (a == "failure") s = BtStatus::Failure;
            else if (a == "running") s = BtStatus::Running;
            else t->error = label + ": returned \"" + a + "\" (answer \"success\", \"failure\" or \"running\")";
            break;
        }
        default: t->error = label + ": returned a " + luaL_typename(L, -1) + " (answer \"success\", \"failure\", \"running\", true or false)"; break;
        }
        if (!t->error.empty()) s = BtStatus::Failure;
    }
    lua_pop(L, 1);
    return s;
}

bool ScriptBehavior::convert(lua_State* L, int index, const std::string& path, int depth, BtFactory& factory, json& out,
                             const std::string& source, std::string& error) {
    index = lua_absindex(L, index);
    if (depth > BtFactory::kMaxDepth) {
        error = path + ": nested more than " + std::to_string(BtFactory::kMaxDepth) + " deep (a table that contains itself?)";
        return false;
    }
    // A C function is only promised 20 free stack slots; every level uses a few.
    if (!lua_checkstack(L, 12)) {
        error = path + ": too deep for Lua's stack";
        return false;
    }
    switch (lua_type(L, index)) {
    case LUA_TFUNCTION: {
        out = { { "type", "action" }, { "name", leaf(L, index, false, path, factory, source) } };
        return true;
    }
    case LUA_TSTRING: // the name of a bt.define'd tree
        out = { { "type", "subtree" }, { "name", std::string(lua_tostring(L, index)) } };
        return true;
    case LUA_TTABLE: break;
    default:
        error = path + ": a node is a table like {\"sequence\", ...}, a function or the name of a bt.define'd tree";
        return false;
    }

    lua_rawgeti(L, index, 1);
    const bool typed = lua_type(L, -1) == LUA_TSTRING;
    const std::string type = typed ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    if (!typed) {
        error = path + ": the first item of a node is its type (\"sequence\", \"action\", ...)";
        return false;
    }
    out = json::object();
    out["type"] = type;
    for (const char* key : kNamed) {
        lua_getfield(L, index, key);
        if (!lua_isnil(L, -1)) out[key] = ScriptVM::toJson(L, -1);
        lua_pop(L, 1);
    }
    const int n = static_cast<int>(lua_rawlen(L, index));
    auto at = [&](int i) { // pushes item i
        lua_rawgeti(L, index, i);
        return lua_gettop(L);
    };
    auto numberAt = [&](int i, const char* field) {
        lua_rawgeti(L, index, i);
        const bool isNumber = lua_type(L, -1) == LUA_TNUMBER;
        if (isNumber) out[field] = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return isNumber;
    };
    const std::string label = out.contains("label") && out["label"].is_string() ? out["label"].get<std::string>() : path;

    if (isComposite(type)) {
        json kids = json::array();
        for (int i = 2; i <= n; ++i) {
            json c;
            const int top = at(i);
            const bool ok = convert(L, top, path + ".children[" + std::to_string(i - 2) + "]", depth + 1, factory, c, source, error);
            lua_pop(L, 1);
            if (!ok) return false;
            kids.push_back(std::move(c));
        }
        out["children"] = std::move(kids);
        return true;
    }
    // decorators: {"type", [number], child}
    int childAt = 2;
    if (isPlainDecorator(type)) {
        // nothing before the child
    } else if (type == "repeat" || type == "retry" || type == "cooldown" || type == "timeout" || type == "delay") {
        const char* field = type == "repeat" ? "count" : type == "retry" ? "attempts" : "seconds";
        if (numberAt(2, field)) childAt = 3;
    } else {
        childAt = 0; // a leaf
    }
    if (childAt != 0) {
        const int top = at(childAt);
        json c;
        const bool ok = convert(L, top, path + ".child", depth + 1, factory, c, source, error);
        lua_pop(L, 1);
        if (!ok) return false;
        out["child"] = std::move(c);
        return true;
    }
    if (type == "action" || type == "condition") {
        lua_rawgeti(L, index, 2);
        const bool isFn = lua_isfunction(L, -1);
        if (!isFn) {
            lua_pop(L, 1);
            error = path + ": " + type + " takes a function: {\"" + type + "\", function(bb, dt) ... end}";
            return false;
        }
        const int fnTop = lua_gettop(L);
        int abortTop = 0;
        if (type == "action") {
            lua_rawgeti(L, index, 3);
            if (lua_isfunction(L, -1)) abortTop = lua_gettop(L);
        }
        out["name"] = leaf(L, fnTop, type == "condition", label, factory, source, abortTop);
        lua_settop(L, fnTop - 1);
        return true;
    }
    if (type == "check" || type == "set") {
        lua_rawgeti(L, index, 2);
        if (lua_type(L, -1) == LUA_TSTRING) out["key"] = std::string(lua_tostring(L, -1));
        lua_pop(L, 1);
        if (type == "check") {
            lua_rawgeti(L, index, 3);
            if (lua_type(L, -1) == LUA_TSTRING) out["op"] = std::string(lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_rawgeti(L, index, 4);
            if (!lua_isnil(L, -1)) out["value"] = ScriptVM::toJson(L, -1);
            lua_pop(L, 1);
        } else {
            lua_rawgeti(L, index, 3);
            if (!lua_isnil(L, -1)) out["value"] = ScriptVM::toJson(L, -1);
            lua_pop(L, 1);
        }
        return true;
    }
    if (type == "wait") {
        numberAt(2, "seconds");
        return true;
    }
    if (type == "chance") {
        numberAt(2, "probability");
        return true;
    }
    if (type == "subtree") {
        lua_rawgeti(L, index, 2);
        if (lua_type(L, -1) == LUA_TSTRING) out["name"] = std::string(lua_tostring(L, -1));
        lua_pop(L, 1);
        return true;
    }
    return true; // success, failure, and unknown types (the factory names those)
}

// The blackboard a leaf function sees: bb.hp reads, bb.hp = 5 writes.
void ScriptBehavior::pushProxy(lua_State* L, int id) {
    lua_newtable(L);
    lua_newtable(L); // its metatable
    for (int k = 0; k < 2; ++k) {
        lua_pushlightuserdata(L, this);
        lua_pushinteger(L, id);
        lua_pushcclosure(L, k == 0 ? &ScriptBehavior::proxyIndex : &ScriptBehavior::proxyNewIndex, 2);
        lua_setfield(L, -2, k == 0 ? "__index" : "__newindex");
    }
    lua_pushliteral(L, "locked");
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);
}

int ScriptBehavior::proxyIndex(lua_State* L) {
    auto* self = static_cast<ScriptBehavior*>(lua_touserdata(L, lua_upvalueindex(1)));
    Tree* t = self->find(static_cast<int>(lua_tointeger(L, lua_upvalueindex(2))));
    if (!t) return luaL_error(L, "this behaviour tree no longer exists");
    if (lua_type(L, 2) != LUA_TSTRING) {
        lua_pushnil(L);
        return 1;
    }
    const ai::Blackboard::Value* v = t->tree.blackboard().find(lua_tostring(L, 2));
    if (!v) lua_pushnil(L);
    else pushValue(L, *v);
    return 1;
}

int ScriptBehavior::proxyNewIndex(lua_State* L) {
    auto* self = static_cast<ScriptBehavior*>(lua_touserdata(L, lua_upvalueindex(1)));
    Tree* t = self->find(static_cast<int>(lua_tointeger(L, lua_upvalueindex(2))));
    if (!t) return luaL_error(L, "this behaviour tree no longer exists");
    if (lua_type(L, 2) != LUA_TSTRING) return luaL_error(L, "blackboard keys are strings");
    const std::string key = lua_tostring(L, 2);
    if (lua_isnil(L, 3)) {
        t->tree.blackboard().erase(key);
        return 0;
    }
    ai::Blackboard::Value v;
    if (!readValue(L, 3, v)) return luaL_error(L, "blackboard.%s: a value is a number, boolean, string or Vec(x, y, z), not a %s", key.c_str(), luaL_typename(L, 3));
    t->tree.blackboard().put(key, std::move(v));
    return 0;
}

void ScriptBehavior::release(const std::string& source) {
    for (auto it = m_trees.begin(); it != m_trees.end();) {
        if (it->second->source == source) {
            if (m_vm) m_vm->unref(it->second->proxyRef);
            it = m_trees.erase(it);
        } else {
            ++it;
        }
    }
    if (const auto d = m_defNames.find(source); d != m_defNames.end()) {
        for (const DefName& n : d->second) {
            if (n.tree) m_defs.removeTree(n.name);
            else {
                m_defs.removeAction(n.name);
                m_defs.removeCondition(n.name);
            }
        }
        m_defNames.erase(d);
    }
    if (const auto r = m_refs.find(source); r != m_refs.end()) {
        if (m_vm)
            for (int ref : r->second) m_vm->unref(ref);
        m_refs.erase(r);
    }
}

void ScriptBehavior::bind(ScriptVM& vm) {
    m_vm = &vm;
    auto treeArg = [this](lua_State* L, int idx, const char* what) -> Tree& {
        const int id = static_cast<int>(luaL_checkinteger(L, idx));
        Tree* t = find(id);
        if (!t) luaL_error(L, "%s: no behaviour tree %d (destroyed, or its script was reloaded)", what, id);
        return *t;
    };

    // bt.create(node, {seed = 1}) -> tree
    vm.registerFunction("bt", "create", [this, &vm](lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        if (m_trees.size() >= kMaxTrees) luaL_error(L, "bt.create: %d trees already (bt.destroy the ones you don't need)", static_cast<int>(kMaxTrees));
        const std::string source = vm.currentSource();
        BtFactory factory = m_defs;
        json spec;
        std::string error;
        const size_t refsBefore = m_refs[source].size();
        const bool converted = convert(L, 1, "root", 0, factory, spec, source, error);
        std::unique_ptr<ai::BtNode> root = converted ? factory.build(spec, error) : nullptr;
        if (!root) {
            // The functions this attempt kept alive are not needed.
            auto& refs = m_refs[source];
            for (size_t i = refsBefore; i < refs.size(); ++i) vm.unref(refs[i]);
            refs.resize(refsBefore);
            luaL_error(L, "bt.create: %s", error.c_str());
        }
        uint32_t seed = 1;
        if (lua_istable(L, 2)) seed = static_cast<uint32_t>(ScriptVM::fieldNumber(L, 2, "seed", 1.0f));
        auto t = std::make_unique<Tree>(Tree{ ai::BehaviorTree(std::move(root), seed), source, 0, false });
        const int id = m_nextId++;
        pushProxy(L, id);
        t->proxyRef = luaL_ref(L, LUA_REGISTRYINDEX);
        m_trees[id] = std::move(t);
        lua_pushinteger(L, id);
        return 1;
    });

    // bt.define(name, node): a tree other trees use as "name" or {"subtree", "name"}
    vm.registerFunction("bt", "define", [this, &vm](lua_State* L) {
        const std::string name = luaL_checkstring(L, 1);
        luaL_checktype(L, 2, LUA_TTABLE);
        const std::string source = vm.currentSource();
        json spec;
        std::string error;
        BtFactory scratch = m_defs; // functions go in `scratch`; kept only when the tree is good
        const size_t refsBefore = m_refs[source].size();
        if (!convert(L, 2, name, 0, scratch, spec, source, error)) {
            auto& refs = m_refs[source];
            for (size_t i = refsBefore; i < refs.size(); ++i) vm.unref(refs[i]);
            refs.resize(refsBefore);
            luaL_error(L, "bt.define: %s", error.c_str());
        }
        std::string buildError;
        if (!scratch.build(spec, buildError)) {
            auto& refs = m_refs[source];
            for (size_t i = refsBefore; i < refs.size(); ++i) vm.unref(refs[i]);
            refs.resize(refsBefore);
            luaL_error(L, "bt.define: %s", buildError.c_str());
        }
        // Adopt the new actions and conditions: every name this tree uses starts with "lua#".
        std::vector<std::string> used;
        std::function<void(const json&)> walk = [&](const json& j) {
            if (j.is_object()) {
                const auto n = j.find("name");
                if (n != j.end() && n->is_string() && n->get<std::string>().rfind("lua#", 0) == 0) used.push_back(n->get<std::string>());
                for (const auto& [k, v] : j.items()) walk(v);
            } else if (j.is_array()) {
                for (const json& v : j) walk(v);
            }
        };
        walk(spec);
        for (const std::string& n : used) m_defNames[source].push_back({ false, n });
        m_defs = scratch;
        m_defs.addTree(name, spec);
        m_defNames[source].push_back({ true, name });
        return 0;
    });

    vm.registerFunction("bt", "tick", [this, treeArg](lua_State* L) {
        Tree& t = treeArg(L, 1, "bt.tick");
        const double dt = luaL_checknumber(L, 2);
        if (!std::isfinite(dt)) luaL_error(L, "bt.tick: the time step must be a number");
        if (t.ticking) luaL_error(L, "bt.tick: this tree is already ticking (a leaf function called bt.tick on its own tree)");
        const int id = static_cast<int>(lua_tointeger(L, 1));
        Ticking tk;
        tk.L = L;
        tk.id = id;
        Ticking* previous = m_ticking;
        m_ticking = &tk;
        t.ticking = true;
        const BtStatus s = t.tree.tick(static_cast<float>(dt));
        if (Tree* again = find(id)) again->ticking = false;
        m_ticking = previous;
        if (!tk.error.empty()) luaL_error(L, "bt.tick: %s", tk.error.c_str());
        lua_pushstring(L, ai::btStatusName(s));
        return 1;
    });

    vm.registerFunction("bt", "reset", [this, treeArg](lua_State* L) {
        Tree& t = treeArg(L, 1, "bt.reset");
        if (t.ticking) luaL_error(L, "bt.reset: this tree is ticking");
        Ticking tk; // an abort callback may run
        tk.L = L;
        tk.id = static_cast<int>(lua_tointeger(L, 1));
        Ticking* previous = m_ticking;
        m_ticking = &tk;
        t.tree.reset();
        m_ticking = previous;
        if (!tk.error.empty()) luaL_error(L, "bt.reset: %s", tk.error.c_str());
        return 0;
    });

    vm.registerFunction("bt", "destroy", [this, &vm, treeArg](lua_State* L) {
        Tree& t = treeArg(L, 1, "bt.destroy");
        if (t.ticking) luaL_error(L, "bt.destroy: this tree is ticking");
        const int id = static_cast<int>(lua_tointeger(L, 1));
        vm.unref(t.proxyRef);
        m_trees.erase(id);
        return 0;
    });

    vm.registerFunction("bt", "exists", [this](lua_State* L) {
        lua_pushboolean(L, find(static_cast<int>(luaL_checkinteger(L, 1))) != nullptr);
        return 1;
    });

    vm.registerFunction("bt", "count", [this](lua_State* L) {
        lua_pushinteger(L, static_cast<lua_Integer>(m_trees.size()));
        return 1;
    });

    vm.registerFunction("bt", "set", [treeArg](lua_State* L) {
        Tree& t = treeArg(L, 1, "bt.set");
        const std::string key = luaL_checkstring(L, 2);
        if (lua_isnil(L, 3)) {
            t.tree.blackboard().erase(key);
            return 0;
        }
        ai::Blackboard::Value v;
        if (!readValue(L, 3, v)) luaL_error(L, "bt.set: a value is a number, boolean, string or Vec(x, y, z)");
        t.tree.blackboard().put(key, std::move(v));
        return 0;
    });

    vm.registerFunction("bt", "get", [treeArg](lua_State* L) {
        Tree& t = treeArg(L, 1, "bt.get");
        const ai::Blackboard::Value* v = t.tree.blackboard().find(luaL_checkstring(L, 2));
        if (!v) lua_pushnil(L);
        else pushValue(L, *v);
        return 1;
    });

    vm.registerFunction("bt", "status", [treeArg](lua_State* L) {
        Tree& t = treeArg(L, 1, "bt.status");
        lua_pushstring(L, ai::btStatusName(t.tree.lastStatus()));
        return 1;
    });

    vm.registerFunction("bt", "describe", [treeArg](lua_State* L) {
        Tree& t = treeArg(L, 1, "bt.describe");
        const std::string text = t.tree.describe();
        lua_pushlstring(L, text.data(), text.size());
        return 1;
    });
}

} // namespace kke
