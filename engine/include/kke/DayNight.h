#pragma once

#include "kke/Sky.h"

#include <glm/glm.hpp>

#include <vector>

namespace kke {

struct Lighting;

// A day/night clock (ENG-F82, docs/MOODS.md "Time of day"): the sun rises
// in the east, crosses the south and sets in the west; the sky, the sun's
// colour and strength, the fill light and the exposure follow it, through
// golden hour and dusk to a moonlit night and back.
//
// Pure maths and a little state, no GPU: lightingAt(hour) says what the
// scene looks like at that time, apply() puts it into a Lighting, and
// advance() moves the clock and reports what it crossed.
class DayNight {
public:
    struct Look {
        float sunElevation = 0.0f;  // degrees; below 0 the sun is down
        float sunAzimuth = 0.0f;    // degrees round from north (-z) towards east (+x)
        glm::vec3 towardsSun{0.0f, 1.0f, 0.0f};
        Sky sky;                    // a gradient sky
        // The key light (lights[0]): the sun by day, the moon by night.
        glm::vec3 keyDirection{0.0f, -1.0f, 0.0f}; // the way the light travels
        glm::vec3 keyColor{1.0f};
        float keyIntensity = 0.0f;
        bool moonlit = false;
        float exposure = 1.0f;
    };

    enum class Event { Dawn, Dusk, Hour };
    struct Crossed {
        Event event;
        int hour; // the hour just reached (Hour), or the clock's hour then (Dawn, Dusk)
    };

    float hour = 12.0f;            // 0..24
    float dayLengthSeconds = 1200; // real seconds for a whole day (20 minutes)
    bool running = false;
    float noonElevation = 62.0f;   // how high the sun climbs (lower = northern winter)
    float sunrise = 6.0f;          // the hour the sun crosses the horizon going up
    float sunset = 18.0f;          // and going down

    // What the scene looks like at `h` (wrapped into 0..24).
    Look lookAt(float h) const;
    Look look() const { return lookAt(hour); }
    float sunElevationAt(float h) const;
    bool isDay() const { return sunElevationAt(hour) > 0.0f; }

    // Moves the clock by `dt` real seconds when running; returns what it
    // passed, in order (hours, dawn, dusk).
    std::vector<Crossed> advance(float dt);

    // Puts the look into `lighting`: lights[0] always; the sky (and so the
    // fog's colour and the ambient light) only with `withSky`, because a new
    // sky is re-analysed for reflections and ambient light (kke::SkyResolver),
    // too slow to do every frame. Turns lights[1] (a mood's fill) off: the sky
    // fills the shadows.
    static void apply(const Look& look, Lighting& lighting, bool withSky = true);
    // Whether two looks' skies differ enough to be worth a new sky (any
    // colour more than `tolerance` apart, relative).
    static bool skyChanged(const Sky& a, const Sky& b, float tolerance = 0.03f);
};

} // namespace kke
