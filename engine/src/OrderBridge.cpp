#include "kke/OrderBridge.h"

namespace kke {

AiOrderBridge::AiOrderBridge(OrderBoard& board, ai::AiWorld& world) : m_board(board), m_world(world) {
    OrderBoard::Listener l;
    l.ordered = [this](uint32_t unit, const UnitOrder& o) { apply(unit, o); };
    l.finished = [this](uint32_t unit, const UnitOrder& o, bool) {
        // A fetch that ended some other way (the ball went away, a new
        // order): whatever it carried stays with it until dropped.
        if (o.kind == OrderKind::Fetch) {
            auto it = m_fetch.find(unit);
            if (it != m_fetch.end() && !it->second.carrying) m_fetch.erase(it);
        }
    };
    m_listener = board.listen(std::move(l));
}

AiOrderBridge::~AiOrderBridge() { m_board.unlisten(m_listener); }

uint32_t AiOrderBridge::carrying(uint32_t unit) const {
    auto it = m_fetch.find(unit);
    return it != m_fetch.end() && it->second.carrying ? it->second.thing : 0;
}

void AiOrderBridge::apply(uint32_t unit, const UnitOrder& o) {
    const ai::Agent* agent = m_world.agent(unit);
    if (!agent || agent->actor) return;
    ai::Order a;
    switch (o.kind) {
    case OrderKind::Move:
    case OrderKind::Regroup: {
        a.kind = ai::Order::Kind::MoveTo;
        a.position = o.point;
        a.distance = 0.4f;
        const glm::vec3 d = o.point - agent->position;
        a.run = glm::length(glm::vec2(d.x, d.z)) > runBeyond;
        break;
    }
    case OrderKind::Follow:
        a.kind = ai::Order::Kind::Follow;
        a.target = o.target;
        a.distance = followDistance;
        break;
    case OrderKind::Stay: {
        a.kind = ai::Order::Kind::Hold;
        a.position = o.hasPoint ? o.point : agent->position;
        const glm::vec3 d = a.position - agent->position;
        a.run = glm::length(glm::vec2(d.x, d.z)) > stayRunBeyond; // a spot to hold (cover): get there
        break;
    }
    case OrderKind::Sit:
        a.kind = ai::Order::Kind::Hold;
        a.position = agent->position;
        break;
    case OrderKind::Attack:
    case OrderKind::FocusFire:
        a.kind = ai::Order::Kind::Attack;
        a.target = o.target;
        a.run = true;
        break;
    case OrderKind::Fetch: {
        Fetch& f = m_fetch[unit];
        if (f.carrying && f.thing == o.target) {
            // Already has it: straight back.
            f.issuer = o.issuer;
            a.kind = ai::Order::Kind::Interact;
            a.target = o.issuer;
            a.distance = followDistance * 0.5f;
            a.run = true;
            break;
        }
        f = Fetch{ o.target, o.issuer, false };
        a.kind = ai::Order::Kind::Interact;
        a.target = o.target;
        a.distance = fetchReach;
        a.run = true;
        break;
    }
    case OrderKind::Pet:
        a.kind = ai::Order::Kind::Interact;
        a.target = o.target ? o.target : o.issuer;
        a.distance = 1.0f;
        if (const ai::Agent* by = m_world.agent(a.target)) {
            const glm::vec3 d = by->position - agent->position;
            a.run = glm::length(glm::vec2(d.x, d.z)) > runBeyond;
        }
        break;
    case OrderKind::Drop:
        if (drop) drop(unit);
        m_fetch.erase(unit);
        m_world.clearOrder(unit);
        m_board.complete(unit, true);
        return;
    case OrderKind::Free:
    case OrderKind::None:
        m_world.clearOrder(unit);
        return;
    }
    m_world.order(unit, a);
}

void AiOrderBridge::handle(const std::vector<ai::AiEvent>& events) {
    for (const ai::AiEvent& e : events) {
        if (e.kind != ai::AiEvent::Kind::Arrived) continue;
        const UnitOrder* o = m_board.current(e.who);
        if (!o) continue;
        const uint32_t unit = e.who;
        switch (o->kind) {
        case OrderKind::Move:
        case OrderKind::Regroup: m_board.complete(unit, true); break;
        case OrderKind::Fetch: {
            auto it = m_fetch.find(unit);
            if (it == m_fetch.end()) break;
            Fetch& f = it->second;
            if (!f.carrying && e.other == f.thing) {
                if (pickUp && !pickUp(unit, f.thing)) {
                    m_fetch.erase(it);
                    m_board.complete(unit, false);
                    break;
                }
                f.carrying = true;
                ai::Order back;
                back.kind = ai::Order::Kind::Interact;
                back.target = f.issuer;
                back.distance = followDistance * 0.5f;
                back.run = true;
                m_world.order(unit, back);
            } else if (f.carrying && e.other == f.issuer) {
                const Fetch done = f;
                m_fetch.erase(it);
                if (deliver) deliver(unit, done.thing, done.issuer);
                m_board.complete(unit, true);
            }
            break;
        }
        case OrderKind::Pet:
            if (petStart) petStart(unit, e.other);
            else m_board.complete(unit, true);
            break;
        default: break;
        }
    }
}

} // namespace kke
