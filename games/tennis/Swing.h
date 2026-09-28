#pragma once

// The strokes (pure maths, unit-tested in tests/test_tennis.cpp): every
// swing a player makes, what shape it has, how long each part takes, how
// the timing of the release turns into a good or a bad hit, and the
// player's stamina. Body.cpp draws a stroke from its shape (the racket
// head's path, the shoulders' turn, the knees); Play.cpp picks the stroke
// and times it.
//
//   A swing has three parts on one clock, t:
//     -2 .. -1  the takeback (the shot button held: the longer, the more power)
//     -1 ..  0  the forward swing (the button let go), 0 is contact
//      0 ..  1  the follow-through
//   t below -2 is the ready position.

#include "Shot.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace tennis {

enum class Stroke : uint8_t {
    Ready,      // between shots, racket in front
    Toss,       // serving: the ball goes up (the other arm)
    Drive,      // topspin groundstroke: low to high, over the shoulder
    Flat,       // flat drive: through the ball, less margin, more pace
    Slice,      // high to low, open face, the ball floats and stays low
    Lob,        // open face, low to high: up over someone at the net
    Drop,       // soft slice that dies just over the net
    Volley,     // before the bounce, near the net: a short punch
    HalfVolley, // just after the bounce, low: a block from the knees
    Smash,      // an overhead on a high ball, like a serve
    Stretch,    // wide and late, on the run: a defensive reach
    ServeFlat,  // straight through the ball: the fastest
    ServeSlice, // across the ball: it swings wide
    ServeKick,  // up the back of the ball: it jumps high
    Count
};
const char* strokeName(Stroke s);

constexpr float kSwingIdle = -3.0f; // t of a player not swinging

struct StrokeShape {
    // The racket head's centre, body frame (x right, y up, z forward), on
    // the forehand (a backhand is the mirror image, x -> -x).
    glm::vec3 ready{0.12f, 1.35f, 0.5f};
    glm::vec3 takeback{0.0f}; // the end of the backswing
    glm::vec3 slot{0.0f};     // halfway forward (below the ball for topspin, above for slice)
    glm::vec3 contact{0.0f};  // a default contact (the real one is where the ball is)
    glm::vec3 finish{0.0f};   // the end of the follow-through
    float faceOpen = 0.0f;    // degrees at contact: + open (slice, lob), - closed (topspin)
    float twist = 0.0f;       // degrees the shoulders turn away at the takeback
    float crouch = 0.0f;      // m the hips drop, loaded at the takeback
    float windUp = 0.3f;      // s from ready to a full takeback
    float forward = 0.2f;     // s from the takeback to contact
    float follow = 0.3f;      // s from contact to the finish
    float window = 0.035f;    // s early or late: still a clean hit (quality ~0.8)
    float maxError = 0.14f;   // s early or late past which it's a miss
    float speed = 1.0f;       // on the shot's speed
    float control = 1.0f;     // on the aim's wobble (below 1: steadier)
    bool twoHandedBackhand = false;
    bool overhead = false;    // contact up high: timed by the ball's height, not its distance in front
    float plane = 0.45f;      // m: in front of the body where the racket meets the ball (overhead: the height)
};
const StrokeShape& shapeOf(Stroke s);

bool isServe(Stroke s);

// Which stroke a player plays: from the shot they asked for and where the
// ball is. rel: the ball in the body frame (x right, y up, z forward);
// sinceBounce: s since it bounced (< 0: it hasn't); fromNet: m from the
// net to the player's feet.
struct StrokeChoice {
    Stroke stroke = Stroke::Drive;
    bool backhand = false;
};
StrokeChoice pickStroke(ShotKind wanted, const glm::vec3& rel, float sinceBounce, float fromNet);
// The serve that fits the shot button (Flat, Slice, the rest kick).
Stroke serveFor(ShotKind wanted);

// Where the racket and the body are at swing time t, for a ball met at
// `contact` (body frame). Body.cpp turns this into bones.
struct RacketPose {
    glm::vec3 head{0.0f};   // body frame
    float faceOpen = 0.0f;  // degrees
    float twist = 0.0f;     // degrees the shoulders are turned away (+) or through (-)
    float crouch = 0.0f;    // m
    bool twoHands = false;  // the other hand on the grip too
};
RacketPose racketAt(Stroke s, bool backhand, float t, const glm::vec3& contact);

// How good a hit is for its timing: error in s (+ late, - early).
// 1 dead on, ~0.8 at the edge of the window, 0 at maxError (a miss).
// `windowScale`: stamina and the chosen difficulty narrow or widen it.
float timingQuality(float error, const StrokeShape& shape, float windowScale = 1.0f);
// "Perfect", "Early", "Late", "Very early", "Very late".
const char* timingWord(float error, const StrokeShape& shape, float windowScale = 1.0f);

// The player's legs and arm: running flat out and hitting hard wear them
// down; walking and the time between points bring them back.
struct Stamina {
    float level = 1.0f; // 0..1
    // m/s the player runs: above a jog it costs, more the faster.
    void run(float speed, float dt);
    void rest(float dt, bool betweenPoints);
    void swing(float power, bool serve);
    // What it does to them.
    float speedFactor() const;   // on top running speed
    float powerCap() const;      // the most power a takeback can build
    float windowScale() const;   // on the timing window (tired: narrower)
    float aimWobble() const;     // m added to the aim's wobble
    float chargeRate() const;    // power per second of takeback
};

} // namespace tennis
