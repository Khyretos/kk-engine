#pragma once

#include "kke/Hair.h"
#include "kke/HumanPack.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace kke {

// Hair on a MakeHuman body (docs/HUMANS.md "Hair"): the engine's strand
// hair (kke/Hair.h, docs/HAIR.md) grown from this body's own scalp, so
// it fits every head the sliders make.

// Where hair grows on a shaped body (Human::shapedPositions()): the skin
// the head bone moves, above the ears and behind the hairline. centre and
// radius are the sphere that fits it best (least squares), up and front
// the head's own directions.
struct HumanScalp {
    std::vector<uint32_t> verts; // body vertices
    glm::vec3 centre{0.0f};
    float radius = 0.0f;
    glm::vec3 up{0.0f, 1.0f, 0.0f}, front{0.0f, 0.0f, 1.0f};
};
HumanScalp findScalp(const HumanPack& pack, const std::vector<glm::vec3>& shaped);

// A hairstyle (hairstyleOnHead() names, or any hairStyleNames() type) on
// this body: hairstyleOnHead() on the scalp's sphere, then every root set
// on the skin under it; roots with no scalp under them (over the face or
// the ears) are dropped. Keeps desc.style's colours. False for an unknown
// style or a body with no scalp.
bool hairOnHuman(HairDesc& desc, const std::string& style, const HumanPack& pack, const std::vector<glm::vec3>& shaped,
                 int guides);

} // namespace kke
