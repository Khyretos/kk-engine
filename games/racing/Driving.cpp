// Driving: each car's Jolt vehicle, its players' pedals or its CPU
// driver, where it is on the track and how it's drawn (the cameras are
// Cameras.cpp).

#include "RacingModule.h"

#include "kke/Application.h"
#include "kke/ImpactSynth.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>


#include <algorithm>
#include <cmath>

namespace racing {

namespace {

constexpr float kPitSpeed = 14.0f;  // m/s: the pit lane's limit (50 km/h)
constexpr float kGravity = 9.81f;

// Where the fingers are on a phone: the left half steers (how far from
// where the thumb went down), the right half's outer side is the
// accelerator and its inner side the brake. Player 1 only.
struct Touch {
    float steer = 0.0f, throttle = 0.0f, brake = 0.0f;
    bool any = false;
};
Touch readTouch() {
    Touch t;
    int devices = 0;
    SDL_TouchID* ids = SDL_GetTouchDevices(&devices);
    if (!ids) return t;
    for (int d = 0; d < devices; ++d) {
        int count = 0;
        SDL_Finger** fingers = SDL_GetTouchFingers(ids[d], &count);
        if (!fingers) continue;
        for (int i = 0; i < count; ++i) {
            const SDL_Finger& f = *fingers[i];
            t.any = true;
            if (f.x < 0.5f) t.steer = std::clamp((f.x - 0.25f) / 0.18f, -1.0f, 1.0f);
            else if (f.x > 0.75f) t.throttle = 1.0f;
            else t.brake = 1.0f;
        }
        SDL_free(fingers);
    }
    SDL_free(ids);
    return t;
}

float approach(float from, float to, float rate) { return from + std::clamp(to - from, -rate, rate); }

} // namespace

float RacingModule::random01(Car& c) {
    c.rng ^= c.rng << 13;
    c.rng ^= c.rng >> 17;
    c.rng ^= c.rng << 5;
    return static_cast<float>(c.rng & 0xffffff) / static_cast<float>(0x1000000);
}

void RacingModule::buildCar(Car& c, int slot) {
    kke::RigidWorld& w = m_rigid->world();
    c.art = &m_garage->art(c.type, c.kit, c.paint);
    const CarType& type = carTypes()[static_cast<size_t>(std::clamp(c.type, 0, static_cast<int>(carTypes().size()) - 1))];
    const glm::vec3 start(static_cast<float>(slot) * 4.0f, 1.0f, -50.0f);
    if (c.remote) {
        // Another machine drives it: a solid copy, moved to where it says.
        kke::RigidWorld::BodyDesc d;
        d.shape = kke::RigidWorld::Shape::ConvexHull;
        d.motion = kke::RigidWorld::Motion::Kinematic;
        d.points = c.art->hull;
        d.position = start;
        d.material = kke::AudioMaterialTable::Metal;
        c.body = w.add(d);
    } else {
        kke::VehicleDesc d = vehicleDesc(type, *c.art, event() == Event::Drag, event() == Event::Drift);
        d.position = start;
        d.material = kke::AudioMaterialTable::Metal;
        c.vehicle = w.addVehicle(d);
        c.body = w.vehicleBody(c.vehicle);
        // Out of the tyre warmers: in the window from the first corner
        // (drag cars heat theirs with a burnout).
        if (event() == Event::Drag) w.setVehicleTyreTemperature(c.vehicle, -1, 40.0f, 35.0f);
        else w.setVehicleTyreTemperature(c.vehicle, -1, 75.0f, 60.0f);
    }
    c.xf = c.prevXf = glm::translate(glm::mat4(1.0f), start);
    for (int i = 0; i < 4; ++i) c.wheelLocal[i] = glm::translate(glm::mat4(1.0f), c.art->wheelCenter[i]);
    c.bodyInst = m_models->spawn(c.art->body, c.xf);
    m_models->setOverlayEnabled(c.bodyInst, false);
    for (int i = 0; i < 4; ++i) {
        c.wheelInst[i] = m_models->spawn(c.art->wheel[i & 1], c.xf * c.wheelLocal[i]);
        m_models->setOverlayEnabled(c.wheelInst[i], false);
    }
    if (c.art->steering) {
        c.steerInst = m_models->spawn(c.art->steering, c.xf);
        m_models->setOverlayEnabled(c.steerInst, false);
    }
    c.glassHidden = false;
    c.dented.clear();
    c.dentedNormals.clear();
    c.pressed.clear();
    makeShell(c, slot);
    c.detached = 0;
    for (uint32_t& l : c.tyreLook) l = 0;
}

void RacingModule::removeCar(Car& c) {
    kke::RigidWorld& w = m_rigid->world();
    dropShell(c);
    if (c.bodyInst) m_models->remove(c.bodyInst);
    c.bodyInst = 0;
    for (kke::ModelModule::InstanceId& i : c.wheelInst) {
        if (i) m_models->remove(i);
        i = 0;
    }
    if (c.steerInst) m_models->remove(c.steerInst);
    c.steerInst = 0;
    if (c.vehicle) w.removeVehicle(c.vehicle);
    else if (c.body != kke::RigidWorld::kNoBody) w.remove(c.body);
    c.vehicle = 0;
    c.body = kke::RigidWorld::kNoBody;
}

void RacingModule::readCarState(Car& c) {
    kke::RigidWorld& w = m_rigid->world();
    w.vehicleState(c.vehicle, c.state);
    c.prevXf = c.xf;
    c.xf = w.transform(c.body);
    c.pastVelocity[c.pastHead] = c.velocity;
    c.pastHead = (c.pastHead + 1) % 6;
    c.velocity = w.velocity(c.body);
    const glm::mat4 inv = glm::inverse(c.xf);
    for (size_t i = 0; i < 4 && i < c.state.wheels.size(); ++i) c.wheelLocal[i] = inv * c.state.wheels[i].transform;
}

void RacingModule::updateTrackPosition(Car& c, float dt) {
    const Track& t = *m_track;
    const Track::Where was = c.where;
    const glm::vec3 pos = carPosition(c);
    c.where = t.locate(pos, c.where.sample);
    c.upsideDown = glm::dot(carUp(c), glm::vec3(0.0f, 1.0f, 0.0f)) < 0.35f ? c.upsideDown + dt : 0.0f;
    if (t.arena()) {
        // The pen: no line, no wrong way; only over the wall is off.
        c.wrongWay = 0.0f;
        c.offTrack = !t.insideArena(pos, -3.0f) || pos.y < -3.0f ? c.offTrack + dt : 0.0f;
        return;
    }
    if (t.stage() && was.s < t.startS() && c.where.s >= t.startS() && m_phase == Phase::Racing) c.lapStart = m_raceClock; // the stage clock starts
    if (t.closed()) {
        const float L = t.length();
        if (was.s > L * 0.75f && c.where.s < L * 0.25f) crossLine(c, 1);
        else if (was.s < L * 0.25f && c.where.s > L * 0.75f) crossLine(c, -1);
        c.progress = static_cast<float>(c.lap) * L + c.where.s;
    } else {
        c.progress = c.where.s - t.startS();
        if (m_phase == Phase::Racing && !c.finished && c.where.s >= t.finishS()) finishCar(c);
    }
    const Track::Sample f = t.at(c.where.s);
    c.wrongWay = glm::dot(c.velocity, f.forward) < -3.0f ? c.wrongWay + dt : 0.0f;
    // A stage has no walls: off into the fields is allowed, lost in them isn't.
    const float slack = t.stage() ? 30.0f : 4.0f;
    const bool off = c.where.u < t.minU() - slack || c.where.u > t.maxU() + slack || pos.y < t.groundHeight(pos.x, pos.z) - 3.0f;
    c.offTrack = off ? c.offTrack + dt : 0.0f;
}

kke::VehicleInput RacingModule::readPlayer(Car& c) {
    kke::VehicleInput in;
    const kke::InputMap& map = m_input->map(c.player);
    float steer = map.axis("steer");
    in.throttle = map.axis("throttle");
    in.brake = map.axis("brake");
    in.handBrake = map.held("handbrake") ? 1.0f : 0.0f;
    readWheel(c, steer, in.throttle, in.brake); // a wheel and pedals (Controllers.cpp)
    if (c.player == 0) {
        const Touch t = readTouch();
        if (t.any) {
            steer = t.steer;
            in.throttle = std::max(in.throttle, t.throttle);
            in.brake = std::max(in.brake, t.brake);
        }
    }
    // Steering: a keyboard's full lock arrives over a fifth of a second,
    // and less of it at speed (a stick too: small moves stay small).
    const float speed = std::fabs(carSpeed(c));
    steer *= 1.0f - 0.5f * std::clamp((speed - 12.0f) / 50.0f, 0.0f, 1.0f);
    in.steer = approach(c.input.steer, steer, 6.0f / 60.0f);
    // Stopped (or rolling back) with the brake held and no throttle: reverse.
    if (event() != Event::Drag && carSpeed(c) < 1.0f && in.brake > 0.3f && in.throttle < 0.1f) {
        in.throttle = -in.brake;
        in.brake = 0.0f;
    }
    return in;
}

// The CPU driver: follows its lane round the track a little ahead of the
// car, as fast as the corner ahead allows (the tyres' grip over the
// curve, helped by the banking), moves over for a slower car in front,
// pits when the car is hurt, flicks the handbrake into drift corners and
// launches and shifts on the strip.
kke::VehicleInput RacingModule::readCpu(Car& c, float dt) {
    kke::VehicleInput in;
    const Track& t = *m_track;
    const float speed = carSpeed(c);
    const glm::vec3 pos = carPosition(c), fwd = carForward(c), left = carLeft(c);
    const CarType& type = carTypes()[static_cast<size_t>(c.type)];
    auto steerTo = [&](const glm::vec3& target) {
        const glm::vec3 to = target - pos;
        const float angle = std::atan2(glm::dot(to, left), glm::dot(to, fwd)); // + = to the left
        return std::clamp(-angle / glm::radians(type.steer) * 1.3f, -1.0f, 1.0f);
    };
    if (event() == Event::Drag) {
        in.steer = steerTo(t.point(c.where.s + 25.0f, c.aiLane));
        const bool go = m_phase == Phase::Racing && m_raceClock >= c.aiReact && !c.finished;
        // Held on the brake until the green; after the line, a straight stop.
        in.throttle = go ? 1.0f : 0.0f;
        in.brake = go ? 0.0f : 1.0f;
        if (c.finished) in.brake = std::clamp(speed / 20.0f, 0.3f, 1.0f);
        // Up a gear near the top of the revs.
        const float rpm = c.state.rpm / type.maxRpm;
        if (go && rpm > c.aiShiftAt && c.gear < 6 && !c.state.switchingGear) c.gear = c.state.gear + 1;
        return in;
    }

    // Lanes: a new one every few seconds on the oval (two abreast races),
    // the middle of the road elsewhere.
    const float hw = t.halfWidth();
    const float clear = carHalfWidthOf(c) * 2.0f + 0.9f;
    // Is there a car beside us between here and lane u?
    auto sideBlocked = [&](float u) {
        for (const Car& o : m_cars) {
            if (&o == &c) continue;
            const float along = t.delta(c.where.s, o.where.s);
            if (along < -9.0f || along > 7.0f) continue;
            const float lo = std::min(c.where.u, u) - clear * 0.6f, hi = std::max(c.where.u, u) + clear * 0.6f;
            if (o.where.u > lo && o.where.u < hi && std::fabs(o.where.u - c.where.u) > 0.5f) return true;
        }
        return false;
    };
    // After the green everyone holds their line for a few seconds.
    if (m_phase == Phase::Racing) c.aiLaneTimer -= dt;
    if (c.aiLaneTimer <= 0.0f) {
        c.aiLaneTimer = 3.0f + random01(c) * 5.0f;
        const float want = event() == Event::Oval ? (random01(c) * 2.0f - 1.0f) * hw * 0.5f : (random01(c) * 2.0f - 1.0f) * hw * 0.2f;
        if (!sideBlocked(want)) c.aiLane = want;
    }
    float lane = c.aiLane;
    float limit = 1e9f;
    // The pits: in along the apron, stop in the box, out again.
    if (c.wantsPit && t.hasPits()) {
        const float toPit = t.delta(c.where.s, t.pitStart());
        if (toPit < 120.0f && toPit > -(t.pitEnd() - t.pitStart())) {
            lane = t.pitU();
            if (toPit < 60.0f) limit = kPitSpeed;
            const float box = t.pitStart() + (t.pitEnd() - t.pitStart()) * 0.55f;
            if (t.inPits(c.where) && t.delta(c.where.s, box) < 12.0f) limit = 0.0f; // in the box
        }
    }
    // Something slower ahead in this lane: go round it on the side with
    // room if nobody's there, otherwise follow it.
    const float lookAhead = std::clamp(10.0f + speed * 0.9f, 12.0f, 50.0f);
    for (const Car& o : m_cars) {
        if (&o == &c) continue;
        const float ahead = t.delta(c.where.s, o.where.s);
        if (ahead < 1.0f || ahead > 10.0f + speed * 1.2f) continue;
        const float closing = speed - carSpeed(o);
        if (std::fabs(o.where.u - lane) > clear || (closing < 0.5f && !o.totalled && ahead > 12.0f)) continue;
        if (m_crashTest && m_phase == Phase::Racing) {
            lane = o.where.u; // the damage test: straight into it
            break;
        }
        const float leftLane = std::min(o.where.u + clear, hw - 1.3f), rightLane = std::max(o.where.u - clear, -hw + 1.3f);
        const bool leftOk = leftLane - o.where.u >= clear * 0.9f && !sideBlocked(leftLane);
        const bool rightOk = o.where.u - rightLane >= clear * 0.9f && !sideBlocked(rightLane);
        if (leftOk || rightOk) {
            lane = leftOk && (!rightOk || std::fabs(leftLane - c.where.u) < std::fabs(rightLane - c.where.u)) ? leftLane : rightLane;
            c.aiLane = lane;
            c.aiLaneTimer = 2.5f;
        } else {
            // Boxed in: sit behind it, a car length back.
            const float gap = ahead - 7.0f;
            limit = std::min(limit, std::max(0.0f, carSpeed(o) + gap * 0.6f));
        }
    }
    if (!(c.wantsPit && lane == t.pitU())) lane = std::clamp(lane, -hw + 1.3f, hw - 1.3f);
    in.steer = steerTo(t.point(c.where.s + lookAhead, lane));

    // How fast: the tightest bit of track within braking distance.
    // The docks are tight: the drift event is taken more carefully, braking earlier.
    const bool drift = event() == Event::Drift;
    // On a stage's gravel the grip's two thirds of tarmac's (docs/VEHICLES.md "Tyres").
    const float grip = type.grip * (drift ? 0.72f : event() == Event::Rally ? 0.6f : 0.95f);
    const float brakeDist = speed * speed / (2.0f * (drift ? 5.5f : 7.5f)) + (drift ? 25.0f : 15.0f);
    const float k = t.maxCurvature(c.where.s, brakeDist);
    const float bank = std::fabs(t.at(c.where.s + brakeDist * 0.5f).bank);
    const float corner = std::sqrt(grip * kGravity * (1.0f + 1.4f * std::sin(bank)) / std::max(k, 1e-4f));
    float target = std::min(corner * c.aiPace, 95.0f);
    if (c.finished) target = std::min(target, m_track->stage() ? 0.0f : 25.0f); // a lap to cool down; the end of a stage is the end of the road
    target = std::min(target, limit);
    if (m_phase != Phase::Racing) target = 0.0f; // on the grid: no burnouts from the CPU drivers
    if (speed < target - 1.0f) {
        in.throttle = std::fabs(in.steer) > 0.8f ? 0.6f : 1.0f;
    } else if (speed > target + 1.5f) {
        in.brake = std::clamp((speed - target) / 6.0f, 0.25f, 1.0f);
    } else {
        in.throttle = 0.35f;
    }
    // Drift corners: a flick of the handbrake on the way in, then held
    // sideways on the throttle, the front wheels caught along the slide
    // and aimed down the road (only with nobody close by).
    if (drift && c.skill >= 1) {
        c.aiSlide = std::max(0.0f, c.aiSlide - dt);
        bool alone = true;
        for (const Car& o : m_cars)
            if (&o != &c && std::fabs(t.delta(c.where.s, o.where.s)) < 18.0f) alone = false;
        glm::vec3 v = c.velocity - glm::dot(c.velocity, carUp(c)) * carUp(c);
        const float vFwd = glm::dot(v, fwd), vLeft = glm::dot(v, left);
        const float slip = glm::degrees(std::atan2(vLeft, std::max(vFwd, 0.1f))); // + = sliding toward the car's left
        const bool corner = k > 1.0f / 60.0f;
        if (alone && corner && std::fabs(c.where.u) < hw * 0.35f && c.aiSlide <= 0.0f && std::fabs(slip) < 8.0f && speed > 11.0f && speed < 24.0f && random01(c) < 0.04f)
            c.aiSlide = 0.4f + 0.05f * static_cast<float>(c.skill);
        if (c.aiSlide > 0.0f) {
            // Turned in hard the way the road bends, the rear locked.
            const glm::vec3 now = t.at(c.where.s).forward, later = t.at(c.where.s + 25.0f).forward;
            const float bend = glm::cross(now, later).y; // + = bends left
            in.steer = bend > 0.0f ? -0.5f : 0.5f;
            in.handBrake = 1.0f;
            in.brake = 0.0f;
            in.throttle = 0.7f;
        } else if (alone && std::fabs(slip) > 8.0f && std::fabs(slip) < 70.0f && speed > 6.0f) {
            // Catch it: wheels along the way the car's going, plus the aim.
            const glm::vec3 dir = glm::length(v) > 0.1f ? glm::normalize(v) : fwd;
            const glm::vec3 dirLeft = glm::normalize(glm::cross(carUp(c), dir));
            const glm::vec3 to = t.point(c.where.s + lookAhead, 0.0f) - pos;
            const float aim = glm::degrees(std::atan2(glm::dot(to, dirLeft), glm::dot(to, dir)));
            const float wheel = slip + 0.8f * aim; // degrees, + = left
            in.steer = std::clamp(-wheel / type.steer, -1.0f, 1.0f);
            const float want = 22.0f + 4.0f * static_cast<float>(c.skill); // degrees of slide
            in.throttle = std::clamp(0.55f + (want - std::fabs(slip)) * 0.03f, 0.15f, 1.0f);
            in.brake = 0.0f;
            in.handBrake = 0.0f;
        }
    }
    // Spun round: counted, and put back on the track by driveCar.
    const glm::vec3 along = t.at(c.where.s).forward;
    if (m_phase == Phase::Racing && glm::dot(fwd, along) < -0.2f) c.aiWrongWay += dt;
    else c.aiWrongWay = 0.0f;
    // Stuck against something: back out, turned the other way.
    if (m_phase == Phase::Racing && !c.finished && in.throttle > 0.5f && std::fabs(speed) < 1.0f) c.aiStuck += dt;
    else c.aiStuck = std::max(0.0f, c.aiStuck - dt);
    if (c.aiStuck > 1.5f) {
        c.aiStuck = 0.0f;
        c.aiReverse = 1.3f;
    }
    if (c.aiReverse > 0.0f) {
        c.aiReverse -= dt;
        in.throttle = -1.0f;
        in.brake = 0.0f;
        in.handBrake = 0.0f;
        in.steer = -in.steer;
    }
    return in;
}

float RacingModule::carHalfWidthOf(const Car& c) { return c.art ? (c.art->boundsMax.x - c.art->boundsMin.x) * 0.5f : 1.0f; }

void RacingModule::driveCar(Car& c, float dt) {
    kke::RigidWorld& w = m_rigid->world();
    const CarType& type = carTypes()[static_cast<size_t>(c.type)];
    const size_t player = static_cast<size_t>(std::max(0, c.player));
    const bool mine = c.seat >= 0 && !c.cpu;
    // A player's own "back on the track"; everyone after being stuck or
    // upside down for a while.
    const bool asked = mine && player < m_resetAsked.size() && m_resetAsked[player] != 0;
    if (!c.totalled && (asked || c.upsideDown > (mine ? 4.0f : 2.5f) || c.offTrack > 3.0f || (c.cpu && c.aiWrongWay > 2.0f) || (c.cpu && c.aiStuck > 1.4f && c.aiReverse > 0.0f && std::fabs(carSpeed(c)) < 0.3f))) {
        const Track& t = *m_track;
        if (t.arena()) {
            // Right way up where it is, well inside the wall.
            glm::vec3 p = carPosition(c);
            p.y = 0.0f;
            while (!t.insideArena(p, 5.0f) && glm::length(p) > 1.0f) p *= 0.9f;
            glm::vec3 f = carForward(c);
            f.y = 0.0f;
            placeCar(c, p + glm::vec3(0.0f, 0.15f, 0.0f), glm::length(f) > 0.2f ? glm::normalize(f) : -glm::normalize(p + glm::vec3(0.01f)), glm::vec3(0.0f, 1.0f, 0.0f));
        } else {
            resetCarOnTrack(c, c.where.s, std::clamp(c.where.u, -t.halfWidth() + 1.5f, t.halfWidth() - 1.5f));
        }
        c.note = "Back on the track";
        c.noteTime = 1.5f;
        return;
    }

    kke::VehicleInput in = !c.cpu ? readPlayer(c) : event() == Event::Derby ? readDerbyCpu(c, dt) : readCpu(c, dt);
    // The strip: players change gear themselves.
    if (event() == Event::Drag) {
        if (mine && player < m_shift.size() && m_shift[player] != 0) {
            const float rpm = c.state.rpm / type.maxRpm;
            const int want = std::clamp(c.gear + m_shift[player], 1, 6);
            if (want > c.gear) {
                c.note = rpm > 0.9f ? "Perfect shift" : rpm > 0.78f ? "Good shift" : "Early shift";
                c.noteTime = 1.2f;
            }
            c.gear = want;
        }
        in.gear = c.gear;
    }
    // Before the green everyone is held (throttle and all: a burnout).
    // On the strip the brake is yours once the ambers come on.
    if (m_phase == Phase::Lobby || m_phase == Phase::Countdown) {
        const bool staging = event() == Event::Drag && m_phase == Phase::Countdown && m_countdown < 1.5f;
        if (!staging) in.brake = 1.0f;
        if (m_phase == Phase::Lobby) in.throttle = 0.0f;
        if (in.throttle < 0.0f) in.throttle = 0.0f;
    }
    // Drift: held on the line until this car's run.
    if (m_phase == Phase::Racing && m_raceClock < c.releaseAt) {
        in.throttle = c.cpu ? 0.0f : std::max(0.0f, in.throttle); // a player may burn out; the CPU drivers wait
        in.brake = 1.0f;
        in.handBrake = 0.0f;
        c.aiStuck = 0.0f;
        c.note = fmt::format("Your run: {:.0f}", std::ceil(c.releaseAt - m_raceClock));
        c.noteTime = 0.2f;
    }
    if (m_phase == Phase::Finished && !c.cpu && !mine) in = kke::VehicleInput{ 0.0f, 1.0f, 0.0f, 0.0f, 1 };
    // The pit lane's limiter, and repairs in the box.
    const bool pits = m_track->inPits(c.where);
    if (pits && carSpeed(c) > kPitSpeed) in.throttle = std::min(in.throttle, 0.0f);
    if (pits && std::fabs(carSpeed(c)) < 1.5f && m_damage > 0 && !c.totalled && (c.health < 99.9f || c.bent[0] + c.bent[1] + c.bent[2] + c.bent[3] > 0.0f || tyresHurt(c))) {
        c.pitTime += dt;
        repairCar(c, 30.0f * dt);
        c.note = fmt::format("Repairing: {:.0f}%", c.health);
        c.noteTime = 0.5f;
        if (c.cpu) in = kke::VehicleInput{ 0.0f, 1.0f, 0.0f, 0.0f, 1 };
        if (c.health >= 99.9f) {
            ++c.pitStops;
            c.wantsPit = false;
            c.note = "Repaired: go!";
            c.noteTime = 1.5f;
        }
    } else if (c.pitTime > 0.0f && !pits) {
        c.pitTime = 0.0f;
    }
    // Damage: a bent wheel pulls the car toward it.
    in.steer = std::clamp(in.steer + (c.bent[1] - c.bent[0]) * 0.1f, -1.0f, 1.0f);
    if (c.totalled) in = kke::VehicleInput{ 0.0f, 0.3f, 0.0f, 1.0f, 0 };
    c.input = in;
    w.setVehicleInput(c.vehicle, in);
}

void RacingModule::updateRemoteCar(Car& c, float dt) {
    kke::RigidWorld& w = m_rigid->world();
    c.prevXf = c.xf;
    c.xf = w.transform(c.body);
    c.velocity = c.hasNet ? c.net.velocity : glm::vec3(0.0f);
    if (c.hasNet) {
        // Where its machine has it (NetModule smooths the position between
        // messages; the rotation is eased toward the newest one here).
        // The host's CPU cars come a few times a second: carried on by
        // their speed in between.
        if (c.netId < 0) c.net.position += c.net.velocity * dt;
        const glm::quat now = glm::quat_cast(glm::mat3(c.xf));
        // Put back on the track, or a new race: it jumps there. Swept there
        // in one step instead, the solid copy would cross the gap at
        // hundreds of m/s and knock any car on the way flying (a crash
        // nobody drove into). A step's normal travel, plus slack for a
        // late message, is a drive.
        const float drive = std::max(5.0f, glm::length(c.net.velocity) * 0.5f);
        if (glm::length(c.net.position - glm::vec3(c.xf[3])) > drive) {
            w.setTransform(c.body, c.net.position, glm::normalize(c.net.rotation));
            c.prevXf = c.xf = w.transform(c.body); // drawn there now, no streak across the gap
        } else {
            w.moveKinematic(c.body, c.net.position, glm::normalize(glm::slerp(now, glm::normalize(c.net.rotation), 0.5f)), dt);
        }
    }
    c.where = m_track->locate(carPosition(c), c.where.sample);
    c.progress = c.hasNet ? c.net.progress : 0.0f;
    // Wheels: turning with the speed, the front ones steered.
    const float r = c.art->wheelRadius;
    c.wheelSpin += carSpeed(c) / std::max(r, 0.1f) * dt;
    const CarType& type = carTypes()[static_cast<size_t>(c.type)];
    for (int i = 0; i < 4; ++i) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), c.art->wheelCenter[i]);
        if (i < 2) m = glm::rotate(m, glm::radians(-c.net.steer * type.steer), glm::vec3(0.0f, 1.0f, 0.0f));
        c.wheelLocal[i] = glm::rotate(m, c.wheelSpin, glm::vec3(1.0f, 0.0f, 0.0f));
    }
}

glm::mat4 RacingModule::drawTransform(const Car& c) const {
    const float a = std::clamp(m_app->fixedAlpha(), 0.0f, 1.0f);
    const glm::vec3 p = glm::mix(glm::vec3(c.prevXf[3]), glm::vec3(c.xf[3]), a);
    const glm::quat q = glm::slerp(glm::quat_cast(glm::mat3(c.prevXf)), glm::quat_cast(glm::mat3(c.xf)), a);
    return glm::translate(glm::mat4(1.0f), p) * glm::mat4_cast(q);
}

void RacingModule::placeInstances(Car& c) {
    const glm::mat4 xf = drawTransform(c);
    m_models->setTransform(c.bodyInst, xf);
    for (int i = 0; i < 4; ++i) m_models->setTransform(c.wheelInst[i], xf * c.wheelLocal[i]);
    if (c.steerInst) {
        // The wheel turns about eight times as far as the front wheels do.
        const float turn = -c.input.steer * glm::radians(carTypes()[static_cast<size_t>(c.type)].steer) * 8.0f;
        m_models->setTransform(c.steerInst, xf * glm::translate(glm::mat4(1.0f), c.art->steerCenter) * glm::rotate(glm::mat4(1.0f), turn, c.art->steerAxis));
    }
    if (c.dentsChanged) {
        c.dentsChanged = false;
        if (c.glassHidden) {
            // Sitting inside: the windows folded away to nothing (the
            // dents, if any, kept).
            m_glassScratch = c.dented.empty() ? c.art->positions : c.dented;
            for (size_t p : c.art->glassParts)
                if (p < m_glassScratch.size()) std::fill(m_glassScratch[p].begin(), m_glassScratch[p].end(), c.art->eye);
            m_models->setDeformedVertices(c.bodyInst, m_glassScratch, c.dented.empty() ? c.art->normals : c.dentedNormals, true);
        } else if (c.dented.empty()) {
            m_models->setDeformedVertices(c.bodyInst, {}, {}, true);
        } else {
            m_models->setDeformedVertices(c.bodyInst, c.dented, c.dentedNormals, true);
        }
    }
}

} // namespace racing
