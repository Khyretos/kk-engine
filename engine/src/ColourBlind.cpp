#include "kke/ColourBlind.h"

#include <algorithm>

namespace kke {

namespace {

// Row-major rows in, a column-major glm::mat3 out.
glm::mat3 fromRows(float a, float b, float c, float d, float e, float f, float g, float h, float i) {
    return glm::mat3(a, d, g, b, e, h, c, f, i);
}

// Machado, Oliveira and Fernandes 2009, "A Physiologically-based Model for
// Simulation of Color Vision Deficiency": severity 1.0, for linear RGB.
glm::mat3 machado(ColourBlindMode mode) {
    switch (mode) {
    case ColourBlindMode::Protanopia:
        return fromRows(0.152286f, 1.052583f, -0.204868f, 0.114503f, 0.786281f, 0.099216f, -0.003882f, -0.048116f, 1.051998f);
    case ColourBlindMode::Deuteranopia:
        return fromRows(0.367322f, 0.860646f, -0.227968f, 0.280085f, 0.672501f, 0.047413f, -0.011820f, 0.042940f, 0.968881f);
    case ColourBlindMode::Tritanopia:
        return fromRows(1.255528f, -0.076749f, -0.178779f, -0.078411f, 0.930809f, 0.147602f, 0.004733f, 0.691367f, 0.303900f);
    case ColourBlindMode::Achromatopsia: // Rec. 709 luminance
        return fromRows(0.2126f, 0.7152f, 0.0722f, 0.2126f, 0.7152f, 0.0722f, 0.2126f, 0.7152f, 0.0722f);
    case ColourBlindMode::Off:
        break;
    }
    return glm::mat3(1.0f);
}

glm::mat3 blend(const glm::mat3& full, float strength) {
    const float s = std::clamp(strength, 0.0f, 1.0f);
    return glm::mat3(1.0f) + (full - glm::mat3(1.0f)) * s;
}

} // namespace

ColourBlindMode colourBlindModeFromName(const std::string& name) {
    if (name == "protanopia") return ColourBlindMode::Protanopia;
    if (name == "deuteranopia") return ColourBlindMode::Deuteranopia;
    if (name == "tritanopia") return ColourBlindMode::Tritanopia;
    if (name == "achromatopsia") return ColourBlindMode::Achromatopsia;
    return ColourBlindMode::Off;
}

const char* colourBlindModeName(ColourBlindMode mode) {
    switch (mode) {
    case ColourBlindMode::Protanopia: return "protanopia";
    case ColourBlindMode::Deuteranopia: return "deuteranopia";
    case ColourBlindMode::Tritanopia: return "tritanopia";
    case ColourBlindMode::Achromatopsia: return "achromatopsia";
    case ColourBlindMode::Off: break;
    }
    return "off";
}

glm::mat3 colourBlindSimulation(ColourBlindMode mode, float strength) { return blend(machado(mode), strength); }

glm::mat3 colourBlindCorrection(ColourBlindMode mode, float strength) {
    if (mode == ColourBlindMode::Off) return glm::mat3(1.0f);
    if (mode == ColourBlindMode::Achromatopsia) return blend(machado(mode), strength); // nothing to move into: drop the colour
    const glm::mat3 identity(1.0f);
    // Where the error goes: red and green loss into the other channel pair, blue's into red and green.
    const glm::mat3 shift = mode == ColourBlindMode::Tritanopia ? fromRows(1.0f, 0.0f, 0.7f, 0.0f, 1.0f, 0.7f, 0.0f, 0.0f, 0.0f)
                                                                : fromRows(0.0f, 0.0f, 0.0f, 0.7f, 1.0f, 0.0f, 0.7f, 0.0f, 1.0f);
    // corrected = c + shift * (c - sim * c)  =  (I + shift * (I - sim)) * c
    const glm::mat3 full = identity + shift * (identity - machado(mode));
    return blend(full, strength);
}

} // namespace kke
