#pragma once

#include <glm/glm.hpp>

// Where a ball goes (pure maths, unit-tested in tests/test_tennis.cpp):
// the flight between bounces, the bounce, and the shot that puts a ball
// from the racket on a spot across the net. The ball in the game is a
// FEMFX rubber ball (Ball.h); this is how players, the CPU and the
// network agree on where it is heading. Court space (Court.h).
namespace tennis {

constexpr float kGravity = 9.81f;
// Air drag, 1/s: the ball loses this share of its speed each second. Real
// drag grows with the square of the speed (a 25 m/s drive loses about a
// third by the far baseline, a slow lob little); linear drag is close over
// a rally's speeds and keeps every sum below exact, so the CPU, the
// network and the ball all agree to the millimetre.
constexpr float kDrag = 0.4f;

// A ball in the air: gravity plus a steady extra pull down (topspin
// dips, slice floats: `gravity` is the sum), slowed by the air.
struct Flight {
    glm::vec3 pos{0.0f}, vel{0.0f};
    float gravity = kGravity;
    float drag = kDrag;
    glm::vec3 at(float t) const;
    glm::vec3 velocityAt(float t) const;
    // When its centre comes down to `height` (after the top of the arc);
    // -1 if it never does (it's rising past the top of the world).
    float timeDownTo(float height) const;
    // When it crosses the plane z = `z` going the way it goes; -1 if never.
    float timeAtZ(float z) const;
    float timeAtNet() const { return timeAtZ(0.0f); }
};

// How the ball comes off the court (measured from the FEMFX ball: Ball.cpp
// logs it with KKE_TENNIS_BALLTEST=1).
// How much of a spin's pull is left after the court took its bite.
constexpr float kPullAfterBounce = 0.4f;

struct BounceModel {
    float restitution = 0.75f;   // vertical speed kept (ITF: a 2.54 m drop comes back to 1.35-1.47 m)
    float keepAlong = 0.62f;     // horizontal speed kept (friction takes the rest; a medium-paced hard court)
};
// The flight after the ball lands at the end of `f` (time t, where its
// centre is kBallRadius above the court).
Flight bounce(const Flight& f, float t, const BounceModel& m, float gravityAfter = kGravity);

enum class ShotKind { Flat, Topspin, Slice, Lob, Drop, Serve };
// The extra downward pull a spin gives, m/s^2 on top of gravity.
float spinPull(ShotKind kind);

struct ShotPlan {
    glm::vec3 velocity{0.0f};
    float time = 0.0f;     // flight time to the target
    float gravity = kGravity;
    bool clearsNet = true; // false: even the highest arc tried hits the net
};
// The launch velocity that carries a ball from `from` to land at `target`
// (on the court: y is ignored, the ball lands with its centre at
// kBallRadius) at about `speed` m/s across the ground, clearing the net
// by `netMargin` metres. Too flat to clear the net: the arc rises (and the
// shot slows) until it does.
ShotPlan planShot(const glm::vec3& from, const glm::vec3& target, float speed, ShotKind kind, float netMargin = 0.25f);

// Where a ball on this flight first comes down to `height` after its
// next bounce, and when (a player's contact point: waist high after the
// bounce). Returns false when it doesn't happen within `horizon` seconds.
bool afterBounceAt(const Flight& f, float height, const BounceModel& m, glm::vec3& where, float& when, float horizon = 4.0f);

// Where a player on `side` meets a ball coming at them, after its bounce:
// on the way down about waist high; a deep, high bouncer is taken earlier
// (on the rise, or at the top) rather than chased to the fence. False
// when it doesn't bounce on that side within `horizon` seconds.
bool meetPoint(const Flight& f, int side, const BounceModel& m, glm::vec3& where, float& when, float horizon = 4.0f);
// The same on a ball that has already bounced: where to take it on the arc
// it's on now.
void meetOnArc(const Flight& up, glm::vec3& where, float& when, float horizon = 4.0f);

} // namespace tennis
