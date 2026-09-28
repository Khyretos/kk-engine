#pragma once

#include "Shot.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <random>

// The CPU player (pure logic, no engine: tests/test_tennis.cpp checks its
// footwork). Each step it's told where the ball is heading and
// answers where to stand and whether to swing now, with what shot, aimed
// where. Levels differ in footspeed, how late they react, how straight
// they aim and how often they go for a winner.
namespace tennis {

struct BotSkill {
    float speed = 5.0f;        // m/s at a run
    float react = 0.25f;       // s after the other side's hit before moving
    float aimError = 1.2f;     // m of wobble on the target
    float power = 0.5f;        // 0..1 how hard it hits
    float risk = 0.3f;         // 0..1 how close to the lines it aims
    static BotSkill forLevel(int level); // 0 Easy, 1 Normal, 2 Hard, 3 Expert
};

class Bot {
public:
    Bot(uint32_t seed, int level);
    const BotSkill& skill() const { return m_skill; }

    struct View {
        Flight ball;               // where it is and where it's going (court space)
        bool ballInPlay = false;
        bool mayHit = false;       // the rules let this player hit it now
        bool bounced = false;      // it has bounced once since the last hit
        bool myTurn = true;        // doubles: this player (not the partner) goes for it
        int side = 1;              // my half: +1 or -1
        glm::vec3 feet{0.0f};
        glm::vec3 opponent{0.0f};  // where the nearest opponent stands
        bool atNet = false;        // doubles: the server's partner holds the net
        BounceModel bounce;
    };
    struct Decision {
        glm::vec3 moveTo{0.0f};
        float urgency = 1.0f;      // 0..1 of top speed
        bool swing = false;
        ShotKind kind = ShotKind::Topspin;
        glm::vec2 aim{0.0f};       // x: -1 left .. 1 right as the bot sees it; y: -1 short .. 1 deep
        float charge = 0.5f;
    };
    // The other side just hit (start the reaction clock).
    void onOpponentHit() { m_sinceHit = 0.0f; }
    Decision think(const View& v, float dt);

    // Where a player of half `side` should be to take a ball arriving at
    // `contact` on the forehand (the ball ~0.7 m to the right).
    static glm::vec3 standFor(const glm::vec3& contact, int side);

private:
    BotSkill m_skill;
    std::mt19937 m_rng;
    float m_sinceHit = 10.0f;
    bool m_planned = false;
    ShotKind m_kind = ShotKind::Topspin;
    glm::vec2 m_aim{0.0f};
};

} // namespace tennis
