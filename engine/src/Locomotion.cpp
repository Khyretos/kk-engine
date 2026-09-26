#include "kke/Locomotion.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace kke {

namespace {

// Yaw convention shared with CameraRig and the demos: 0 = -Z, 90 = +X.
float yawOf(const glm::vec3& d) { return glm::degrees(std::atan2(d.x, -d.z)); }
glm::vec3 dirOf(float yaw) {
    const float r = glm::radians(yaw);
    return glm::vec3(std::sin(r), 0.0f, -std::cos(r));
}
glm::vec3 flat(const glm::vec3& v) { return glm::vec3(v.x, 0.0f, v.z); }
float smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
// Turns `from` toward `to` by at most `maxDegrees`.
glm::vec3 turnToward(const glm::vec3& from, const glm::vec3& to, float maxDegrees) {
    const float a = yawOf(from), diff = Locomotion::angleBetween(a, yawOf(to));
    return dirOf(a + std::clamp(diff, -maxDegrees, maxDegrees));
}

} // namespace

float Locomotion::angleBetween(float fromYaw, float toYaw) {
    float d = std::remainder(toYaw - fromYaw, 360.0f);
    if (d <= -180.0f) d += 360.0f;
    return d;
}

glm::vec2 Locomotion::leapParabola(float xl, float yl, float overshoot) {
    // y = a x^2 + b x passes through (0,0). Through (xl, yl):
    //   b = (yl - a xl^2) / xl
    // and the peak -b^2 / 4a sits at yl + h, which gives a quadratic in a:
    //   xl^4 a^2 + xl^2 (4 (yl + h) - 2 yl) a + yl^2 = 0
    // Of its two roots, the one whose peak x = -b / 2a lies between the
    // points (the other peaks outside and only touches (xl, yl) on the way
    // down or up).
    xl = std::max(xl, 1e-4f);
    const float peak = yl + std::max(overshoot, 1e-4f);
    if (peak <= 0.0f) return glm::vec2(0.0f, yl / xl); // no arc possible: straight line
    const float A = xl * xl * xl * xl, B = xl * xl * (4.0f * peak - 2.0f * yl), C = yl * yl;
    const float disc = std::sqrt(std::max(0.0f, B * B - 4.0f * A * C));
    glm::vec2 best(0.0f, yl / xl);
    float bestErr = 1e30f;
    for (float a : { (-B - disc) / (2.0f * A), (-B + disc) / (2.0f * A) }) {
        if (a >= -1e-9f) continue;
        const float b = (yl - a * xl * xl) / xl;
        const float px = -b / (2.0f * a);
        const float err = px < 0.0f ? -px : px > xl ? px - xl : 0.0f;
        if (err < bestErr) { bestErr = err; best = glm::vec2(a, b); }
    }
    return best;
}

Locomotion::Locomotion(RigidWorld& world, RigidWorld::CharacterId id) : Locomotion(world, id, Settings{}) {}

Locomotion::Locomotion(RigidWorld& world, RigidWorld::CharacterId id, const Settings& settings)
    : m_world(world), m_id(id), m_settings(settings) {}

float Locomotion::facingYaw() const { return yawOf(m_facing); }

void Locomotion::setFacing(const glm::vec3& dir) {
    if (glm::length(flat(dir)) > 1e-4f) m_facing = glm::normalize(flat(dir));
}

float Locomotion::traversalProgress() const {
    if (m_state != State::Vault && m_state != State::Climb) return 0.0f;
    return m_duration > 0.0f ? std::clamp(m_stateTime / m_duration, 0.0f, 1.0f) : 1.0f;
}

float Locomotion::targetSpeed(const Input& in) const {
    const Settings& s = m_settings;
    return in.crouch ? s.crouchSpeed : in.fast ? s.sprintSpeed : in.slow ? s.walkSpeed : s.runSpeed;
}

void Locomotion::enter(State s) {
    m_state = s;
    m_stateTime = 0.0f;
}

// ---------------------------------------------------------------------------
// Area awareness
// ---------------------------------------------------------------------------

Locomotion::Obstacle Locomotion::probe(const glm::vec3& direction, const Sensor& sensor) const {
    Obstacle o;
    const Settings& s = m_settings;
    glm::vec3 dir = flat(direction);
    if (glm::length(dir) < 1e-4f) return o;
    dir = glm::normalize(dir);
    const glm::vec3 feet = m_world.characterPosition(m_id);

    // 1. The front face: rays forward from the body at knee, hip and chest
    //    height (anything lower is a step the controller climbs by itself).
    RigidWorld::RayHit face;
    for (float y : { s.stepHeight + 0.05f, 0.9f, 1.4f }) {
        RigidWorld::RayHit h = m_world.raycast(feet + glm::vec3(0.0f, y, 0.0f), dir, s.radius + sensor.reach);
        if (!h.hit || std::abs(h.normal.y) > 0.5f) continue; // floors and ceilings aren't faces
        if (!face.hit || h.distance < face.distance) face = h;
    }
    if (!face.hit) return o;
    glm::vec3 n = flat(face.normal);
    if (glm::length(n) < 1e-3f) return o;
    n = glm::normalize(n);
    // Approach square to the face, whatever angle the ray came in at.
    const glm::vec3 in = -n;
    o.face = face.point;
    o.normal = n;

    // 2. The top: a ray down, just past the face, from above the highest
    //    top this sensor can reach. Starting inside something = too tall.
    const float probeTop = feet.y + sensor.maxHeight + 0.3f;
    const glm::vec3 overFace(face.point.x + in.x * 0.08f, probeTop, face.point.z + in.z * 0.08f);
    RigidWorld::RayHit top = m_world.raycast(overFace, glm::vec3(0, -1, 0), sensor.maxHeight + 0.3f);
    if (!top.hit || top.distance < 0.02f || top.normal.y < 0.7f) return o;
    const float topY = top.point.y;
    o.height = topY - feet.y;
    if (o.height < s.stepHeight || o.height > sensor.maxHeight) return o;
    // Room above the top edge for hands and a tucked body.
    if (m_world.raycast(top.point + glm::vec3(0, 0.02f, 0), glm::vec3(0, 1, 0), 0.9f).hit) return o;

    // 3. Depth: walk across the top until it drops away.
    o.depth = 1e9f;
    const float stepLen = 0.1f;
    for (float d = stepLen; d <= s.vaultMaxDepth + 0.5f; d += stepLen) {
        glm::vec3 p = face.point + in * (0.08f + d);
        RigidWorld::RayHit h = m_world.raycast(glm::vec3(p.x, topY + 0.3f, p.z), glm::vec3(0, -1, 0), 0.45f);
        if (!h.hit) { o.depth = d; break; }
    }

    // 4. Classify, cheapest first. A thin, low obstacle with a floor
    //    behind it is a vault; anything with room on top is a climb.
    if (o.height <= s.vaultMaxHeight && o.depth <= s.vaultMaxDepth) {
        glm::vec3 land = face.point + in * (0.08f + o.depth + s.radius + 0.35f);
        RigidWorld::RayHit floor = m_world.raycast(glm::vec3(land.x, topY + 0.2f, land.z), glm::vec3(0, -1, 0), o.height + 1.7f);
        if (floor.hit && floor.normal.y > 0.7f) {
            glm::vec3 feetThere(land.x, floor.point.y + 0.01f, land.z);
            // Tucked body over the top, standing body at the landing.
            glm::vec3 over = face.point + in * (0.08f + o.depth * 0.5f);
            if (m_world.capsuleFits(glm::vec3(over.x, topY + 0.05f, over.z), 0.9f, s.radius) &&
                m_world.capsuleFits(feetThere + glm::vec3(0, 0.02f, 0), s.height, s.radius)) {
                o.kind = Obstacle::Kind::Vault;
                o.target = feetThere;
                return o;
            }
        }
    }
    if (o.depth >= 2.0f * s.radius + 0.1f) {
        glm::vec3 onTop = face.point + in * (0.08f + s.radius + 0.2f);
        glm::vec3 feetThere(onTop.x, topY + 0.01f, onTop.z);
        if (m_world.capsuleFits(feetThere + glm::vec3(0, 0.02f, 0), s.height, s.radius)) {
            o.kind = Obstacle::Kind::Climb;
            o.target = feetThere;
        }
    }
    return o;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void Locomotion::update(const Input& in, float dt) {
    if (dt <= 0.0f) return;
    m_jumped = m_landed = false;
    m_stateTime += dt;
    const glm::vec3 vel = m_world.characterVelocity(m_id);
    m_measuredSpeed = glm::length(glm::vec2(vel.x, vel.z));
    // Queued input: "go up" pressed a moment too early still counts.
    m_buffer = in.goUp ? m_settings.jumpBuffer : std::max(0.0f, m_buffer - dt);

    if (m_state == State::Vault || m_state == State::Climb) {
        updateTraversal(dt);
        return;
    }
    const bool grounded = m_world.characterOnGround(m_id);
    if (m_state == State::Ground) updateGround(in, dt, grounded);
    else updateAir(in, dt, grounded);
}

void Locomotion::teleport(const glm::vec3& feet) {
    m_world.setCharacterKinematic(m_id, false);
    m_world.teleportCharacter(m_id, feet);
    m_world.setCharacterInput(m_id, RigidWorld::CharacterInput{});
    m_speed = 0.0f;
    m_buffer = 0.0f;
    m_sinceGrounded = 0.0f;
    enter(State::Ground);
}

void Locomotion::jump(const Input& in) {
    const Settings& s = m_settings;
    // The take-off is the last moment on the ground: it may re-aim the
    // run at the input direction (PointDown's "glue" window), so a jump
    // right after a turn goes where you asked, not where you were going.
    glm::vec3 wish = flat(in.move);
    if (glm::length(wish) > 0.1f) m_moveDir = glm::normalize(wish);
    RigidWorld::CharacterInput ci;
    ci.move = m_moveDir * m_speed;
    ci.jump = true;
    ci.jumpSpeed = s.jumpSpeed;
    ci.airSteer = 1e6f;
    m_world.setCharacterInput(m_id, ci);
    m_airEntrySpeed = m_speed;
    m_airPeak = m_world.characterPosition(m_id).y;
    m_jumped = true;
    m_jumpedFromGround = true;
    m_buffer = 0.0f;
    enter(State::Air);
}

bool Locomotion::tryTraversal(const Input& in, const Sensor& sensor, bool inAir) {
    // Look where the player is steering, or straight ahead when standing.
    glm::vec3 wish = flat(in.move);
    glm::vec3 look = glm::length(wish) > 0.1f ? glm::normalize(wish) : m_facing;
    Obstacle o = probe(look, sensor);
    if (o.kind == Obstacle::Kind::None) return false;
    // Only when heading into it, not glancing along it.
    if (glm::dot(look, -o.normal) < 0.55f) return false;
    if (inAir) {
        // A mid-air grab: hands must reach the top, and it can't be far below.
        if (o.height < 0.6f || o.kind == Obstacle::Kind::Vault) o.kind = Obstacle::Kind::Climb;
        if (o.height < 0.6f) return false;
        const glm::vec3 feetThere = o.target;
        if (!m_world.capsuleFits(feetThere + glm::vec3(0, 0.02f, 0), m_settings.height, m_settings.radius)) return false;
    }
    startTraversal(o.kind == Obstacle::Kind::Vault ? State::Vault : State::Climb, o);
    return true;
}

void Locomotion::updateGround(const Input& in, float dt, bool grounded) {
    const Settings& s = m_settings;
    const glm::vec3 feet = m_world.characterPosition(m_id);
    if (grounded) {
        m_sinceGrounded = 0.0f;
        m_jumpedFromGround = false;
    } else {
        m_sinceGrounded += dt;
        // Floor tier two: a few cm above the floor (a kerb, a stair edge
        // going down) is still walking. Snap down instead of falling.
        const glm::vec3 vel = m_world.characterVelocity(m_id);
        RigidWorld::RayHit below = m_world.raycast(feet + glm::vec3(0, 0.05f, 0), glm::vec3(0, -1, 0), s.groundSnap + 0.05f);
        if (below.hit && below.normal.y > 0.7f && vel.y <= 0.5f) {
            m_world.moveCharacter(m_id, glm::vec3(feet.x, below.point.y + 0.005f, feet.z));
            m_sinceGrounded = 0.0f;
        } else if (m_sinceGrounded > s.coyoteTime || vel.y > 0.5f) {
            // Tier three: really in the air.
            m_airEntrySpeed = m_speed;
            m_airPeak = feet.y;
            enter(State::Air);
            updateAir(in, dt, false);
            return;
        }
    }

    // Translate "go up" for this state: vault or climb if the world offers
    // one ahead, otherwise jump.
    if (m_buffer > 0.0f && !in.crouch) {
        const bool sprinting = in.fast && m_speed > s.runSpeed * 0.9f;
        if (tryTraversal(in, sprinting ? s.sprintSensor : s.walkSensor, false)) return;
        jump(in);
        return;
    }

    // Speed and direction, separately (PointDown MM1).
    glm::vec3 wish = flat(in.move);
    const float amount = std::min(1.0f, glm::length(wish));
    float target = 0.0f;
    if (amount > 0.05f) {
        wish = glm::normalize(wish);
        target = targetSpeed(in) * amount;
        if (m_speed < 0.2f) {
            m_moveDir = wish; // from standing: just face it
        } else {
            float diff = angleBetween(yawOf(m_moveDir), yawOf(wish));
            // A near-180 reversal keeps turning the way it started instead
            // of flipping sides every frame on float noise.
            if (std::abs(diff) > 180.0f - s.reverseHysteresis && (diff > 0.0f) != (m_lastTurnSign > 0.0f))
                diff = diff > 0.0f ? diff - 360.0f : diff + 360.0f;
            if (std::abs(diff) > 0.5f) m_lastTurnSign = diff > 0.0f ? 1.0f : -1.0f;
            const float rate = (in.fast ? s.sprintTurnRate : s.turnRate) * dt;
            m_moveDir = dirOf(yawOf(m_moveDir) + std::clamp(diff, -rate, rate));
            // Creatures that value their lives slow down in sharp turns.
            if (std::abs(diff) > s.turnSlowAngle) target *= s.turnSpeedFactor;
        }
    }
    const float rate = (target > m_speed ? s.acceleration : s.deceleration) * dt;
    m_speed += std::clamp(target - m_speed, -rate, rate);
    // Pushing into a wall doesn't build up speed that isn't there.
    m_speed = std::min(m_speed, std::max(m_measuredSpeed + s.acceleration * dt * 2.0f, target * 0.25f));

    RigidWorld::CharacterInput ci;
    ci.move = m_moveDir * m_speed;
    m_world.setCharacterInput(m_id, ci);
    if (m_speed > 0.2f) m_facing = turnToward(m_facing, m_moveDir, 900.0f * dt);
}

void Locomotion::updateAir(const Input& in, float dt, bool grounded) {
    const Settings& s = m_settings;
    const glm::vec3 feet = m_world.characterPosition(m_id);
    m_airPeak = std::max(m_airPeak, feet.y);
    if (grounded && m_stateTime > 0.05f) {
        m_landed = true;
        m_fallHeight = m_airPeak - feet.y;
        m_sinceGrounded = 0.0f;
        m_jumpedFromGround = false;
        m_speed = m_measuredSpeed;
        const glm::vec3 vel = m_world.characterVelocity(m_id);
        if (m_measuredSpeed > 0.1f) m_moveDir = glm::normalize(flat(vel));
        enter(State::Ground);
        // Queued "go up" fires on landing (bunny hop / vault chain).
        if (m_buffer > 0.0f) updateGround(in, 0.0f, true);
        return;
    }
    m_sinceGrounded += dt;
    // Coyote time: just ran off an edge, a jump still works.
    if (m_buffer > 0.0f && !m_jumpedFromGround && m_sinceGrounded <= s.coyoteTime) {
        jump(in);
        return;
    }
    const glm::vec3 vel = m_world.characterVelocity(m_id);
    // Grab a ledge in front while rising slowly or falling.
    if (vel.y < 1.5f && (m_buffer > 0.0f || glm::length(flat(in.move)) > 0.5f) && tryTraversal(in, s.airSensor, true)) return;

    // Air control is a small acceleration added to the flight, capped at
    // the take-off speed (PointDown MM2): it corrects a jump, it doesn't
    // fly the character around.
    glm::vec3 h = flat(vel);
    glm::vec3 wish = flat(in.move);
    const float amount = std::min(1.0f, glm::length(wish));
    if (amount > 0.05f) {
        wish = glm::normalize(wish);
        h += wish * (s.airAcceleration * amount * dt);
        const float cap = std::max(m_airEntrySpeed, s.airSpeedMin);
        if (glm::length(h) > cap) h = glm::normalize(h) * cap;
        // The body faces where we meant to go; a wall that deflects the
        // velocity doesn't turn the head.
        m_facing = turnToward(m_facing, wish, s.airTurnRate * dt);
    }
    RigidWorld::CharacterInput ci;
    ci.move = h;
    ci.airSteer = 1e6f;
    m_world.setCharacterInput(m_id, ci);
}

// ---------------------------------------------------------------------------
// Vault / climb: scripted, kinematic, along a checked path
// ---------------------------------------------------------------------------

void Locomotion::startTraversal(State st, const Obstacle& o) {
    const Settings& s = m_settings;
    m_obstacle = o;
    m_start = m_world.characterPosition(m_id);
    m_startFacing = m_facing;
    // Where the body is when the hands are on the top edge.
    m_wallPoint = glm::vec3(o.face.x, m_start.y, o.face.z) + o.normal * (s.radius + 0.05f);
    const float entry = std::max(m_measuredSpeed, m_speed);
    if (st == State::Vault) {
        const glm::vec3 d = flat(o.target - m_start);
        const float dist = glm::length(d);
        // Fast approach = quick vault that keeps the momentum (speed vault).
        m_duration = std::clamp(dist / std::max(entry, 2.5f), s.vaultMinTime, s.vaultMaxTime);
        m_exitSpeed = std::max(entry * 0.9f, s.runSpeed * 0.5f);
        // Arc over the top: raise the peak until the feet clear both
        // edges (hands on top, legs tucked: a little under the top is fine).
        const float yl = o.target.y - m_start.y;
        const float topRel = o.height;
        const float frontX = std::max(0.05f, glm::dot(flat(o.face - m_start), glm::normalize(d)));
        const float backX = std::min(dist - 0.05f, frontX + o.depth);
        float overshoot = std::max(0.05f, topRel + s.vaultClearance - yl);
        for (int i = 0; i < 16; ++i) {
            m_arc = leapParabola(dist, yl, overshoot);
            auto y = [&](float x) { return m_arc.x * x * x + m_arc.y * x; };
            if (y(frontX) >= topRel - 0.1f && y(backX) >= topRel - 0.1f) break;
            overshoot += 0.08f;
        }
    } else {
        m_duration = s.climbTime * (0.6f + 0.4f * std::clamp(o.height / s.sprintSensor.maxHeight, 0.0f, 1.0f));
        m_exitSpeed = 0.0f;
    }
    m_world.setCharacterKinematic(m_id, true);
    m_world.setCharacterVelocity(m_id, glm::vec3(0.0f));
    enter(st);
}

void Locomotion::updateTraversal(float) {
    const Settings& s = m_settings;
    const float t = traversalProgress();
    const glm::vec3 in = -m_obstacle.normal;
    // Correction window: square up to the obstacle.
    const float c = smooth(m_stateTime / std::max(1e-3f, s.correctionTime));
    m_facing = dirOf(yawOf(m_startFacing) + angleBetween(yawOf(m_startFacing), yawOf(in)) * c);

    glm::vec3 p;
    if (m_state == State::Vault) {
        // Constant horizontal speed (momentum), parabolic height.
        const glm::vec3 d = flat(m_obstacle.target - m_start);
        const float x = glm::length(d) * t;
        p = m_start + d * t;
        p.y = m_start.y + m_arc.x * x * x + m_arc.y * x;
    } else {
        // Up to the edge (hands on top, feet walking the wall), then over.
        const float topY = m_obstacle.target.y;
        const float split = 0.6f;
        if (t < split) {
            const float u = smooth(t / split);
            p = glm::mix(m_start, m_wallPoint, std::min(1.0f, u * 2.0f));
            p.y = glm::mix(m_start.y, topY + 0.03f, u);
        } else {
            const float u = smooth((t - split) / (1.0f - split));
            p = glm::mix(glm::vec3(m_wallPoint.x, topY + 0.03f, m_wallPoint.z), m_obstacle.target, u);
        }
    }
    m_world.moveCharacter(m_id, p);

    if (t >= 1.0f) {
        m_world.moveCharacter(m_id, m_obstacle.target);
        m_world.setCharacterKinematic(m_id, false);
        m_moveDir = in;
        m_speed = m_exitSpeed;
        m_world.setCharacterVelocity(m_id, in * m_exitSpeed);
        RigidWorld::CharacterInput ci;
        ci.move = in * m_exitSpeed;
        m_world.setCharacterInput(m_id, ci);
        m_landed = true;
        m_fallHeight = 0.0f;
        m_sinceGrounded = 0.0f;
        enter(State::Ground);
    }
}

} // namespace kke
