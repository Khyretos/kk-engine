#pragma once

#include "kke/ClimbWall.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>
#include <utility>

namespace kke {

// Free climbing on a ClimbWall: two hands and two feet on holds, a body
// that hangs between them, and stamina that runs out. The player picks
// *which* hold each limb goes to, and when:
//
//   trigger (LT / RT, Q / E,          a hand reaches for the hold it's
//     left / right mouse)             aimed at (aimTarget), within `span`
//                                     of the other hand
//   bumper (LB / RB, Z / X)           a foot steps onto the foothold
//                                     shown for it (footTarget)
//   jump held, let go (A, Space)      a lunge: the longer it's held, the
//                                     further the whole body jumps in the
//                                     aimed direction. Both hands and feet
//                                     leave the rock; the player must
//                                     press a hand's trigger while a hold
//                                     is in reach to catch it, or fall.
//                                     A tap does nothing (no floating).
//   jump with both hands on a lip     over the top (a mantle)
//
// The left stick (WASD) aims on the wall; the target each hand would go
// to is aimTarget(hand) and each foot's footTarget(foot) (the game
// highlights them). A game can also pick a hold outright (the mouse
// crosshair, a bot) with Input::pick.
//
// Stamina: with both feet planted on footholds and both hands on holds
// it comes back, slowly but surely (a little faster with both hands on
// one hold, a comfortable rest). One foot on takes some of the weight;
// with no feet on holds every move and every second costs double. It
// drains faster on one hand, on poor holds (crimps, slopers) and on
// overhangs, and comes back fast standing on a ledge (the game calls
// recover()). At zero the grip goes.
//
// Each hand works its own side of the body (Settings::crossReach), each
// foot too; the body is kept off the rock and out of ledges (no knees,
// head or legs through stone). Pure logic, no physics: the game moves
// the character capsule to feet() (kinematic) while climbing, and hands
// it back to kke::Locomotion when state() is Fell or Topped. Unit-tested
// in tests/test_climb_wall.cpp.
class Climber {
public:
    enum class State : uint8_t { Off, Climbing, Mantle, Fell, Topped };
    // How a hand is moving: a reach (trigger), or a catch out of a lunge.
    enum class Move : uint8_t { None, Precise, Catch };
    static constexpr int kLeft = 0, kRight = 1;

    struct Settings {
        float span = 1.55f;         // m: a hand's reach, from the other hand's hold
        float reachTime = 0.42f;    // s: a hand to its hold
        float quickTime = 0.17f;    // s: a short move (matching, the first grab)
        float maxStamina = 100.0f;
        float drainTwoHands = 1.5f; // per second on two jugs, vertical rock, feet off
        float drainOneHand = 6.0f;
        float overhangDrain = 0.02f; // + this fraction per degree past vertical
        float costPrecise = 1.5f;   // a hand's reach
        // No feet on holds: every move and every second costs this many
        // times as much (climbing on the arms alone).
        float handsOnly = 2.0f;
        // Both feet planted, both hands on holds: stamina back per second
        // (times the holds' grip; slow but steady), this much more with
        // both hands on one hold, half under a steep overhang.
        float feetRecover = 1.5f;
        float matchRecover = 1.35f;
        float footRelief = 0.4f;    // one foot planted: this share of the drain off
        // Feet.
        float stepTime = 0.25f;     // s: a foot onto its hold
        float costStep = 0.4f;
        // The lunge: jump held for chargeTime is a full one. The hips go
        // dynoMin..dynoMax metres in the aimed direction over dynoRise
        // seconds (slowing to the dead point), then drop. A hand catches a
        // hold within catchReach of its shoulder; nothing caught within
        // catchWindow of the dead point is a fall. Below minCharge, a
        // release does nothing (a tap isn't a jump).
        float chargeTime = 0.75f;
        float minCharge = 0.15f;
        float dynoMin = 0.35f, dynoMax = 1.35f;
        float dynoRise = 0.3f;
        float catchWindow = 0.4f;
        float catchReach = 0.76f;
        float costDynoMin = 6.0f, costDynoMax = 16.0f;
        float hang = 0.95f;         // m from the hands down to the hips
        float bodyOut = 0.34f;      // m from the rock to the hips, hanging at ease
        float bodyIn = 0.16f;       // m: the closest the hips come to the rock, arms stretched
        float bodyFollow = 7.0f;    // 1/s: the body settling under the hands
        float mantleTime = 0.9f;    // s up and over an edge
        float hipsHeight = 0.95f;   // m from the feet to the hips (the capsule's feet = hips - this)
        // The body's proportions: the hips never hang so far from a hold
        // that the arm on it can't reach (and a game's IK puts each hand
        // exactly on its hold). A game with a character sets these from
        // its skeleton; the defaults are a 1.8 m person.
        // m from the shoulder to the wrist, arm (nearly) straight, plus the
        // few centimetres the shoulder itself lifts toward a far hold.
        float armReach = 0.64f;
        // The hand on a hold: the knuckles at the hold, the fingers up and
        // tipped over it into the rock, so the wrist is below and out.
        float handLength = 0.1f;    // m from the wrist to the knuckles
        float fingerTilt = 0.45f;   // how far the fingers tip into the rock (0 = straight up)
        float knuckleOut = 0.015f;  // m the knuckles sit out from the hold's point
        float handWidth = 0.09f;    // m across the knuckles: two hands on one hold sit side by side
        float shoulderUp = 0.5f;    // m from the hips up to the shoulders
        float shoulderHalf = 0.18f; // m from the spine out to each shoulder
        float headUp = 0.82f;       // m from the hips to the top of the head
        float bodyDepth = 0.14f;    // m from the spine to the front (or back) of the body
        float legReach = 0.9f;      // m from the hip joint to a foothold
        float hipHalf = 0.1f;       // m from the spine out to each hip joint
        // A hand whose hold ends up this far past the arm's reach lets go:
        // a cut loose.
        float cutLoose = 0.08f;
        float pullSpeed = 4.0f;     // m/s: the body pulled up to a hand that caught a hold out of reach
        // Each hand works its own half of the body. Between the shoulders
        // is the middle, where either hand goes; a hand crosses in front of
        // the chest to at most this far past the other shoulder, and only
        // if the arm is long enough to go round the front of it. Further
        // over is the other hand's: no arm reaches behind the back or over
        // the other shoulder.
        float crossReach = 0.3f;
    };

    struct Input {
        glm::vec2 aim{0.0f};         // on the wall: x = right, y = up; length 0..1
        bool reach[2] = {};          // a hand's trigger pressed this frame (reach, or catch in a lunge)
        bool step[2] = {};           // a foot's bumper pressed this frame
        bool jump = false;           // held: charging a lunge; let go: the lunge
        int pick[2] = { -1, -1 };    // a hold chosen outright for a hand (crosshair, bot); -1 = by aim
        int footPick[2] = { -1, -1 }; // the same for a foot
        bool letGo = false;
    };

    explicit Climber(const ClimbWall& wall);
    Climber(const ClimbWall& wall, const Settings& settings);

    // Grab the rock from standing (or hanging) with the feet at `feet`:
    // each hand takes the best hold within reach above, each foot the best
    // foothold under the body. False = nothing to hold on to here.
    bool start(const glm::vec3& feet);
    void update(const Input& input, float dt);
    // Standing (not climbing): stamina comes back at `perSecond`.
    void recover(float perSecond, float dt);
    void resetStamina() { m_stamina = m_s.maxStamina; }
    // Something hit the climber (a falling rock): `cost` stamina at once.
    // At zero they fall on the next update, as when they tire out.
    void knock(float cost);

    State state() const { return m_state; }
    bool climbing() const { return m_state == State::Climbing || m_state == State::Mantle; }
    glm::vec3 hips() const { return m_hips; }
    glm::vec3 feet() const { return m_hips - glm::vec3(0.0f, m_s.hipsHeight, 0.0f); }
    // Horizontal unit vector into the rock (where the body faces).
    glm::vec3 facing() const { return m_facing; }
    // Where the mantle ends (feet), while mantling and once Topped.
    glm::vec3 mantleFeet() const { return m_mantleFeet; }
    int mantleLedge() const { return m_mantleLedge; } // -1 = the summit
    float mantleProgress() const;

    // Where the wrist is with the hand closed on a point on the rock with
    // normal n (the game's IK puts the wrist there), and the way the
    // fingers point.
    glm::vec3 wristAt(const glm::vec3& grip, const glm::vec3& n) const;
    glm::vec3 fingerDirection(const glm::vec3& n) const;
    // Where each shoulder is (the arm's root; reach is measured from it).
    glm::vec3 shoulder(int h) const { return shoulderAt(h, m_hips); }

    glm::vec3 hand(int h) const { return m_hand[h].pos; }
    // Where hand h's knuckles close on its hold: the hold's point, moved
    // along a ledge's lip to where the hand took it, and beside the other
    // hand when both share a hold.
    glm::vec3 grip(int h) const;
    glm::vec3 foot(int f) const { return m_foot[f].pos; }
    int handHold(int h) const { return m_hand[h].hold; }
    int footHold(int f) const { return m_foot[f].hold; }
    bool footMoving(int f) const { return m_foot[f].t < m_s.stepTime; }
    int feetPlanted() const { return (m_foot[0].hold >= 0 ? 1 : 0) + (m_foot[1].hold >= 0 ? 1 : 0); }
    bool handMoving(int h) const { return m_hand[h].move != Move::None; }
    Move handMove(int h) const { return m_hand[h].move; }
    float handProgress(int h) const;
    int handTarget(int h) const { return m_hand[h].target; }
    // The lunge: charging (jump held), how far (0..1), in the air, and
    // where the hips will be at the top of it.
    bool charging() const { return m_dyno.charging; }
    float charge() const { return m_dyno.charge; }
    bool flying() const { return m_dyno.flying; }
    glm::vec3 lungeApex() const;
    // Where the hips would be at the top of a lunge aimed `aim` at `charge`
    // (0..1) from where they are now (a bot plans with it).
    glm::vec3 lungeApexFor(const glm::vec2& aim, float charge) const;
    // The hold each hand would go to right now (-1 = none in reach). While
    // a lunge charges: what it could catch at the top; in the air: what
    // the trigger catches now.
    int aimTarget(int h) const { return m_aim[h]; }
    // The foothold each foot would step onto (-1 = none in reach).
    int footTarget(int f) const { return m_footAim[f]; }
    // Could the body hang with hand h on `hold` and the other hand where it
    // is? (The arms reach both, with the body between them.) A reach only
    // goes to such holds.
    bool canSpan(int h, int hold) const;
    // The same with the other hand on `otherHold` (planning a move ahead).
    bool canHang(int h, int hold, int otherHold) const;
    // Can hand h take `point` with the body's middle at `hips`: on its own
    // side, in the middle, or no further than Settings::crossReach past
    // the other shoulder?
    bool onItsSide(int h, const glm::vec3& point, const glm::vec3& hips) const;
    // Would hand h have to reach over past the other hand's shoulder to
    // take `hold` (the other hand on its hold)? Such a hold is the other
    // hand's: this one can match the other hand's hold first, then the
    // other hand goes.
    bool crossesOver(int h, int hold) const;
    // How far a hand reaches from the other hand's hold.
    float reachNow(int) const { return m_s.span; }
    // The hold hand h would catch with the hips at `hips` (-1 = none): the
    // lunge's catch, and what it could catch at its top.
    int catchTarget(int h, const glm::vec3& hips) const;

    float stamina() const { return m_stamina; }
    float staminaFraction() const { return m_stamina / m_s.maxStamina; }
    float drainRate() const { return m_drain; } // per second now (negative = recovering)

    // One-frame events.
    int brokeHold() const { return m_broke; }   // a loose hold came off (index)
    bool missed() const { return m_missed; }    // a hand closed on nothing
    bool grabbed() const { return m_grabbed; }  // a hand caught a hold
    bool fell() const { return m_fellNow; }
    bool lunged() const { return m_lunged; }    // a lunge left the rock
    int cutLoose() const { return m_cut; }      // a hand's hold ended out of reach: it let go (hand, -1 = none)
    int slipped() const { return m_slipped; }   // a foot's hold ended out of the leg's reach: it came off (foot, -1 = none)

    // Holds that have come off stay off (the game drops them as bodies).
    bool holdGone(int hold) const;

    Settings& settings() { return m_s; }
    const Settings& settings() const { return m_s; }
    const ClimbWall& wall() const { return m_wall; }

private:
    struct Hand {
        int hold = -1;           // holding (and not moving)
        glm::vec3 pos{0.0f};
        Move move = Move::None;
        glm::vec3 from{0.0f};
        int target = -1;         // -1 while moving = a throw at nothing
        glm::vec3 to{0.0f};
        float t = 0.0f, duration = 0.0f;
        float slide = 0.0f;      // on a lip: where along it (x) the hand took it
        float share = 0.0f;      // 0..1: moved aside for the other hand on the same hold
        float retry = 0.0f;      // s before a missed catch can try again
    };
    struct Foot {
        int hold = -1;           // -1 = off (hanging free)
        int next = -1;           // stepping onto this hold
        glm::vec3 pos{0.0f};
        glm::vec3 from{0.0f}, target{0.0f};
        float t = 1e9f;          // s into a step (>= stepTime: arrived)
    };
    struct Dyno {
        bool charging = false, flying = false;
        float charge = 0.0f;
        glm::vec2 aim{0.0f, 1.0f}; // the aim while charging, and the jump's direction
        glm::vec3 from{0.0f}, apex{0.0f};
        float t = 0.0f, rise = 0.3f;
    };

    int findTarget(int h, const glm::vec2& aim, float reach) const;
    int findFoothold(int f, const glm::vec2& aim) const;
    glm::vec3 pivot(int h) const;
    bool usable(int hold, int h) const;
    // Where hand h would take `hold` (a lip: along it, under the shoulder).
    glm::vec3 pointOn(int h, int hold, const glm::vec3& hips) const;
    // Where hand h's knuckles are on `hold`: along a lip at `slide`, moved
    // aside by `share` of half a hand for another hand on the same hold.
    glm::vec3 gripAt(int h, int hold, float slide, float share) const;
    bool onOneLip() const;
    void launch(int h, int target, const glm::vec2& aim, float reach);
    void land(int h);
    void updateBody(float dt);
    void updateFeet(const Input& in, float dt);
    void updateLunge(const Input& in, float dt);
    glm::vec3 shoulderAt(int h, const glm::vec3& hips) const;
    // How much arm it takes from hand h's shoulder to `wrist` (round the
    // front of the chest when the wrist is past the other shoulder).
    float armPath(int h, const glm::vec3& wrist, const glm::vec3& hips) const;
    // Moves `hips` as little as it can so every hand on the rock (and, with
    // `reaching`, every hand on its way to it) is within reach of its
    // shoulder, and stays off the rock.
    void fitArms(glm::vec3& hips, bool reaching) const;
    void fitWrists(glm::vec3& hips, const glm::vec3 wrist[2], const bool use[2]) const;
    // The whole body (feet to head) out of the rock and out of ledges.
    void keepOffRock(glm::vec3& hips) const;
    glm::vec3 hipJointAt(int f, const glm::vec3& hips) const;
    glm::vec3 hipJoint(int f) const { return hipJointAt(f, m_hips); }
    glm::vec3 hangingFoot(int f) const;
    void updateStamina(float dt);
    float costFactor() const { return feetPlanted() == 0 ? m_s.handsOnly : 1.0f; }
    void fall();
    void startMantle();

    const ClimbWall& m_wall;
    Settings m_s;
    State m_state = State::Off;
    Hand m_hand[2];
    Foot m_foot[2];
    Dyno m_dyno;
    int m_aim[2] = { -1, -1 };
    int m_footAim[2] = { -1, -1 };
    bool m_jumpWas = false;
    glm::vec3 m_hips{0.0f}, m_facing{0.0f, 0.0f, -1.0f};
    float m_stamina = 100.0f, m_drain = 0.0f;
    float m_pull = 0.0f; // s left of a catch pulling the body up
    glm::vec3 m_mantleFrom{0.0f}, m_mantleFeet{0.0f};
    int m_mantleLedge = -1;
    float m_mantleT = 0.0f;
    std::vector<int> m_gone;
    int m_broke = -1, m_cut = -1, m_slipped = -1;
    bool m_missed = false, m_grabbed = false, m_fellNow = false, m_lunged = false;
};

// A climber that plays itself: follows ClimbWall::route() hand over hand,
// keeps its feet on footholds, lunges past holds while it has stamina to
// spare (and catches), rests when tired, and mantles at the top. The
// race's rival and the headless checks use it.
class ClimbBot {
public:
    explicit ClimbBot(std::vector<int> route) : m_route(std::move(route)) {}
    Climber::Input think(const Climber& c, float dt);
    // Tired below this fraction: head for a ledge or a pair of jugs.
    float restBelow = 0.45f;
    // Passing a ledge below this fraction: stand on it and rest.
    float ledgeRestBelow = 0.95f;
    // Resting (a ledge, two jugs): go again once rested to this fraction.
    float restUntil = 0.9f;
    // Seconds between moves when fresh (twice that when spent).
    float pause = 0.3f;
    // Fresh, it lunges past holds it could reach one by one.
    bool lunges = true;
    const std::vector<int>& route() const { return m_route; }

private:
    int routeIndex(int hold) const;
    std::vector<int> m_route;
    float m_wait = 0.0f;
    int m_lungePick = -1;                   // a lunge being charged, or in the air: the hold to catch
    int m_lungeHand = -1;                   // and the hand that catches it
    float m_lungeNeed = 0.0f;               // the charge it needs
    glm::vec2 m_lungeAim{0.0f, 1.0f};       // and the way it goes
    int m_came[2] = { -1, -1 };             // the hold each hand last left
    std::vector<std::pair<int, int>> m_path; // a way round being followed: (hand, hold) moves
    Climber::Input decide(const Climber& c, float dt);
    bool feetFirst(const Climber& c, Climber::Input& in) const;
    std::vector<std::pair<int, int>> findWay(const Climber& c, int at, int last, float reach) const;
};

} // namespace kke
