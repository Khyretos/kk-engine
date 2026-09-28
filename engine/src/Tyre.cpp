#include "kke/Tyre.h"

#include <algorithm>
#include <cmath>

namespace kke {

namespace {

// A car tyre's heat, lumped (J/K and W/K): the tread's surface layer
// warms in seconds, the carcass and its air in minutes.
constexpr float kSurfaceCapacity = 1500.0f;
constexpr float kCoreCapacity = 12000.0f;
constexpr float kSurfaceToCore = 80.0f;
constexpr float kSlideShare = 0.45f;       // of the sliding power that heats the tread (the rest heats the road)
constexpr float kRolling = 0.012f;         // the rubber's flex (rolling resistance) that heats the carcass
constexpr float kWearEnergy = 8.0e6f;      // J of sliding in the tread that wears a tyre out (cool)
constexpr float kShredDistance = 1500.0f;  // m a flat tyre lasts before it's off the rim
constexpr float kKelvin = 273.15f;

} // namespace

Tyre::Tyre(const TyreDesc& desc, float ambient) : m_desc(desc), m_ambient(ambient), m_surface(ambient), m_core(ambient) {}

void Tyre::update(const Step& s) {
    const float dt = s.dt;
    if (dt <= 0.0f) return;
    m_slidePower = 0.0f;
    if (m_condition == TyreCondition::Detached) return;
    const float load = std::max(0.0f, s.load), speed = std::fabs(s.speed);

    // Air: a puncture lets it out; low enough and it's flat.
    if (m_leak > 0.0f && m_condition == TyreCondition::Inflated) {
        m_air = std::max(0.0f, m_air - m_leak * dt);
        if (m_air < 0.25f) m_condition = TyreCondition::Flat;
    }

    const float slide = std::fabs(s.longitudinalForce) * std::fabs(s.longitudinalSlipSpeed) + std::fabs(s.lateralForce) * std::fabs(s.lateralSlipSpeed);
    m_slidePower = slide;
    if (m_condition == TyreCondition::Rim) return; // the rim: nothing left to heat or wear

    // Heat. The surface: sliding in, the air out (faster with speed), the
    // core and the road. The core: the rubber flexing as it rolls (more
    // when soft), the surface, the air and the rim.
    const float heating = std::max(0.0f, m_desc.heat);
    if (heating > 0.0f) {
        const float soft = 1.0f + 2.5f * std::max(0.0f, 1.0f - pressure() / std::max(0.1f, m_desc.pressure));
        const float slideIn = slide * kSlideShare * s.groundHeat * heating;
        const float rollIn = kRolling * load * speed * soft * heating;
        const float toCore = kSurfaceToCore * (m_surface - m_core);
        const float surfaceOut = (15.0f + 5.0f * speed) * (m_surface - m_ambient) + (load > 0.0f ? 40.0f : 0.0f) * (m_surface - (m_ambient + 10.0f));
        const float coreOut = (6.0f + 1.8f * speed) * (m_core - m_ambient);
        // Explicit steps are fine at these rates (the fastest, the surface
        // at 330 W/K over 1500 J/K, needs dt < 4 s).
        m_surface += (slideIn - toCore - surfaceOut) / kSurfaceCapacity * dt;
        m_core += (rollIn + toCore - coreOut) / kCoreCapacity * dt;
    }

    // Wear: sliding scrubs the tread off, hot rubber much faster; a flat
    // tyre shreds as it rolls.
    if (m_desc.wearRate > 0.0f) {
        const float hot = 1.0f + 2.0f * std::max(0.0f, (m_surface - 120.0f) / 30.0f);
        m_wear += m_desc.wearRate * slide * kSlideShare * hot / kWearEnergy * dt;
    }
    if (m_condition == TyreCondition::Flat) m_wear += speed * dt / kShredDistance;
    if (m_wear >= 1.0f) {
        m_wear = 1.0f;
        if (m_condition == TyreCondition::Inflated) {
            m_condition = TyreCondition::Flat; // it blows
            m_air = 0.0f;
            m_wear = 0.0f; // what's left of the carcass shreds from here
        } else if (m_condition == TyreCondition::Flat) {
            m_condition = TyreCondition::Rim;
        }
    }
}

float Tyre::grip() const {
    switch (m_condition) {
    case TyreCondition::Detached: return 0.0f;
    case TyreCondition::Rim: return 0.3f;
    case TyreCondition::Flat: return 0.5f;
    case TyreCondition::Inflated: break;
    }
    float g = 1.0f;
    if (m_desc.heat > 0.0f) {
        // The window: the surface mostly, the carcass some.
        const float t = 0.75f * m_surface + 0.25f * m_core;
        const float x = (t - m_desc.optimalTemp) / std::max(1.0f, m_desc.window);
        g -= m_desc.tempLoss * std::min(x * x, 1.0f);
        if (x > 1.0f) g -= 0.1f * std::min(x - 1.0f, 1.0f); // cooked: greasy
        // Pressure: best a little above the cold setting (hot tyres run ~10% up).
        const float p = pressure() / std::max(0.1f, m_desc.pressure);
        const float dp = (p - 1.1f) / 0.4f;
        g -= 0.08f * std::min(dp * dp, 1.0f);
    }
    g -= 0.1f * m_wear;
    return std::max(0.2f, g);
}

float Tyre::loadFactor(float load, float nominalLoad) const {
    if (nominalLoad <= 0.0f || load <= 0.0f || m_desc.loadSensitivity <= 0.0f) return 1.0f;
    // (1 - k) at twice the load: an exponent of -log2(1 - k).
    const float e = -std::log2(std::clamp(1.0f - m_desc.loadSensitivity, 0.05f, 1.0f));
    return std::clamp(std::pow(load / nominalLoad, -e), 0.7f, 1.3f);
}

float Tyre::radiusScale() const {
    const float rim = std::clamp(m_desc.rimRatio, 0.3f, 0.95f);
    switch (m_condition) {
    case TyreCondition::Detached: return 0.25f; // the hub
    case TyreCondition::Rim: return rim;
    case TyreCondition::Flat: return rim + (1.0f - rim) * 0.45f; // on its folded sidewalls
    case TyreCondition::Inflated: break;
    }
    return 1.0f - (1.0f - m_air) * 0.08f;
}

float Tyre::dragScale() const {
    switch (m_condition) {
    case TyreCondition::Detached: return 8.0f;
    case TyreCondition::Rim: return 3.0f;
    case TyreCondition::Flat: return 6.0f;
    case TyreCondition::Inflated: break;
    }
    return 1.0f + 2.5f * std::max(0.0f, 1.0f - m_air);
}

float Tyre::pressure() const {
    if (m_condition != TyreCondition::Inflated) return 0.0f;
    // The gas law: the air inside is at the core's temperature, set cold.
    return m_desc.pressure * m_air * (m_core + kKelvin) / (m_ambient + kKelvin);
}

void Tyre::puncture(float leak) {
    if (m_condition == TyreCondition::Inflated) m_leak = std::max(m_leak, std::max(0.0f, leak));
}

void Tyre::setCondition(TyreCondition c) {
    m_condition = c;
    if (c != TyreCondition::Inflated) m_air = 0.0f;
}

void Tyre::setTemperature(float surface, float core) {
    m_surface = surface;
    m_core = core;
}

void Tyre::replace() {
    m_surface = m_core = m_ambient;
    m_wear = 0.0f;
    m_air = 1.0f;
    m_leak = 0.0f;
    m_slidePower = 0.0f;
    m_condition = TyreCondition::Inflated;
}

} // namespace kke
