#include "Care.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace pet_companion {

namespace {

float approach(float value, float target, float rate, float dt) { return value + (target - value) * (1.0f - std::exp(-rate * dt)); }

} // namespace

void Care::update(const Needs& n, const World& w, float dt) {
    m_needs = n;
    if (m_dead) return;
    const float worst = std::max(n.hunger, n.thirst);
    // Health: going without for long, or living among its own mess, makes
    // it ill; looked after, it recovers (slowly: a few minutes from sick).
    float sickening = 0.0f;
    if (n.hunger > 0.85f) sickening += 0.002f;
    if (n.thirst > 0.85f) sickening += 0.003f;
    if (w.poops > 0 && w.oldestPoop > 90.0f) sickening += 0.0015f * float(std::min(w.poops, 4));
    if (sickening > 0.0f) m_health -= sickening * dt;
    else if (worst < 0.6f && n.tiredness < 0.8f) m_health += 0.006f * dt;
    m_health = std::clamp(m_health, 0.0f, 1.0f);
    // At zero and still going without: a minute later it dies. Any care
    // that brings its health up even a little stops the clock.
    if (m_health <= 0.0f && sickening > 0.0f) m_dying += dt;
    else m_dying = std::max(0.0f, m_dying - dt * 2.0f);
    if (m_dying >= kDyingTime) {
        m_dead = true;
        m_happy = 0.0f;
        return;
    }

    // Happiness drifts toward how its life is going.
    m_lonely = w.playerDistance > 15.0f ? m_lonely + dt : std::max(0.0f, m_lonely - dt * 4.0f);
    float content = 0.6f;
    content -= std::max(0.0f, worst - 0.5f) * 0.8f;
    content -= std::max(0.0f, n.tiredness - 0.7f) * 0.4f;
    content -= 0.06f * float(std::min(w.poops, 4));
    content -= std::min(0.25f, m_lonely / 240.0f);
    content -= (1.0f - m_health) * 0.4f;
    if (w.playing) content += 0.25f;
    m_happy = approach(m_happy, std::clamp(content, 0.0f, 1.0f), 0.03f, dt);
    m_excited = approach(m_excited, sick() ? 0.0f : 0.2f, 0.25f, dt);

    // What went in comes out: a meal takes a while to digest.
    m_digestion = std::min(1.2f, m_digestion);
    m_bladder = std::min(1.2f, m_bladder + dt / 300.0f);
}

void Care::ate(float amount) { m_digestion += amount * 1.1f; }
void Care::drank(float amount) { m_bladder += amount * 0.8f; }

void Care::petted(float dt) {
    m_happy = std::min(1.0f, m_happy + dt * 0.12f);
    m_health = std::min(1.0f, m_health + dt * 0.002f);
}

void Care::praised(float amount) {
    m_happy = std::min(1.0f, m_happy + amount);
    excite(amount * 2.0f);
}

void Care::excite(float amount) { m_excited = std::clamp(m_excited + amount, 0.0f, 1.0f); }

std::string Care::mood() const {
    if (m_dead) return "passed away";
    if (m_dying > 0.0f) return "dying: it needs food and water now";
    if (sick()) return "feeling poorly";
    if (m_needs.thirst > 0.75f) return "thirsty";
    if (m_needs.hunger > 0.75f) return "hungry";
    if (m_needs.tiredness > 0.85f) return "sleepy";
    if (m_happy > 0.85f) return "over the moon";
    if (m_excited > 0.6f) return "excited";
    if (m_happy > 0.6f) return "happy";
    if (m_happy > 0.35f) return "content";
    return "sad";
}

std::string Care::moodColor() const {
    if (m_dead) return "#aab3cc";
    if (sick() || m_happy < 0.35f) return "#ff8a7a";
    if (m_needs.thirst > 0.75f || m_needs.hunger > 0.75f || m_needs.tiredness > 0.85f) return "#ffcf5c";
    if (m_happy > 0.6f) return "#6fe39a";
    return "#e8ecf4";
}

nlohmann::json Care::save() const {
    return { { "happiness", m_happy }, { "health", m_health }, { "digestion", m_digestion }, { "bladder", m_bladder }, { "dead", m_dead } };
}

void Care::load(const nlohmann::json& j) {
    if (!j.is_object()) return;
    m_happy = std::clamp(j.value("happiness", m_happy), 0.0f, 1.0f);
    m_health = std::clamp(j.value("health", m_health), 0.0f, 1.0f);
    m_digestion = std::clamp(j.value("digestion", m_digestion), 0.0f, 1.2f);
    m_bladder = std::clamp(j.value("bladder", m_bladder), 0.0f, 1.2f);
    m_dead = j.value("dead", false);
}

} // namespace pet_companion
