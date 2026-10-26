#pragma once
// ScriptClock: the time of day Lua scripts run with clock.* (docs/MOODS.md "Time of day").
#include "kke/DayNight.h"

#include <string>

namespace kke {

class Application;
class ScriptVM;

// One clock for the game. While it runs, the sun moves every frame and the
// sky catches up whenever it has changed enough (at most every
// `skyInterval` seconds: a new sky is re-analysed for ambient light and
// reflections). Scripts hear "Dawn", "Dusk" and "Hour" hooks.
class ScriptClock {
public:
    explicit ScriptClock(Application* app) : m_app(app) {}
    void bind(ScriptVM& vm);
    // Advances the clock, fires the hooks, lights the scene.
    void update(float dt, ScriptVM& vm);
    void release(const std::string& source);

    DayNight& clock() { return m_clock; }
    bool active() const { return m_active; }
    size_t skyUpdates() const { return m_skyUpdates; }

    float skyInterval = 0.5f;

private:
    void light(bool forceSky);
    Application* m_app;
    DayNight m_clock;
    bool m_active = false;      // lighting the scene (running or frozen at an hour)
    std::string m_owner;        // the script that started it
    std::string m_mood;         // the mood when it started: a new mood stops the clock
    Sky m_lastSky;
    float m_sinceSky = 0.0f;
    size_t m_skyUpdates = 0;
};

} // namespace kke
