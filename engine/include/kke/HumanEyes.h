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

// MakeHuman's eyebrow and eyelash meshes live in its asset library, not
// in the base the pack is cooked from. addBrows() grows both from the
// face itself (docs/HUMANS.md "Brows and lashes"): each brow about 260
// short hairs lying flat on the skin over the brow ridge, thick at the
// inner end and thin at the outer, and about 60 curved lashes along each
// upper lid's edge, every hair a thin tapered strip.
//
// `skin` is the body mesh ("body" as Human::bake makes it, in the same
// space as the eyes in `model`); each hair copies the skin weights of the
// skin vertex under its root, so a skinned brow follows the face and the
// lashes the lids. Two meshes are added, "brows" and "lashes", in
// `colour` (the lashes darker). The hairs come from a fixed seed: the
// same face gives the same brows. Returns false when `model` has no eyes
// or `skin` no face over them. With `update` = true, moves the meshes it
// added onto the face as it is now.
bool addBrows(ModelData& model, const ModelMesh& skin, const glm::vec3& colour, bool update = false);

} // namespace kke
