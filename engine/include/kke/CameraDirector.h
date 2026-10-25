#pragma once
// Cutscene cameras (GAPS F27): a director that takes the camera away from
// gameplay for a while and gives it back smoothly.
//
//   play    a path through shots (position, look-at point, optional field
//           of view, time), smooth through every shot (Catmull-Rom). It
//           blends in from what the player saw, and at the end either
//           blends back to gameplay or holds the last shot.
//   hold    one fixed shot (a conversation, a door opening) until release.
//   release back to gameplay over a blend.
//   shake   a short tremble on top of whichever camera is shown (hits,
//           explosions, landings), fading out over its time.
//
// The director knows nothing of the engine's frame: update() takes the
// camera gameplay produced this frame and changes it. Application runs
// it after every module's update (Application::setCameraDirector), so
// the player's own camera code keeps running and is there to blend back to.
#include "kke/Application.h"

#include <optional>
#include <vector>

namespace kke {

class CameraDirector {
public:
    struct Shot {
        glm::vec3 position{0.0f};
        glm::vec3 target{0.0f, 0.0f, -1.0f};
        float fovDegrees = 0.0f; // 0 = the gameplay camera's
        float time = 0.0f;       // seconds from the start of the path
    };
    struct PathOptions {
        bool loop = false;     // round and round until release (no Done)
        bool hold = false;     // stay on the last shot after it ends, until release
        float blendIn = 0.5f;  // seconds from what was shown to the path
        float blendOut = 0.5f; // seconds back to gameplay after the end
    };

    // Shots are sorted by time and shifted so the first is at 0. A single
    // shot is the same as hold().
    void play(std::vector<Shot> shots, const PathOptions& options);
    void hold(const Shot& shot, float blendIn = 0.5f);
    // Back to gameplay over `blendOut` seconds (0 = cut). Nothing if idle.
    void release(float blendOut = 0.5f);
    // Gameplay at once, the shake too.
    void stop();
    // strength: 0..1 (1 = a big explosion next to the player); stacks up to 1.
    void shake(float strength, float seconds);

    // The camera is the director's (a path, a hold, or blending out of one).
    bool directing() const { return m_phase != Phase::Idle; }
    bool playing() const { return m_phase == Phase::Playing; }
    bool shaking() const { return m_shakeLeft > 0.0f; }
    float duration() const { return m_shots.empty() ? 0.0f : m_shots.back().time; }
    float time() const { return m_time; }

    struct Result {
        bool changed = false;  // the camera is not plain gameplay this frame
        bool finished = false; // a path (not a looping one) reached its end this frame
    };
    Result update(float dt, Camera& camera);

    // Where a path is at `time` (clamped), for tools and tests.
    static Shot sample(const std::vector<Shot>& shots, float time);

private:
    enum class Phase { Idle, Playing, Holding, BlendingOut };
    void begin(Phase phase, float blendIn);

    Phase m_phase = Phase::Idle;
    std::vector<Shot> m_shots;
    PathOptions m_options;
    Shot m_held;
    float m_time = 0.0f;
    float m_weight = 0.0f; // 0 = gameplay (or m_from), 1 = the director's shot
    float m_blendIn = 0.5f, m_blendOut = 0.5f;
    std::optional<Camera> m_from; // a new path started while another was shown: blend from that
    Camera m_last;                // what update() showed last
    bool m_hasLast = false;
    float m_shake = 0.0f, m_shakeLeft = 0.0f, m_shakeTotal = 0.0f, m_shakeTime = 0.0f;
};

} // namespace kke
