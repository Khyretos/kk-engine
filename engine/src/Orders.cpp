#include "kke/Orders.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kke {

namespace {

constexpr float kPi = 3.14159265358979f;

struct KindInfo { OrderKind kind; const char* name; const char* label; };
constexpr KindInfo kKinds[] = {
    { OrderKind::Move, "move", "Go there" },
    { OrderKind::Follow, "follow", "Follow me" },
    { OrderKind::Stay, "stay", "Hold position" },
    { OrderKind::Attack, "attack", "Attack" },
    { OrderKind::FocusFire, "focus", "Focus fire" },
    { OrderKind::Fetch, "fetch", "Fetch" },
    { OrderKind::Drop, "drop", "Drop it" },
    { OrderKind::Sit, "sit", "Sit" },
    { OrderKind::Pet, "pet", "Pet" },
    { OrderKind::Regroup, "regroup", "Regroup" },
    { OrderKind::Free, "free", "At ease" },
};

glm::vec3 flat(const glm::vec3& v) { return glm::vec3(v.x, 0.0f, v.z); }

glm::vec3 rightOf(const glm::vec3& forward) { return glm::vec3(-forward.z, 0.0f, forward.x); }

bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

} // namespace

const char* orderName(OrderKind kind) {
    for (const KindInfo& k : kKinds)
        if (k.kind == kind) return k.name;
    return "";
}

OrderKind orderFromName(const std::string& name) {
    for (const KindInfo& k : kKinds)
        if (name == k.name) return k.kind;
    // The words people use for the same thing.
    if (name == "come" || name == "heel") return OrderKind::Follow;
    if (name == "hold" || name == "wait") return OrderKind::Stay;
    if (name == "focusfire" || name == "focus_fire") return OrderKind::FocusFire;
    if (name == "go") return OrderKind::Move;
    return OrderKind::None;
}

const char* orderLabel(OrderKind kind) {
    for (const KindInfo& k : kKinds)
        if (k.kind == kind) return k.label;
    return "";
}

bool orderNeedsTarget(OrderKind kind) {
    return kind == OrderKind::Attack || kind == OrderKind::FocusFire || kind == OrderKind::Fetch || kind == OrderKind::Follow ||
           kind == OrderKind::Pet;
}

bool orderNeedsPoint(OrderKind kind) { return kind == OrderKind::Move; }

// ---------------------------------------------------------------- formations

const char* formationName(Formation f) {
    switch (f) {
    case Formation::Line: return "line";
    case Formation::Wedge: return "wedge";
    case Formation::Column: return "column";
    case Formation::Circle: return "circle";
    }
    return "wedge";
}

Formation formationFromName(const std::string& name, Formation fallback) {
    if (name == "line") return Formation::Line;
    if (name == "wedge") return Formation::Wedge;
    if (name == "column" || name == "file") return Formation::Column;
    if (name == "circle" || name == "ring") return Formation::Circle;
    return fallback;
}

glm::vec3 yawForward(float yawDegrees) {
    const float y = glm::radians(yawDegrees);
    return glm::vec3(std::sin(y), 0.0f, -std::cos(y));
}

float yawOf(const glm::vec3& direction) {
    if (direction.x == 0.0f && direction.z == 0.0f) return 0.0f;
    return glm::degrees(std::atan2(direction.x, -direction.z));
}

std::vector<glm::vec3> formationSlots(Formation f, int count, const glm::vec3& center, float yawDegrees, float spacing) {
    std::vector<glm::vec3> out;
    if (count <= 0) return out;
    out.reserve(static_cast<size_t>(count));
    const glm::vec3 fwd = yawForward(yawDegrees), right = rightOf(fwd);
    switch (f) {
    case Formation::Line: {
        // Centre out, right before left: slot 0 is the middle one.
        std::vector<float> xs;
        for (int i = 0; i < count; ++i) xs.push_back(float(i) - float(count - 1) * 0.5f);
        std::stable_sort(xs.begin(), xs.end(), [](float a, float b) {
            if (std::fabs(a) != std::fabs(b)) return std::fabs(a) < std::fabs(b);
            return a > b;
        });
        for (float x : xs) out.push_back(center + right * (x * spacing));
        break;
    }
    case Formation::Wedge:
        // Slot 0 at the point, then pairs further back and out, right first.
        for (int i = 0; i < count; ++i) {
            const int row = (i + 1) / 2;
            const float side = (i % 2 == 1) ? 1.0f : -1.0f;
            out.push_back(center - fwd * (float(row) * spacing * 0.8f) + right * (i == 0 ? 0.0f : side * float(row) * spacing * 0.7f));
        }
        break;
    case Formation::Column: {
        const int width = count > 4 ? 2 : 1;
        for (int i = 0; i < count; ++i) {
            const int row = i / width, col = i % width;
            const float x = (float(col) - float(width - 1) * 0.5f) * spacing;
            out.push_back(center - fwd * (float(row) * spacing) + right * x);
        }
        break;
    }
    case Formation::Circle:
        if (count == 1) {
            out.push_back(center);
            break;
        }
        {
            const float radius = std::max(spacing, spacing * float(count) / (2.0f * kPi));
            for (int i = 0; i < count; ++i) {
                const float a = 2.0f * kPi * float(i) / float(count);
                out.push_back(center + (fwd * std::cos(a) + right * std::sin(a)) * radius);
            }
        }
        break;
    }
    return out;
}

std::vector<int> assignSlots(const std::vector<glm::vec3>& units, const std::vector<glm::vec3>& slots) {
    // Hungarian method (Kuhn-Munkres with potentials), O(n^3), rows =
    // units, columns = slots, cost = straight-line distance. With the
    // least total distance no two paths cross.
    const int n = static_cast<int>(std::min(units.size(), slots.size()));
    std::vector<int> result(units.size(), -1);
    if (n == 0) return result;
    const double inf = std::numeric_limits<double>::infinity();
    auto cost = [&](int i, int j) { return double(glm::length(units[size_t(i - 1)] - slots[size_t(j - 1)])); };
    std::vector<double> u(size_t(n) + 1, 0.0), v(size_t(n) + 1, 0.0);
    std::vector<int> p(size_t(n) + 1, 0), way(size_t(n) + 1, 0);
    for (int i = 1; i <= n; ++i) {
        p[0] = i;
        int j0 = 0;
        std::vector<double> minv(size_t(n) + 1, inf);
        std::vector<char> used(size_t(n) + 1, 0);
        do {
            used[size_t(j0)] = 1;
            const int i0 = p[size_t(j0)];
            double delta = inf;
            int j1 = 0;
            for (int j = 1; j <= n; ++j) {
                if (used[size_t(j)]) continue;
                const double cur = cost(i0, j) - u[size_t(i0)] - v[size_t(j)];
                if (cur < minv[size_t(j)]) {
                    minv[size_t(j)] = cur;
                    way[size_t(j)] = j0;
                }
                if (minv[size_t(j)] < delta) {
                    delta = minv[size_t(j)];
                    j1 = j;
                }
            }
            for (int j = 0; j <= n; ++j) {
                if (used[size_t(j)]) {
                    u[size_t(p[size_t(j)])] += delta;
                    v[size_t(j)] -= delta;
                } else {
                    minv[size_t(j)] -= delta;
                }
            }
            j0 = j1;
        } while (p[size_t(j0)] != 0);
        do {
            const int j1 = way[size_t(j0)];
            p[size_t(j0)] = p[size_t(j1)];
            j0 = j1;
        } while (j0);
    }
    for (int j = 1; j <= n; ++j)
        if (p[size_t(j)] > 0) result[size_t(p[size_t(j)] - 1)] = j - 1;
    return result;
}

// ---------------------------------------------------------------- following

glm::vec3 followSlot(const glm::vec3& leader, const glm::vec3& leaderVelocity, float headingDegrees, const glm::vec3& follower,
                     const FollowSettings& s) {
    const glm::vec3 v = flat(leaderVelocity);
    const float speed = glm::length(v);
    const glm::vec3 dir = speed > 0.3f ? v / speed : yawForward(headingDegrees);
    const glm::vec3 right = rightOf(dir);
    // Stay on the side you're on: crossing means walking through the leader's path.
    const float side = glm::dot(flat(follower - leader), right) < 0.0f ? -1.0f : 1.0f;
    const float a = glm::radians(s.sideAngle);
    const glm::vec3 offset = dir * std::cos(a) + right * (side * std::sin(a));
    const float run = s.runSpeed > 0.0f ? std::min(speed / s.runSpeed, 1.0f) : 0.0f;
    // Aim a little ahead of where the leader is, so the follower keeps pace
    // instead of forever catching up.
    glm::vec3 slot = leader + offset * s.distance + right * (side * s.runAside * run) + v * 0.3f;
    if (inLeadersWay(leader, leaderVelocity, slot, s)) slot += right * (side * s.pathWidth);
    slot.y = leader.y;
    return slot;
}

bool inLeadersWay(const glm::vec3& leader, const glm::vec3& leaderVelocity, const glm::vec3& point, const FollowSettings& s) {
    const glm::vec3 v = flat(leaderVelocity);
    if (glm::length(v) < 0.3f) return false;
    const glm::vec3 path = v * s.lookAhead;
    const glm::vec3 d = flat(point - leader);
    const float len2 = glm::dot(path, path);
    const float t = glm::dot(d, path) / len2;
    if (t <= 0.0f) return false; // behind the leader
    const glm::vec3 closest = path * std::min(t, 1.0f);
    return glm::length(d - closest) < s.pathWidth;
}

// ---------------------------------------------------------------- selection

void Selection::select(uint32_t id) {
    m_ids.clear();
    if (id) m_ids.push_back(id);
}

void Selection::set(std::vector<uint32_t> ids) {
    m_ids.clear();
    for (uint32_t id : ids) add(id);
}

void Selection::add(uint32_t id) {
    if (id && !contains(id)) m_ids.push_back(id);
}

void Selection::toggle(uint32_t id) {
    if (contains(id)) m_ids.erase(std::find(m_ids.begin(), m_ids.end(), id));
    else add(id);
}

void Selection::remove(uint32_t id) {
    m_ids.erase(std::remove(m_ids.begin(), m_ids.end(), id), m_ids.end());
    for (auto& g : m_groups) g.erase(std::remove(g.begin(), g.end(), id), g.end());
}

bool Selection::contains(uint32_t id) const { return std::find(m_ids.begin(), m_ids.end(), id) != m_ids.end(); }

void Selection::storeGroup(int group) {
    if (group >= 1 && group <= 9) m_groups[group] = m_ids;
}

bool Selection::recallGroup(int group) {
    if (group < 1 || group > 9 || m_groups[group].empty()) return false;
    m_ids = m_groups[group];
    return true;
}

const std::vector<uint32_t>& Selection::group(int group) const { return m_groups[(group >= 1 && group <= 9) ? group : 0]; }

std::vector<uint32_t> unitsInRect(const std::vector<ScreenUnit>& units, const glm::vec2& a, const glm::vec2& b) {
    const glm::vec2 mn = glm::min(a, b), mx = glm::max(a, b);
    std::vector<uint32_t> out;
    for (const ScreenUnit& u : units)
        if (u.onScreen && u.screen.x >= mn.x && u.screen.x <= mx.x && u.screen.y >= mn.y && u.screen.y <= mx.y) out.push_back(u.id);
    return out;
}

uint32_t unitNear(const std::vector<ScreenUnit>& units, const glm::vec2& point, float radius) {
    uint32_t best = 0;
    float bestD = radius;
    for (const ScreenUnit& u : units) {
        if (!u.onScreen) continue;
        const float d = glm::length(u.screen - point);
        if (d <= bestD) {
            bestD = d;
            best = u.id;
        }
    }
    return best;
}

// ---------------------------------------------------------------- context orders

Order contextOrder(const std::vector<uint32_t>& units, const PointerTarget& target, const UnitAbilities& can, const PointerModifiers& mods,
                   uint32_t issuer) {
    Order o;
    o.issuer = issuer;
    o.queue = mods.queue;
    if (units.empty()) return o;
    o.units = units;
    auto moveThere = [&] {
        o.kind = OrderKind::Move;
        o.point = target.point;
        o.hasPoint = true;
    };
    switch (target.relation) {
    case Relation::Hostile:
        if (can.attack && target.thing) {
            o.kind = mods.force ? OrderKind::FocusFire : OrderKind::Attack;
            o.target = target.thing;
        } else {
            moveThere();
        }
        break;
    case Relation::Item:
        if (can.fetch && target.thing) {
            o.kind = OrderKind::Fetch;
            o.target = target.thing;
        } else {
            moveThere();
        }
        break;
    case Relation::Self:
    case Relation::Friend:
    case Relation::Own:
        if (target.thing && std::find(units.begin(), units.end(), target.thing) == units.end()) {
            o.kind = OrderKind::Follow;
            o.target = target.thing;
        } else {
            moveThere();
        }
        break;
    case Relation::Ground:
        moveThere();
        if (mods.force) o.kind = OrderKind::Stay;
        break;
    }
    return o;
}

// ---------------------------------------------------------------- radial menu

glm::vec2 RadialMenu::direction(int i) const {
    const float a = 2.0f * kPi * float(i) / float(std::max(m_items, 1));
    return glm::vec2(std::sin(a), -std::cos(a));
}

int RadialMenu::pick(const glm::vec2& dir) {
    if (m_items <= 0) return m_picked = -1;
    // Clockwise from the top, y down.
    float a = std::atan2(dir.x, -dir.y);
    if (a < 0.0f) a += 2.0f * kPi;
    const float sector = 2.0f * kPi / float(m_items);
    if (m_picked >= 0 && m_picked < m_items) {
        float d = std::fabs(a - sector * float(m_picked));
        d = std::min(d, 2.0f * kPi - d);
        if (d <= sector * 0.5f + glm::radians(hysteresisDegrees)) return m_picked;
    }
    m_picked = int(std::floor((a + sector * 0.5f) / sector)) % m_items;
    return m_picked;
}

int RadialMenu::updateStick(const glm::vec2& stick, float deadzone) {
    // Back in the middle keeps the pick: flick, then let go of the button.
    if (glm::length(stick) < deadzone) return m_picked;
    return pick(stick);
}

int RadialMenu::updatePointer(const glm::vec2& offset, float deadzonePixels) {
    // A pointer can come back to the middle on purpose: that is "never mind".
    if (glm::length(offset) < deadzonePixels) return m_picked = -1;
    return pick(offset);
}

// ---------------------------------------------------------------- the board

OrderBoard::OrderBoard(PositionFn position) : m_position(std::move(position)) {}

uint64_t OrderBoard::issue(const Order& order, std::string* why) {
    auto fail = [&](const char* reason) -> uint64_t {
        if (why) *why = reason;
        return 0;
    };
    if (order.kind == OrderKind::None) return fail("no order");
    if (order.units.empty()) return fail("nobody to give it to");
    if (orderNeedsTarget(order.kind) && !order.target) return fail("this order needs a target");
    if (orderNeedsPoint(order.kind) && !order.hasPoint) return fail("this order needs a place");
    if (order.hasPoint && !finite(order.point)) return fail("the place must be real numbers");

    std::vector<uint32_t> units;
    for (uint32_t u : order.units)
        if (u && std::find(units.begin(), units.end(), u) == units.end() && u != order.target) units.push_back(u);
    if (units.empty()) return fail("nobody to give it to");

    auto where = [&](uint32_t u) {
        if (m_position) {
            const glm::vec3 p = m_position(u);
            if (finite(p)) return p;
        }
        return order.point;
    };

    // Per-unit slots for orders about a place.
    std::vector<glm::vec3> slot(units.size(), order.point);
    std::vector<char> hasSlot(units.size(), 0);
    float yaw = order.yawDegrees;
    const bool formation = order.kind == OrderKind::Move || order.kind == OrderKind::Regroup || (order.kind == OrderKind::Stay && order.hasPoint);
    if (formation) {
        glm::vec3 center = order.point;
        if (order.kind == OrderKind::Regroup && !order.hasPoint) {
            if (!order.issuer) return fail("regroup needs a place or someone to gather on");
            center = where(order.issuer);
        }
        glm::vec3 from(0.0f);
        std::vector<glm::vec3> at;
        for (uint32_t u : units) {
            at.push_back(where(u));
            from += at.back();
        }
        from /= float(units.size());
        if (!order.hasYaw) yaw = yawOf(center - from);
        const Formation shape = order.kind == OrderKind::Regroup ? Formation::Circle : order.formation;
        const float spacing = std::isfinite(order.spacing) && order.spacing > 0.1f ? order.spacing : 1.6f;
        const std::vector<glm::vec3> slots = formationSlots(shape, int(units.size()), center, yaw, spacing);
        const std::vector<int> pick = assignSlots(at, slots);
        for (size_t i = 0; i < units.size(); ++i) {
            slot[i] = slots[size_t(pick[i])];
            hasSlot[i] = 1;
        }
    } else if (order.kind == OrderKind::Stay) {
        for (size_t i = 0; i < units.size(); ++i) {
            slot[i] = where(units[i]);
            hasSlot[i] = 1;
        }
    }

    const uint64_t id = m_nextId++;
    for (size_t i = 0; i < units.size(); ++i) {
        UnitOrder o;
        o.id = id;
        o.kind = order.kind;
        o.target = order.target;
        o.point = slot[i];
        o.hasPoint = hasSlot[i] != 0;
        o.yawDegrees = yaw;
        o.hasYaw = formation || order.hasYaw;
        o.issuer = order.issuer;
        o.squad = units;
        auto it = m_units.find(units[i]);
        if (it == m_units.end()) it = m_units.emplace(units[i], Entry{}).first;
        Entry& e = it->second;
        if (order.queue && e.has) {
            e.queue.push_back(std::move(o));
            continue;
        }
        // A new order replaces the old ones without a "finished": it was
        // neither done nor failed, just overruled.
        e.queue.clear();
        start(units[i], e, o);
    }
    return id;
}

void OrderBoard::start(uint32_t unit, Entry& e, const UnitOrder& o) {
    e.current = o;
    e.current.status = UnitOrder::Status::Given;
    e.has = true;
    notifyOrdered(unit, e.current);
}

const UnitOrder* OrderBoard::current(uint32_t unit) const {
    auto it = m_units.find(unit);
    return it != m_units.end() && it->second.has ? &it->second.current : nullptr;
}

OrderKind OrderBoard::currentKind(uint32_t unit) const {
    const UnitOrder* o = current(unit);
    return o ? o->kind : OrderKind::None;
}

size_t OrderBoard::queued(uint32_t unit) const {
    auto it = m_units.find(unit);
    return it != m_units.end() ? it->second.queue.size() : 0;
}

void OrderBoard::markRunning(uint32_t unit) {
    auto it = m_units.find(unit);
    if (it != m_units.end() && it->second.has && it->second.current.status == UnitOrder::Status::Given)
        it->second.current.status = UnitOrder::Status::Running;
}

void OrderBoard::advance(uint32_t unit, Entry& e) {
    (void)unit;
    e.has = false;
    if (!e.queue.empty()) {
        e.current = std::move(e.queue.front());
        e.queue.pop_front();
        e.current.status = UnitOrder::Status::Given;
        e.has = true;
    }
}

void OrderBoard::complete(uint32_t unit, bool ok) {
    auto it = m_units.find(unit);
    if (it == m_units.end() || !it->second.has) return;
    UnitOrder old = it->second.current;
    old.status = ok ? UnitOrder::Status::Done : UnitOrder::Status::Failed;
    advance(unit, it->second);
    const bool next = it->second.has;
    const UnitOrder nextOrder = next ? it->second.current : UnitOrder{};
    // Copies only from here: a listener may give new orders or forget units.
    notifyFinished(unit, old, ok);
    if (next) notifyOrdered(unit, nextOrder);
}

void OrderBoard::cancel(uint32_t unit) {
    auto it = m_units.find(unit);
    if (it == m_units.end()) return;
    it->second.queue.clear();
    it->second.has = false;
}

void OrderBoard::forget(uint32_t unit) { m_units.erase(unit); }

void OrderBoard::targetGone(uint32_t thing) {
    if (!thing) return;
    std::vector<std::pair<uint32_t, bool>> ended;
    for (auto& [unit, e] : m_units) {
        std::deque<UnitOrder> kept;
        for (UnitOrder& q : e.queue)
            if (q.target != thing) kept.push_back(std::move(q));
        e.queue = std::move(kept);
        if (e.has && e.current.target == thing)
            ended.emplace_back(unit, e.current.kind == OrderKind::Attack || e.current.kind == OrderKind::FocusFire);
    }
    for (const auto& [unit, ok] : ended) complete(unit, ok);
}

std::vector<uint32_t> OrderBoard::units() const {
    std::vector<uint32_t> out;
    for (const auto& [unit, e] : m_units) out.push_back(unit);
    return out;
}

int OrderBoard::listen(Listener l) {
    const int handle = m_nextListener++;
    m_listeners.emplace(handle, std::move(l));
    return handle;
}

void OrderBoard::unlisten(int handle) { m_listeners.erase(handle); }

void OrderBoard::notifyOrdered(uint32_t unit, const UnitOrder& o) {
    const auto listeners = m_listeners;
    for (const auto& [h, l] : listeners)
        if (l.ordered) l.ordered(unit, o);
}

void OrderBoard::notifyFinished(uint32_t unit, const UnitOrder& o, bool ok) {
    const auto listeners = m_listeners;
    for (const auto& [h, l] : listeners)
        if (l.finished) l.finished(unit, o, ok);
}

// ---------------------------------------------------------------- intent

const char* intentName(Intent::Kind kind) {
    switch (kind) {
    case Intent::Kind::Idle: return "idle";
    case Intent::Kind::Walking: return "walking";
    case Intent::Kind::Running: return "running";
    case Intent::Kind::LookingAt: return "looking";
    case Intent::Kind::Approaching: return "approaching";
    }
    return "idle";
}

const Intent& IntentReader::update(const IntentSample& player, const std::vector<IntentCandidate>& things, float dt) {
    const glm::vec3 v = flat(player.velocity);
    const float speed = glm::length(v);
    const bool moving = speed >= m_s.walkSpeed;
    m_stillTime = moving ? 0.0f : m_stillTime + dt;

    // What the eyes rest on: the thing nearest the middle of the view.
    const glm::vec3 eye = player.position + glm::vec3(0.0f, 1.5f, 0.0f);
    const float lookCos = std::cos(glm::radians(m_s.lookCone));
    uint32_t looked = 0;
    float bestCos = lookCos;
    const float viewLen = glm::length(player.view);
    // Approach: walking toward something close.
    const float approachCos = std::cos(glm::radians(m_s.approachCone));
    uint32_t approached = 0;
    float approachDist = m_s.approachRange;
    for (const IntentCandidate& c : things) {
        if (!c.thing) continue;
        if (viewLen > 0.0f) {
            const glm::vec3 to = c.position + glm::vec3(0.0f, 0.3f, 0.0f) - eye;
            const float d = glm::length(to);
            if (d > 0.01f && d <= m_s.lookRange) {
                const float cs = glm::dot(to / d, player.view / viewLen);
                if (cs >= bestCos) {
                    bestCos = cs;
                    looked = c.thing;
                }
            }
        }
        if (moving) {
            const glm::vec3 to = flat(c.position - player.position);
            const float d = glm::length(to);
            if (d > 0.01f && d < approachDist && glm::dot(to / d, v / speed) >= approachCos) {
                approachDist = d;
                approached = c.thing;
            }
        }
    }
    if (looked != m_lookThing) {
        m_lookThing = looked;
        m_lookTime = 0.0f;
    }
    if (looked) m_lookTime += dt;

    Intent next;
    next.heading = player.position + v * m_s.lookAhead;
    if (approached) {
        next.kind = Intent::Kind::Approaching;
        next.thing = approached;
    } else if (looked && m_lookTime >= m_s.lookDwell) {
        next.kind = Intent::Kind::LookingAt;
        next.thing = looked;
    } else if (moving) {
        next.kind = speed >= m_s.runSpeed ? Intent::Kind::Running : Intent::Kind::Walking;
    } else if (m_stillTime >= m_s.idleAfter) {
        next.kind = Intent::Kind::Idle;
    } else {
        // A moment's pause doesn't change what they're doing.
        next.kind = m_intent.kind == Intent::Kind::Running || m_intent.kind == Intent::Kind::Walking ? m_intent.kind : Intent::Kind::Idle;
    }
    const bool same = next.kind == m_intent.kind && next.thing == m_intent.thing;
    next.seconds = next.kind == Intent::Kind::LookingAt ? m_lookTime : (same ? m_intent.seconds + dt : dt);
    m_intent = next;
    return m_intent;
}

} // namespace kke
