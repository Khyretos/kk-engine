#pragma once

#include <string>

namespace kke {

// Reflections on smooth surfaces (Application::setReflections, ENG-F02):
// Off (the prefiltered sky alone), Screen (what is on screen, found by
// marching the depth), Kke (screen first, then rays through simple stand-ins
// for what is off screen, within a budget) or Classic (rays through the full
// geometry).
enum class Reflections { Off, Screen, Kke, Classic };
// "off", "screen", "kke", "classic"; anything else gives `fallback`.
Reflections reflectionsFromName(const std::string& name, Reflections fallback);

struct ReflectionSettings {
    float maxRoughness = 0.3f; // rougher surfaces keep the prefiltered sky
    float maxDistance = 25.0f; // metres a reflected ray is followed
    int steps = 48;            // depth samples along its line on screen (then 4 halvings)
    float thickness = 0.5f;    // metres a surface is assumed deep
};

} // namespace kke
