#pragma once

#include "DogBody.h"

#include "kke/modules/ModelModule.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace command_kit {
class Scenery;
}

namespace pet_companion {

// Dog agility (README.md, "Agility"): a course of numbered obstacles in
// the field next to the garden. You run it with your dog: point at the
// next obstacle (the context order) or run up to it and the dog takes it;
// the clock starts at the first and stops at the last; a wrong obstacle
// is a fault (5 seconds). The obstacles are POLYGON Dogs' (bar jump, tyre,
// weave poles, A-frame, seesaw); without the pack, jumps and poles are
// blocks.
//
// Taking an obstacle is a scripted move along it (the dog's Jolt body is
// kinematic for that moment): over a bar or through a tyre in an arc,
// weaving between the poles, up and down the ramp along its own surface.
struct Obstacle {
    enum class Kind { Jump, Tyre, Weave, Ramp, SeeSaw };
    Kind kind = Kind::Jump;
    glm::vec3 centre{0.0f};
    glm::vec3 dir{0.0f, 0.0f, 1.0f}; // the way the dog crosses it
    float length = 0.7f;             // along dir
    float clear = 0.6f;              // Jump: the bar's top; Tyre: the middle of the hole (m above the ground)
    std::vector<float> poles;        // Weave: where the poles stand along dir (from the centre)
    std::vector<float> profile;      // Ramp, SeeSaw: surface height every 0.1 m along dir, from -length/2
    kke::ModelModule::InstanceId model = 0;

    glm::vec3 entry() const { return centre - dir * (length * 0.5f + 0.9f); }
    glm::vec3 exit() const { return centre + dir * (length * 0.5f + 0.9f); }
    float span() const { return length + 1.8f; }
    float speed(float run) const; // how fast a dog goes over it
    float height(float along) const; // Ramp, SeeSaw: the surface under `along` (from the centre)
};
const char* obstacleName(Obstacle::Kind k);

// Where the dog is `t` metres into an obstacle (0 at entry(), span() at exit()).
struct ObstaclePose {
    glm::vec3 feet{0.0f};
    glm::vec3 forward{0.0f, 0.0f, 1.0f};
    DogAct act = DogAct::Move;
    float phase = 0.0f;
    float pitch = 0.0f; // degrees, nose up
};
ObstaclePose traverse(const Obstacle& o, float t, float dogHeight);

class AgilityCourse {
public:
    // Places the course with its art (or blocks), and the start and
    // finish line, and adds their colliders through `scenery`.
    void build(command_kit::Scenery& scenery, kke::ModelModule& models);
    const std::vector<Obstacle>& obstacles() const { return m_obstacles; }
    glm::vec3 start() const { return m_start; }
    glm::vec3 startFacing() const { return m_startFacing; }

    // The run.
    bool running() const { return m_running; }
    bool finished() const { return m_finished; }
    int next() const { return m_next; } // index of the obstacle due next
    float time() const { return m_time; }
    int faults() const { return m_faults; }
    float best() const { return m_best; } // < 0: none yet
    void setBest(float best) { m_best = best; }
    void reset();
    void update(float dt);
    // The dog went over obstacle i: true when it was the right one (a
    // wrong one is a fault). The first one starts the clock.
    bool took(int i);
    // You sent it to obstacle i out of order.
    void refused();
    // The total with faults, after the last obstacle.
    float result() const { return m_time + 5.0f * float(m_faults); }
    // The obstacle nearest a point (within `within` metres), or -1.
    int near(const glm::vec3& p, float within) const;

private:
    std::vector<Obstacle> m_obstacles;
    glm::vec3 m_start{14.0f, 0.0f, 0.0f}, m_startFacing{1.0f, 0.0f, 0.0f};
    bool m_running = false, m_finished = false;
    int m_next = 0, m_faults = 0;
    float m_time = 0.0f, m_best = -1.0f;
};

} // namespace pet_companion
