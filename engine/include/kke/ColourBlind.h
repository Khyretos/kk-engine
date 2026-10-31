#pragma once

#include <glm/glm.hpp>

#include <string>

namespace kke {

// Colour-blind modes (ENG-F85): a matrix on display-linear colour that
// tonemap_post.frag applies as the last step of the scene's colour, so the
// colours a person with that condition confuses move apart.
//
//   protanopia      no red cones     deuteranopia    no green cones
//   tritanopia      no blue cones    achromatopsia   no colour at all (the picture is
//                                    desaturated: lightness is all there is to read)
//
// The correction is the usual daltonization: take what the condition loses
// (the colour minus how the colour looks to them, Machado et al. 2009's
// matrices for full severity) and move it into the channels they do see
// (red's loss into green and blue; blue's into red and green). Strength 0..1
// blends from no change to the full correction.
//
// It changes the 3D scene, not the UI drawn over it: a game's UI should not
// rely on colour alone (shapes, icons, text), which is what its own docs say.
enum class ColourBlindMode { Off, Protanopia, Deuteranopia, Tritanopia, Achromatopsia };

ColourBlindMode colourBlindModeFromName(const std::string& name); // unknown -> Off
const char* colourBlindModeName(ColourBlindMode mode);

// The correction to multiply display-linear colour by (identity for Off or strength 0).
glm::mat3 colourBlindCorrection(ColourBlindMode mode, float strength = 1.0f);

// How a person with the condition sees colours (a developer's preview of
// their game: KKE_COLOURBLIND_SIM=protanopia). The same blend by strength.
glm::mat3 colourBlindSimulation(ColourBlindMode mode, float strength = 1.0f);

} // namespace kke
