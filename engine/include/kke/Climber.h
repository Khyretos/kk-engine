#pragma once

#include "kke/ClimbWall.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace kke {

// Free climbing on a ClimbWall: two hands on holds, feet found under the
// body, a body that hangs from the hands, and stamina that runs out.
// The player picks *which* hold each hand goes to, and *how*:
//
//   bumper (LB / RB, Q / E)            precise reach: slow, cheap, only
//                                      holds within `span` of the other hand
//   trigger held, released (LT / RT,   power lunge: the longer (or harder)
//     left / right mouse)              it's held, the further the hand
//                                      flies (up to `lungeSpan`); costly,
//                                      and a loose hold breaks under it
//   trigger held + bumper              quick snatch: `span` reach, fast,
//                                      costs more than a precise reach
//
// The left stick (WASD) aims on the wall; the target each hand would go
// to is aimTarget(hand) (the game highlights it). A game can also pick a
// hold outright (the mouse crosshair, a bot) with Input::pick.
//
// Stamina drains while hanging: faster on one hand, on poor holds
// (crimps, slopers), on overhangs, and with no feet on holds; it comes
// back slowly on two good holds with feet on, and fast standing on a
// ledge (the game calls recover()). At zero the grip goes.
//
// Both hands on the same ledge's (or the summit's) edge and pushing up
// mantles over it. Pure logic, no physics: the game moves the character
// capsule to feet() (kinematic) while climbing, and hands it back to
// kke::Locomotion when state() is Fell or Topped. Unit-tested in
// tests/test_climb_wall.cpp.
class Climber {
public:
    enum class State : uint8_t { Off, Climbing, Mantle, Fell, Topped };
    enum class Move : uint8_t { None, Precise, Quick, Lunge };
    static constexpr int kLeft = 0, kRight = 1;

    struct Settings {
        float span = 1.55f;         // m: precise / quick reach, from the other hand's hold
        float lungeSpan = 2.45f;    // m: a fully charged lunge
        float reachTime = 0.42f, quickTime = 0.17f, lungeTime = 0.3f; // s
        float chargeTime = 0.75f;   // s to a full charge
        float maxStamina = 100.0f;
        float drainTwoHands = 1.8f; // per second on two jugs, vertical rock, no feet
        float drainOneHand = 6.0f;
        float overhangDrain = 0.025f; // + this fraction per degree past vertical
        float footRelief = 0.4f;    // share of the drain the feet take (both on holds)
        float shakeOut = 3.0f;      // per second back on two jugs with feet on, not overhanging
        float costPrecise = 1.5f, costQuick = 5.0f, costLungeMin = 6.0f, costLungeMax = 16.0f;
        float hang = 0.95f;         // m from the hands down to the hips
        float bodyOut = 0.34f;      // m from the rock to the hips
        float bodyFollow = 7.0f;    // 1/s: the body settling under the hands
        float mantleTime = 0.9f;    // s up and over an edge
        float hipsHeight = 0.95f;   // m from the feet to the hips (the capsule's feet = hips - this)
    };

    struct Input {
        glm::vec2 aim{0.0f};         // on the wall: x = right, y = up; length 0..1
        bool reach[2] = {};          // bumper pressed this frame
        float power[2] = {};         // trigger 0..1
        int pick[2] = { -1, -1 };    // a hold chosen outright (crosshair, bot); -1 = by aim
        bool letGo = false;
    };

    explicit Climber(const ClimbWall& wall);
    Climber(const ClimbWall& wall, const Settings& settings);

    // Grab the rock from standing (or hanging) with the feet at `feet`:
    // each hand takes the best hold within reach above. False = nothing
    // to hold on to here.
    bool start(const glm::vec3& feet);
    void update(const Input& input, float dt);
    // Standing (not climbing): stamina comes back at `perSecond`.
    void recover(float perSecond, float dt);
    void resetStamina() { m_stamina = m_s.maxStamina; }

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

    glm::vec3 hand(int h) const { return m_hand[h].pos; }
    glm::vec3 foot(int f) const { return m_foot[f].pos; }
    int handHold(int h) const { return m_hand[h].hold; }
    int footHold(int f) const { return m_foot[f].hold; }
    bool handMoving(int h) const { return m_hand[h].move != Move::None; }
    Move handMove(int h) const { return m_hand[h].move; }
    float handProgress(int h) const;
    int handTarget(int h) const { return m_hand[h].target; }
    float charge(int h) const { return m_hand[h].charge; }
    // The hold each hand would go to right now (-1 = none in reach).
    int aimTarget(int h) const { return m_aim[h]; }
    // How far hand h can reach from its pivot at the current charge.
    float reachNow(int h) const;

    float stamina() const { return m_stamina; }
    float staminaFraction() const { return m_stamina / m_s.maxStamina; }
    float drainRate() const { return m_drain; } // per second now (negative = recovering)

    // One-frame events.
    int brokeHold() const { return m_broke; }   // a loose hold came off (index)
    bool missed() const { return m_missed; }    // a hand closed on nothing
    bool grabbed() const { return m_grabbed; }  // a hand caught a hold
    bool fell() const { return m_fellNow; }

    // Holds that have come off stay off (the game drops them as bodies).
    bool holdGone(int hold) const;

    Settings& settings() { return m_s; }
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
        float charge = 0.0f;     // 0..1 while a trigger is held
        bool charging = false;
    };
    struct Foot {
        int hold = -1;           // -1 = smearing on the rock
        glm::vec3 pos{0.0f};
        glm::vec3 target{0.0f};
    };

    int findTarget(int h, const glm::vec2& aim, float reach) const;
    glm::vec3 pivot(int h) const;
    bool usable(int hold, int h) const;
    void launch(int h, Move m, int target, const glm::vec2& aim, float reach);
    void land(int h);
    void updateBody(float dt, bool fast);
    void placeFeet(bool force);
    void updateStamina(float dt);
    void fall();
    void tryMantle(const Input& in);

    const ClimbWall& m_wall;
    Settings m_s;
    State m_state = State::Off;
    Hand m_hand[2];
    Foot m_foot[2];
    int m_aim[2] = { -1, -1 };
    glm::vec3 m_hips{0.0f}, m_facing{0.0f, 0.0f, -1.0f};
    glm::vec3 m_feetAnchor{0.0f}; // hips when the feet were last placed
    float m_stamina = 100.0f, m_drain = 0.0f;
    glm::vec3 m_mantleFrom{0.0f}, m_mantleFeet{0.0f};
    int m_mantleLedge = -1;
    float m_mantleT = 0.0f;
    std::vector<int> m_gone;
    int m_broke = -1;
    bool m_missed = false, m_grabbed = false, m_fellNow = false;
};

// A climber that plays itself: follows ClimbWall::route() hand over hand,
// quick snatches while it has stamina to spare, precise reaches when
// tired, and mantles at the top. The race's rival and the headless
// checks use it.
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
    const std::vector<int>& route() const { return m_route; }

private:
    int routeIndex(int hold) const;
    std::vector<int> m_route;
    float m_wait = 0.0f;
};

} // namespace kke
