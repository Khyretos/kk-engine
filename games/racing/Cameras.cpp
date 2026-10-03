// The cameras. Each player has their own (Y on a controller, C on the
// keyboard, a flight stick's second button: the next one) and looks
// round with the right stick, a flight stick's hat, or the mouse with its
// right button held; let go and the view swings back to the road.
//
//   Chase         behind the car, its heading lagging the car's a little
//   Far chase     further back and higher: the pack round you
//   Cockpit       the driver's seat: the dashboard in front of you, the
//                 steering wheel turning with yours, the car rolling and
//                 pitching round you
//   First person  the driver's eyes: the same seat, the head thrown about
//                 by the g-forces (braking leans you forward, a corner
//                 sideways) and turning into the corner before the car does
//   Bonnet        on the bonnet, nothing in the way
//   Wheel         bolted to the sill, looking back at the front tyre (it
//                 squashes, it bulges, it smokes)
//   TV            trackside, following the car past
//
// Inside the car the windows are folded away (Driving.cpp placeInstances):
// the pack's glass is drawn dark and solid from outside.

#include "RacingModule.h"

#include "kke/Log.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"

#include <glm/gtc/constants.hpp>

#include <cctype>

namespace racing {

namespace {

constexpr float kLookRate = 12.0f;      // 1/s: how fast the view follows the stick
constexpr float kLookBack = 0.6f;       // s the mouse's look stays before it swings back
constexpr float kInsideNear = 0.08f;    // m: the near plane sitting inside (the wheel is ~0.5 m away)
constexpr float kOutsideNear = 0.2f;

bool inside(CamMode m) { return m == CamMode::Cockpit || m == CamMode::FirstPerson; }

} // namespace

const char* camModeName(CamMode m) {
    switch (m) {
    case CamMode::Chase: return "Chase";
    case CamMode::Far: return "Far chase";
    case CamMode::Cockpit: return "Cockpit";
    case CamMode::FirstPerson: return "First person";
    case CamMode::Bonnet: return "Bonnet";
    case CamMode::Wheel: return "Wheel";
    case CamMode::Tv: return "TV";
    case CamMode::Count: break;
    }
    return "Chase";
}

CamMode camModeNamed(const std::string& name) {
    std::string n;
    for (char ch : name) n += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const char* ids[] = { "chase", "far", "cockpit", "first", "bonnet", "wheel", "tv" };
    for (int i = 0; i < static_cast<int>(CamMode::Count); ++i)
        if (n == ids[i] || n == std::to_string(i)) return static_cast<CamMode>(i);
    if (n == "firstperson" || n == "first_person" || n == "helmet") return CamMode::FirstPerson;
    return CamMode::Count;
}

kke::Camera& RacingModule::cameraOf(Car& c) { return c.seat >= 0 && c.player == 0 ? m_app->camera() : c.camera; }

void RacingModule::nextCamera(Car& c) {
    c.cam = static_cast<CamMode>((static_cast<int>(c.cam) + 1) % static_cast<int>(CamMode::Count));
    c.camInit = false;
    c.look = c.lookWant = glm::vec2(0.0f);
    c.note = std::string("Camera: ") + camModeName(c.cam);
    c.noteTime = 1.2f;
    kke::log::get(name())->info("{}: {} camera", c.name, camModeName(c.cam));
}

// Where the right stick points is where you look: up is ahead, right to
// the right, down behind you (as Forza and GT do). The mouse (right
// button held) turns the view by as much as it moves; let go and it
// swings back.
void RacingModule::readLook(Car& c, float dt) {
    const kke::InputMap& map = m_input->map(c.player);
    const float maxYaw = inside(c.cam) || c.cam == CamMode::Bonnet ? glm::radians(125.0f) : glm::pi<float>();
    const glm::vec2 stick = map.axis2("look");
    const float len = glm::length(stick);
    if (len > 0.25f) {
        const float yaw = std::atan2(stick.x, -stick.y);
        const float amount = std::min(1.0f, (len - 0.25f) / 0.6f);
        c.lookWant = glm::vec2(std::clamp(yaw, -maxYaw, maxYaw) * amount, 0.0f);
        c.lookHold = 0.0f;
    } else if (map.held("look.free")) {
        float sensitivity = 0.004f, invert = 1.0f;
        if (m_shell) {
            sensitivity *= m_shell->engineSettings().controls.mouseSensitivity;
            if (m_shell->engineSettings().controls.invertY) invert = -1.0f;
        }
        const glm::vec2 d = map.axis2("look.mouse") * sensitivity;
        c.lookWant.x = std::clamp(c.lookWant.x + d.x, -maxYaw, maxYaw);
        c.lookWant.y = std::clamp(c.lookWant.y - d.y * invert, -0.6f, 0.5f);
        c.lookHold = kLookBack;
    } else {
        c.lookHold -= dt;
        if (c.lookHold <= 0.0f) c.lookWant = glm::vec2(0.0f);
    }
    // Round the back the short way (from 170 degrees right to 170 left
    // is 20 degrees, not 340).
    if (c.lookWant.x - c.look.x > glm::pi<float>()) c.look.x += glm::two_pi<float>();
    else if (c.lookWant.x - c.look.x < -glm::pi<float>()) c.look.x -= glm::two_pi<float>();
    c.look += (c.lookWant - c.look) * (1.0f - std::exp(-kLookRate * dt));
}

// First person: the head on its neck. The car's acceleration in its own
// frame pushes it the other way (braking throws you forward, a left
// turn to the right, a landing down), a spring brings it back; and the
// eyes turn into the corner ahead of the car, the way a driver looks to
// the apex.
void RacingModule::updateHead(Car& c, float dt) {
    if (dt <= 0.0f) return;
    const glm::vec3 accel = (c.velocity - c.lastVel) / dt;
    c.lastVel = c.velocity;
    if (c.cam != CamMode::FirstPerson) {
        c.head = c.headVel = glm::vec3(0.0f);
        c.headYaw = 0.0f;
        return;
    }
    const glm::mat3 toCar = glm::transpose(glm::mat3(c.xf));
    glm::vec3 g = toCar * accel; // car space: +X left, +Y up, +Z forward
    g.y = std::clamp(g.y, -25.0f, 25.0f);
    glm::vec3 want = -g * glm::vec3(0.0045f, 0.0025f, 0.004f);
    const float far = glm::length(want);
    if (far > 0.1f) want *= 0.1f / far;
    // A damped spring: quick, a little bounce.
    const float k = 160.0f, damp = 18.0f;
    c.headVel += ((want - c.head) * k - c.headVel * damp) * dt;
    c.head += c.headVel * dt;
    const float into = c.input.steer * 0.28f;
    c.headYaw += (into - c.headYaw) * (1.0f - std::exp(-5.0f * dt));
}

// Inside: the windows away (Driving.cpp placeInstances) while this car's
// own player sits in it.
void RacingModule::updateInterior(Car& c) {
    const bool hide = c.seat >= 0 && !c.remote && inside(c.cam) && !c.art->glassParts.empty();
    if (hide != c.glassHidden) {
        c.glassHidden = hide;
        c.dentsChanged = true;
    }
}

void RacingModule::updateCamera(Car& c, CamMode mode, float dt, kke::Camera& out) {
    const glm::mat4 xf = drawTransform(c);
    const glm::vec3 pos(xf[3]);
    glm::vec3 fwd(xf[2]);
    fwd.y = 0.0f;
    fwd = glm::length(fwd) > 1e-3f ? glm::normalize(fwd) : glm::vec3(0.0f, 0.0f, 1.0f);
    if (c.lookBack) fwd = -fwd;
    const float speed = std::fabs(carSpeed(c));
    const float height = c.art ? c.art->boundsMax.y : 1.4f;
    out.up = glm::vec3(0.0f, 1.0f, 0.0f);
    out.nearPlane = inside(mode) ? kInsideNear : kOutsideNear;
    if (mode == CamMode::Tv && m_track) {
        // A spot beside the track ahead of the car; the next one once it's well past.
        const Track& t = *m_track;
        const float past = glm::dot(pos - m_tvSpot, t.at(c.where.s).forward);
        if (m_tvCar != static_cast<int>(&c - m_cars.data()) || past > 45.0f || glm::length(m_tvSpot) < 1e-3f) {
            m_tvCar = static_cast<int>(&c - m_cars.data());
            const float s = c.where.s + 60.0f + std::min(speed, 60.0f) * 0.5f;
            const Track::Sample f = t.at(s);
            const glm::vec3 leftFlat(f.forward.z, 0.0f, -f.forward.x);
            // Up on a pole, above the wall and the banking between it and the road.
            const float edge = t.point(s, -t.halfWidth()).y + t.desc().wall;
            m_tvSpot = glm::vec3(f.p.x, 0.0f, f.p.z) - leftFlat * (t.halfWidth() + 9.0f) + glm::vec3(0.0f, std::max(6.0f, edge + 5.0f), 0.0f);
        }
        out.position = m_tvSpot;
        out.target = pos + glm::vec3(0.0f, 0.8f, 0.0f);
        out.fovDegrees = std::clamp(55.0f - glm::length(pos - m_tvSpot) * 0.25f, 18.0f, 55.0f);
        c.camInit = false;
        return;
    }
    if (mode == CamMode::Wheel && c.art) {
        const glm::vec3 wheel = c.art->wheelCenter[0];
        const glm::vec3 mount(c.art->boundsMax.x + 0.55f, c.art->wheelRadius * 1.15f, wheel.z + 1.5f);
        out.position = glm::vec3(xf * glm::vec4(mount, 1.0f));
        out.target = glm::vec3(xf * glm::vec4(wheel - glm::vec3(0.0f, c.art->wheelRadius * 0.35f, 0.0f), 1.0f));
        out.fovDegrees = 55.0f;
        c.camInit = false;
        return;
    }
    if ((inside(mode) || mode == CamMode::Bonnet) && c.art) {
        // Fixed to the car: it rolls and pitches with you, no lag.
        glm::vec3 eye;
        float yaw = c.look.x + (c.lookBack ? glm::pi<float>() : 0.0f), pitch = c.look.y;
        if (mode == CamMode::Bonnet) {
            eye = glm::vec3((c.art->boundsMin.x + c.art->boundsMax.x) * 0.5f, height * 0.82f, c.lookBack ? -0.8f : 0.4f);
        } else {
            eye = c.art->eye;
            pitch -= 0.07f; // the dashboard in the bottom of the view
            if (mode == CamMode::FirstPerson) {
                eye += c.head;
                yaw += c.headYaw;
                pitch -= c.head.z * 0.6f; // thrown forward: looking down a little
            }
        }
        // Car space: +Z forward, +X left; yaw + turns right.
        const glm::vec3 dir(-std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
        out.position = glm::vec3(xf * glm::vec4(eye, 1.0f));
        out.target = out.position + glm::normalize(glm::mat3(xf) * dir) * 10.0f;
        out.up = glm::normalize(glm::vec3(xf[1]));
        c.camPos = out.position;
        c.camInit = false;
        // Faster feels faster: a little wider (less than outside, where
        // there's no dashboard to give the speed away).
        const float base = mode == CamMode::Cockpit ? 72.0f : mode == CamMode::FirstPerson ? 66.0f : 62.0f;
        out.fovDegrees = base + std::clamp(speed - 15.0f, 0.0f, 60.0f) * (mode == CamMode::Bonnet ? 0.25f : 0.12f);
        return;
    }

    const bool far = mode == CamMode::Far;
    const float back = far ? 10.5f : 6.0f + speed * 0.015f;
    float up = far ? 3.6f : 1.5f + height * 0.6f;
    if (!c.camInit) c.camDir = fwd;
    // Always the same distance behind; only the heading lags, so a turn
    // swings the camera out but speed never drops it back into the
    // car behind.
    c.camDir += (fwd - c.camDir) * (1.0f - std::exp(-(c.lookBack ? 30.0f : 6.0f) * dt));
    c.camDir.y = 0.0f;
    c.camDir = glm::length(c.camDir) > 1e-3f ? glm::normalize(c.camDir) : fwd;
    // Looking round: the camera swings round the car (all the way to in
    // front of it, looking back), and the mouse's up and down lift it.
    const float s = std::sin(-c.look.x), co = std::cos(-c.look.x);
    const glm::vec3 heading(c.camDir.x * co + c.camDir.z * s, 0.0f, -c.camDir.x * s + c.camDir.z * co);
    up = std::max(0.6f, up - c.look.y * back * 0.8f);
    const glm::vec3 want = pos - heading * back + glm::vec3(0.0f, up, 0.0f);
    const glm::vec3 look = pos + heading * 4.0f + glm::vec3(0.0f, height * 0.7f, 0.0f);
    if (!c.camInit) {
        c.camLook = look;
        c.camPos = want;
        c.camInit = true;
    }
    // Height follows a little behind (bumps, landings).
    c.camPos = glm::vec3(want.x, c.camPos.y + (want.y - c.camPos.y) * (1.0f - std::exp(-10.0f * dt)), want.z);
    if (std::fabs(c.camPos.y - want.y) > 3.0f) c.camPos.y = want.y;
    // Never below the road.
    if (m_track) {
        const Track::Where w = m_track->locate(c.camPos, c.where.sample);
        if (std::fabs(w.u) < m_track->halfWidth() && w.height < 0.6f) c.camPos.y += 0.6f - w.height;
    }
    c.camLook += (look - c.camLook) * (1.0f - std::exp(-14.0f * dt));
    out.position = c.camPos;
    out.target = c.camLook;
    // Faster feels faster: a wider view.
    out.fovDegrees = 62.0f + std::clamp(speed - 15.0f, 0.0f, 60.0f) * 0.25f;
}

} // namespace racing
