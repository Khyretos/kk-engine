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
    if (glm::length(dir) < 1e-4f) { o.why = "no direction"; return o; }
    dir = glm::normalize(dir);
    const glm::vec3 feet = m_world.characterPosition(m_id);

    // Faces are anything steeper than the walkable slope (Jolt's default
    // 50 degrees): normal.y below cos(50).
    const float maxFaceNormalY = 0.64f;
    // 1. The front face: rays forward from the body at knee, hip and chest
    //    height (anything lower is a step the controller climbs by itself),
    //    from the middle and both shoulders: a single centre ray slips
    //    through the seam between two fence panels the capsule can't.
    RigidWorld::RayHit face;
    const glm::vec3 side(-dir.z, 0.0f, dir.x);
    for (float y : { s.stepHeight + 0.05f, 0.9f, 1.4f }) {
        for (float x : { 0.0f, -0.6f * s.radius, 0.6f * s.radius }) {
            RigidWorld::RayHit h = m_world.raycast(feet + glm::vec3(0.0f, y, 0.0f) + side * x, dir, s.radius + sensor.reach);
            // Floors, ceilings and walkable slopes aren't faces; anything
            // steeper than the controller can walk up is.
            if (!h.hit || std::abs(h.normal.y) > maxFaceNormalY) continue;
            if (!face.hit || h.distance < face.distance) face = h;
        }
    }
    if (!face.hit) { o.why = "nothing ahead"; return o; }
    glm::vec3 n = flat(face.normal);
    if (glm::length(n) < 1e-3f) { o.why = "no face"; return o; }
    n = glm::normalize(n);
    // Approach square to the face, whatever angle the ray came in at.
    const glm::vec3 in = -n;
    o.face = face.point;
    o.normal = n;

    // 2. The top: a ray down, just past the face, from above the highest
    //    top this sensor can reach. Starting inside something = too tall.
    const float probeTop = feet.y + sensor.maxHeight + 0.3f;
    //    A rounded or bevelled rim (rocks) gets a second look further in.
    RigidWorld::RayHit top;
    for (float inset : { 0.08f, 0.25f }) {
        const glm::vec3 overFace(face.point.x + in.x * inset, probeTop, face.point.z + in.z * inset);
        top = m_world.raycast(overFace, glm::vec3(0, -1, 0), sensor.maxHeight + 0.3f);
        if (!top.hit || top.distance < 0.02f || top.normal.y >= 0.7f) break;
    }
    if (!top.hit || top.distance < 0.02f) { o.why = "too tall"; return o; }
    if (top.normal.y < 0.7f) { o.why = "top not flat"; return o; }
    const float topY = top.point.y;
    o.height = topY - feet.y;
    if (o.height < s.stepHeight) { o.why = "a step"; return o; }
    if (o.height > sensor.maxHeight) { o.why = "too tall"; return o; }
    // Room above the top edge for hands and a tucked body.
    if (m_world.raycast(top.point + glm::vec3(0, 0.02f, 0), glm::vec3(0, 1, 0), 0.9f).hit) { o.why = "no room above"; return o; }

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
        // Rock and terrain tops aren't flat: stand on whatever is under the
        // target spot (up to a step above the edge), a little higher if
        // the ground rises under the capsule's rim.
        glm::vec3 onTop = face.point + in * (0.08f + s.radius + 0.2f);
        RigidWorld::RayHit ground = m_world.raycast(glm::vec3(onTop.x, topY + s.stepHeight + 0.3f, onTop.z), glm::vec3(0, -1, 0), s.stepHeight + 0.6f);
        if (!ground.hit || ground.distance < 0.02f || ground.normal.y < 0.7f) { o.why = "no floor on top"; return o; }
        for (float lift : { 0.02f, 0.1f, 0.2f }) {
            glm::vec3 feetThere(onTop.x, ground.point.y + lift, onTop.z);
            if (m_world.capsuleFits(feetThere, s.height, s.radius)) {
                o.kind = Obstacle::Kind::Climb;
                o.target = feetThere;
                return o;
            }
        }
        o.why = "no room on top";
        return o;
    }
    o.why = o.height <= s.vaultMaxHeight && o.depth <= s.vaultMaxDepth ? "no room to land" : "too thin to stand on";
    return o;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void Locomotion::update(const Input& in, float dt) {
    if (dt <= 0.0f) return;
    m_jumped = m_landed = false;
    m_stateTime += dt;
    // Speed from how far the feet really moved: running into a wall is
    // standing still (the controller's velocity still says "running").
    // Physics moves the feet on its own fixed tick, not once per frame, so
    // the distance is over the time simulated since the last frame; a frame
    // with no physics step keeps the last reading instead of reading 0
    // (which flashed the animation between idle and walk). Vaults and
    // climbs move the feet here every frame, so they use the frame time.
    const glm::vec3 feetNow = m_world.characterPosition(m_id);
    const double simNow = m_world.simulatedTime();
    const float moved = float(simNow - m_lastSimTime);
    const bool scripted = m_state == State::Vault || m_state == State::Climb || m_state == State::Hang;
    const float over = scripted ? dt : moved;
    if (m_haveLastFeet && over > 0.0f) m_measuredSpeed = glm::length(glm::vec2(feetNow.x - m_lastFeet.x, feetNow.z - m_lastFeet.z)) / over;
    m_lastFeet = feetNow;
    m_lastSimTime = simNow;
    m_haveLastFeet = true;
    // Queued input: "go up" pressed a moment too early still counts.
    m_buffer = in.goUp ? m_settings.jumpBuffer : std::max(0.0f, m_buffer - dt);

    m_regrab = std::max(0.0f, m_regrab - dt);
    if (m_state == State::Vault || m_state == State::Climb) {
        updateTraversal(dt);
        return;
    }
    if (m_state == State::Hang) {
        updateHang(in, dt);
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
    m_haveLastFeet = false;
    m_measuredSpeed = 0.0f;
    m_speed = 0.0f;
    m_buffer = 0.0f;
    m_sinceGrounded = 0.0f;
    m_shimmy = 0.0f;
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
        if (m_regrab > 0.0f) return false;
        if (o.height < 0.6f || o.kind == Obstacle::Kind::Vault) o.kind = Obstacle::Kind::Climb;
        if (o.height < 0.6f) return false;
        // High and "go up" not pressed: hang from it.
        if (o.height >= m_settings.hangMinHeight && m_buffer <= 0.0f) {
            glm::vec3 feet, edge, n;
            if (findEdge(m_world.characterPosition(m_id), o.normal, o.target.y, feet, edge, n)) {
                startHang(o);
                return true;
            }
        }
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
        // Landing on a slope: the feet slid sideways while the velocity is
        // (nearly) straight down, so there may be no direction to keep.
        if (m_measuredSpeed > 0.1f && glm::length(flat(vel)) > 1e-3f) m_moveDir = glm::normalize(flat(vel));
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
    if (vel.y < 1.5f && m_buffer <= 0.0f && glm::length(flat(in.move)) > 0.5f && tryHang(in)) return;

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

// ---------------------------------------------------------------------------
// Ledge hang and shimmy (*Ledge actions*)
// ---------------------------------------------------------------------------

bool Locomotion::findEdge(const glm::vec3& feet, const glm::vec3& normal, float topY, glm::vec3& outFeet, glm::vec3& outEdge,
                          glm::vec3& outNormal) const {
    const Settings& s = m_settings;
    // The wall, at chest height below the top.
    const glm::vec3 in = -normal;
    const glm::vec3 chest(feet.x, topY - 0.3f, feet.z);
    RigidWorld::RayHit wall = m_world.raycast(chest + normal * 0.2f, in, s.radius + 1.2f); // a grab starts up to a sensor reach away
    if (!wall.hit || std::abs(wall.normal.y) > 0.64f) return false;
    glm::vec3 n = flat(wall.normal);
    if (glm::length(n) < 1e-3f) return false;
    n = glm::normalize(n);
    if (glm::dot(n, normal) < 0.7f) return false; // a corner, not the same wall
    // The top, just in from the face.
    const glm::vec3 over = wall.point - n * 0.12f;
    RigidWorld::RayHit top = m_world.raycast(glm::vec3(over.x, topY + 0.5f, over.z), glm::vec3(0, -1, 0), 0.5f + s.hangTopTolerance + 0.3f);
    if (!top.hit || top.normal.y < 0.64f || std::abs(top.point.y - topY) > s.hangTopTolerance) return false;
    outNormal = n;
    outEdge = glm::vec3(wall.point.x, top.point.y, wall.point.z);
    outFeet = glm::vec3(wall.point.x, top.point.y - s.hangReach, wall.point.z) + n * (s.radius + 0.05f);
    return true;
}

bool Locomotion::tryHang(const Input& in) {
    const Settings& s = m_settings;
    if (m_regrab > 0.0f) return false;
    const glm::vec3 look = glm::normalize(flat(in.move));
    const glm::vec3 feet = m_world.characterPosition(m_id);
    // The wall below where a hangable top would be, then its top.
    const glm::vec3 low = feet + glm::vec3(0.0f, s.hangMinHeight - 0.2f, 0.0f);
    RigidWorld::RayHit wall = m_world.raycast(low, look, s.radius + s.airSensor.reach);
    if (!wall.hit || std::abs(wall.normal.y) > 0.64f) return false;
    glm::vec3 n = flat(wall.normal);
    if (glm::length(n) < 1e-3f) return false;
    n = glm::normalize(n);
    if (glm::dot(look, -n) < 0.55f) return false; // heading into it, not along it
    const float highest = s.hangReach + 0.3f;
    const glm::vec3 over = wall.point - n * 0.12f;
    RigidWorld::RayHit top = m_world.raycast(glm::vec3(over.x, feet.y + highest + 0.05f, over.z), glm::vec3(0, -1, 0),
                                             highest + 0.05f - (s.hangMinHeight - 0.2f));
    if (!top.hit || top.normal.y < 0.64f) return false;
    const float h = top.point.y - feet.y;
    if (h < s.hangMinHeight || h > highest) return false;
    Obstacle o;
    o.kind = Obstacle::Kind::Climb;
    o.face = wall.point;
    o.normal = n;
    o.height = h;
    o.target = glm::vec3(over.x, top.point.y, over.z);
    glm::vec3 f, e, nn;
    if (!findEdge(feet, n, top.point.y, f, e, nn)) return false;
    startHang(o);
    return true;
}

void Locomotion::startHang(const Obstacle& o) {
    glm::vec3 feet, edge, n;
    if (!findEdge(m_world.characterPosition(m_id), o.normal, o.target.y, feet, edge, n)) return;
    m_obstacle = o;
    m_obstacle.normal = n;
    m_start = m_world.characterPosition(m_id);
    m_startFacing = m_facing;
    m_hangFeet = feet;
    m_hangEdge = edge;
    m_shimmy = 0.0f;
    m_speed = 0.0f;
    m_world.setCharacterKinematic(m_id, true);
    m_world.setCharacterVelocity(m_id, glm::vec3(0.0f));
    enter(State::Hang);
}

void Locomotion::letGo() {
    const glm::vec3 feet = m_world.characterPosition(m_id);
    m_world.setCharacterKinematic(m_id, false);
    m_world.setCharacterVelocity(m_id, m_obstacle.normal * m_settings.dropPush);
    m_airEntrySpeed = 0.0f;
    m_airPeak = feet.y;
    m_jumpedFromGround = true; // no coyote jump off a ledge you let go of
    m_sinceGrounded = m_settings.coyoteTime + 1.0f;
    m_regrab = m_settings.regrabDelay;
    m_shimmy = 0.0f;
    enter(State::Air);
}

void Locomotion::updateHang(const Input& in, float dt) {
    const Settings& s = m_settings;
    const glm::vec3 n = m_obstacle.normal;
    m_facing = -n;
    // Pull in to the hang position first.
    if (m_stateTime < s.hangEnterTime) {
        const float u = smooth(m_stateTime / s.hangEnterTime);
        m_world.moveCharacter(m_id, glm::mix(m_start, m_hangFeet, u));
        return;
    }
    if (in.crouch) {
        letGo();
        return;
    }
    if (m_buffer > 0.0f) {
        // Climb up from here: the same checked climb as from the ground.
        Sensor reach{ s.radius + 0.5f, s.hangReach + 0.3f };
        Obstacle o = probe(-n, reach);
        if (o.kind != Obstacle::Kind::None && m_world.capsuleFits(o.target + glm::vec3(0, 0.02f, 0), s.height, s.radius)) {
            m_buffer = 0.0f;
            startTraversal(State::Climb, o);
            return;
        }
    }
    // Shimmy: the sideways part of the input, one checked step at a time.
    const glm::vec3 right = glm::normalize(glm::cross(-n, glm::vec3(0, 1, 0)));
    const float side = glm::dot(flat(in.move), right);
    m_shimmy = 0.0f;
    glm::vec3 feet = m_hangFeet;
    if (std::abs(side) > 0.2f) {
        const float v = s.shimmySpeed * std::clamp(side, -1.0f, 1.0f);
        const glm::vec3 next = m_hangFeet + right * (v * dt);
        glm::vec3 f, e, nn;
        if (findEdge(next, n, m_hangEdge.y, f, e, nn) && m_world.capsuleFits(f + glm::vec3(0, 0.02f, 0), s.height, s.radius)) {
            m_hangFeet = f;
            m_hangEdge = e;
            m_obstacle.normal = nn;
            m_shimmy = v;
            feet = f;
        }
    }
    m_world.moveCharacter(m_id, feet);
}

} // namespace kke
