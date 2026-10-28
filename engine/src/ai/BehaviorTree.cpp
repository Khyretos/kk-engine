#include "kke/ai/BehaviorTree.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <utility>

namespace kke::ai {

const char* btStatusName(BtStatus s) {
    switch (s) {
    case BtStatus::Success: return "success";
    case BtStatus::Failure: return "failure";
    case BtStatus::Running: return "running";
    }
    return "failure";
}

// ---- Blackboard -------------------------------------------------------------

void Blackboard::put(const std::string& key, Value v) {
    auto it = m_values.find(key);
    if (it == m_values.end()) {
        m_values.emplace(key, std::move(v));
        ++m_version;
    } else if (!(it->second == v)) {
        it->second = std::move(v);
        ++m_version;
    }
}

const Blackboard::Value* Blackboard::find(const std::string& key) const {
    const auto it = m_values.find(key);
    return it == m_values.end() ? nullptr : &it->second;
}

void Blackboard::erase(const std::string& key) {
    if (m_values.erase(key) != 0) ++m_version;
}

void Blackboard::clear() {
    if (!m_values.empty()) ++m_version;
    m_values.clear();
}

bool Blackboard::getBool(const std::string& key, bool fallback) const {
    const Value* v = find(key);
    if (!v) return fallback;
    if (const bool* b = std::get_if<bool>(v)) return *b;
    if (const double* d = std::get_if<double>(v)) return *d != 0.0;
    if (const std::string* s = std::get_if<std::string>(v)) return !s->empty();
    return fallback;
}

double Blackboard::getNumber(const std::string& key, double fallback) const {
    const Value* v = find(key);
    if (!v) return fallback;
    if (const double* d = std::get_if<double>(v)) return *d;
    if (const bool* b = std::get_if<bool>(v)) return *b ? 1.0 : 0.0;
    return fallback;
}

std::string Blackboard::getString(const std::string& key, const std::string& fallback) const {
    const Value* v = find(key);
    if (!v) return fallback;
    return text(*v);
}

glm::vec3 Blackboard::getVec3(const std::string& key, const glm::vec3& fallback) const {
    const Value* v = find(key);
    if (const glm::vec3* p = v ? std::get_if<glm::vec3>(v) : nullptr) return *p;
    return fallback;
}

std::vector<std::string> Blackboard::keys() const {
    std::vector<std::string> out;
    out.reserve(m_values.size());
    for (const auto& [k, v] : m_values) out.push_back(k);
    return out;
}

std::string Blackboard::text(const Value& v) {
    char buf[96];
    if (const bool* b = std::get_if<bool>(&v)) return *b ? "true" : "false";
    if (const double* d = std::get_if<double>(&v)) {
        std::snprintf(buf, sizeof buf, "%g", *d);
        return buf;
    }
    if (const std::string* s = std::get_if<std::string>(&v)) return *s;
    const glm::vec3& p = std::get<glm::vec3>(v);
    std::snprintf(buf, sizeof buf, "(%g, %g, %g)", static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z));
    return buf;
}

bool btOpFromName(const std::string& name, BtOp& out) {
    static const std::pair<const char*, BtOp> table[] = {
        { "==", BtOp::Equal }, { "=", BtOp::Equal },    { "!=", BtOp::NotEqual }, { "~=", BtOp::NotEqual }, { "<", BtOp::Less },
        { "<=", BtOp::LessEqual }, { ">", BtOp::Greater }, { ">=", BtOp::GreaterEqual },
    };
    for (const auto& [n, op] : table)
        if (name == n) {
            out = op;
            return true;
        }
    return false;
}

const char* btOpName(BtOp op) {
    switch (op) {
    case BtOp::Equal: return "==";
    case BtOp::NotEqual: return "!=";
    case BtOp::Less: return "<";
    case BtOp::LessEqual: return "<=";
    case BtOp::Greater: return ">";
    case BtOp::GreaterEqual: return ">=";
    }
    return "==";
}

namespace {
bool numeric(const Blackboard::Value& v, double& out) {
    if (const double* d = std::get_if<double>(&v)) {
        out = *d;
        return true;
    }
    if (const bool* b = std::get_if<bool>(&v)) {
        out = *b ? 1.0 : 0.0;
        return true;
    }
    return false;
}

template <typename T>
bool order(const T& a, BtOp op, const T& b) {
    switch (op) {
    case BtOp::Equal: return a == b;
    case BtOp::NotEqual: return !(a == b);
    case BtOp::Less: return a < b;
    case BtOp::LessEqual: return !(b < a);
    case BtOp::Greater: return b < a;
    case BtOp::GreaterEqual: return !(a < b);
    }
    return false;
}

// 0..1 from the generator, the same on every platform (the standard
// distributions are not).
float unit(std::mt19937& rng) { return static_cast<float>(rng() >> 8) * (1.0f / 16777216.0f); }
} // namespace

bool btCompare(const Blackboard::Value& a, BtOp op, const Blackboard::Value& b) {
    double x = 0.0, y = 0.0;
    if (numeric(a, x) && numeric(b, y)) return order(x, op, y);
    if (const auto* s = std::get_if<std::string>(&a))
        if (const auto* t = std::get_if<std::string>(&b)) return order(*s, op, *t);
    if (const auto* p = std::get_if<glm::vec3>(&a))
        if (const auto* q = std::get_if<glm::vec3>(&b)) {
            if (op == BtOp::Equal) return *p == *q;
            if (op == BtOp::NotEqual) return !(*p == *q);
            return false;
        }
    return op == BtOp::NotEqual; // different kinds are never equal
}

// ---- Nodes ------------------------------------------------------------------

BtStatus BtNode::tick(BtContext& c) {
    m_visited = true;
    ++m_ticks;
    m_last = onTick(c);
    return m_last;
}

void BtNode::abort(BtContext& c) {
    if (m_visited && m_last == BtStatus::Running) onAbort(c);
    m_visited = false;
    m_last = BtStatus::Failure;
}

void BtNode::onAbort(BtContext& c) {
    for (auto& child : m_children) child->abort(c);
}

BtNode* BtNode::add(std::unique_ptr<BtNode> child) {
    m_children.push_back(std::move(child));
    return m_children.back().get();
}

std::string describeNode(const BtNode& n, int depth) {
    std::string out(static_cast<size_t>(depth) * 2, ' ');
    out += n.type();
    if (!n.name().empty() && n.name() != n.type()) out += " \"" + n.name() + "\"";
    const std::string d = n.detail();
    if (!d.empty()) out += " " + d;
    out += n.visited() ? std::string(" [") + btStatusName(n.lastStatus()) + "]" : " [idle]";
    out += "\n";
    for (const auto& child : n.children()) out += describeNode(*child, depth + 1);
    return out;
}

namespace {

BtNode& only(BtNode& n) { return *n.children().front(); } // decorators have exactly one child

// ---- composites ----

// sequence: every child must succeed; selector: the first to succeed wins.
// Remembers the running child and carries on there next tick.
class Memory : public BtNode {
public:
    Memory(const char* type, bool selector, BtChildren c, std::string name) : BtNode(type, std::move(name)), m_selector(selector) {
        m_children = std::move(c);
    }
    std::string detail() const override { return std::to_string(m_i + 1 > m_children.size() ? m_children.size() : m_i + 1) + "/" + std::to_string(m_children.size()); }

protected:
    BtStatus onTick(BtContext& c) override {
        for (size_t i = m_i; i < m_children.size(); ++i) {
            const BtStatus s = m_children[i]->tick(c);
            if (s == BtStatus::Running) {
                m_i = i;
                return BtStatus::Running;
            }
            if (s == (m_selector ? BtStatus::Success : BtStatus::Failure)) {
                m_i = 0;
                return s;
            }
        }
        m_i = 0;
        return m_selector ? BtStatus::Failure : BtStatus::Success;
    }
    void onAbort(BtContext& c) override {
        BtNode::onAbort(c);
        m_i = 0;
    }

private:
    bool m_selector;
    size_t m_i = 0;
};

// The same without memory: every tick starts at the first child, so a
// condition up front is re-checked all the time and a stronger branch
// pre-empts what was running (which is aborted).
class Reactive : public BtNode {
public:
    Reactive(const char* type, bool selector, BtChildren c, std::string name) : BtNode(type, std::move(name)), m_selector(selector) {
        m_children = std::move(c);
    }

protected:
    BtStatus onTick(BtContext& c) override {
        const int old = m_running;
        m_running = -1;
        BtStatus result = m_selector ? BtStatus::Failure : BtStatus::Success;
        for (size_t i = 0; i < m_children.size(); ++i) {
            const BtStatus s = m_children[i]->tick(c);
            if (s == BtStatus::Running) {
                if (old >= 0 && old != static_cast<int>(i)) m_children[static_cast<size_t>(old)]->abort(c);
                m_running = static_cast<int>(i);
                return BtStatus::Running;
            }
            if (s == (m_selector ? BtStatus::Success : BtStatus::Failure)) {
                result = s;
                break;
            }
        }
        if (old >= 0) m_children[static_cast<size_t>(old)]->abort(c);
        return result;
    }
    void onAbort(BtContext& c) override {
        BtNode::onAbort(c);
        m_running = -1;
    }

private:
    bool m_selector;
    int m_running = -1;
};

class Parallel : public BtNode {
public:
    Parallel(BtChildren c, int successNeeded, int failureNeeded, std::string name)
        : BtNode("parallel", std::move(name)), m_success(successNeeded), m_failure(failureNeeded) {
        m_children = std::move(c);
    }
    std::string detail() const override {
        size_t done = 0;
        for (uint8_t d : m_done) done += d != 0;
        return std::to_string(done) + "/" + std::to_string(m_children.size()) + " done";
    }

protected:
    BtStatus onTick(BtContext& c) override {
        const size_t n = m_children.size();
        if (n == 0) return BtStatus::Success;
        m_done.resize(n, 0);
        const size_t needS = m_success <= 0 ? n : std::min(static_cast<size_t>(m_success), n);
        const size_t needF = m_failure <= 0 ? n : std::min(static_cast<size_t>(m_failure), n);
        size_t ok = 0, bad = 0;
        for (size_t i = 0; i < n; ++i) {
            if (m_done[i] == 0) {
                const BtStatus s = m_children[i]->tick(c);
                if (s == BtStatus::Success) m_done[i] = 1;
                else if (s == BtStatus::Failure) m_done[i] = 2;
            }
            ok += m_done[i] == 1;
            bad += m_done[i] == 2;
        }
        BtStatus result = BtStatus::Running;
        if (bad >= needF) result = BtStatus::Failure;
        else if (ok >= needS) result = BtStatus::Success;
        else if (ok + bad == n) result = BtStatus::Failure; // all finished and neither goal reached
        if (result != BtStatus::Running) finish(c);
        return result;
    }
    void onAbort(BtContext& c) override { finish(c); }

private:
    void finish(BtContext& c) {
        for (auto& child : m_children) child->abort(c);
        m_done.assign(m_children.size(), 0);
    }
    int m_success, m_failure;
    std::vector<uint8_t> m_done; // per child: 0 running, 1 succeeded, 2 failed
};

// A weighted shuffle of the children each time the node starts, then a
// selector or sequence over that order.
class RandomPick : public BtNode {
public:
    RandomPick(const char* type, bool selector, BtChildren c, std::vector<float> weights, std::string name)
        : BtNode(type, std::move(name)), m_selector(selector), m_weights(std::move(weights)) {
        m_children = std::move(c);
    }

protected:
    BtStatus onTick(BtContext& c) override {
        if (m_order.empty()) shuffle(c.rng);
        for (size_t k = m_at; k < m_order.size(); ++k) {
            const BtStatus s = m_children[m_order[k]]->tick(c);
            if (s == BtStatus::Running) {
                m_at = k;
                return BtStatus::Running;
            }
            if (s == (m_selector ? BtStatus::Success : BtStatus::Failure)) {
                forget();
                return s;
            }
        }
        forget();
        return m_selector ? BtStatus::Failure : BtStatus::Success;
    }
    void onAbort(BtContext& c) override {
        BtNode::onAbort(c);
        forget();
    }

private:
    void forget() {
        m_order.clear();
        m_at = 0;
    }
    void shuffle(std::mt19937& rng) {
        std::vector<size_t> pool(m_children.size());
        std::iota(pool.begin(), pool.end(), size_t{ 0 });
        auto weight = [&](size_t i) { return i < m_weights.size() ? std::max(m_weights[i], 1e-4f) : 1.0f; };
        while (!pool.empty()) {
            float total = 0.0f;
            for (size_t i : pool) total += weight(i);
            float r = unit(rng) * total;
            size_t pick = pool.size() - 1;
            for (size_t k = 0; k < pool.size(); ++k) {
                r -= weight(pool[k]);
                if (r < 0.0f) {
                    pick = k;
                    break;
                }
            }
            m_order.push_back(pool[pick]);
            pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(pick));
        }
    }
    bool m_selector;
    std::vector<float> m_weights;
    std::vector<size_t> m_order;
    size_t m_at = 0;
};

// ---- decorators ----

class Decorator : public BtNode {
public:
    Decorator(const char* type, std::unique_ptr<BtNode> child) : BtNode(type) { m_children.push_back(std::move(child)); }
};

class Inverter : public Decorator {
public:
    explicit Inverter(std::unique_ptr<BtNode> child) : Decorator("inverter", std::move(child)) {}

protected:
    BtStatus onTick(BtContext& c) override {
        const BtStatus s = only(*this).tick(c);
        if (s == BtStatus::Success) return BtStatus::Failure;
        if (s == BtStatus::Failure) return BtStatus::Success;
        return s;
    }
};

class Force : public Decorator {
public:
    Force(const char* type, BtStatus to, std::unique_ptr<BtNode> child) : Decorator(type, std::move(child)), m_to(to) {}

protected:
    BtStatus onTick(BtContext& c) override {
        const BtStatus s = only(*this).tick(c);
        return s == BtStatus::Running ? s : m_to;
    }

private:
    BtStatus m_to;
};

class Repeat : public Decorator {
public:
    Repeat(std::unique_ptr<BtNode> child, int count) : Decorator("repeat", std::move(child)), m_count(count) {}
    std::string detail() const override { return std::to_string(m_n) + (m_count > 0 ? "/" + std::to_string(m_count) : std::string("/inf")); }

protected:
    BtStatus onTick(BtContext& c) override {
        const BtStatus s = only(*this).tick(c);
        if (s == BtStatus::Running) return s;
        if (s == BtStatus::Failure) {
            m_n = 0;
            return s;
        }
        ++m_n;
        if (m_count > 0 && m_n >= m_count) {
            m_n = 0;
            return BtStatus::Success;
        }
        return BtStatus::Running;
    }
    void onAbort(BtContext& c) override {
        Decorator::onAbort(c);
        m_n = 0;
    }

private:
    int m_count, m_n = 0;
};

class Retry : public Decorator {
public:
    Retry(std::unique_ptr<BtNode> child, int attempts) : Decorator("retry", std::move(child)), m_attempts(attempts) {}
    std::string detail() const override { return std::to_string(m_n) + (m_attempts > 0 ? "/" + std::to_string(m_attempts) : std::string("/inf")) + " tries"; }

protected:
    BtStatus onTick(BtContext& c) override {
        const BtStatus s = only(*this).tick(c);
        if (s == BtStatus::Running) return s;
        if (s == BtStatus::Success) {
            m_n = 0;
            return s;
        }
        ++m_n;
        if (m_attempts > 0 && m_n >= m_attempts) {
            m_n = 0;
            return BtStatus::Failure;
        }
        return BtStatus::Running;
    }
    void onAbort(BtContext& c) override {
        Decorator::onAbort(c);
        m_n = 0;
    }

private:
    int m_attempts, m_n = 0;
};

class UntilFail : public Decorator {
public:
    explicit UntilFail(std::unique_ptr<BtNode> child) : Decorator("until_fail", std::move(child)) {}

protected:
    BtStatus onTick(BtContext& c) override {
        return only(*this).tick(c) == BtStatus::Failure ? BtStatus::Success : BtStatus::Running;
    }
};

class Cooldown : public Decorator {
public:
    Cooldown(std::unique_ptr<BtNode> child, float seconds) : Decorator("cooldown", std::move(child)), m_seconds(seconds) {}
    std::string detail() const override {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.2f s", static_cast<double>(m_seconds));
        return buf;
    }

protected:
    BtStatus onTick(BtContext& c) override {
        if (c.time < m_readyAt) return BtStatus::Failure;
        const BtStatus s = only(*this).tick(c);
        if (s != BtStatus::Running) m_readyAt = c.time + static_cast<double>(m_seconds);
        return s;
    }
    void onAbort(BtContext& c) override {
        Decorator::onAbort(c);
        m_readyAt = c.time + static_cast<double>(m_seconds);
    }

private:
    float m_seconds;
    double m_readyAt = 0.0;
};

class Timeout : public Decorator {
public:
    Timeout(std::unique_ptr<BtNode> child, float seconds) : Decorator("timeout", std::move(child)), m_seconds(seconds) {}

protected:
    BtStatus onTick(BtContext& c) override {
        if (m_active) m_elapsed += c.dt;
        const BtStatus s = only(*this).tick(c);
        if (s != BtStatus::Running) {
            m_active = false;
            m_elapsed = 0.0f;
            return s;
        }
        m_active = true;
        if (m_elapsed >= m_seconds) {
            only(*this).abort(c);
            m_active = false;
            m_elapsed = 0.0f;
            return BtStatus::Failure;
        }
        return BtStatus::Running;
    }
    void onAbort(BtContext& c) override {
        Decorator::onAbort(c);
        m_active = false;
        m_elapsed = 0.0f;
    }

private:
    float m_seconds, m_elapsed = 0.0f;
    bool m_active = false;
};

class Delay : public Decorator {
public:
    Delay(std::unique_ptr<BtNode> child, float seconds) : Decorator("delay", std::move(child)), m_seconds(seconds) {}

protected:
    BtStatus onTick(BtContext& c) override {
        if (m_waited < m_seconds) {
            m_waited += c.dt;
            if (m_waited < m_seconds) return BtStatus::Running;
        }
        const BtStatus s = only(*this).tick(c);
        if (s != BtStatus::Running) m_waited = 0.0f;
        return s;
    }
    void onAbort(BtContext& c) override {
        Decorator::onAbort(c);
        m_waited = 0.0f;
    }

private:
    float m_seconds, m_waited = 0.0f;
};

// ---- leaves ----

class Action : public BtNode {
public:
    Action(BtAction fn, std::string name, BtAbort onAbort) : BtNode("action", std::move(name)), m_fn(std::move(fn)), m_abort(std::move(onAbort)) {}

protected:
    BtStatus onTick(BtContext& c) override { return m_fn ? m_fn(c) : BtStatus::Failure; }
    void onAbort(BtContext& c) override {
        if (m_abort) m_abort(c);
    }

private:
    BtAction m_fn;
    BtAbort m_abort;
};

class Condition : public BtNode {
public:
    Condition(BtCondition fn, std::string name) : BtNode("condition", std::move(name)), m_fn(std::move(fn)) {}

protected:
    BtStatus onTick(BtContext& c) override { return m_fn && m_fn(c) ? BtStatus::Success : BtStatus::Failure; }

private:
    BtCondition m_fn;
};

class Check : public BtNode {
public:
    Check(std::string key, BtOp op, Blackboard::Value value, bool valueIsKey, bool truthy, bool expected)
        : BtNode("check"), m_key(std::move(key)), m_op(op), m_value(std::move(value)), m_valueIsKey(valueIsKey), m_truthy(truthy), m_expected(expected) {
        setName(m_truthy ? (m_expected ? m_key : "not " + m_key) : m_key + " " + btOpName(m_op) + " " + Blackboard::text(m_value));
    }

protected:
    BtStatus onTick(BtContext& c) override {
        if (m_truthy) return c.blackboard.getBool(m_key, false) == m_expected ? BtStatus::Success : BtStatus::Failure;
        const Blackboard::Value* lhs = c.blackboard.find(m_key);
        if (!lhs) return BtStatus::Failure;
        const Blackboard::Value* rhs = &m_value;
        if (m_valueIsKey) {
            rhs = c.blackboard.find(std::get<std::string>(m_value));
            if (!rhs) return BtStatus::Failure;
        }
        return btCompare(*lhs, m_op, *rhs) ? BtStatus::Success : BtStatus::Failure;
    }

private:
    std::string m_key;
    BtOp m_op;
    Blackboard::Value m_value;
    bool m_valueIsKey, m_truthy, m_expected;
};

class Set : public BtNode {
public:
    Set(std::string key, Blackboard::Value value) : BtNode("set", key + " = " + Blackboard::text(value)), m_key(std::move(key)), m_value(std::move(value)) {}

protected:
    BtStatus onTick(BtContext& c) override {
        c.blackboard.put(m_key, m_value);
        return BtStatus::Success;
    }

private:
    std::string m_key;
    Blackboard::Value m_value;
};

class Wait : public BtNode {
public:
    explicit Wait(float seconds) : BtNode("wait"), m_seconds(seconds) {}
    std::string detail() const override {
        char buf[48];
        std::snprintf(buf, sizeof buf, "%.2f/%.2f s", static_cast<double>(m_elapsed), static_cast<double>(m_seconds));
        return buf;
    }

protected:
    BtStatus onTick(BtContext& c) override {
        m_elapsed += c.dt;
        if (m_elapsed >= m_seconds) {
            m_elapsed = 0.0f;
            return BtStatus::Success;
        }
        return BtStatus::Running;
    }
    void onAbort(BtContext&) override { m_elapsed = 0.0f; }

private:
    float m_seconds, m_elapsed = 0.0f;
};

class Chance : public BtNode {
public:
    explicit Chance(float p) : BtNode("chance"), m_p(p) {}
    std::string detail() const override {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g%%", static_cast<double>(m_p * 100.0f));
        return buf;
    }

protected:
    BtStatus onTick(BtContext& c) override { return unit(c.rng) < m_p ? BtStatus::Success : BtStatus::Failure; }

private:
    float m_p;
};

class Constant : public BtNode {
public:
    explicit Constant(BtStatus s) : BtNode(btStatusName(s)), m_s(s) {}

protected:
    BtStatus onTick(BtContext&) override { return m_s; }

private:
    BtStatus m_s;
};

} // namespace

std::unique_ptr<BtNode> btSequence(BtChildren c, std::string name) { return std::make_unique<Memory>("sequence", false, std::move(c), std::move(name)); }
std::unique_ptr<BtNode> btSelector(BtChildren c, std::string name) { return std::make_unique<Memory>("selector", true, std::move(c), std::move(name)); }
std::unique_ptr<BtNode> btReactiveSequence(BtChildren c, std::string name) { return std::make_unique<Reactive>("reactive_sequence", false, std::move(c), std::move(name)); }
std::unique_ptr<BtNode> btReactiveSelector(BtChildren c, std::string name) { return std::make_unique<Reactive>("reactive_selector", true, std::move(c), std::move(name)); }
std::unique_ptr<BtNode> btParallel(BtChildren c, int successNeeded, int failureNeeded, std::string name) {
    return std::make_unique<Parallel>(std::move(c), successNeeded, failureNeeded, std::move(name));
}
std::unique_ptr<BtNode> btRandomSelector(BtChildren c, std::vector<float> weights, std::string name) {
    return std::make_unique<RandomPick>("random_selector", true, std::move(c), std::move(weights), std::move(name));
}
std::unique_ptr<BtNode> btRandomSequence(BtChildren c, std::vector<float> weights, std::string name) {
    return std::make_unique<RandomPick>("random_sequence", false, std::move(c), std::move(weights), std::move(name));
}
std::unique_ptr<BtNode> btInverter(std::unique_ptr<BtNode> child) { return std::make_unique<Inverter>(std::move(child)); }
std::unique_ptr<BtNode> btSucceeder(std::unique_ptr<BtNode> child) { return std::make_unique<Force>("succeeder", BtStatus::Success, std::move(child)); }
std::unique_ptr<BtNode> btFailer(std::unique_ptr<BtNode> child) { return std::make_unique<Force>("failer", BtStatus::Failure, std::move(child)); }
std::unique_ptr<BtNode> btRepeat(std::unique_ptr<BtNode> child, int count) { return std::make_unique<Repeat>(std::move(child), count); }
std::unique_ptr<BtNode> btRetry(std::unique_ptr<BtNode> child, int attempts) { return std::make_unique<Retry>(std::move(child), attempts); }
std::unique_ptr<BtNode> btUntilFail(std::unique_ptr<BtNode> child) { return std::make_unique<UntilFail>(std::move(child)); }
std::unique_ptr<BtNode> btCooldown(std::unique_ptr<BtNode> child, float seconds) { return std::make_unique<Cooldown>(std::move(child), seconds); }
std::unique_ptr<BtNode> btTimeout(std::unique_ptr<BtNode> child, float seconds) { return std::make_unique<Timeout>(std::move(child), seconds); }
std::unique_ptr<BtNode> btDelay(std::unique_ptr<BtNode> child, float seconds) { return std::make_unique<Delay>(std::move(child), seconds); }
std::unique_ptr<BtNode> btAction(BtAction fn, std::string name, BtAbort onAbort) { return std::make_unique<Action>(std::move(fn), std::move(name), std::move(onAbort)); }
std::unique_ptr<BtNode> btCondition(BtCondition fn, std::string name) { return std::make_unique<Condition>(std::move(fn), std::move(name)); }
std::unique_ptr<BtNode> btCheck(std::string key, BtOp op, Blackboard::Value value, bool valueIsKey) {
    return std::make_unique<Check>(std::move(key), op, std::move(value), valueIsKey, false, true);
}
std::unique_ptr<BtNode> btCheckTruthy(std::string key, bool expected) {
    return std::make_unique<Check>(std::move(key), BtOp::Equal, Blackboard::Value(true), false, true, expected);
}
std::unique_ptr<BtNode> btSet(std::string key, Blackboard::Value value) { return std::make_unique<Set>(std::move(key), std::move(value)); }
std::unique_ptr<BtNode> btWait(float seconds) { return std::make_unique<Wait>(seconds); }
std::unique_ptr<BtNode> btChance(float probability) { return std::make_unique<Chance>(probability); }
std::unique_ptr<BtNode> btConstant(BtStatus status) { return std::make_unique<Constant>(status); }

// ---- The tree ---------------------------------------------------------------

BehaviorTree::BehaviorTree(std::unique_ptr<BtNode> root, uint32_t seed) : m_root(std::move(root)), m_rng(seed) {}

BtStatus BehaviorTree::tick(float dt) {
    if (!m_root) return m_status = BtStatus::Failure;
    if (!(dt > 0.0f)) dt = 0.0f; // also a NaN
    m_time += static_cast<double>(dt);
    ++m_ticks;
    BtContext c{ m_blackboard, dt, m_time, m_rng };
    return m_status = m_root->tick(c);
}

void BehaviorTree::reset() {
    if (!m_root) return;
    BtContext c{ m_blackboard, 0.0f, m_time, m_rng };
    m_root->abort(c);
    m_status = BtStatus::Failure;
}

void BehaviorTree::setRoot(std::unique_ptr<BtNode> root) {
    reset();
    m_root = std::move(root);
}

std::string BehaviorTree::describe() const { return m_root ? describeNode(*m_root) : std::string("(empty tree)\n"); }

// ---- From data --------------------------------------------------------------

using json = nlohmann::json;

BtFactory::BtFactory() = default;
BtFactory::~BtFactory() = default;
BtFactory::BtFactory(const BtFactory&) = default;
BtFactory& BtFactory::operator=(const BtFactory&) = default;

void BtFactory::addAction(const std::string& name, BtAction fn, BtAbort onAbort) { m_actions[name] = Action{ std::move(fn), std::move(onAbort) }; }
void BtFactory::addCondition(const std::string& name, BtCondition fn) { m_conditions[name] = std::move(fn); }
void BtFactory::addTree(const std::string& name, json spec) { m_trees[name] = std::move(spec); }
void BtFactory::removeTree(const std::string& name) { m_trees.erase(name); }

namespace {

const char* const kTypes[] = {
    "sequence", "selector", "reactive_sequence", "reactive_selector", "parallel", "random_selector", "random_sequence",
    "inverter", "succeeder", "failer", "repeat", "retry", "until_fail", "cooldown", "timeout", "delay",
    "action", "condition", "check", "set", "wait", "chance", "success", "failure", "subtree",
};

std::string typeList() {
    std::string out;
    for (const char* t : kTypes) out += (out.empty() ? "" : ", ") + std::string(t);
    return out;
}

bool toValue(const json& j, Blackboard::Value& out) {
    if (j.is_boolean()) out = j.get<bool>();
    else if (j.is_number()) out = j.get<double>();
    else if (j.is_string()) out = j.get<std::string>();
    else if (j.is_array() && j.size() == 3 && j[0].is_number() && j[1].is_number() && j[2].is_number())
        out = glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
    else return false;
    return true;
}

// A numeric field: `fallback` when absent (or required: false when absent).
bool number(const json& spec, const char* key, bool required, double fallback, double& out, const std::string& path, std::string& error) {
    const auto it = spec.find(key);
    if (it == spec.end()) {
        if (required) {
            error = path + ": needs \"" + key + "\"";
            return false;
        }
        out = fallback;
        return true;
    }
    if (!it->is_number()) {
        error = path + ": \"" + key + "\" must be a number";
        return false;
    }
    out = it->get<double>();
    return true;
}

bool text(const json& spec, const char* key, std::string& out, const std::string& path, std::string& error) {
    const auto it = spec.find(key);
    if (it == spec.end() || !it->is_string() || it->get<std::string>().empty()) {
        error = path + ": needs \"" + key + "\" (a name)";
        return false;
    }
    out = it->get<std::string>();
    return true;
}
} // namespace

std::unique_ptr<BtNode> BtFactory::build(const json& spec, std::string& error) const {
    error.clear();
    size_t nodes = 0;
    auto root = buildNode(spec, "root", 0, nodes, error);
    if (!error.empty()) return nullptr;
    return root;
}

std::unique_ptr<BtNode> BtFactory::buildNode(const json& spec, const std::string& path, int depth, size_t& nodes, std::string& error) const {
    if (!error.empty()) return nullptr;
    if (depth > kMaxDepth) {
        error = path + ": nested more than " + std::to_string(kMaxDepth) + " deep (a subtree that contains itself?)";
        return nullptr;
    }
    if (++nodes > kMaxNodes) {
        error = path + ": more than " + std::to_string(kMaxNodes) + " nodes";
        return nullptr;
    }

    if (spec.is_string()) {
        const std::string name = spec.get<std::string>();
        if (const auto a = m_actions.find(name); a != m_actions.end()) return btAction(a->second.fn, name, a->second.onAbort);
        if (const auto c = m_conditions.find(name); c != m_conditions.end()) return btCondition(c->second, name);
        error = path + ": no action or condition called '" + name + "'";
        return nullptr;
    }
    if (!spec.is_object()) {
        error = path + ": a node is an object ({\"type\": ...}) or the name of an action";
        return nullptr;
    }
    const auto typeIt = spec.find("type");
    if (typeIt == spec.end() || !typeIt->is_string()) {
        error = path + ": has no \"type\" (one of " + typeList() + ")";
        return nullptr;
    }
    const std::string type = typeIt->get<std::string>();
    const bool known = std::any_of(std::begin(kTypes), std::end(kTypes), [&](const char* t) { return type == t; });
    if (!known) {
        error = path + ": unknown type '" + type + "' (one of " + typeList() + ")";
        return nullptr;
    }
    std::string label = spec.value("label", std::string());
    if (label.empty() && spec.contains("name") && spec["name"].is_string()) label = spec["name"].get<std::string>();

    auto kids = [&](BtChildren& out) {
        const auto it = spec.find("children");
        if (it == spec.end() || !it->is_array()) {
            error = path + ": " + type + " needs \"children\" (a list of nodes)";
            return false;
        }
        for (size_t i = 0; i < it->size(); ++i) {
            auto child = buildNode((*it)[i], path + ".children[" + std::to_string(i) + "]", depth + 1, nodes, error);
            if (!child) return false;
            out.push_back(std::move(child));
        }
        return true;
    };
    auto one = [&](std::unique_ptr<BtNode>& out) {
        const json* c = nullptr;
        if (const auto it = spec.find("child"); it != spec.end()) c = &*it;
        else if (const auto cs = spec.find("children"); cs != spec.end() && cs->is_array() && cs->size() == 1) c = &(*cs)[0];
        if (!c) {
            error = path + ": " + type + " needs one \"child\"";
            return false;
        }
        out = buildNode(*c, path + ".child", depth + 1, nodes, error);
        return out != nullptr;
    };
    auto named = [&](std::unique_ptr<BtNode> n) {
        if (n && !label.empty()) n->setName(label);
        return n;
    };

    BtChildren c;
    std::unique_ptr<BtNode> child;
    double x = 0.0;
    if (type == "sequence") return kids(c) ? named(btSequence(std::move(c))) : nullptr;
    if (type == "selector") return kids(c) ? named(btSelector(std::move(c))) : nullptr;
    if (type == "reactive_sequence") return kids(c) ? named(btReactiveSequence(std::move(c))) : nullptr;
    if (type == "reactive_selector") return kids(c) ? named(btReactiveSelector(std::move(c))) : nullptr;
    if (type == "parallel") {
        // "success": "all" (default), "one" or a count; "failure": "one" (default), "all" or a count.
        auto policy = [&](const char* key, int fallback, int& out) {
            const auto it = spec.find(key);
            if (it == spec.end()) {
                out = fallback;
                return true;
            }
            if (it->is_string() && it->get<std::string>() == "all") out = 0;
            else if (it->is_string() && it->get<std::string>() == "one") out = 1;
            else if (it->is_number() && it->get<double>() >= 1.0) out = static_cast<int>(it->get<double>());
            else {
                error = path + ": \"" + key + "\" is \"all\", \"one\" or a count";
                return false;
            }
            return true;
        };
        int s = 0, f = 1;
        if (!policy("success", 0, s) || !policy("failure", 1, f) || !kids(c)) return nullptr;
        return named(btParallel(std::move(c), s, f));
    }
    if (type == "random_selector" || type == "random_sequence") {
        std::vector<float> weights;
        if (const auto it = spec.find("weights"); it != spec.end()) {
            if (!it->is_array()) {
                error = path + ": \"weights\" is a list of numbers";
                return nullptr;
            }
            for (const json& w : *it) {
                if (!w.is_number()) {
                    error = path + ": \"weights\" is a list of numbers";
                    return nullptr;
                }
                weights.push_back(w.get<float>());
            }
        }
        if (!kids(c)) return nullptr;
        if (!weights.empty() && weights.size() != c.size()) {
            error = path + ": " + std::to_string(weights.size()) + " weights for " + std::to_string(c.size()) + " children";
            return nullptr;
        }
        return named(type == "random_selector" ? btRandomSelector(std::move(c), std::move(weights)) : btRandomSequence(std::move(c), std::move(weights)));
    }
    if (type == "inverter") return one(child) ? named(btInverter(std::move(child))) : nullptr;
    if (type == "succeeder") return one(child) ? named(btSucceeder(std::move(child))) : nullptr;
    if (type == "failer") return one(child) ? named(btFailer(std::move(child))) : nullptr;
    if (type == "until_fail") return one(child) ? named(btUntilFail(std::move(child))) : nullptr;
    if (type == "repeat") {
        if (!number(spec, "count", false, 0.0, x, path, error) || !one(child)) return nullptr;
        return named(btRepeat(std::move(child), static_cast<int>(x)));
    }
    if (type == "retry") {
        if (!number(spec, "attempts", false, 0.0, x, path, error) || !one(child)) return nullptr;
        return named(btRetry(std::move(child), static_cast<int>(x)));
    }
    if (type == "cooldown" || type == "timeout" || type == "delay") {
        if (!number(spec, "seconds", true, 0.0, x, path, error) || !one(child)) return nullptr;
        if (x < 0.0) {
            error = path + ": \"seconds\" can't be negative";
            return nullptr;
        }
        const float s = static_cast<float>(x);
        if (type == "cooldown") return named(btCooldown(std::move(child), s));
        if (type == "timeout") return named(btTimeout(std::move(child), s));
        return named(btDelay(std::move(child), s));
    }
    if (type == "action" || type == "condition") {
        std::string name;
        if (!text(spec, "name", name, path, error)) return nullptr;
        if (type == "action") {
            const auto a = m_actions.find(name);
            if (a == m_actions.end()) {
                error = path + ": no action called '" + name + "'";
                return nullptr;
            }
            return named(btAction(a->second.fn, name, a->second.onAbort));
        }
        const auto cd = m_conditions.find(name);
        if (cd == m_conditions.end()) {
            error = path + ": no condition called '" + name + "'";
            return nullptr;
        }
        return named(btCondition(cd->second, name));
    }
    if (type == "check") {
        std::string key;
        if (!text(spec, "key", key, path, error)) return nullptr;
        const auto opIt = spec.find("op");
        if (opIt == spec.end()) {
            const auto ex = spec.find("expected");
            return named(btCheckTruthy(key, ex == spec.end() || !ex->is_boolean() || ex->get<bool>()));
        }
        BtOp op = BtOp::Equal;
        if (!opIt->is_string() || !btOpFromName(opIt->get<std::string>(), op)) {
            error = path + ": \"op\" is one of ==, !=, <, <=, >, >=";
            return nullptr;
        }
        if (const auto vk = spec.find("valueKey"); vk != spec.end()) {
            if (!vk->is_string()) {
                error = path + ": \"valueKey\" is a key name";
                return nullptr;
            }
            return named(btCheck(key, op, Blackboard::Value(vk->get<std::string>()), true));
        }
        const auto v = spec.find("value");
        Blackboard::Value value;
        if (v == spec.end() || !toValue(*v, value)) {
            error = path + ": check needs \"value\" (a number, true/false, a string or [x, y, z]) or \"valueKey\"";
            return nullptr;
        }
        return named(btCheck(key, op, std::move(value)));
    }
    if (type == "set") {
        std::string key;
        if (!text(spec, "key", key, path, error)) return nullptr;
        const auto v = spec.find("value");
        Blackboard::Value value;
        if (v == spec.end() || !toValue(*v, value)) {
            error = path + ": set needs \"value\" (a number, true/false, a string or [x, y, z])";
            return nullptr;
        }
        return named(btSet(key, std::move(value)));
    }
    if (type == "wait") return number(spec, "seconds", true, 0.0, x, path, error) ? named(btWait(static_cast<float>(x))) : nullptr;
    if (type == "chance") {
        if (!number(spec, "probability", true, 0.0, x, path, error)) return nullptr;
        if (x < 0.0 || x > 1.0) {
            error = path + ": \"probability\" is 0 to 1";
            return nullptr;
        }
        return named(btChance(static_cast<float>(x)));
    }
    if (type == "success") return named(btConstant(BtStatus::Success));
    if (type == "failure") return named(btConstant(BtStatus::Failure));
    // subtree
    std::string name;
    if (!text(spec, "name", name, path, error)) return nullptr;
    const auto t = m_trees.find(name);
    if (t == m_trees.end()) {
        error = path + ": no tree called '" + name + "'";
        return nullptr;
    }
    auto sub = buildNode(t->second, path + "<" + name + ">", depth + 1, nodes, error);
    if (sub && label.empty()) sub->setName(name);
    return sub ? named(std::move(sub)) : nullptr;
}

} // namespace kke::ai
