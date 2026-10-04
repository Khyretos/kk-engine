// Wheels: what the tyre model (kke/Tyre.h, docs/VEHICLES.md "Tyres") looks
// like. A tyre squashes where it meets the road and its sidewalls bulge,
// more under load (a landing, a banked turn); a puncture lets it down onto
// its sidewalls, driven on flat it shreds off the rim and the bare rim
// sparks on the road; a bent wheel wobbles as it turns; a hard enough hit
// on a wrecked corner tears the wheel off and it rolls away on its own.
// On loose ground the tyres throw up dust and stones instead of smoke.
//
// The squash is the nearest cars' only (the player's and a few around it):
// it moves every tyre vertex every frame the wheel turns, and from further
// away a 3 cm flat spot can't be seen. BeamNG does it with a soft-body
// tyre; this is the cheap trick of the same look (docs/VEHICLES.md).

#include "RacingModule.h"

#include "kke/Application.h"
#include "kke/ImpactSynth.h"
#include "kke/Log.h"
#include "kke/ParticleEffects.h"
#include "kke/modules/RigidBodyModule.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace racing {

namespace {

constexpr float kTyreStiffness = 90000.0f; // N/m, drawn (a real tyre ~200 kN/m: doubled so the squash reads)
constexpr float kRimRatio = 0.72f;         // kke::TyreDesc::rimRatio
constexpr size_t kSquashCars = 6;          // cars whose tyres squash (nearest the cameras)
constexpr float kSquashDistance = 35.0f;   // m
constexpr size_t kMaxLooseWheels = 16;
constexpr float kLooseWheelLife = 30.0f;   // s a torn-off wheel lies about

glm::vec3 dustColor(Ground g);

} // namespace

Ground groundNamed(const std::string& name) {
    if (name == "concrete") return Ground::Concrete;
    if (name == "grass") return Ground::Grass;
    if (name == "gravel") return Ground::Gravel;
    if (name == "dirt") return Ground::Dirt;
    if (name == "mud") return Ground::Mud;
    if (name == "snow") return Ground::Snow;
    return Ground::Tarmac;
}

void RacingModule::setGround(uint32_t material, Ground g) {
    m_grounds[material] = g;
    kke::GroundGrip grip;
    switch (g) {
    case Ground::Tarmac: grip = kke::GroundGrip::tarmac(); break;
    case Ground::Concrete: grip = kke::GroundGrip::concrete(); break;
    case Ground::Grass: grip = kke::GroundGrip::grass(); break;
    case Ground::Gravel: grip = kke::GroundGrip::gravel(); break;
    case Ground::Dirt: grip = kke::GroundGrip::dirt(); break;
    case Ground::Mud: grip = kke::GroundGrip::mud(); break;
    case Ground::Snow: grip = kke::GroundGrip::snow(); break;
    }
    m_rigid->world().setGroundGrip(material, grip);
}

Ground RacingModule::groundOf(uint32_t material) const {
    const auto it = m_grounds.find(material);
    return it == m_grounds.end() ? Ground::Tarmac : it->second;
}

// A bent rim runs out of true: the wheel's plane tilts in the wheel's own
// frame, so as it turns the tilt goes round with it (the wobble). A flat
// tyre flops about on its sidewalls.
void RacingModule::wobbleWheels(Car& c, float dt) {
    for (size_t i = 0; i < 4 && i < c.state.wheels.size(); ++i) {
        const kke::VehicleWheelState& w = c.state.wheels[i];
        c.wheelAngle[i] = std::fmod(c.wheelAngle[i] + w.angularVelocity * dt, glm::two_pi<float>());
        float degrees = 6.0f * c.bent[i];
        if (w.condition == kke::TyreCondition::Flat) degrees += 3.0f;
        else if (w.condition == kke::TyreCondition::Rim) degrees += 1.5f;
        if (degrees < 0.1f) continue;
        c.wheelLocal[i] = glm::rotate(c.wheelLocal[i], glm::radians(degrees), glm::vec3(0.0f, 0.0f, 1.0f));
    }
}

void RacingModule::updateTyreLooks() {
    // Where the players look from (split screen: every view).
    std::vector<glm::vec3> eyes;
    for (Car& c : m_cars)
        if (c.seat >= 0 && !c.remote) eyes.push_back(cameraOf(c).position);
    if (eyes.empty()) eyes.push_back(m_app->camera().position);
    std::vector<std::pair<float, size_t>> near;
    for (size_t i = 0; i < m_cars.size(); ++i) {
        const Car& c = m_cars[i];
        if (c.remote || c.state.wheels.size() < 4) continue;
        float d = 1e9f;
        for (const glm::vec3& e : eyes) d = std::min(d, glm::length(carPosition(c) - e));
        if (d < kSquashDistance) near.emplace_back(d, i);
    }
    std::sort(near.begin(), near.end());
    if (near.size() > kSquashCars) near.resize(kSquashCars);
    std::vector<bool> squash(m_cars.size(), false);
    for (const auto& n : near) squash[n.second] = true;

    for (size_t ci = 0; ci < m_cars.size(); ++ci) {
        Car& c = m_cars[ci];
        for (int i = 0; i < 4; ++i) {
            if (!c.wheelInst[i]) continue;
            const bool gone = (c.detached >> i) & 1u;
            uint32_t look = 0;
            glm::vec3 down(0.0f);
            float sink = 0.0f;
            kke::TyreCondition cond = kke::TyreCondition::Inflated;
            if (squash[ci] && !gone) {
                const kke::VehicleWheelState& w = c.state.wheels[static_cast<size_t>(i)];
                const float r = c.art->wheelRadius;
                cond = w.condition;
                // How far the road pushes the tread in: the load (a spring), and
                // what a flat or missing tyre has lost.
                const float squashed = w.contact ? std::min(w.load / kTyreStiffness, 0.25f * r) : 0.0f;
                sink = squashed + std::max(0.0f, r - w.radius);
                if (cond != kke::TyreCondition::Inflated || sink > 0.006f) {
                    // The road's direction in the wheel's own (spinning) frame.
                    down = glm::normalize(glm::transpose(glm::mat3(c.wheelLocal[i])) * glm::vec3(0.0f, -1.0f, 0.0f));
                    const int qy = static_cast<int>(std::lround(down.y * 90.0f)) & 0xff, qz = static_cast<int>(std::lround(down.z * 90.0f)) & 0xff;
                    const int qs = static_cast<int>(std::lround(sink * 400.0f)) & 0x7f;
                    look = 1u + static_cast<uint32_t>(qy) + (static_cast<uint32_t>(qz) << 8) + (static_cast<uint32_t>(qs) << 16) + (static_cast<uint32_t>(cond) << 24);
                }
            }
            if (look == c.tyreLook[i]) continue;
            c.tyreLook[i] = look;
            if (look == 0) {
                m_models->setDeformedVertices(c.wheelInst[i], {}, {}, true);
                continue;
            }
            const int side = i & 1;
            const CarArt& art = *c.art;
            m_tyreScratch = art.wheelPositions[side];
            const float r = art.wheelRadius, rim = r * kRimRatio, halfWidth = art.wheelWidth * 0.5f;
            const float reach = r - sink; // the tread goes no nearer the road than this from the axle
            const float bulge = cond == kke::TyreCondition::Flat ? 0.9f : 0.5f;
            for (std::vector<glm::vec3>& part : m_tyreScratch)
                for (glm::vec3& p : part) {
                    const float radial = std::sqrt(p.y * p.y + p.z * p.z);
                    if (radial <= rim * 1.02f) continue; // the rim itself
                    if (cond == kke::TyreCondition::Rim) {
                        // Shreds of rubber left round the rim.
                        p.y *= rim * 1.01f / radial;
                        p.z *= rim * 1.01f / radial;
                        continue;
                    }
                    const float d = glm::dot(p, down);
                    const float zone = d - (reach - sink * 1.5f); // the patch and the sidewall just above it
                    if (zone <= 0.0f) continue;
                    p -= down * std::max(0.0f, d - reach);
                    const float wall = std::clamp(std::fabs(p.x) / std::max(0.01f, halfWidth), 0.0f, 1.0f);
                    p.x += (p.x < 0.0f ? -1.0f : 1.0f) * std::min(zone, sink * 1.5f) * bulge * wall;
                }
            m_models->setDeformedVertices(c.wheelInst[i], m_tyreScratch, art.wheelNormals[side], true);
        }
    }
}

// Sparks off a bare rim, rubber off a flat, dust and stones on loose
// ground (the tarmac's smoke and marks are updateEffects').
void RacingModule::wheelEffects(Car& c, int wheel, float dt) {
    if (c.remote || static_cast<size_t>(wheel) >= c.state.wheels.size() || ((c.detached >> wheel) & 1u)) return;
    const kke::VehicleWheelState& w = c.state.wheels[static_cast<size_t>(wheel)];
    if (!w.contact) return;
    const glm::vec3 up = carUp(c);
    const float speed = glm::length(c.velocity);
    c.rimSpark[wheel] -= dt;
    if (w.condition == kke::TyreCondition::Rim) {
        // Metal on the road: a stream of sparks behind it.
        if (speed > 1.5f && c.rimSpark[wheel] <= 0.0f) {
            c.rimSpark[wheel] = 0.035f;
            m_fx->sparks(w.contactPoint + up * 0.03f, up, 2 + static_cast<int>(speed / 5.0f), 1.5f + speed * 0.25f, c.velocity * 0.9f);
        }
        return;
    }
    if (w.condition == kke::TyreCondition::Flat) {
        // The carcass flapping: puffs of dark rubber.
        if (speed > 3.0f && c.rimSpark[wheel] <= 0.0f) {
            c.rimSpark[wheel] = 0.09f;
            m_fx->smoke(w.contactPoint + up * 0.2f, up * 0.8f - c.velocity * 0.1f, glm::vec3(0.1f), 0.16f, 0.9f, 0.55f);
        }
    }
    const Ground g = groundOf(w.groundMaterial);
    if (!loose(g)) return;
    // Loose ground: dust off every tyre at speed, a cloud and stones where
    // they slide or spin (a rally car's roost).
    const float slide = std::clamp(w.slidePower / 40000.0f, 0.0f, 1.0f);
    const float amount = std::clamp(speed / 30.0f, 0.0f, 1.0f) * 0.35f + slide;
    if (amount < 0.08f || c.rimSpark[wheel] > 0.0f) return;
    c.rimSpark[wheel] = 0.05f / amount;
    const glm::vec3 color = dustColor(g);
    const glm::vec3 back = -carForward(c);
    const float heavy = g == Ground::Mud ? 0.5f : 1.0f; // mud flies in lumps, not clouds
    m_fx->smoke(w.contactPoint + up * 0.25f, c.velocity * 0.3f + up * (0.5f + slide) + back * (1.0f + 3.0f * slide), color,
                (0.35f + 0.5f * amount) * heavy, 1.8f + 1.5f * amount * heavy, 0.3f + 0.3f * std::min(amount, 1.0f));
    if (slide > 0.15f && g != Ground::Snow && g != Ground::Grass) {
        const int stones = 1 + static_cast<int>(slide * 4.0f);
        for (int s = 0; s < stones; ++s) {
            kke::ParticleEffects::Particle p;
            p.kind = kke::ParticleEffects::Kind::Smoke;
            p.position = w.contactPoint + up * 0.1f;
            p.velocity = c.velocity * 0.5f + back * (3.0f + 6.0f * slide * random01(c)) + up * (1.5f + 3.0f * random01(c)) +
                         carLeft(c) * ((random01(c) - 0.5f) * 3.0f);
            p.color = color * 0.6f;
            p.radius = 0.035f + 0.03f * random01(c);
            p.growth = 0.0f;
            p.opacity = 1.0f;
            p.life = 0.9f;
            p.drag = 0.3f;
            p.rise = -9.8f; // stones fall
            p.seed = random01(c);
            m_fx->emit(p);
        }
    }
}

// A hit at a corner: the tyre may puncture or blow; a wheel already bent
// all the way can be torn off.
void RacingModule::damageWheel(Car& c, int wheel, float speed, const glm::vec3& push) {
    if (!c.vehicle || ((c.detached >> wheel) & 1u)) return;
    kke::RigidWorld& w = m_rigid->world();
    const float harsh = m_damage == 2 ? 1.6f : 1.0f;
    if (speed > 8.0f && random01(c) < (speed - 8.0f) / 30.0f * harsh) {
        if (speed > 20.0f && random01(c) < 0.35f) w.setVehicleTyre(c.vehicle, wheel, kke::TyreCondition::Flat); // a blowout
        else w.punctureVehicleTyre(c.vehicle, wheel, 0.08f + 0.5f * random01(c));
    }
    if (c.bent[wheel] >= 0.99f && speed > 12.0f && random01(c) < 0.3f * harsh) tearOffWheel(c, wheel, push);
}

void RacingModule::tearOffWheel(Car& c, int wheel, const glm::vec3& push) {
    // A remote car (online) has no vehicle here: its machine tore it off,
    // and this one shows it going, from where its wheel is drawn.
    if (wheel < 0 || wheel > 3 || ((c.detached >> wheel) & 1u)) return;
    if (c.vehicle ? static_cast<size_t>(wheel) >= c.state.wheels.size() : !c.remote) return;
    kke::RigidWorld& w = m_rigid->world();
    c.detached = static_cast<uint8_t>(c.detached | (1u << wheel));
    if (c.vehicle) w.setVehicleTyre(c.vehicle, wheel, kke::TyreCondition::Detached);
    m_models->setVisible(c.wheelInst[wheel], false);
    const glm::mat4 xf = c.vehicle ? c.state.wheels[static_cast<size_t>(wheel)].transform : c.xf * c.wheelLocal[wheel];
    const CarArt& art = *c.art;
    // The wheel as a body of its own: a 12-sided drum, spinning as it was.
    kke::RigidWorld::BodyDesc b;
    b.shape = kke::RigidWorld::Shape::ConvexHull;
    for (int k = 0; k < 12; ++k) {
        const float a = glm::two_pi<float>() * static_cast<float>(k) / 12.0f;
        for (float x : { -art.wheelWidth * 0.5f, art.wheelWidth * 0.5f })
            b.points.push_back(glm::vec3(x, std::cos(a) * art.wheelRadius, std::sin(a) * art.wheelRadius));
    }
    b.position = glm::vec3(xf[3]);
    b.rotation = glm::normalize(glm::quat_cast(glm::mat3(xf)));
    b.velocity = c.velocity + push;
    b.angularVelocity = glm::cross(carUp(c), c.velocity) / std::max(0.1f, art.wheelRadius);
    b.mass = 22.0f;
    b.friction = 0.9f;
    b.restitution = 0.35f;
    b.material = kke::AudioMaterialTable::Rubber;
    LooseWheel lw;
    lw.body = w.add(b);
    lw.inst = m_models->spawn(art.wheel[wheel & 1], xf);
    m_models->setOverlayEnabled(lw.inst, false);
    lw.prev = lw.now = xf;
    if (m_looseWheels.size() >= kMaxLooseWheels) {
        w.remove(m_looseWheels.front().body);
        m_models->remove(m_looseWheels.front().inst);
        m_looseWheels.erase(m_looseWheels.begin());
    }
    m_looseWheels.push_back(lw);
    m_fx->sparks(b.position, carUp(c), 30, 6.0f, c.velocity * 0.5f);
    sound(b.position, kke::AudioMaterialTable::Metal, 0.9f);
    kke::log::get(name())->info("{} lost a wheel ({})", c.name, wheel);
    c.note = "Lost a wheel!";
    c.noteTime = 2.5f;
}

bool RacingModule::tyresHurt(const Car& c) {
    if (c.detached) return true;
    for (const kke::VehicleWheelState& w : c.state.wheels)
        if (w.condition != kke::TyreCondition::Inflated || w.wear > 0.6f) return true;
    return false;
}

void RacingModule::refitWheels(Car& c) {
    if (c.vehicle) {
        kke::RigidWorld& w = m_rigid->world();
        w.replaceVehicleTyres(c.vehicle);
        w.setVehicleTyreTemperature(c.vehicle, -1, 70.0f, 55.0f); // off the warmers
    }
    for (int i = 0; i < 4; ++i) {
        if ((c.detached >> i) & 1u) m_models->setVisible(c.wheelInst[i], true);
        c.tyreLook[i] = ~0u; // redrawn next frame
    }
    c.detached = 0;
}

void RacingModule::updateLooseWheels(float dt) {
    kke::RigidWorld& w = m_rigid->world();
    for (size_t i = m_looseWheels.size(); i-- > 0;) {
        LooseWheel& lw = m_looseWheels[i];
        lw.age += dt;
        lw.prev = lw.now;
        lw.now = w.transform(lw.body);
        if (lw.age > kLooseWheelLife || lw.now[3].y < -5.0f) {
            w.remove(lw.body);
            m_models->remove(lw.inst);
            m_looseWheels.erase(m_looseWheels.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        m_models->setTransform(lw.inst, lw.now);
    }
}

void RacingModule::clearLooseWheels() {
    kke::RigidWorld& w = m_rigid->world();
    for (LooseWheel& lw : m_looseWheels) {
        w.remove(lw.body);
        m_models->remove(lw.inst);
    }
    m_looseWheels.clear();
}

namespace {

glm::vec3 dustColor(Ground g) {
    switch (g) {
    case Ground::Grass: return { 0.42f, 0.4f, 0.28f };
    case Ground::Gravel: return { 0.62f, 0.57f, 0.48f };
    case Ground::Dirt: return { 0.55f, 0.43f, 0.31f };
    case Ground::Mud: return { 0.3f, 0.22f, 0.14f };
    case Ground::Snow: return { 0.94f, 0.95f, 0.98f };
    case Ground::Tarmac:
    case Ground::Concrete: break;
    }
    return { 0.6f, 0.6f, 0.6f };
}

} // namespace

} // namespace racing
