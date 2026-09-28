#pragma once

#include "Court.h"
#include "Shot.h"

#include <cstdint>
#include <string>
#include <vector>

namespace kke {
class PhysicsModule;
struct BridgeBox;
} // namespace kke

namespace tennis {

// One court's ball, in two parts:
//  - the flight: where the ball is and goes, worked out here each step
//    (gravity, the spin's pull, the bounce, the net, the fence, the roof).
//    Every rule, the CPU and the network read this one; it is the same on
//    every machine that saw the same shots.
//  - the body: a FEMFX rubber ball (the physics demo's, tennis ball
//    sized) steered along the flight. It flattens on the strings and on
//    the court and wobbles back, which is what you see.
// Why not let FEMFX fly it: its implicit solver is made for soft, heavy
// things. On a 57 g ball it loses most of gravity and nearly all of the
// bounce (KKE_TENNIS_BALLTEST=1 measured it: a 2 m drop hit the court at
// 1.2 m/s instead of 6.2 and didn't come back up), and a 30 m/s ball
// passed through a thin wall between two steps. Real tennis needs the
// numbers right, so the flight is ours and FEMFX does the squash.
// Everything in court space (Court.h).
class Ball {
public:
    struct Event {
        enum class Kind { Bounce, Net, Out } kind = Kind::Bounce;
        glm::vec3 at{0.0f}; // court space
    };

    Ball(kke::PhysicsModule& physics, const CourtPlace& place, const std::string& texturePath);
    ~Ball();
    Ball(const Ball&) = delete;
    Ball& operator=(const Ball&) = delete;
    bool valid() const { return m_handle != 0; }

    // Put it somewhere at rest shape (a serve's toss, a new point).
    void place(const glm::vec3& local, const glm::vec3& velocity = glm::vec3(0.0f));
    // A racket's hit: its new velocity, spin (rad/s about an axis, court
    // space), the spin's extra pull down while it flies (Shot.h
    // spinPull), and how hard the strings squash it (0..1, the look).
    void strike(const glm::vec3& velocity, const glm::vec3& spin, float pull, float squash);
    // The same, from where the hitter's machine met it (online).
    void strikeAt(const glm::vec3& at, const glm::vec3& velocity, const glm::vec3& spin, float pull, float squash) {
        m_pos = at;
        strike(velocity, spin, pull, squash);
    }
    // Call once per fixed step after the physics stepped: moves the
    // flight, notes what it touched, steers the body along.
    void step(float dt);

    glm::vec3 position() const { return m_pos; }   // centre, court space
    glm::vec3 velocity() const { return m_vel; }
    glm::vec3 worldPosition() const { return m_place.toWorld(m_pos); }
    float pull() const { return m_pull; }
    bool rolling() const { return m_rolling; }
    Flight flight() const { return { m_pos, m_vel, kGravity + m_pull }; }
    std::vector<Event> takeEvents();

    // A network follower (a client's copy): take the host's flight when
    // it differs from ours (a shot, a correction).
    void follow(const Flight& target, bool rolling);

    // The fence, the roof and the net, as FEMFX boxes (append to the list
    // PhysicsModule::setExternalBoxes gets). `key` numbers them.
    static void courtBoxes(const CourtPlace& place, uint64_t keyBase, std::vector<kke::BridgeBox>& out);

    const BounceModel& bounceModel() const { return m_bounce; }
    // Where the FEMFX body is (its centre, court space): the camera and
    // the test log it, the rules don't.
    glm::vec3 bodyPosition() const;

private:
    void steerBody(float dt);

    kke::PhysicsModule& m_physics;
    CourtPlace m_place;
    uint32_t m_handle = 0;
    glm::vec3 m_pos{0.0f}, m_vel{0.0f};
    float m_pull = 0.0f;
    bool m_rolling = false;
    std::vector<Event> m_events;
    BounceModel m_bounce;
};

} // namespace tennis
