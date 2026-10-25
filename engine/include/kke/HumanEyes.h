#pragma once

#include "kke/ModelAsset.h"

#include <glm/glm.hpp>

namespace kke {

// MakeHuman's helper eyes are plain white balls: the iris is a texture
// the pack doesn't carry. addIrises() puts what a face needs on each
// eyeball's front (docs/HUMANS.md "Eyes"): a pupil, the iris and its
// darker rim (the limbus), as thin domes just over the ball, 32 sides
// each, so they stay round however coarse the eyeball is.
//
// The model must hold MakeHuman's eye meshes ("helper-l-eye",
// "helper-r-eye", as Human::bake makes them). Three meshes are added
// (pupil, iris, limbus; both eyes in each), with materials in `colour`
// (the limbus darker, the pupil near black), skinned like the eyeball's
// front when the eyes are skinned. Returns false when the model has no
// eyes. Call it again after Human::reshape with `update` = true to move
// the meshes it added onto the new eyes instead of adding more.
bool addIrises(ModelData& model, const glm::vec3& colour, bool update = false);

} // namespace kke
