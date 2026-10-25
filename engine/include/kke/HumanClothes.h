#pragma once

#include "kke/HumanPack.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace kke {

// Clothes that fit every MakeHuman body (docs/HUMANS.md "Clothes"),
// cut from two shapes the base mesh carries and every morph target moves
// with the skin: a body suit ("helper-tights", a few millimetres over the
// skin, wrists to ankles) and a long skirt ("helper-skirt", waist to
// ankles). A T-shirt is the suit's torso and upper arms; a tennis skirt
// the skirt's top third, simulated as cloth. So whatever the sliders do,
// the clothes are already the right size: no fitting step, and in a
// multiplayer game a garment travels as its name and colour.

struct GarmentInfo {
    std::string id, label;
    std::string slot; // "top", "bottom", "socks", "shoes": one garment per slot is worn
};
// Every garment, by slot: tops (t-shirt, tank top, crop top, long
// sleeve), bottoms (shorts, trousers, tennis skirt, skirt, long skirt),
// socks, shoes.
const std::vector<GarmentInfo>& garments();
const GarmentInfo* findGarment(const std::string& id);

// The pack vertices a garment is made of, in `group`. cloth: hangs and
// swings (a skirt), pinned along `pinned` (its waistband). offset: how
// far over the base shape it is drawn (m), so layers stack: socks under
// shoes, a top over trousers.
struct GarmentShape {
    std::string group;
    std::vector<uint32_t> verts;
    bool cloth = false;
    std::vector<uint32_t> pinned;
    float offset = 0.0f;
};
// shaped: Human::shapedPositions(), for the cuts made by height (a skirt's
// length, a sock's). Empty verts for an unknown garment.
GarmentShape garmentShape(const HumanPack& pack, const std::vector<glm::vec3>& shaped, const std::string& id);

} // namespace kke
