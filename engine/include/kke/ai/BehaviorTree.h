#pragma once

#include <glm/vec3.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace kke::ai {

// Behaviour trees (ENG-F71): the scripted layer on top of utility AI
// (kke/ai/Utility.h). Utility AI picks what an animal wants from continuous
// needs; a behaviour tree runs a plan with steps and phases: a boss that
// opens with a charge, enrages under 30% health and summons adds, a guard
// that patrols, spots, chases and gives up. Both can drive the same orders.
//
// A tree is ticked once per frame. Every node answers Success, Failure or
// Running; a Running node is ticked again next frame, where it left off.
//
//   composites   sequence, selector (fallback), reactive_sequence,
//                reactive_selector (re-check from the start every tick and
//                drop what a stronger branch pre-empts: Unreal's "observer
//                aborts"), parallel, random_selector, random_sequence
//   decorators   inverter, succeeder, failer, repeat, retry, until_fail,
//                cooldown, timeout, delay
//   leaves       action, condition, check (a blackboard compare), set,
//                wait, chance, success, failure, subtree
//
// Nodes talk through a Blackboard (a typed key-value store). Trees are
// built in C++ or from a JSON description (BtFactory), the way scripts and
// data files do. Unit-tested in tests/test_behavior_tree.cpp.

enum class BtStatus : uint8_t { Success, Failure, Running };
const char* btStatusName(BtStatus s);

// What nodes share: numbers, booleans, strings and vectors by name.
class Blackboard {
public:
    using Value = std::variant<bool, double, std::string, glm::vec3>;

    void set(const std::string& key, bool v) { put(key, Value(v)); }
    void set(const std::string& key, int v) { put(key, Value(static_cast<double>(v))); }
    void set(const std::string& key, double v) { put(key, Value(v)); }
    void set(const std::string& key, float v) { put(key, Value(static_cast<double>(v))); }
    void set(const std::string& key, const std::string& v) { put(key, Value(v)); }
    void set(const std::string& key, const char* v) { put(key, Value(std::string(v))); }
    void set(const std::string& key, const glm::vec3& v) { put(key, Value(v)); }
    void put(const std::string& key, Value v);

    bool has(const std::string& key) const { return m_values.count(key) != 0; }
    const Value* find(const std::string& key) const;
    void erase(const std::string& key);
    void clear();

    // Reads convert: a number is true when not 0, a string when not empty;
    // a bool reads as 1 or 0; a missing key (or a type that can't convert)
    // gives the fallback.
    bool getBool(const std::string& key, bool fallback = false) const;
    double getNumber(const std::string& key, double fallback = 0.0) const;
    std::string getString(const std::string& key, const std::string& fallback = {}) const;
    glm::vec3 getVec3(const std::string& key, const glm::vec3& fallback = glm::vec3(0.0f)) const;

    // Counts every change (setting the value a key already holds changes nothing).
    uint64_t version() const { return m_version; }
    std::vector<std::string> keys() const;
    size_t size() const { return m_values.size(); }

    // "5", "true", "hello", "(1, 2, 3)": for logs and the debug text.
    static std::string text(const Value& v);

private:
    std::map<std::string, Value> m_values;
    uint64_t m_version = 0;
};

// Compare a blackboard value to another value. ==, != on everything;
// <, <=, >, >= on numbers (a bool counts as 0 or 1) and strings.
// False for types that can't be compared.
enum class BtOp : uint8_t { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };
bool btOpFromName(const std::string& name, BtOp& out);
const char* btOpName(BtOp op);
bool btCompare(const Blackboard::Value& a, BtOp op, const Blackboard::Value& b);

struct BtContext {
    Blackboard& blackboard;
    float dt = 0.0f;     // seconds since the last tick
    double time = 0.0;   // seconds since the tree started
    std::mt19937& rng;   // the tree's: a seed gives the same run every time
};

class BtNode {
public:
    explicit BtNode(std::string type, std::string name = {}) : m_type(std::move(type)), m_name(std::move(name)) {}
    virtual ~BtNode() = default;
    BtNode(const BtNode&) = delete;
    BtNode& operator=(const BtNode&) = delete;

    BtStatus tick(BtContext& c);
    // Stops what the node is doing (its parent dropped it for something
    // else). A node that wasn't Running is left alone.
    void abort(BtContext& c);

    const std::string& type() const { return m_type; }
    const std::string& name() const { return m_name; }
    void setName(std::string n) { m_name = std::move(n); }
    // Last tick's answer; `visited` is false until the node first runs
    // (and again after an abort).
    BtStatus lastStatus() const { return m_last; }
    bool visited() const { return m_visited; }
    uint64_t ticks() const { return m_ticks; }

    const std::vector<std::unique_ptr<BtNode>>& children() const { return m_children; }
    BtNode* add(std::unique_ptr<BtNode> child);

protected:
    virtual BtStatus onTick(BtContext& c) = 0;
    // Called when a Running node is aborted: stop a child, forget progress.
    virtual void onAbort(BtContext& c);
    std::vector<std::unique_ptr<BtNode>> m_children;

public:
    // The detail shown after the name in describe() ("3/5", "0.4 s").
    virtual std::string detail() const { return {}; }

private:
    std::string m_type, m_name;
    BtStatus m_last = BtStatus::Failure;
    bool m_visited = false;
    uint64_t m_ticks = 0;
};

// An indented dump: type, name, detail and last status per node.
std::string describeNode(const BtNode& n, int depth = 0);

// ---- Building nodes in C++ --------------------------------------------------

using BtChildren = std::vector<std::unique_ptr<BtNode>>;
using BtAction = std::function<BtStatus(BtContext&)>;
using BtCondition = std::function<bool(BtContext&)>;
using BtAbort = std::function<void(BtContext&)>;

std::unique_ptr<BtNode> btSequence(BtChildren c, std::string name = {});
std::unique_ptr<BtNode> btSelector(BtChildren c, std::string name = {});
std::unique_ptr<BtNode> btReactiveSequence(BtChildren c, std::string name = {});
std::unique_ptr<BtNode> btReactiveSelector(BtChildren c, std::string name = {});
// Success when `successNeeded` children succeed (0 = all of them), Failure
// when `failureNeeded` fail (1 = the first one). The rest are aborted.
std::unique_ptr<BtNode> btParallel(BtChildren c, int successNeeded = 0, int failureNeeded = 1, std::string name = {});
// Children in a random order each time the node starts (weights: one per
// child, larger = earlier more often; empty = all equal).
std::unique_ptr<BtNode> btRandomSelector(BtChildren c, std::vector<float> weights = {}, std::string name = {});
std::unique_ptr<BtNode> btRandomSequence(BtChildren c, std::vector<float> weights = {}, std::string name = {});

std::unique_ptr<BtNode> btInverter(std::unique_ptr<BtNode> child);
std::unique_ptr<BtNode> btSucceeder(std::unique_ptr<BtNode> child);
std::unique_ptr<BtNode> btFailer(std::unique_ptr<BtNode> child);
// Runs the child `count` times in a row (0 or less: for ever). A failing
// child ends it with Failure. One iteration per tick.
std::unique_ptr<BtNode> btRepeat(std::unique_ptr<BtNode> child, int count = 0);
// Runs the child until it succeeds, at most `attempts` times (0 or less: for ever).
std::unique_ptr<BtNode> btRetry(std::unique_ptr<BtNode> child, int attempts = 0);
// Runs the child until it fails; then Success.
std::unique_ptr<BtNode> btUntilFail(std::unique_ptr<BtNode> child);
// Fails for `seconds` after the child finishes.
std::unique_ptr<BtNode> btCooldown(std::unique_ptr<BtNode> child, float seconds);
// Aborts the child and fails when it has been Running for `seconds`.
std::unique_ptr<BtNode> btTimeout(std::unique_ptr<BtNode> child, float seconds);
// Waits `seconds` (Running) before the child starts.
std::unique_ptr<BtNode> btDelay(std::unique_ptr<BtNode> child, float seconds);

std::unique_ptr<BtNode> btAction(BtAction fn, std::string name = {}, BtAbort onAbort = {});
std::unique_ptr<BtNode> btCondition(BtCondition fn, std::string name = {});
// key <op> value; or key <op> otherKey when `valueIsKey`. No op: the key's truthiness.
std::unique_ptr<BtNode> btCheck(std::string key, BtOp op, Blackboard::Value value, bool valueIsKey = false);
std::unique_ptr<BtNode> btCheckTruthy(std::string key, bool expected = true);
std::unique_ptr<BtNode> btSet(std::string key, Blackboard::Value value);
std::unique_ptr<BtNode> btWait(float seconds);
std::unique_ptr<BtNode> btChance(float probability);
std::unique_ptr<BtNode> btConstant(BtStatus status);

// ---- The tree ---------------------------------------------------------------

class BehaviorTree {
public:
    explicit BehaviorTree(std::unique_ptr<BtNode> root = nullptr, uint32_t seed = 1);

    // Runs the tree once. An empty tree fails.
    BtStatus tick(float dt);
    // Aborts whatever is running; the next tick starts from the root.
    void reset();
    void setRoot(std::unique_ptr<BtNode> root);

    Blackboard& blackboard() { return m_blackboard; }
    const Blackboard& blackboard() const { return m_blackboard; }
    BtNode* root() { return m_root.get(); }
    const BtNode* root() const { return m_root.get(); }
    BtStatus lastStatus() const { return m_status; }
    double time() const { return m_time; }
    uint64_t ticks() const { return m_ticks; }
    void seed(uint32_t s) { m_rng.seed(s); }
    std::string describe() const;

private:
    std::unique_ptr<BtNode> m_root;
    Blackboard m_blackboard;
    std::mt19937 m_rng;
    double m_time = 0.0;
    uint64_t m_ticks = 0;
    BtStatus m_status = BtStatus::Failure;
};

// ---- From data --------------------------------------------------------------

// Builds trees from JSON, the form scripts and data files use:
//
//   { "type": "selector", "children": [
//       { "type": "sequence", "children": [
//           { "type": "check", "key": "health", "op": "<", "value": 30 },
//           { "type": "action", "name": "enrage" } ] },
//       "melee" ] }
//
// A bare string is an action (or a condition) registered under that name.
// Decorators take one "child" (or a one-item "children"). Fields:
//   sequence, selector, reactive_*, parallel, random_*: children
//        parallel: "success" ("all" or a number), "failure" ("one" or a number)
//        random_*: "weights": [..]
//   inverter, succeeder, failer: child
//   repeat: child, count (0 = for ever) - retry: child, attempts - until_fail: child
//   cooldown, timeout, delay: child, seconds
//   action, condition: name (registered)       subtree: name (registered tree)
//   check: key, op (==, !=, <, <=, >, >=; none = truthy), value or valueKey
//   set: key, value          wait: seconds     chance: probability
//   success, failure
// Any node may carry a "name" for the debug text.
class BtFactory {
public:
    BtFactory();
    ~BtFactory();
    BtFactory(const BtFactory&);
    BtFactory& operator=(const BtFactory&);
    void addAction(const std::string& name, BtAction fn, BtAbort onAbort = {});
    void addCondition(const std::string& name, BtCondition fn);
    // A tree other trees can use as {"type": "subtree", "name": ...}.
    void addTree(const std::string& name, nlohmann::json spec);
    void removeAction(const std::string& name) { m_actions.erase(name); }
    void removeCondition(const std::string& name) { m_conditions.erase(name); }
    void removeTree(const std::string& name);
    bool hasAction(const std::string& name) const { return m_actions.count(name) != 0; }
    bool hasCondition(const std::string& name) const { return m_conditions.count(name) != 0; }

    // nullptr with `error` set ("root.children[1].child: unknown type
    // 'sequnce'") when the description is wrong.
    std::unique_ptr<BtNode> build(const nlohmann::json& spec, std::string& error) const;

    static constexpr int kMaxDepth = 48;   // nesting, subtrees included
    static constexpr size_t kMaxNodes = 4096;

private:
    struct Action { BtAction fn; BtAbort onAbort; };
    std::unique_ptr<BtNode> buildNode(const nlohmann::json& spec, const std::string& path, int depth, size_t& nodes, std::string& error) const;
    std::map<std::string, Action> m_actions;
    std::map<std::string, BtCondition> m_conditions;
    std::map<std::string, nlohmann::json> m_trees;
};

} // namespace kke::ai
