#pragma once
// ScriptWeather: the weather.* Lua API over kke::Weather (docs/MOODS.md "Weather").
#include "kke/Weather.h"

#include <glm/glm.hpp>

#include <functional>
#include <string>

namespace kke {

class ScriptVM;
struct Lighting;

// Runs a Weather for a game and turns it into what the player sees: rain,
// downpour and snow emitters that follow the camera, a wind for the particles,
// and the lighting dimmed, greyed and fogged (with the lightning's flashes on
// top). It stays quiet until a script uses it (weather.set, weather.random),
// so a game that has no weather is not touched.
//
// The lighting is changed without fighting whoever else sets it (a mood, the
// day/night clock): it remembers the values it wrote, and when it finds
// something else there it takes that as the new base and applies the weather
// over it.
class ScriptWeather {
public:
    // The particles are the game's to start: `family` is "rain" or "snow", `effect` the effect that
    // should be playing for it ("rain", "downpour", "snow"; "" for none), `count` how many emitters
    // of it, at `position` with the ground at `floor`. Called every update (it moves them).
    std::function<void(const std::string& family, const std::string& effect, int count, const glm::vec3& position, float floor)> precipitation;
    std::function<void(const glm::vec3& wind)> wind; // for the particles that drift
    void attach(Lighting* lighting) { m_lighting = lighting; }
    void setFolder(std::string f) { m_folder = std::move(f); }

    void bind(ScriptVM& vm);
    // `hour`: the clock's, when one runs (-1: none).
    void update(float dt, ScriptVM& vm, const glm::vec3& camera, float hour);
    // The script that started it is gone: the weather stops and the lighting goes back.
    void release(const std::string& source);

    Weather& weather() { return m_weather; }
    bool active() const { return m_active; }
    // Half the height above the camera the particles are born and the ground below it.
    float rainHeight = 10.0f, snowHeight = 7.5f, groundBelow = 1.8f;

private:
    struct Applied {
        float sun = 0.0f, exposure = 1.0f, fogDensity = 0.0f, saturation = 1.0f;
        bool fogEnabled = false, sunEnabled = false;
        glm::vec3 ambient{ 0.0f };
        bool operator==(const Applied& o) const {
            return sun == o.sun && exposure == o.exposure && fogDensity == o.fogDensity && saturation == o.saturation && fogEnabled == o.fogEnabled &&
                   sunEnabled == o.sunEnabled && ambient == o.ambient;
        }
    };
    static Applied read(const Lighting& l);
    static void write(Lighting& l, const Applied& a);
    void applyLighting();
    void restoreLighting();
    void stopParticles();

    Weather m_weather;
    Lighting* m_lighting = nullptr;
    std::string m_folder, m_owner;
    bool m_active = false, m_particles = true, m_lightingOn = true;
    bool m_haveSet = false;
    Applied m_base, m_set;
    glm::vec3 m_lastWind{ 1.0e9f };
    bool m_hourFromClock = true;
};

} // namespace kke
