// The strokes (Swing.h): each one's shape and clock, which one fits a
// ball, how timing becomes quality, and stamina. The shapes follow how
// coaches break a stroke down (ready, unit turn and takeback, the drop
// into the slot, contact in front, follow-through) with the numbers of
// real strokes: a groundstroke's forward swing takes about 0.2 s, a
// volley's about 0.1 s; a clean hit is within about 30 ms of the ideal
// contact point in time (a ball at 25 m/s covers 75 cm in 30 ms).

#include "Swing.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace tennis {

namespace {

float ease(float u) {
    u = std::clamp(u, 0.0f, 1.0f);
    return u * u * (3.0f - 2.0f * u);
}

// A quadratic Bezier from a to c that passes through b halfway.
glm::vec3 through(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float u) {
    const glm::vec3 ctrl = 2.0f * b - 0.5f * (a + c);
    const float v = 1.0f - u;
    return v * v * a + 2.0f * v * u * ctrl + u * u * c;
}

std::array<StrokeShape, static_cast<size_t>(Stroke::Count)> makeShapes() {
    std::array<StrokeShape, static_cast<size_t>(Stroke::Count)> s{};
    auto& ready = s[static_cast<size_t>(Stroke::Ready)];
    ready.takeback = ready.slot = ready.contact = ready.finish = ready.ready;

    auto& toss = s[static_cast<size_t>(Stroke::Toss)];
    toss.takeback = { 0.3f, 1.95f, -0.35f }; // the trophy position, waiting for the ball
    toss.slot = toss.contact = toss.finish = toss.takeback;
    toss.twist = 75.0f;
    toss.crouch = 0.12f;

    // Topspin drive: takeback high behind, drop below the ball, brush up
    // and finish over the other shoulder (the "windscreen wiper").
    auto& drive = s[static_cast<size_t>(Stroke::Drive)];
    drive.takeback = { 0.75f, 1.25f, -0.55f };
    drive.slot = { 0.7f, 0.55f, -0.1f };
    drive.contact = { 0.7f, 0.95f, 0.45f };
    drive.finish = { -0.4f, 1.7f, 0.1f };
    drive.faceOpen = -8.0f;
    drive.twist = 70.0f;
    drive.crouch = 0.12f;
    drive.windUp = 0.35f;
    drive.forward = 0.22f;
    drive.follow = 0.35f;
    drive.twoHandedBackhand = true;

    // Flat: straight through the ball, finishing out in front.
    auto& flat = s[static_cast<size_t>(Stroke::Flat)];
    flat = drive;
    flat.slot = { 0.72f, 0.85f, -0.1f };
    flat.finish = { -0.45f, 1.3f, 0.45f };
    flat.faceOpen = 0.0f;
    flat.twist = 65.0f;
    flat.forward = 0.2f;
    flat.window = 0.028f;
    flat.maxError = 0.12f;
    flat.speed = 1.1f;
    flat.control = 1.15f;

    // Slice: high takeback, cut down and through, open face.
    auto& slice = s[static_cast<size_t>(Stroke::Slice)];
    slice.takeback = { 0.6f, 1.6f, -0.4f };
    slice.slot = { 0.62f, 1.15f, 0.0f };
    slice.contact = { 0.65f, 0.9f, 0.5f };
    slice.finish = { 0.05f, 0.75f, 0.85f };
    slice.faceOpen = 25.0f;
    slice.twist = 55.0f;
    slice.crouch = 0.15f;
    slice.windUp = 0.3f;
    slice.forward = 0.22f;
    slice.follow = 0.3f;
    slice.window = 0.045f;
    slice.maxError = 0.16f;
    slice.speed = 0.85f;
    slice.control = 0.85f;

    // Lob: low takeback, lift up under the ball, open face.
    auto& lob = s[static_cast<size_t>(Stroke::Lob)];
    lob.takeback = { 0.6f, 0.9f, -0.45f };
    lob.slot = { 0.6f, 0.45f, -0.05f };
    lob.contact = { 0.65f, 0.9f, 0.4f };
    lob.finish = { 0.1f, 2.0f, 0.5f };
    lob.faceOpen = 35.0f;
    lob.twist = 45.0f;
    lob.crouch = 0.2f;
    lob.windUp = 0.3f;
    lob.forward = 0.22f;
    lob.window = 0.045f;
    lob.control = 0.9f;
    lob.twoHandedBackhand = true;

    // Drop shot: a short slice that stops.
    auto& drop = s[static_cast<size_t>(Stroke::Drop)];
    drop.takeback = { 0.55f, 1.45f, -0.25f };
    drop.slot = { 0.58f, 1.05f, 0.05f };
    drop.contact = { 0.62f, 0.85f, 0.45f };
    drop.finish = { 0.3f, 0.8f, 0.5f };
    drop.faceOpen = 40.0f;
    drop.twist = 35.0f;
    drop.crouch = 0.15f;
    drop.windUp = 0.25f;
    drop.forward = 0.24f;
    drop.follow = 0.25f;
    drop.window = 0.03f;
    drop.maxError = 0.12f;
    drop.control = 1.2f;

    // Volley: no backswing, a punch in front.
    auto& volley = s[static_cast<size_t>(Stroke::Volley)];
    volley.takeback = { 0.55f, 1.35f, 0.15f };
    volley.slot = { 0.58f, 1.3f, 0.35f };
    volley.contact = { 0.6f, 1.2f, 0.65f };
    volley.finish = { 0.3f, 1.05f, 0.8f };
    volley.faceOpen = 12.0f;
    volley.twist = 25.0f;
    volley.crouch = 0.2f;
    volley.windUp = 0.12f;
    volley.forward = 0.1f;
    volley.follow = 0.15f;
    volley.window = 0.05f;
    volley.maxError = 0.12f;
    volley.speed = 0.85f;
    volley.control = 0.9f;
    volley.plane = 0.65f;

    // Half-volley: low, a block just after the bounce, from the knees.
    auto& half = s[static_cast<size_t>(Stroke::HalfVolley)];
    half.takeback = { 0.55f, 0.75f, -0.05f };
    half.slot = { 0.58f, 0.35f, 0.15f };
    half.contact = { 0.6f, 0.3f, 0.5f };
    half.finish = { 0.2f, 1.0f, 0.6f };
    half.faceOpen = 5.0f;
    half.twist = 30.0f;
    half.crouch = 0.35f;
    half.windUp = 0.12f;
    half.forward = 0.12f;
    half.follow = 0.2f;
    half.window = 0.025f;
    half.maxError = 0.1f;
    half.speed = 0.8f;
    half.control = 1.3f;
    half.plane = 0.5f;

    // Smash: the racket up behind the head, the other hand points at the
    // ball, then down through it like a serve.
    auto& smash = s[static_cast<size_t>(Stroke::Smash)];
    smash.takeback = { 0.35f, 1.9f, -0.45f };
    smash.slot = { 0.25f, 1.45f, -0.55f };
    smash.contact = { 0.3f, 2.45f, 0.4f };
    smash.finish = { -0.35f, 0.8f, 0.4f };
    smash.twist = 60.0f;
    smash.crouch = 0.1f;
    smash.windUp = 0.35f;
    smash.forward = 0.2f;
    smash.follow = 0.35f;
    smash.window = 0.04f;
    smash.speed = 1.15f;
    smash.control = 1.1f;
    smash.overhead = true;
    smash.plane = 2.45f;

    // Stretch: wide and on the run, arm out, a slice block.
    auto& stretch = s[static_cast<size_t>(Stroke::Stretch)];
    stretch.takeback = { 0.9f, 1.0f, -0.2f };
    stretch.slot = { 1.0f, 0.75f, 0.1f };
    stretch.contact = { 1.05f, 0.7f, 0.35f };
    stretch.finish = { 0.5f, 1.2f, 0.6f };
    stretch.faceOpen = 20.0f;
    stretch.twist = 30.0f;
    stretch.crouch = 0.35f;
    stretch.windUp = 0.15f;
    stretch.forward = 0.15f;
    stretch.follow = 0.25f;
    stretch.window = 0.04f;
    stretch.maxError = 0.13f;
    stretch.speed = 0.7f;
    stretch.control = 1.4f;
    stretch.plane = 0.35f;

    // Serves: the trophy position (knees bent, racket up), the racket drops
    // behind the back, the legs drive up into the ball.
    auto& serve = s[static_cast<size_t>(Stroke::ServeFlat)];
    serve.takeback = { 0.35f, 1.95f, -0.35f };
    serve.slot = { 0.3f, 1.3f, -0.55f };
    serve.contact = { 0.25f, 2.6f, 0.35f };
    serve.finish = { -0.4f, 0.8f, 0.4f };
    serve.twist = 80.0f;
    serve.crouch = 0.2f;
    serve.windUp = 0.5f;
    serve.forward = 0.25f;
    serve.follow = 0.4f;
    serve.window = 0.08f; // the serve bar's green is 1.5 of these (Play.cpp): the toss is yours, so a wider window
    serve.maxError = 0.22f;
    serve.control = 1.1f;
    serve.overhead = true;
    serve.plane = 2.65f;
    auto& serveSlice = s[static_cast<size_t>(Stroke::ServeSlice)];
    serveSlice = serve;
    serveSlice.contact = { 0.4f, 2.5f, 0.4f };
    serveSlice.finish = { -0.2f, 0.8f, 0.55f };
    serveSlice.faceOpen = 10.0f;
    serveSlice.speed = 0.9f;
    serveSlice.control = 0.9f;
    serveSlice.plane = 2.55f;
    auto& kick = s[static_cast<size_t>(Stroke::ServeKick)];
    kick = serve;
    kick.slot = { 0.2f, 1.1f, -0.6f };
    kick.contact = { 0.05f, 2.45f, 0.15f };
    kick.finish = { 0.5f, 1.0f, 0.3f }; // up and out to the right
    kick.faceOpen = 5.0f;
    kick.crouch = 0.28f;
    kick.speed = 0.8f;
    kick.control = 0.8f;
    kick.plane = 2.45f;
    return s;
}

const std::array<StrokeShape, static_cast<size_t>(Stroke::Count)>& shapes() {
    static const auto s = makeShapes();
    return s;
}

} // namespace

const char* strokeName(Stroke s) {
    switch (s) {
    case Stroke::Ready: return "ready";
    case Stroke::Toss: return "toss";
    case Stroke::Drive: return "drive";
    case Stroke::Flat: return "flat";
    case Stroke::Slice: return "slice";
    case Stroke::Lob: return "lob";
    case Stroke::Drop: return "drop shot";
    case Stroke::Volley: return "volley";
    case Stroke::HalfVolley: return "half-volley";
    case Stroke::Smash: return "smash";
    case Stroke::Stretch: return "stretch";
    case Stroke::ServeFlat: return "flat serve";
    case Stroke::ServeSlice: return "slice serve";
    case Stroke::ServeKick: return "kick serve";
    case Stroke::Count: break;
    }
    return "?";
}

const StrokeShape& shapeOf(Stroke s) {
    const size_t i = std::min(static_cast<size_t>(s), static_cast<size_t>(Stroke::Count) - 1);
    return shapes()[i];
}

bool isServe(Stroke s) { return s == Stroke::ServeFlat || s == Stroke::ServeSlice || s == Stroke::ServeKick; }

Stroke serveFor(ShotKind wanted) {
    switch (wanted) {
    case ShotKind::Flat: return Stroke::ServeFlat;
    case ShotKind::Slice: return Stroke::ServeSlice;
    default: return Stroke::ServeKick;
    }
}

StrokeChoice pickStroke(ShotKind wanted, const glm::vec3& rel, float sinceBounce, float fromNet) {
    StrokeChoice c;
    c.backhand = rel.x < -0.1f;
    const bool bounced = sinceBounce >= 0.0f;
    if (!bounced && rel.y > 2.05f) {
        c.stroke = Stroke::Smash;
        c.backhand = false; // overheads are hit on the forehand side
        return c;
    }
    if (!bounced && fromNet < 7.0f) {
        c.stroke = wanted == ShotKind::Drop ? Stroke::Drop : Stroke::Volley;
        return c;
    }
    if (bounced && sinceBounce < 0.18f && rel.y < 0.45f) {
        c.stroke = Stroke::HalfVolley;
        return c;
    }
    if (std::abs(rel.x) > 1.25f) {
        c.stroke = Stroke::Stretch;
        return c;
    }
    switch (wanted) {
    case ShotKind::Flat: c.stroke = Stroke::Flat; break;
    case ShotKind::Slice: c.stroke = Stroke::Slice; break;
    case ShotKind::Lob: c.stroke = Stroke::Lob; break;
    case ShotKind::Drop: c.stroke = Stroke::Drop; break;
    default: c.stroke = Stroke::Drive; break;
    }
    return c;
}

RacketPose racketAt(Stroke s, bool backhand, float t, const glm::vec3& contact) {
    const StrokeShape& sh = shapeOf(s);
    const glm::vec3 mirror(backhand ? -1.0f : 1.0f, 1.0f, 1.0f);
    const glm::vec3 ready = sh.ready * mirror, back = sh.takeback * mirror, slot = sh.slot * mirror, finish = sh.finish * mirror;
    RacketPose p;
    p.twoHands = backhand && sh.twoHandedBackhand;
    const float twist = sh.twist;
    const float crouch = sh.crouch;
    if (t <= -2.0f || s == Stroke::Ready) {
        p.head = ready;
    } else if (t <= -1.0f) {
        const float u = ease(t + 2.0f);
        p.head = glm::mix(ready, back, u);
        p.twist = twist * u;
        p.crouch = crouch * u;
        p.faceOpen = sh.faceOpen * 0.5f * u;
    } else if (t <= 0.0f) {
        // The racket head speeds up into the ball.
        const float u = std::clamp(t + 1.0f, 0.0f, 1.0f);
        const float k = u * u * (2.0f - u); // slow out of the takeback, fastest at contact
        p.head = through(back, slot, contact, k);
        p.twist = twist * (1.0f - k);
        p.crouch = crouch * (1.0f - 0.6f * k) - (isServe(s) || s == Stroke::Smash ? 0.1f * k : 0.0f); // serves drive up off the ground
        p.faceOpen = sh.faceOpen * (0.5f + 0.5f * k);
    } else {
        const float u = ease(std::min(t, 1.0f));
        // Carry on the way it was going, then round to the finish.
        const glm::vec3 on = contact + (contact - slot) * 0.35f;
        p.head = through(contact, on * 0.5f + 0.5f * glm::mix(contact, finish, 0.5f), finish, u);
        p.twist = -0.55f * twist * u;
        p.crouch = crouch * 0.4f * (1.0f - u) - (isServe(s) || s == Stroke::Smash ? 0.1f * (1.0f - u) : 0.0f);
        p.faceOpen = sh.faceOpen * (1.0f - u);
    }
    if (backhand) p.twist = -p.twist; // the shoulders turn the other way
    return p;
}

float timingQuality(float error, const StrokeShape& shape, float windowScale) {
    const float window = shape.window * std::max(0.2f, windowScale);
    const float maxError = std::max(window * 1.5f, shape.maxError * std::max(0.5f, windowScale));
    const float e = std::abs(error);
    if (e >= maxError) return 0.0f;
    // A gaussian that's ~0.8 at the window's edge, cut to 0 at maxError.
    const float g = std::exp(-0.22f * (e / window) * (e / window));
    const float edge = std::exp(-0.22f * (maxError / window) * (maxError / window));
    return std::clamp((g - edge) / (1.0f - edge), 0.0f, 1.0f);
}

const char* timingWord(float error, const StrokeShape& shape, float windowScale) {
    const float window = shape.window * std::max(0.2f, windowScale);
    const float e = std::abs(error);
    if (e <= window * 0.5f) return "Perfect";
    if (e <= window * 1.5f) return error < 0.0f ? "Early" : "Late";
    return error < 0.0f ? "Very early" : "Very late";
}

// ------------------------------------------------------------ Stamina

namespace {
constexpr float kJog = 3.2f;         // m/s: running this fast costs nothing
constexpr float kRunCost = 0.018f;   // per s, per m/s above a jog
constexpr float kRecover = 0.02f;    // per s, walking about in a rally
constexpr float kRecoverRest = 0.18f;// per s, between points
constexpr float kSwingCost = 0.015f, kPowerCost = 0.05f, kServeCost = 0.02f;
} // namespace

void Stamina::run(float speed, float dt) {
    if (speed > kJog) level -= kRunCost * (speed - kJog) * dt;
    level = std::clamp(level, 0.0f, 1.0f);
}

void Stamina::rest(float dt, bool betweenPoints) {
    level = std::min(1.0f, level + (betweenPoints ? kRecoverRest : kRecover) * dt);
}

void Stamina::swing(float power, bool serve) {
    level -= kSwingCost + kPowerCost * power * power + (serve ? kServeCost * power : 0.0f);
    level = std::clamp(level, 0.0f, 1.0f);
}

float Stamina::speedFactor() const { return 0.72f + 0.28f * std::sqrt(level); }
float Stamina::powerCap() const { return 0.45f + 0.55f * level; }
float Stamina::windowScale() const { return 0.65f + 0.35f * level; }
float Stamina::aimWobble() const { return 0.6f * (1.0f - level); }
float Stamina::chargeRate() const { return 0.9f + 0.9f * level; }

} // namespace tennis
