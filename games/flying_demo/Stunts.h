#pragma once

// Stunt scoring for the Flying demo's Stunts mode (README.md "Stunts"):
// watches a plane's attitude from frame to frame and names what it did.
// Pure maths (tests/test_flight.cpp flies loops and rolls through it).
//
//   Loop            the nose all the way round (pitch 330+ degrees in 10 s)   500
//   Outside loop    the same, pushing (much harder on the pilot)              800
//   Roll            the wings all the way round (330+ degrees in 4 s)         200
//   Inverted        flying upside down, 2 s or more                  60 a second
//   Knife edge      wings vertical and holding height, 2 s or more   80 a second
//   Low pass        under 15 m over the ground at speed, 1 s or more  150 + 100 a second
// Tricks chained within 3 s of each other multiply: x1.5, x2, x2.5 ...

#include "Flight.h"

#include <string>

namespace flying {

class StuntTracker {
public:
    struct Trick {
        std::string name;   // "" = nothing new this step
        int points = 0;     // with the chain multiplier
        int chain = 1;
    };
    // One step: the plane after it moved (`s`), and how high it is over
    // the ground under it.
    Trick update(const PlaneState& s, float clearance, float dt);
    void crashed();         // -300, and the chain breaks
    void reset();
    int score() const { return m_score; }
    int chain() const { return m_chain; }

private:
    Trick award(const std::string& name, int points);
    glm::quat m_last{1.0f, 0.0f, 0.0f, 0.0f};
    bool m_started = false;
    float m_pitch = 0.0f, m_pitchTime = 0.0f; // degrees turned about the wings, and for how long
    float m_roll = 0.0f, m_rollTime = 0.0f;
    float m_inverted = 0.0f, m_knife = 0.0f, m_low = 0.0f; // s held
    float m_sinceTrick = 99.0f;
    int m_chain = 0;
    int m_score = 0;
};

} // namespace flying
