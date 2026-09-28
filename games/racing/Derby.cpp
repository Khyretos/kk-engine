// The destruction derby: a walled dirt pen, everyone starting round the
// edge facing in, and the last car still running wins. Points for the
// damage you deal (when you drove into them, not when they drove into
// you) and a bonus for each car you finish off; they decide the places
// among the survivors when time runs out. Hits to the front hurt most
// (the engine), the boot least (Damage.cpp hitCar), so the CPU drivers
// who know the trick reverse into people. Sit out too long without
// hitting anyone and you're out, as in a real derby.

#include "RacingModule.h"

#include "kke/Log.h"
#include "kke/modules/RigidBodyModule.h"

#include <algorithm>
#include <cmath>

namespace racing {

namespace {

constexpr float kDerbyTime = 240.0f; // s: then the survivors are placed on points
constexpr float kIdleOut = 90.0f;    // s without driving into anyone: out (while more than two are left)
constexpr float kWreckBonus = 50.0f; // points for finishing a car off
constexpr float kPileupEvery = 7.0f;  // s between pile-ups (KKE_RACE_PILEUP)
constexpr float kPileupSpeed = 30.0f; // m/s each car is launched at (108 km/h)

} // namespace

void RacingModule::updateDerby() {
    int running = static_cast<int>(std::count_if(m_cars.begin(), m_cars.end(), [](const Car& c) { return !c.totalled && !c.finished; }));
    for (Car& c : m_cars) {
        if (c.totalled || c.finished || c.remote || running <= 2) continue; // the last two fight it out
        const float idle = m_raceClock - c.lastAttack;
        if (idle > kIdleOut) {
            knockOut(c, "no hits for 90 s");
            --running;
        } else if (idle > kIdleOut - 10.0f && c.seat >= 0) {
            c.note = fmt::format("Hit someone! {:.0f}", std::ceil(kIdleOut - idle));
            c.noteTime = 0.3f;
        }
    }
    // One left (or the time's up): they're done, the table decides.
    if ((m_cars.size() > 1 && running <= 1) || m_raceClock > kDerbyTime)
        for (Car& c : m_cars)
            if (!c.totalled && !c.finished) finishCar(c);
}

// `into` points into the victim: the attacker is whoever was moving that
// way faster just before (after, they've bounced). A glancing rub with
// both moving the same way is nobody's; a head-on is both of theirs.
void RacingModule::derbyHit(Car& victim, Car* by, const glm::vec3& into, float hurt) {
    if (!by || by == &victim) return;
    const float push = glm::dot(velocityBefore(*by) - velocityBefore(victim), into);
    if (push < 1.0f) return;
    by->derbyPoints += hurt;
    by->lastAttack = m_raceClock;
    victim.lastHitBy = static_cast<int>(by - m_cars.data());
    victim.lastHitAt = m_raceClock;
}

void RacingModule::knockOut(Car& c, const std::string& why) {
    if (!c.totalled) {
        c.health = 0.0f;
        c.totalled = true;
        c.outAt = m_raceClock;
        applyDamage(c);
    }
    c.note = why.empty() ? std::string("WRECKED") : "Out: " + why;
    c.noteTime = 5.0f;
    // The wreck goes to whoever hit it last, if that was recent.
    if (why.empty() && c.lastHitBy >= 0 && m_raceClock - c.lastHitAt < 10.0f) {
        Car& by = m_cars[static_cast<size_t>(c.lastHitBy)];
        by.derbyPoints += kWreckBonus;
        ++by.wrecked;
        by.note = fmt::format("Wrecked {}! +{:.0f}", c.name, kWreckBonus);
        by.noteTime = 2.5f;
        kke::log::get(name())->info("{} wrecked {}", by.name, c.name);
    } else if (!why.empty()) {
        kke::log::get(name())->info("{} is out: {}", c.name, why);
    }
}

// A CPU derby driver: picks a victim (close, hurt, in front of it), drives
// at where it's going to be, and when the victim is close behind, puts it
// in reverse and backs into it. Keeps off the wall, and backs out of a jam.
kke::VehicleInput RacingModule::readDerbyCpu(Car& c, float dt) {
    kke::VehicleInput in;
    if (m_phase != Phase::Racing) return in; // held on the brake (driveCar)
    const Track& t = *m_track;
    const float speed = carSpeed(c);
    const glm::vec3 pos = carPosition(c), fwd = carForward(c), left = carLeft(c);
    const CarType& type = carTypes()[static_cast<size_t>(c.type)];
    auto steerAt = [&](float angle) { return std::clamp(-angle / glm::radians(type.steer) * 1.3f, -1.0f, 1.0f); }; // angle + = to the left

    // A victim every few seconds.
    c.aiLaneTimer -= dt;
    const bool gone = c.aiTarget < 0 || c.aiTarget >= static_cast<int>(m_cars.size()) || m_cars[static_cast<size_t>(c.aiTarget)].totalled;
    if (gone || c.aiLaneTimer <= 0.0f) {
        c.aiLaneTimer = 2.0f + random01(c) * 2.5f;
        float best = 1e9f;
        c.aiTarget = -1;
        for (size_t i = 0; i < m_cars.size(); ++i) {
            const Car& o = m_cars[i];
            if (&o == &c || o.totalled) continue;
            const glm::vec3 to = carPosition(o) - pos;
            const float d = glm::length(to);
            const float inFront = d > 0.1f ? glm::dot(to / d, fwd) : 0.0f;
            const float score = d + o.health * 0.15f - inFront * 8.0f + random01(c) * 8.0f;
            if (score < best) {
                best = score;
                c.aiTarget = static_cast<int>(i);
            }
        }
    }

    if (c.aiTarget < 0) {
        // Nobody left to hit: a slow lap of the pen.
        in.steer = steerAt(std::atan2(glm::dot(-pos, left), glm::dot(-pos, fwd)) - 1.2f);
        in.throttle = speed < 6.0f ? 0.5f : 0.0f;
        return in;
    }
    const Car& o = m_cars[static_cast<size_t>(c.aiTarget)];
    const glm::vec3 there = carPosition(o);
    const float dist = glm::length(there - pos);
    // Where it'll be when we get there (not quite: they dodge).
    const glm::vec3 aim = there + o.velocity * std::clamp(dist / std::max(std::fabs(speed), 6.0f), 0.0f, 1.2f) * 0.6f;
    const glm::vec3 to = aim - pos;
    const float ahead = glm::dot(to, fwd) / std::max(glm::length(to), 0.1f);

    const bool backwards = c.skill >= 1 && dist < 16.0f && ahead < -0.35f;
    // Nose on the wall (or nearly) and the victim elsewhere: back off it,
    // swinging the nose round toward them (a three-point turn).
    const bool walled = !t.insideArena(pos + fwd * 3.5f, 1.0f) && ahead < 0.7f;
    if (walled && !backwards) {
        in.steer = -steerAt(std::atan2(glm::dot(to, left), glm::dot(to, fwd)));
        in.throttle = -0.8f;
    } else if (backwards) {
        // Boot first: the rear goes the way the wheel's turned.
        in.steer = steerAt(std::atan2(glm::dot(to, left), -glm::dot(to, fwd)));
        in.throttle = speed > -9.0f ? -1.0f : -0.4f;
    } else {
        const float angle = std::atan2(glm::dot(to, left), glm::dot(to, fwd));
        in.steer = steerAt(angle);
        // Build up speed for a hit, but turn first when it's off to the side.
        float want = std::clamp(6.0f + dist * 0.6f, 8.0f, 17.0f) * (0.85f + 0.05f * static_cast<float>(c.skill));
        if (std::fabs(angle) > glm::radians(60.0f)) want = 7.0f;
        if (speed < want - 1.0f) in.throttle = std::fabs(in.steer) > 0.8f ? 0.7f : 1.0f;
        else if (speed > want + 2.0f) in.brake = 0.5f;
        else in.throttle = 0.4f;
        // Heading into the wall: off the gas, turned in toward the middle.
        if (speed > 6.0f && !t.insideArena(pos + fwd * speed * 0.9f, 4.0f)) {
            in.throttle = 0.0f;
            in.brake = 0.4f;
            in.steer = steerAt(std::atan2(glm::dot(-pos, left), glm::dot(-pos, fwd)));
        }
    }

    // Jammed (on the wall, locked with another car, beached): the other
    // way for long enough that the gearbox gets there and the car moves.
    const bool stopped = std::fabs(speed) < 0.8f;
    if (c.aiUnstick == 0.0f) {
        c.aiStuck = stopped ? c.aiStuck + dt : std::max(0.0f, c.aiStuck - dt * 2.0f);
        if (c.aiStuck > 2.0f) {
            c.aiStuck = 0.0f;
            c.aiUnstick = in.throttle > 0.0f ? -2.0f : 2.0f;
        }
    }
    if (c.aiUnstick != 0.0f) {
        const float dir = c.aiUnstick > 0.0f ? 1.0f : -1.0f;
        in.throttle = dir;
        in.brake = 0.0f;
        in.steer = -in.steer;
        c.aiUnstick -= dir * dt;
        if (c.aiUnstick * dir <= 0.0f) c.aiUnstick = 0.0f;
    }
    return in;
}

// The benchmark's worst case (KKE_RACE_PILEUP=1): every car mended, put
// back round the edge of the pen and launched at the middle at 108 km/h,
// all at once, every few seconds. Everything the racing demo can do fires
// together: every FEMFX body crumpling, tyres blowing and wheels tearing
// off, debris, sparks, smoke and fire, dozens of contacts a step.
void RacingModule::updatePileup(float dt) {
    m_pileupTimer -= dt;
    if (m_pileupTimer > 0.0f) return;
    m_pileupTimer = kPileupEvery;
    if (m_pileups > 0)
        kke::log::get(name())->info("pile-up {}: {} of {} cars wrecked", m_pileups,
                                    std::count_if(m_cars.begin(), m_cars.end(), [](const Car& c) { return c.totalled; }), m_cars.size());
    ++m_pileups;
    const Track& t = *m_track;
    const int n = static_cast<int>(m_cars.size());
    kke::RigidWorld& w = m_rigid->world();
    for (int i = 0; i < n; ++i) {
        Car& c = m_cars[static_cast<size_t>(i)];
        if (c.remote) continue;
        repairCar(c, 1000.0f);
        const glm::vec3 forward = t.gridForward(i, n);
        placeCar(c, t.gridPosition(i, n) + glm::vec3(0.0f, 0.15f, 0.0f), forward, glm::vec3(0.0f, 1.0f, 0.0f));
        w.setVelocity(c.body, forward * kPileupSpeed);
        c.velocity = forward * kPileupSpeed;
        std::fill(std::begin(c.pastVelocity), std::end(c.pastVelocity), c.velocity);
    }
    kke::log::get(name())->info("pile-up {}: {} cars at {:.0f} km/h into the middle", m_pileups, n, kPileupSpeed * 3.6f);
}

} // namespace racing
