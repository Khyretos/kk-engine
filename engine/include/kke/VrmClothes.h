#pragma once

#include "kke/HumanClothes.h"
#include "kke/HumanLook.h"
#include "kke/Vrm.h"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace kke {

// The wardrobe on a VRoid avatar (docs/ANIME.md "Clothes"): the garments of
// kke::garments() cut from the avatar's own skin, the same cuts the realistic
// body wears (garmentPiecesOn()), drawn in the toon shader like the rest of the
// avatar. A skirt, a dress's or a coat's tails are a flared skirt built round
// the hips (skinned to the hips and thighs, so it follows the legs; the
// realistic body simulates them as cloth). VRoid's sample avatars wear a
// top, shoes and, the male, trousers of their own; those are hidden by the
// garments that take their place, and back when nothing does.
//
// VrmStock remembers the avatar as it was loaded, so dressVrm() can be called
// again with another outfit. One per avatar; start it empty.
struct VrmStock {
    bool ready = false;
    size_t meshes = 0, materials = 0;                     // the avatar as loaded
    std::map<size_t, std::vector<uint32_t>> stockIndices; // each stock clothes mesh's triangles
    std::map<size_t, std::string> stockSlot;              // "top", "bottom", "shoes" or "" (kept)
    std::vector<size_t> skinMeshes;                       // the body's skin meshes
    GarmentBody body;                                     // the skin in MakeHuman's words (garmentPiecesOn)
    std::vector<ModelVertex> rest;                        // the skin's vertices in model space, at rest
    int hips = -1, leftLeg = -1, rightLeg = -1;           // bones, for the skirts
    float leftSign = 1.0f;                                // +1 if the avatar's left leg is on its +X side
};

// Puts `clothes` on the avatar (replacing whatever dressVrm put on before). The
// caller rebuilds the avatar's renderer afterwards (AnimeRenderer::build).
// Returns the number of garment meshes added.
size_t dressVrm(VrmAvatar& avatar, VrmStock& stock, const std::vector<HumanLook::Worn>& clothes);

} // namespace kke
