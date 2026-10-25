#pragma once

#include "kke/Human.h"
#include "kke/HumanLook.h"
#include "kke/HumanPack.h"
#include "kke/ModelAsset.h"

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

// The garment cut from a bake of its group (Human::bake with shape.group
// among the groups; `mesh` is that group's mesh, sourceVertex its map):
// its triangles, each vertex `offset` out along its normal, skin weights
// kept, so a skinned bake gives a skinned garment. from (optional):
// garment vertex -> bake vertex, to move it again after Human::reshape.
ModelMesh garmentMesh(const ModelMesh& mesh, const std::vector<uint32_t>& sourceVertex, const GarmentShape& shape,
                      std::vector<uint32_t>* from = nullptr);

// MakeHuman's tights helper follows the body's targets, but lags the
// strongest muscle and bust ones by a few millimetres, and the skin
// showed through a shirt on a Strong body. skinUnder() finds the skin
// vertex nearest each garment vertex (once, when it is put on);
// keepOutside() then pushes every garment vertex out along that skin
// vertex's normal until it is at least `gap` over the skin.
std::vector<uint32_t> skinUnder(const ModelMesh& garment, const ModelMesh& skin);
void keepOutside(ModelMesh& garment, const ModelMesh& skin, const std::vector<uint32_t>& under, float gap);

// Adds the clothes to a bake of `human` (Human::bake(), skinned or not):
// a mesh and a material in the garment's colour each, on the same bones,
// so the game animates a dressed person as one model. Skirts are added as
// skinned meshes too (they follow the hips and legs); a game that wants
// them to swing runs them as cloth instead (garmentShape().cloth).
// Every garment is kept outside the skin (keepOutside).
void dress(ModelData& model, const Human& human, const std::vector<HumanLook::Worn>& clothes, bool skeleton = true);

} // namespace kke
