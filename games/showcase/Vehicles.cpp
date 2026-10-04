// Cars and the plane (Kees, 2026-10-03: "drivable cars, flyable planes").
//
// The race track (layout::kZones[4]): an oval 585 m round with kerbs on
// the bends, a start line, a slalom of cones on the far straight and a
// jump by the car park. Three cars wait in the car park: a hatch, a coupe
// and a truck, each its own kke::VehicleDesc on Jolt's vehicle physics
// (docs/VEHICLES.md). Drive a lap and the HUD times it.
//
// The airfield (layout::kZones[3]): a stunt plane at the west end of the
// runway, flown by the Flying demo's flight model (games/flying_demo/
// Flight.h: lift, drag, stalls, the nose that lifts by itself at take-off
// speed) and carried by a kinematic Jolt body, so it pushes crates and
// people out of its way. Land on the runway or any flat field; anything
// else is a crash (a fireball, then the plane is back at the hangar).
//
// Walk up to either and press pickup (F / X) to get in; the same button
// gets you out once you've stopped. KKE_DEMO_DRIVE=1 drives a lap, then
// takes off, flies a circuit and crashes on purpose (checks, screenshots).

#include "ShowcaseModule.h"
#include "Geometry.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace kke_showcase {

using namespace layout;

namespace {

// The oval: two straights along X joined by half circles.
constexpr glm::vec2 kOval(0.0f, 255.0f);   // its middle
constexpr float kStraight = 60.0f;         // half a straight
constexpr float kBend = 55.0f;             // the bends' radius (to the centre line)
constexpr float kTrackHalf = 7.0f;         // half the width
constexpr float kOvalLength = 4.0f * kStraight + 2.0f * glm::pi<float>() * kBend;
// The plane on the runway, nose down it (+X: heading 90).
constexpr glm::vec3 kPlaneHome(196.0f, 0.0f, 40.0f);
constexpr float kPlaneHomeHeading = 90.0f;
constexpr float kRampPitch = 12.0f; // degrees
const glm::vec3 kRamp(40.0f, 0.0f, 165.0f), kRampHalf(3.0f, 0.2f, 6.0f);
const glm::vec3 kConeColor(0.95f, 0.45f, 0.08f);

float wrap180(float deg) {
    deg = std::fmod(deg + 180.0f, 360.0f);
    if (deg < 0.0f) deg += 360.0f;
    return deg - 180.0f;
}

// The centre line `s` metres from the start line (x = 0 on the near
// straight), going +X first: counter-clockwise seen from above.
glm::vec2 ovalPoint(float s) {
    s = std::fmod(s, kOvalLength);
    if (s < 0.0f) s += kOvalLength;
    const float straight = 2.0f * kStraight, bend = glm::pi<float>() * kBend;
    if (s < kStraight) return { s, kOval.y - kBend };
    s -= kStraight;
    if (s < bend) {
        const float a = -0.5f * glm::pi<float>() + s / kBend;
        return { kStraight + kBend * std::cos(a), kOval.y + kBend * std::sin(a) };
    }
    s -= bend;
    if (s < straight) return { kStraight - s, kOval.y + kBend };
    s -= straight;
    if (s < bend) {
        const float a = 0.5f * glm::pi<float>() + s / kBend;
        return { -kStraight + kBend * std::cos(a), kOval.y + kBend * std::sin(a) };
    }
    s -= bend;
    return { -kStraight + s, kOval.y - kBend };
}

// How far `p` is from the oval's centre line.
float ovalDistance(const glm::vec2& p) {
    if (std::abs(p.x) <= kStraight) return std::abs(std::abs(p.y - kOval.y) - kBend);
    const glm::vec2 c(p.x > 0.0f ? kStraight : -kStraight, kOval.y);
    return std::abs(glm::length(p - c) - kBend);
}

// A flat quad (two triangles) on the ground, facing up.
void appendQuad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec3& color, std::vector<kke::Vertex>& v,
                std::vector<uint32_t>& idx) {
    const uint32_t base = static_cast<uint32_t>(v.size());
    for (const glm::vec3& p : { a, b, c, d }) v.push_back({ p, color, glm::vec3(0, 1, 0), glm::vec2(0.0f) });
    // Wound so the face points up whichever way round the corners came.
    const bool up = glm::cross(b - a, c - a).y >= 0.0f;
    if (up) idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    else idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
}

// Rig yaw (0 = -Z, clockwise from above) of a direction.
float yawOf(const glm::vec3& f) { return glm::degrees(std::atan2(f.x, -f.z)); }

// A car's rotation for a rig yaw (cars' noses are +Z in body space).
glm::quat carRotation(float yaw) { return glm::angleAxis(glm::radians(180.0f - yaw), glm::vec3(0, 1, 0)); }

struct CarKind {
    const char* name;
    glm::vec3 half, color;
    float mass, torque, maxRpm, wheelRadius, suspension, steer;
    int drive; // 0 front, 1 rear, 2 all
};
const CarKind kCarKinds[] = {
    { "red hatch", { 0.8f, 0.33f, 1.9f }, { 0.78f, 0.1f, 0.08f }, 1100.0f, 320.0f, 7000.0f, 0.32f, 1.8f, 34.0f, 0 },
    { "blue coupe", { 0.88f, 0.3f, 2.15f }, { 0.12f, 0.3f, 0.8f }, 1300.0f, 520.0f, 7600.0f, 0.34f, 2.2f, 30.0f, 1 },
    { "yellow truck", { 0.98f, 0.45f, 2.35f }, { 0.92f, 0.7f, 0.1f }, 1750.0f, 650.0f, 5600.0f, 0.42f, 1.4f, 32.0f, 2 },
};

kke::VehicleDesc carDesc(const CarKind& k, const glm::vec3& at, float yaw) {
    kke::VehicleDesc d;
    d.halfExtents = k.half;
    d.position = at + glm::vec3(0.0f, k.wheelRadius + k.half.y + 0.25f, 0.0f);
    d.rotation = carRotation(yaw);
    d.mass = k.mass;
    d.maxTorque = k.torque;
    d.maxRpm = k.maxRpm;
    d.shiftUpRpm = k.maxRpm * 0.8f;
    d.material = 3; // metal (sounds)
    for (int i = 0; i < 4; ++i) {
        kke::VehicleWheelDesc w;
        const bool front = i < 2;
        w.position = { (i % 2 == 0 ? 1.0f : -1.0f) * (k.half.x - 0.08f), -0.05f, (front ? 1.0f : -1.0f) * (k.half.z - 0.55f) };
        w.radius = k.wheelRadius;
        w.width = 0.24f;
        w.suspensionFrequency = k.suspension;
        w.suspensionMax = k.drive == 2 ? 0.38f : 0.3f; // the truck sits higher and soaks up the jump
        w.maxSteerDegrees = front ? k.steer : 0.0f;
        w.maxBrakeTorque = front ? 2200.0f : 1500.0f;
        w.maxHandBrakeTorque = front ? 0.0f : 4500.0f;
        d.wheels.push_back(w);
    }
    d.drivenAxles = k.drive == 0 ? std::vector<int>{ 0 } : k.drive == 1 ? std::vector<int>{ 1 } : std::vector<int>{ 0, 1 };
    return d;
}

} // namespace

void ShowcaseModule::defineRideActions() {
    kke::InputMap& in = m_input->map(0);
    using IM = kke::InputModule;
    // Throttle (the plane) / gas (a car), and brake / throttle down. W and S
    // drive a car too; the plane's W and S are the stick (nose down, up).
    in.defineAction({ "drive.gas", "Gas, throttle up (cars, the plane)", "Vehicles", "game", kke::ActionType::Axis1D });
    in.defineAction({ "drive.brake", "Brake, throttle down (cars, the plane)", "Vehicles", "game", kke::ActionType::Axis1D });
    in.addBinding(IM::bind("drive.gas", IM::key(SDL_SCANCODE_LSHIFT), kke::Trigger::Continuous));
    in.addBinding(IM::bind("drive.gas", IM::padAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1), kke::Trigger::Continuous));
    in.addBinding(IM::bind("drive.brake", IM::key(SDL_SCANCODE_LCTRL), kke::Trigger::Continuous));
    in.addBinding(IM::bind("drive.brake", IM::padAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1), kke::Trigger::Continuous));
}

// What never moves: the oval with its kerbs and start line, the car park,
// the jump, the hangar.
void ShowcaseModule::buildVehicles(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    const glm::vec3 asphalt(0.36f, 0.36f, 0.38f), white(0.92f), red(0.8f, 0.12f, 0.1f);
    const float lift = 0.07f; // above the terrain (the roads are 0.06)
    // The track: a strip every 2 m.
    const int steps = static_cast<int>(kOvalLength / 2.0f);
    for (int k = 0; k < steps; ++k) {
        const float s0 = kOvalLength * static_cast<float>(k) / static_cast<float>(steps), s1 = kOvalLength * static_cast<float>(k + 1) / static_cast<float>(steps);
        const glm::vec2 a = ovalPoint(s0), b = ovalPoint(s1);
        const glm::vec2 ta = glm::normalize(ovalPoint(s0 + 0.5f) - ovalPoint(s0 - 0.5f)), tb = glm::normalize(ovalPoint(s1 + 0.5f) - ovalPoint(s1 - 0.5f));
        const glm::vec2 na(-ta.y, ta.x), nb(-tb.y, tb.x); // to the inside (counter-clockwise)
        auto at = [&](const glm::vec2& p) { return glm::vec3(p.x, groundHeight(p.x, p.y) + lift, p.y); };
        appendQuad(at(a - na * kTrackHalf), at(b - nb * kTrackHalf), at(b + nb * kTrackHalf), at(a + na * kTrackHalf), asphalt, v, idx);
        // Kerbs on the bends, red and white, both edges.
        const bool bend = std::abs(a.x) > kStraight + 0.5f;
        if (bend)
            for (float side : { -1.0f, 1.0f }) {
                const glm::vec3 color = (k % 2 == 0) ? red : white;
                const glm::vec2 ea = a + na * side * kTrackHalf, eb = b + nb * side * kTrackHalf;
                const glm::vec2 oa = a + na * side * (kTrackHalf + 1.2f), ob = b + nb * side * (kTrackHalf + 1.2f);
                appendQuad(glm::vec3(ea.x, lift + 0.01f, ea.y), glm::vec3(eb.x, lift + 0.01f, eb.y), glm::vec3(ob.x, lift + 0.01f, ob.y),
                           glm::vec3(oa.x, lift + 0.01f, oa.y), color, v, idx);
            }
    }
    // The start line: a chequered band across the near straight at x = 0.
    for (int row = 0; row < 2; ++row)
        for (int c = 0; c < 14; ++c) {
            const float x = -0.5f + 0.5f * static_cast<float>(row), z = kOval.y - kBend - kTrackHalf + 0.5f + static_cast<float>(c);
            appendBox(glm::translate(glm::mat4(1.0f), glm::vec3(x + 0.25f, lift + 0.012f, z)), glm::vec3(0.25f, 0.005f, 0.5f),
                      (row + c) % 2 == 0 ? white : glm::vec3(0.08f), v, idx);
        }
    // A gantry over the start line.
    for (float dz : { -kTrackHalf - 1.0f, kTrackHalf + 1.0f })
        addStaticBox({ { 0.0f, 3.0f, kOval.y - kBend + dz }, { 0.2f, 3.0f, 0.2f }, glm::vec3(0.5f) }, v, idx);
    appendBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 6.2f, kOval.y - kBend)), glm::vec3(0.25f, 0.5f, kTrackHalf + 1.2f), glm::vec3(0.15f), v, idx);
    // The car park: a slab, bays painted on it.
    appendBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.03f, 170.0f)), glm::vec3(16.0f, 0.04f, 9.0f), asphalt * 0.9f, v, idx);
    for (float x : { -9.0f, -3.0f, 3.0f, 9.0f })
        appendBox(glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.075f, 172.0f)), glm::vec3(0.08f, 0.005f, 3.0f), white, v, idx);
    // The jump: a ramp up toward the oval.
    {
        const glm::quat q = glm::angleAxis(glm::radians(-kRampPitch), glm::vec3(1, 0, 0));
        const float rise = kRampHalf.z * std::sin(glm::radians(kRampPitch)) - kRampHalf.y * std::cos(glm::radians(kRampPitch));
        const glm::vec3 c = kRamp + glm::vec3(0.0f, rise, 0.0f);
        kke::RigidWorld::BodyDesc d;
        d.shape = kke::RigidWorld::Shape::Box;
        d.motion = kke::RigidWorld::Motion::Static;
        d.halfExtents = kRampHalf;
        d.position = c;
        d.rotation = q;
        d.friction = 0.9f;
        m_rigid->world().add(d);
        const glm::mat4 m = glm::translate(glm::mat4(1.0f), c) * glm::mat4_cast(q);
        appendBox(m, kRampHalf, glm::vec3(0.55f, 0.5f, 0.42f), v, idx);
        for (float x : { -2.2f, 0.0f, 2.2f }) // stripes up it
            appendBox(m * glm::translate(glm::mat4(1.0f), glm::vec3(x, kRampHalf.y + 0.004f, 0.0f)), glm::vec3(0.25f, 0.004f, kRampHalf.z), glm::vec3(0.95f, 0.75f, 0.1f), v, idx);
    }
    // The hangar north of the runway's west end, open to the runway.
    {
        const glm::vec3 h(214.0f, 0.0f, 78.0f), grey(0.55f, 0.57f, 0.6f);
        addStaticBox({ h + glm::vec3(-14.0f, 4.0f, 0.0f), { 0.3f, 4.0f, 10.0f }, grey }, v, idx);
        addStaticBox({ h + glm::vec3(14.0f, 4.0f, 0.0f), { 0.3f, 4.0f, 10.0f }, grey }, v, idx);
        addStaticBox({ h + glm::vec3(0.0f, 4.0f, 10.0f), { 14.3f, 4.0f, 0.3f }, grey }, v, idx);
        addStaticBox({ h + glm::vec3(0.0f, 8.3f, 0.0f), { 14.6f, 0.3f, 10.3f }, grey * 0.8f }, v, idx);
        // A wind sock on a pole by the runway.
        addStaticBox({ { 230.0f, 3.0f, 60.0f }, { 0.08f, 3.0f, 0.08f }, white }, v, idx);
        appendBox(glm::translate(glm::mat4(1.0f), glm::vec3(230.8f, 5.8f, 60.0f)), glm::vec3(0.8f, 0.2f, 0.2f), glm::vec3(0.95f, 0.45f, 0.1f), v, idx);
    }
}

void ShowcaseModule::clearVehicles() {
    if (m_ride != Ride::None) exitVehicle(true);
    kke::RigidWorld& w = m_rigid->world();
    for (const Car& c : m_cars) w.removeVehicle(c.id);
    m_cars.clear();
    if (m_plane.body != kke::RigidWorld::kNoBody) w.remove(m_plane.body);
    m_plane = Plane{};
    for (const RangeThing& r : m_cones) w.remove(r.body);
    m_cones.clear();
}

// Cars in their bays, the plane on the runway, the cones up.
void ShowcaseModule::spawnVehicles() {
    clearVehicles();
    kke::RigidWorld& w = m_rigid->world();
    for (size_t i = 0; i < std::size(kCarKinds); ++i) {
        const CarKind& k = kCarKinds[i];
        Car c;
        c.home = glm::vec3(-6.0f + 6.0f * static_cast<float>(i), 0.0f, 172.0f);
        c.homeYaw = 180.0f; // nose to the oval (+Z)
        c.half = k.half;
        c.color = k.color;
        c.name = k.name;
        c.id = w.addVehicle(carDesc(k, c.home, c.homeYaw));
        if (c.id) m_cars.push_back(c);
    }
    m_plane.home = kPlaneHome;
    m_plane.homeYaw = kPlaneHomeHeading;
    m_plane.s = flying::parked(glm::vec3(kPlaneHome.x, groundHeight(kPlaneHome.x, kPlaneHome.z), kPlaneHome.z), kPlaneHomeHeading, m_flight);
    m_plane.c = flying::Controls{};
    m_plane.c.throttle = 0.0f;
    m_plane.drawPos = m_plane.s.position;
    m_plane.drawRot = m_plane.s.rotation;
    {
        kke::RigidWorld::BodyDesc d;
        d.shape = kke::RigidWorld::Shape::Box;
        d.motion = kke::RigidWorld::Motion::Kinematic;
        d.halfExtents = glm::vec3(0.5f, 0.6f, 3.0f); // the fuselage (the wings don't push)
        d.position = m_plane.s.position;
        d.rotation = m_plane.s.rotation;
        d.material = 3;
        m_plane.body = w.add(d);
    }
    // A slalom on the far straight.
    for (int k = 0; k < 8; ++k) {
        kke::RigidWorld::BodyDesc d;
        d.shape = kke::RigidWorld::Shape::ConvexHull;
        d.points = cylinderPoints(0.16f, 0.3f, 8);
        d.position = glm::vec3(-35.0f + 10.0f * static_cast<float>(k), 0.38f, kOval.y + kBend + (k % 2 == 0 ? -2.5f : 2.5f));
        d.mass = 2.5f;
        d.friction = 0.8f;
        const kke::RigidWorld::BodyId id = w.add(d);
        if (id != kke::RigidWorld::kNoBody) m_cones.push_back({ id, RangeKind::Crate, glm::vec3(0.16f, 0.3f, 0.0f), kConeColor, -1.0f });
    }
    m_lapAngle = 0.0f;
    m_lapTime = -1.0f;
    kke::log::get(name())->info("vehicles: {} cars at the race track, the plane at {:.0f} {:.0f}", m_cars.size(), kPlaneHome.x, kPlaneHome.z);
}

// The cars, the plane and the cones in one mesh.
void ShowcaseModule::batchVehicles() {
    static std::vector<kke::Vertex> v;
    static std::vector<uint32_t> i;
    v.clear();
    i.clear();
    kke::RigidWorld& w = m_rigid->world();
    const glm::vec3 tyre(0.06f), glass(0.12f, 0.16f, 0.2f), chrome(0.7f);
    for (Car& c : m_cars) {
        w.vehicleState(c.id, c.state);
        const glm::mat4 t = w.transform(w.vehicleBody(c.id));
        const glm::vec3 h = c.half;
        appendBox(t, h, c.color, v, i);
        // The cabin, a little back from the middle, and its glass.
        const glm::vec3 cabin(h.x * 0.86f, c.half.y > 0.4f ? 0.36f : 0.27f, h.z * 0.42f);
        const glm::mat4 ct = t * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, h.y + cabin.y, -0.12f * h.z));
        appendBox(ct, cabin, glass, v, i);
        appendBox(ct * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, cabin.y, 0.0f)), glm::vec3(cabin.x, 0.03f, cabin.z * 0.9f), c.color, v, i);
        // Lights: white at the front (+Z), red at the back.
        for (float x : { -1.0f, 1.0f }) {
            appendBox(t * glm::translate(glm::mat4(1.0f), glm::vec3(x * h.x * 0.7f, h.y * 0.3f, h.z + 0.01f)), glm::vec3(0.16f, 0.06f, 0.02f), glm::vec3(1.0f, 0.97f, 0.85f), v, i);
            appendBox(t * glm::translate(glm::mat4(1.0f), glm::vec3(x * h.x * 0.7f, h.y * 0.3f, -h.z - 0.01f)), glm::vec3(0.16f, 0.06f, 0.02f), glm::vec3(0.9f, 0.05f, 0.05f), v, i);
        }
        // Wheels: the state's transforms (spin, steer, suspension), axle along X.
        const float r = c.state.wheels.empty() ? 0.33f : c.state.wheels[0].radius;
        for (const kke::VehicleWheelState& ws : c.state.wheels) {
            const glm::mat4 wt = ws.transform * glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(0, 0, 1));
            appendCylinder(wt, r, 0.12f, tyre, v, i);
            appendCylinder(wt, r * 0.55f, 0.125f, chrome, v, i);
        }
    }
    // The plane (forward -Z, origin 1.1 m above its wheels).
    if (m_plane.body != kke::RigidWorld::kNoBody) {
        const float alpha = m_app->fixedAlpha();
        const glm::vec3 pos = glm::mix(m_plane.drawPos, m_plane.s.position, alpha);
        const glm::quat rot = glm::slerp(m_plane.drawRot, m_plane.s.rotation, alpha);
        glm::mat4 t = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot);
        const bool wreck = m_plane.wreck >= 0.0f;
        const float soot = wreck ? 0.3f : 1.0f;
        const glm::vec3 body = glm::vec3(0.93f, 0.93f, 0.9f) * soot, paint = glm::vec3(0.8f, 0.12f, 0.1f) * soot;
        auto part = [&](const glm::vec3& at, const glm::vec3& half, const glm::vec3& color) {
            appendBox(t * glm::translate(glm::mat4(1.0f), at), half, color, v, i);
        };
        part({ 0.0f, 0.0f, 0.0f }, { 0.45f, 0.5f, 3.0f }, body);
        part({ 0.0f, 0.0f, -3.2f }, { 0.4f, 0.42f, 0.3f }, paint);
        part({ 0.0f, 0.55f, -0.3f }, { 0.36f, 0.22f, 0.55f }, glass);
        part({ 0.0f, -0.18f, -0.7f }, { 4.6f, 0.07f, 0.85f }, paint);
        part({ 4.2f, -0.1f, -0.7f }, { 0.4f, 0.075f, 0.86f }, body); // white tips
        part({ -4.2f, -0.1f, -0.7f }, { 0.4f, 0.075f, 0.86f }, body);
        part({ 0.0f, 0.12f, 2.75f }, { 1.7f, 0.05f, 0.42f }, paint);
        part({ 0.0f, 0.7f, 2.8f }, { 0.05f, 0.65f, 0.45f }, paint);
        // The propeller: spins with the throttle.
        if (!wreck) {
            const glm::mat4 pt = t * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -3.55f)) * glm::rotate(glm::mat4(1.0f), m_plane.prop, glm::vec3(0, 0, 1));
            appendBox(pt, glm::vec3(0.07f, 1.05f, 0.03f), glm::vec3(0.15f), v, i);
            appendSphere(pt, 0.16f, chrome, v, i);
        }
        // Wheels under the wings and at the tail.
        for (float x : { -0.8f, 0.8f }) {
            part({ x, -0.6f, -1.1f }, { 0.04f, 0.35f, 0.04f }, chrome);
            appendCylinder(t * glm::translate(glm::mat4(1.0f), glm::vec3(x, -0.8f, -1.1f)) * glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(0, 0, 1)), 0.3f,
                           0.07f, tyre, v, i);
        }
        appendCylinder(t * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 2.85f)) * glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(0, 0, 1)), 0.1f, 0.04f,
                       tyre, v, i);
    }
    for (const RangeThing& c : m_cones) {
        const glm::mat4 t = w.transform(c.body);
        appendCylinder(t, c.half.x, c.half.y, c.color, v, i, true);
    }
    m_vehBatchIndices = i.size();
    if (!i.empty()) m_vehBatch->upload(v, i);
}

int ShowcaseModule::carInReach() const {
    const kke::RigidWorld& w = m_rigid->world();
    const glm::vec3 me = w.characterPosition(m_player);
    int best = -1;
    float bestD = 2.2f; // m from the car's side
    for (size_t k = 0; k < m_cars.size(); ++k) {
        const glm::mat4 t = w.transform(w.vehicleBody(m_cars[k].id));
        const glm::vec3 local = glm::vec3(glm::inverse(t) * glm::vec4(me + glm::vec3(0, 0.8f, 0), 1.0f));
        const glm::vec3 out = glm::max(glm::abs(local) - m_cars[k].half, glm::vec3(0.0f));
        const float d = glm::length(glm::vec2(out.x, out.z));
        if (d < bestD && std::abs(local.y) < 2.0f) {
            bestD = d;
            best = static_cast<int>(k);
        }
    }
    return best;
}

bool ShowcaseModule::planeInReach() const {
    if (m_plane.body == kke::RigidWorld::kNoBody || m_plane.wreck >= 0.0f || !m_plane.s.onGround) return false;
    const glm::vec3 me = m_rigid->world().characterPosition(m_player);
    const glm::vec3 d = me - m_plane.s.position;
    return glm::length(glm::vec2(d.x, d.z)) < 4.5f && std::abs(d.y) < 3.0f;
}

bool ShowcaseModule::useVehicle() {
    if (m_ride != Ride::None) {
        exitVehicle();
        return true;
    }
    if (m_held.body != kke::RigidWorld::kNoBody || itemInReach() >= 0) return false;
    if (const int c = carInReach(); c >= 0) {
        enterVehicle(Ride::Car, c);
        return true;
    }
    if (planeInReach()) {
        enterVehicle(Ride::Plane, -1);
        return true;
    }
    return false;
}

void ShowcaseModule::enterVehicle(Ride ride, int car) {
    dropHeld();
    m_ride = ride;
    m_rideCar = car;
    m_carIn = kke::VehicleInput{};
    m_walkArm = m_rig.settings.armLength;
    m_rideArm = ride == Ride::Plane ? 15.0f : 7.0f;
    m_lookIdle = 10.0f; // swing in behind at once
    m_aimBlend = 0.0f;
    m_equipBatchIndices = 0; // the character (and what it holds) is hidden in the seat
    m_rigid->world().setCharacterKinematic(m_player, true);
    if (ride == Ride::Plane) m_plane.c = flying::Controls{ 0.0f, 0.0f, 0.0f, 0.0f, false };
    m_lapAngle = 0.0f;
    m_lapTime = -1.0f;
    toast(ride == Ride::Plane ? "In the plane" : std::string("In the ") + m_cars[static_cast<size_t>(car)].name);
    kke::log::get(name())->info("vehicles: in the {}", ride == Ride::Plane ? "plane" : m_cars[static_cast<size_t>(car)].name);
}

void ShowcaseModule::exitVehicle(bool force) {
    if (m_ride == Ride::None) return;
    kke::RigidWorld& w = m_rigid->world();
    glm::vec3 spot;
    if (m_ride == Ride::Car && m_rideCar >= 0 && static_cast<size_t>(m_rideCar) < m_cars.size()) {
        const Car& c = m_cars[static_cast<size_t>(m_rideCar)];
        if (std::abs(c.state.speed) > 4.0f && !force) {
            toast("Slow down to get out");
            return;
        }
        const glm::mat4 t = w.transform(w.vehicleBody(c.id));
        spot = glm::vec3(t * glm::vec4(c.half.x + 0.9f, 0.0f, 0.0f, 1.0f)); // the driver's side (+X is the car's left)
        m_carIn = kke::VehicleInput{};
        m_carIn.handBrake = 1.0f;
        w.setVehicleInput(c.id, m_carIn);
    } else {
        if (!force && m_plane.wreck < 0.0f && !(m_plane.s.onGround && m_plane.s.airspeed < 3.0f)) {
            toast(m_plane.s.onGround ? "Stop first to get out" : "Land first to get out");
            return;
        }
        spot = m_plane.s.position + m_plane.s.rotation * glm::vec3(-1.4f, 0.0f, 1.9f);
        if (m_plane.wreck >= 0.0f) spot = m_plane.s.position + glm::vec3(6.0f, 0.0f, 6.0f); // thrown clear
        m_plane.c.throttle = 0.0f;
    }
    spot.y = groundHeight(spot.x, spot.z) + 0.05f;
    m_ride = Ride::None;
    m_rideCar = -1;
    m_loco->teleport(spot); // kinematic off again
    m_rig.settings.armLength = m_walkArm;
    m_ik.reset();
    if (kke::AudioModule* audio = m_app->getModule<kke::AudioModule>(); audio && m_engineVoice) audio->mixer().stop(m_engineVoice);
    m_engineVoice = 0;
    if (m_engineStream) m_engineStream->close();
    m_engineStream.reset();
    kke::log::get(name())->info("vehicles: out at {:.1f} {:.1f} {:.1f}", spot.x, spot.y, spot.z);
}

void ShowcaseModule::flipCar() {
    if (m_ride != Ride::Car || m_rideCar < 0) return;
    const Car& c = m_cars[static_cast<size_t>(m_rideCar)];
    kke::RigidWorld& w = m_rigid->world();
    const kke::RigidWorld::BodyId b = w.vehicleBody(c.id);
    const glm::vec3 p = w.position(b);
    const glm::vec3 f = w.rotation(b) * glm::vec3(0, 0, 1);
    w.setTransform(b, glm::vec3(p.x, groundHeight(p.x, p.z) + 1.4f, p.z), carRotation(yawOf(glm::vec3(f.x, 0.0f, f.z))));
    w.setVelocity(b, glm::vec3(0.0f));
    w.setAngularVelocity(b, glm::vec3(0.0f));
    w.resetVehicle(c.id);
    toast("Back on its wheels");
}

kke::RigidWorld::BodyId ShowcaseModule::rideBody() const {
    if (m_ride == Ride::Car && m_rideCar >= 0) return m_rigid->world().vehicleBody(m_cars[static_cast<size_t>(m_rideCar)].id);
    if (m_ride == Ride::Plane) return m_plane.body;
    return kke::RigidWorld::kNoBody;
}

glm::vec3 ShowcaseModule::ridePosition() const {
    if (m_ride == Ride::Plane) return glm::mix(m_plane.drawPos, m_plane.s.position, m_app->fixedAlpha());
    const kke::RigidWorld& w = m_rigid->world();
    return w.position(rideBody());
}

flying::Ground ShowcaseModule::flightGround() const {
    flying::Ground g;
    g.height = [this](float x, float z) { return groundHeight(x, z); };
    // The runway, or any flat field (not a hillside, not the mountain).
    g.landable = [this](float x, float z) {
        if (x > 180.0f && x < 580.0f && std::abs(z - 40.0f) < 16.0f) return true;
        const float h = groundHeight(x, z);
        return std::abs(groundHeight(x + 5.0f, z) - h) < 0.6f && std::abs(groundHeight(x, z + 5.0f) - h) < 0.6f;
    };
    return g;
}

// The controls while riding (readActions hands over when m_ride is set).
void ShowcaseModule::readRide(kke::InputMap& in, float dt) {
    glm::vec2 move = in.axis2("move");
    float gas = in.axis("drive.gas"), brake = in.axis("drive.brake");
    bool handBrake = in.held("jump");
    if (glm::length(in.axis2("look")) > 0.0f || glm::length(in.axis2("look.rate")) > 0.05f) m_lookIdle = 0.0f;
    if (m_captured && in.axis("camera.zoom") != 0.0f) m_rideArm = std::clamp(m_rideArm - in.axis("camera.zoom") * 1.0f, 4.0f, 30.0f);
    if (const float z = in.axis("zoom.pad"); z != 0.0f) m_rideArm = std::clamp(m_rideArm - z * 8.0f * dt, 4.0f, 30.0f);
    if (in.pressed("camera.toggle"))
        m_rig.mode = m_rig.mode == kke::CameraRig::Mode::ThirdPerson ? kke::CameraRig::Mode::FirstPerson : kke::CameraRig::Mode::ThirdPerson;
    if (m_demoDrive >= 0.0f) updateDriveDemo(dt, move, gas, brake, handBrake);
    if (in.pressed("pickup")) {
        exitVehicle();
        return;
    }
    if (in.pressed("reset")) flipCar();
    if (m_ride == Ride::Car && m_rideCar >= 0) {
        const Car& c = m_cars[static_cast<size_t>(m_rideCar)];
        kke::VehicleInput vi;
        const float ahead = std::max(move.y, gas);
        const float back = std::max(-move.y, brake);
        vi.steer = std::clamp(move.x, -1.0f, 1.0f);
        if (ahead > 0.05f) {
            vi.throttle = ahead;
            if (c.state.speed < -1.0f) { vi.throttle = 0.0f; vi.brake = ahead; } // rolling back: stop first
        } else if (back > 0.05f) {
            if (c.state.speed > 1.0f) vi.brake = back;
            else vi.throttle = -back; // stopped: reverse
        }
        vi.handBrake = handBrake ? 1.0f : 0.0f;
        m_carIn = vi;
    } else if (m_ride == Ride::Plane) {
        flying::Controls& c = m_plane.c;
        c.pitch = -move.y; // stick forward (W, up): nose down
        c.roll = move.x;
        c.yaw = m_plane.s.onGround ? move.x : 0.35f * move.x; // on the ground the stick steers the tail wheel; in the air a little rudder with the roll
        c.throttle = std::clamp(c.throttle + (gas - brake) * 0.6f * dt, 0.0f, 1.0f);
        c.brake = handBrake;
    }
}

// Physics rate: the car's controls; the plane flies a step and its body follows.
void ShowcaseModule::stepVehicles(float dt) {
    kke::RigidWorld& w = m_rigid->world();
    for (size_t k = 0; k < m_cars.size(); ++k) {
        if (m_ride == Ride::Car && static_cast<int>(k) == m_rideCar) {
            w.setVehicleInput(m_cars[k].id, m_carIn);
            if (m_lapTime >= 0.0f) m_lapTime += dt; // laps in physics time: the same on a slow PC
        }
        // Fell out of the world: back to its bay.
        if (w.position(w.vehicleBody(m_cars[k].id)).y < -20.0f) {
            w.setTransform(w.vehicleBody(m_cars[k].id), m_cars[k].home + glm::vec3(0, 1.5f, 0), carRotation(m_cars[k].homeYaw));
            w.setVelocity(w.vehicleBody(m_cars[k].id), glm::vec3(0.0f));
            w.resetVehicle(m_cars[k].id);
        }
    }
    if (m_plane.body == kke::RigidWorld::kNoBody) return;
    Plane& p = m_plane;
    p.drawPos = p.s.position;
    p.drawRot = p.s.rotation;
    if (p.wreck >= 0.0f) {
        p.wreck += dt;
        if (p.wreck > 4.0f) {
            // Back at the hangar end of the runway (and you beside it).
            const bool inside = m_ride == Ride::Plane;
            if (inside) exitVehicle(true);
            p.s = flying::parked(glm::vec3(p.home.x, groundHeight(p.home.x, p.home.z), p.home.z), p.homeYaw, m_flight);
            p.c = flying::Controls{ 0.0f, 0.0f, 0.0f, 0.0f, false };
            p.wreck = -1.0f;
            p.drawPos = p.s.position;
            p.drawRot = p.s.rotation;
            w.setTransform(p.body, p.s.position, p.s.rotation);
            kke::log::get(name())->info("vehicles: the plane is back on the runway");
        }
        return;
    }
    if (m_ride != Ride::Plane) {
        p.c.throttle = std::max(0.0f, p.c.throttle - dt); // nobody in it: the engine idles down
        p.c.pitch = p.c.roll = p.c.yaw = 0.0f;
        p.c.brake = true;
    }
    const glm::vec3 before = p.s.position;
    flying::step(p.s, p.c, m_flight, flightGround(), dt);
    // Into something built (a wall, the hangar, a ramp): a crash too.
    const glm::vec3 moved = p.s.position - before;
    if (!p.s.crashed && glm::length(moved) > 1e-3f && !p.s.onGround) {
        const kke::RigidWorld::BodyId self = p.body;
        const float len = glm::length(moved) + 3.2f; // the nose is 3.2 m ahead
        const kke::RigidWorld::RayHit hit = w.raycast(before, moved / glm::length(moved), len, [self](kke::RigidWorld::BodyId b, kke::RigidWorld::Motion m) {
            return b != self && m == kke::RigidWorld::Motion::Static;
        });
        if (hit.hit && hit.distance > 3.2f * 0.5f && p.s.position.y - groundHeight(p.s.position.x, p.s.position.z) > 1.3f) {
            p.s.crashed = true;
            p.s.position = hit.point - moved / glm::length(moved) * 3.0f;
        }
    }
    p.prop += dt * (8.0f + 60.0f * p.c.throttle);
    if (p.s.crashed) {
        p.wreck = 0.0f;
        const glm::vec3 at = p.s.position;
        kke::log::get(name())->info("vehicles: the plane crashed at {:.1f} {:.1f} {:.1f} ({:.0f} km/h)", at.x, at.y, at.z, glm::length(moved) / dt * 3.6f);
        explode(at, 1.2f);
        // The wreck slews and stops where it hit.
        p.s.rotation = glm::normalize(p.s.rotation * glm::angleAxis(glm::radians(25.0f), glm::normalize(glm::vec3(0.3f, 0.2f, 1.0f))));
        p.s.position.y = groundHeight(at.x, at.z) + 0.5f;
        p.s.velocity = glm::vec3(0.0f);
    }
    w.moveKinematic(p.body, p.s.position, p.s.rotation, dt);
}

// Riding: the character sits in the seat (hidden), the camera chases.
void ShowcaseModule::updateRide(float dt) {
    kke::RigidWorld& w = m_rigid->world();
    const glm::vec3 at = ridePosition();
    glm::vec3 fwd(0.0f, 0.0f, -1.0f);
    float speed = 0.0f;
    if (m_ride == Ride::Car) {
        const Car& c = m_cars[static_cast<size_t>(m_rideCar)];
        fwd = w.rotation(rideBody()) * glm::vec3(0, 0, 1);
        speed = c.state.speed;
        // A lap: once round the middle of the oval while on the track.
        const glm::vec2 p(at.x, at.z);
        const float ang = std::atan2(p.y - kOval.y, p.x - kOval.x);
        const bool onTrack = ovalDistance(p) < kTrackHalf + 3.0f;
        if (onTrack) {
            if (m_lapTime < 0.0f) {
                m_lapTime = 0.0f;
                m_lapAngle = 0.0f;
            } else {
                float d = ang - m_lapPrev;
                if (d > glm::pi<float>()) d -= glm::two_pi<float>();
                if (d < -glm::pi<float>()) d += glm::two_pi<float>();
                m_lapAngle += d;
            }
        }
        m_lapPrev = ang;
        if (std::abs(m_lapAngle) >= glm::two_pi<float>()) {
            m_lastLap = m_lapTime;
            if (m_bestLap < 0.0f || m_lastLap < m_bestLap) m_bestLap = m_lastLap;
            ++m_laps;
            m_lapAngle -= m_lapAngle > 0.0f ? glm::two_pi<float>() : -glm::two_pi<float>();
            m_lapTime = 0.0f;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Lap %.1f s", static_cast<double>(m_lastLap));
            toast(buf);
            kke::log::get(name())->info("vehicles: lap {} in {:.1f} s (best {:.1f} s)", m_laps, m_lastLap, m_bestLap);
        }
    } else {
        fwd = m_plane.s.forward();
        speed = m_plane.s.airspeed;
    }
    // The character rides along (zones, the HUD and the map read where it is).
    w.moveCharacter(m_player, at - glm::vec3(0.0f, m_ride == Ride::Plane ? 1.1f : 0.5f, 0.0f));
    if (m_charInstance) m_models->setVisible(m_charInstance, false);
    // The camera swings in behind once you stop turning it; it rides higher
    // and further back the faster you go.
    m_lookIdle += dt;
    if (m_lookIdle > 1.2f && glm::length(glm::vec2(fwd.x, fwd.z)) > 0.1f) {
        const float k = 1.0f - std::exp(-(m_ride == Ride::Plane ? 2.5f : 3.0f) * dt);
        m_rig.yaw += wrap180(yawOf(fwd) - m_rig.yaw) * k;
        const float wantPitch = m_ride == Ride::Plane ? -8.0f - 0.5f * glm::degrees(std::asin(std::clamp(fwd.y, -1.0f, 1.0f))) : -10.0f;
        m_rig.pitch += (std::clamp(wantPitch, -60.0f, 30.0f) - m_rig.pitch) * (1.0f - std::exp(-2.0f * dt));
    }
    m_rig.settings.armLength = m_rideArm + std::min(std::abs(speed), 60.0f) * (m_ride == Ride::Plane ? 0.05f : 0.04f);
    m_rig.settings.pivotHeight = m_ride == Ride::Plane ? 1.6f : 1.2f;
    m_rig.settings.eyeHeight = m_ride == Ride::Plane ? 0.6f : 0.55f;
    m_rig.settings.shoulderOffset = 0.0f;
    const kke::RigidWorld::BodyId self = rideBody();
    m_rig.update(dt, at, [&w, self](const glm::vec3& from, const glm::vec3& d, float maxD) {
        auto h = w.raycast(from, d, maxD, [self](kke::RigidWorld::BodyId b, kke::RigidWorld::Motion m) { return b != self && m == kke::RigidWorld::Motion::Static; });
        return h.hit ? h.distance : maxD;
    }, m_app->camera());
    if (m_shake > 0.01f) {
        kke::Camera& cam = m_app->camera();
        const float t = static_cast<float>(SDL_GetTicks()) * 0.001f;
        const glm::vec3 j = glm::vec3(std::sin(t * 61.0f), std::sin(t * 47.0f + 1.3f), std::sin(t * 53.0f + 2.1f)) * (0.12f * m_shake);
        cam.position += j;
        cam.target += j * 0.5f;
    }
}

// The ridden vehicle's engine: synthesized (kke::EngineSound), from where it is.
void ShowcaseModule::updateEngineSound(float dt) {
    kke::AudioModule* audio = m_app->getModule<kke::AudioModule>();
    if (!audio) return;
    kke::AudioMixer& mixer = audio->mixer();
    if (m_ride == Ride::None) return;
    float rpm = 0.0f, throttle = 0.0f, slide = 0.0f, speed = 0.0f;
    if (m_ride == Ride::Car) {
        const Car& c = m_cars[static_cast<size_t>(m_rideCar)];
        rpm = c.state.rpm;
        throttle = std::abs(m_carIn.throttle);
        speed = std::abs(c.state.speed);
        for (const kke::VehicleWheelState& ws : c.state.wheels)
            if (ws.contact && speed > 2.0f)
                slide = std::max(slide, std::clamp((std::abs(ws.lateralSlip) - 8.0f) / 25.0f, 0.0f, 1.0f));
    } else {
        if (m_plane.wreck >= 0.0f) return;
        throttle = m_plane.c.throttle;
        speed = m_plane.s.airspeed;
        rpm = 900.0f + 1900.0f * throttle + 8.0f * speed;
    }
    if (!m_engineVoice || !mixer.isPlaying(m_engineVoice)) {
        kke::EngineSound::Params p = m_ride == Ride::Plane ? kke::EngineSound::inline6() : m_rideCar == 2 ? kke::EngineSound::v8() : m_rideCar == 1 ? kke::EngineSound::inline6() : kke::EngineSound::inline4();
        if (m_ride == Ride::Plane) {
            p.maxRpm = 3000.0f;
            p.exhaustHz = 90.0f;
        } else {
            p.maxRpm = kCarKinds[static_cast<size_t>(m_rideCar)].maxRpm;
        }
        m_engine.setParams(p);
        if (m_engineStream) m_engineStream->close();
        m_engineStream = std::make_shared<kke::AudioStream>(static_cast<size_t>(mixer.sampleRate()) / 2);
        kke::VoiceDesc d;
        d.stream = m_engineStream;
        d.position = ridePosition();
        d.spatial = true;
        d.minDistance = 5.0f;
        d.maxDistance = 200.0f;
        d.gain = 0.8f;
        d.priority = 3.0f;
        d.category = kke::SoundCategory::Ambient;
        d.reverbSend = 0.3f;
        m_engineVoice = audio->play(d);
    } else {
        mixer.setPosition(m_engineVoice, ridePosition());
    }
    m_engine.set(rpm, throttle, slide, speed);
    if (!m_engineStream) return;
    const int rate = mixer.sampleRate();
    const size_t want = static_cast<size_t>(static_cast<float>(rate) * std::clamp(dt * 2.0f, 0.1f, 0.3f));
    const size_t have = m_engineStream->buffered();
    if (have >= want) return;
    m_engineScratch.resize(want - have);
    m_engine.render(m_engineScratch.data(), m_engineScratch.size(), rate);
    m_engineStream->push(m_engineScratch.data(), m_engineScratch.size());
}

// The HUD's line while riding.
std::string ShowcaseModule::rideHud() const {
    char buf[160];
    if (m_ride == Ride::Car) {
        const Car& c = m_cars[static_cast<size_t>(m_rideCar)];
        const int gear = c.state.gear;
        std::string lap;
        if (m_lapTime >= 0.0f) {
            char l[96];
            std::snprintf(l, sizeof(l), ", lap %.1f s", static_cast<double>(m_lapTime));
            lap = l;
            if (m_bestLap > 0.0f) {
                std::snprintf(l, sizeof(l), " (best %.1f s)", static_cast<double>(m_bestLap));
                lap += l;
            }
        }
        std::snprintf(buf, sizeof(buf), "%.0f km/h, gear %s%s", static_cast<double>(std::abs(c.state.speed) * 3.6f), gear < 0 ? "R" : gear == 0 ? "N" : std::to_string(gear).c_str(),
                      lap.c_str());
        return buf;
    }
    if (m_ride == Ride::Plane) {
        if (m_plane.wreck >= 0.0f) return "Crashed! A new plane is on its way to the runway";
        const flying::PlaneState& s = m_plane.s;
        const float height = s.position.y - m_flight.gearHeight - groundHeight(s.position.x, s.position.z);
        std::snprintf(buf, sizeof(buf), "%.0f km/h, %.0f m up, throttle %.0f%%%s", static_cast<double>(s.airspeed * 3.6f), static_cast<double>(std::max(height, 0.0f)),
                      static_cast<double>(m_plane.c.throttle * 100.0f), s.stalled ? ", STALL" : "");
        return buf;
    }
    return "";
}

// KKE_DEMO_DRIVE=1: to the race track, into the red hatch, a lap of the
// oval (steering at a point ahead on the centre line); then the airfield,
// a take-off, a circuit at 80 m on the Flying demo's autopilot, and a dive
// into a field (the crash). Logs each step.
void ShowcaseModule::updateDriveDemo(float dt, glm::vec2& move, float& gas, float& brake, bool& handBrake) {
    m_demoDrive += dt;
    const float t = m_demoDrive;
    auto at = [&](float mark) { return t >= mark && t - dt < mark; };
    kke::RigidWorld& w = m_rigid->world();
    // Phase by what is happening, not only by time (physics is slower on a slow PC).
    if (m_ride == Ride::Car && m_rideCar >= 0) {
        const Car& c = m_cars[static_cast<size_t>(m_rideCar)];
        const glm::vec3 p = w.position(rideBody());
        const glm::vec3 f = w.rotation(rideBody()) * glm::vec3(0, 0, 1);
        glm::vec2 target;
        if (ovalDistance({ p.x, p.z }) > kTrackHalf + 2.0f && m_lapTime < 0.0f) {
            target = ovalPoint(25.0f); // onto the near straight
        } else {
            // The nearest centre-line point, then 18 m on.
            float bestS = 0.0f, bestD = 1e9f;
            for (float s = 0.0f; s < kOvalLength; s += 2.0f) {
                const float d = glm::length(ovalPoint(s) - glm::vec2(p.x, p.z));
                if (d < bestD) { bestD = d; bestS = s; }
            }
            target = ovalPoint(bestS + 18.0f);
        }
        const float err = wrap180(yawOf(glm::vec3(target.x - p.x, 0.0f, target.y - p.z)) - yawOf(f));
        move = glm::vec2(std::clamp(err / 25.0f, -1.0f, 1.0f), 0.0f);
        // Slower into the bends (about 0.5 g round a 55 m radius), flat out on the straights.
        const bool bendAhead = std::abs(target.x) > kStraight - 15.0f;
        const float want = std::abs(err) > 25.0f ? 12.0f : bendAhead ? 16.0f : 26.0f; // m/s
        gas = c.state.speed < want ? 0.8f : 0.0f;
        brake = c.state.speed > want + 4.0f ? 0.5f : 0.0f;
        handBrake = false;
        if (at(std::floor(t / 10.0f) * 10.0f))
            kke::log::get(name())->info("drive demo: car at {:.0f} {:.0f}, {:.0f} km/h, {:.0f} degrees round ({:.0f} s)", p.x, p.z, c.state.speed * 3.6f,
                                        glm::degrees(m_lapAngle), t);
        if (m_laps >= 1 || t > 170.0f) {
            // Stop, get out, on to the airfield.
            gas = 0.0f;
            brake = 1.0f;
            if (std::abs(c.state.speed) < 3.0f) {
                kke::log::get(name())->info("drive demo: {} lap(s), last {:.1f} s; out of the car at {:.0f} s", m_laps, m_lastLap, t);
                exitVehicle();
                travelTo(3);
                m_demoWaypoint = 0;
            }
        }
        return;
    }
    if (m_ride == Ride::Plane) {
        const flying::PlaneState& s = m_plane.s;
        static const glm::vec3 circuit[] = { { 380.0f, 70.0f, 40.0f }, { 430.0f, 90.0f, 200.0f }, { 250.0f, 90.0f, 230.0f }, { 180.0f, 80.0f, 100.0f } };
        if (m_demoWaypoint < 4) {
            const flying::Controls c = flying::steerToward(s, circuit[m_demoWaypoint], flightGround(), 20.0f, 0.8f);
            move = glm::vec2(c.roll, -c.pitch);
            const float want = s.onGround ? 1.0f : c.throttle; // the autopilot's throttle, by the same buttons a player has
            gas = want > m_plane.c.throttle + 0.02f ? 1.0f : 0.0f;
            brake = want < m_plane.c.throttle - 0.02f ? 1.0f : 0.0f;
            const glm::vec3 d = circuit[m_demoWaypoint] - s.position;
            // Near enough, or past it (a wide turn can circle a point for ever): on to the next.
            const float dist = glm::length(glm::vec2(d.x, d.z));
            const bool passed = dist < 200.0f && dist > m_demoClosest + 5.0f;
            m_demoClosest = std::min(m_demoClosest, dist);
            if (dist < 80.0f || passed) {
                m_demoClosest = 1e9f;
                kke::log::get(name())->info("drive demo: plane at waypoint {}: {:.0f} m up, {:.0f} km/h ({:.0f} s)", m_demoWaypoint + 1,
                                            s.position.y - groundHeight(s.position.x, s.position.z), s.airspeed * 3.6f, t);
                ++m_demoWaypoint;
            }
        } else {
            // Nose down into the field: the crash.
            move = glm::vec2(0.0f, 1.0f);
            gas = 1.0f;
        }
        if (m_demoWaypoint == 0 && s.onGround && s.airspeed > 1.0f && at(std::floor(t))) {
            kke::log::get(name())->info("drive demo: plane rolling at {:.0f} km/h", s.airspeed * 3.6f);
        }
        return;
    }
    if (at(0.3f) && m_laps == 0) {
        travelTo(4);
        // To the red hatch's door.
        m_loco->teleport(glm::vec3(-6.0f + 1.8f, 0.05f, 172.0f));
    }
    if (at(1.0f) && m_laps == 0) {
        if (!useVehicle()) kke::log::get(name())->info("drive demo: no car in reach");
    }
    // After the car: at the airfield, walk up to the plane and get in.
    if (m_laps >= 1 || t > 170.0f) {
        if (m_plane.wreck < 0.0f && m_demoWaypoint == 0 && t > 1.5f) {
            if (zoneAt(m_rigid->world().characterPosition(m_player)) != 3) travelTo(3);
            m_loco->teleport(m_plane.s.position - glm::vec3(0.0f, 1.05f, -2.5f));
            if (!useVehicle()) kke::log::get(name())->info("drive demo: the plane isn't in reach");
        } else if (m_demoWaypoint > 0 && m_plane.wreck < 0.0f) { // flown (and crashed): back on the runway
            kke::log::get(name())->info("drive demo: done ({:.0f} s)", t);
            m_demoDrive = -1.0f;
        }
    }
}

} // namespace kke_showcase
