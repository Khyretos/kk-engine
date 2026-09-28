#include "Bot.h"

#include "Court.h"

#include <algorithm>
#include <cmath>

namespace tennis {

BotSkill BotSkill::forLevel(int level) {
    switch (std::clamp(level, 0, 3)) {
    case 0: return { 3.6f, 0.45f, 2.4f, 0.25f, 0.1f };
    case 1: return { 4.6f, 0.3f, 1.5f, 0.5f, 0.3f };
    case 2: return { 5.4f, 0.18f, 0.9f, 0.7f, 0.5f };
    default: return { 6.2f, 0.1f, 0.5f, 0.85f, 0.7f };
    }
}

Bot::Bot(uint32_t seed, int level) : m_skill(BotSkill::forLevel(level)), m_rng(seed) {}

glm::vec3 Bot::standFor(const glm::vec3& contact, int side) {
    // My right is +x on the +1 half (facing -z); the ball goes past the
    // right hip, a little in front.
    const float s = static_cast<float>(side);
    return { contact.x - 0.7f * s, 0.0f, contact.z + 0.35f * s };
}

Bot::Decision Bot::think(const View& v, float dt) {
    m_sinceHit += dt;
    Decision d;
    const float s = static_cast<float>(v.side);
    // Home: the middle of the baseline, or the net for a doubles partner.
    const glm::vec3 home = v.atNet ? glm::vec3(v.feet.x * 0.5f, 0.0f, s * 3.2f) : glm::vec3(0.0f, 0.0f, s * (kHalfLength + 0.6f));
    d.moveTo = home;
    d.urgency = 0.6f;
    if (!v.ballInPlay) {
        m_planned = false;
        return d;
    }
    const bool coming = v.ball.vel.z * s > 0.0f || (v.ball.pos.z * s > 0.0f && std::abs(v.ball.vel.z) < 1.0f);
    if (!coming || !v.myTurn) {
        m_planned = false;
        if (!v.myTurn && coming) d.moveTo = glm::vec3(home.x * 0.5f - v.ball.pos.x * 0.25f, 0.0f, home.z);
        return d;
    }
    if (m_sinceHit < m_skill.react) {
        d.moveTo = v.feet; // still reading the shot
        return d;
    }
    // Where to meet it: out of the air when it reaches us low enough at
    // the net, else waist high after its bounce.
    glm::vec3 contact(0.0f);
    float when = 0.0f;
    bool found = false;
    const bool nearNet = std::abs(v.feet.z) < 5.0f;
    if (nearNet && v.mayHit) {
        // A volley: where it passes our depth.
        const float t = v.ball.timeAtZ(v.feet.z);
        if (t > 0.0f) {
            const glm::vec3 p = v.ball.at(t);
            if (p.y > 0.3f && p.y < 2.2f) {
                contact = p;
                when = t;
                found = true;
            }
        }
    }
    if (!found && v.bounced) {
        meetOnArc(v.ball, contact, when);
        found = true;
    }
    if (!found) found = meetPoint(v.ball, v.side, v.bounce, contact, when);
    if (!found) {
        // Already bounced and on its way: meet it where it will be soon.
        contact = v.ball.at(0.35f);
        contact.y = std::max(contact.y, 0.8f);
        when = 0.35f;
    }
    // Deep balls: back up behind the baseline rather than chase it to the fence.
    d.moveTo = standFor(contact, v.side);
    d.urgency = 1.0f;
    if (!m_planned) {
        m_planned = true;
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        const float roll = u(m_rng);
        const bool opponentAtNet = std::abs(v.opponent.z) < 5.0f;
        if (opponentAtNet && roll < 0.3f) m_kind = ShotKind::Lob;
        else if (roll < 0.12f * m_skill.risk && !opponentAtNet) m_kind = ShotKind::Drop;
        else if (roll < 0.25f) m_kind = ShotKind::Slice;
        else if (roll < 0.25f + 0.3f * m_skill.risk) m_kind = ShotKind::Flat;
        else m_kind = ShotKind::Topspin;
        // Away from the opponent (the open court), more so when brave.
        // Aim is in the hitter's frame: its right is x * s in court space.
        const float oppRight = v.opponent.x * s / kDoublesHalfWidth; // -1..1, bot's frame
        m_aim.x = std::clamp(-oppRight * (0.6f + m_skill.risk * 0.5f) + (u(m_rng) - 0.5f) * 0.6f, -1.0f, 1.0f);
        m_aim.y = std::clamp(0.3f + m_skill.risk * 0.6f - u(m_rng) * 0.6f, -1.0f, 1.0f);
    }
    d.kind = m_kind;
    d.aim = m_aim;
    d.charge = m_skill.power;
    // Ready the swing as the ball comes close; the game hits it when it
    // reaches the hitting spot (TennisModule::tryHit).
    const glm::vec2 flat(v.ball.pos.x - v.feet.x, v.ball.pos.z - v.feet.z);
    d.swing = v.mayHit && glm::length(flat) < 3.0f && v.ball.pos.y > 0.1f && v.ball.pos.y < 3.0f;
    if (d.swing) m_planned = false;
    return d;
}

} // namespace tennis
