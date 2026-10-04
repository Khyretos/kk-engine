#pragma once

#include "kke/ModelAsset.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace kke {

// A humanoid's clothes as colours (docs/OUTFITS.md): the skin, a top, the
// trousers and the shoes, painted onto any skinned humanoid by which bone
// moves each part of it (the UAL mannequin, a Synty character: the usual
// bone names, canonicalBoneName). No textures, no extra meshes: the model
// is split into one mesh per body region, each with a material of its
// own, so the outfit costs what the plain model does.
struct Outfit {
    glm::vec3 skin{0.72f, 0.52f, 0.38f};
    glm::vec3 top{0.35f, 0.65f, 1.0f};
    glm::vec3 bottom{0.16f, 0.18f, 0.24f};
    glm::vec3 shoes{0.92f, 0.92f, 0.9f};
    bool sleeves = false; // long sleeves: the forearms in the top's colour
    bool operator==(const Outfit&) const = default;
};

enum class BodyRegion : uint8_t { Skin, Top, Bottom, Shoes };
// Which region a bone dresses (head and hands are skin; spine, clavicles,
// upper arms the top; pelvis and legs the bottom; feet the shoes). An
// unknown bone counts as the top.
BodyRegion regionOfBone(const std::string& boneName, bool sleeves = false);

// The model in that outfit: each skinned mesh split by region (a
// triangle goes where most of its vertices' weight is), the materials'
// colours the outfit's, textures dropped. Static meshes stay as they are.
ModelData dressModel(const ModelData& model, const Outfit& outfit);
// A short text that is the same for the same outfit: a name for a dressed
// copy (one per outfit, shared by everyone wearing it).
std::string outfitKey(const Outfit& outfit);

// Named choices for menus. Skin tones span the whole range of people
// (lightest to deepest), so everyone can be themselves.
struct NamedColour {
    const char* name;
    glm::vec3 rgb;
};
const std::vector<NamedColour>& skinTones();
const std::vector<NamedColour>& clothColours();

} // namespace kke
