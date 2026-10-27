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
// skin, neck to wrists to toes) and a long skirt ("helper-skirt", waist
// to ankles); gloves from the body's own hands. A T-shirt is the suit's
// torso and upper arms; a tennis skirt the skirt's top third, simulated
// as cloth; a dress a bodice from the suit and a skirt of cloth. Every
// cut is a few numbers in a table (sleeve, neckline, hem, length), so
// the wardrobe is about a hundred garments from the same two shapes,
// and whatever the sliders do, the clothes are already the right size:
// no fitting step, and in a multiplayer game a garment travels as its
// name and colour.

struct GarmentInfo {
    std::string id, label;
    // "top", "outer", "bottom", "dress", "socks", "shoes", "gloves": one
    // garment per slot is worn; a dress takes the top's and the bottom's
    // place (garmentSlotsClash).
    std::string slot;
    // A clothFabric() preset ("cotton", "denim", "satin", "wool",
    // "leather"...): how its cloth pieces hang and how every piece is lit.
    std::string fabric = "cotton";
    glm::vec3 colour{ 0.85f }; // worn in this colour until it is dyed
};
// Every garment, by slot: tops, outerwear, bottoms, dresses, socks,
// shoes, gloves. garmentSlots() lists the slots in wearing order.
const std::vector<GarmentInfo>& garments();
const std::vector<std::string>& garmentSlots();
const GarmentInfo* findGarment(const std::string& id);
// Can't be worn together: the same slot, or a dress with a top or bottom.
bool garmentSlotsClash(const std::string& a, const std::string& b);

// The pack vertices one piece of a garment is made of, in `group`
// ("helper-tights", "helper-skirt" or "body" for gloves). cloth: hangs
// and swings (a skirt), pinned along `pinned` (its waistband). offset:
// how far over the base shape it is drawn (m), so layers stack: socks
// under shoes, a top over trousers, a coat over both.
struct GarmentShape {
    std::string group;
    std::vector<uint32_t> verts;
    bool cloth = false;
    std::vector<uint32_t> pinned;
    float offset = 0.0f;
};
// Every piece of a garment: one for most, a bodice and a skirt for a
// dress, a jacket and its tails for a long coat. shaped:
// Human::shapedPositions(), for the cuts made by height (a skirt's
// length, a sock's). Empty for an unknown garment.
std::vector<GarmentShape> garmentPieces(const HumanPack& pack, const std::vector<glm::vec3>& shaped, const std::string& id);
// The garment's first piece (empty verts for an unknown garment).
GarmentShape garmentShape(const HumanPack& pack, const std::vector<glm::vec3>& shaped, const std::string& id);

// The piece cut from a bake of its group (Human::bake with shape.group
// among the groups; `mesh` is that group's mesh, sourceVertex its map):
// its triangles, each vertex `offset` out along its normal, skin weights
// kept, so a skinned bake gives a skinned garment. from (optional):
// garment vertex -> bake vertex, to move it again after Human::reshape.
ModelMesh garmentMesh(const ModelMesh& mesh, const std::vector<uint32_t>& sourceVertex, const GarmentShape& shape,
                      std::vector<uint32_t>* from = nullptr);

// The material a garment is drawn with when it is not simulated: its
// fabric's roughness and sheen, in `colour`.
ModelMaterial garmentMaterial(const GarmentInfo& garment, const glm::vec3& colour);

// MakeHuman's tights helper follows the body's targets, but lags the
// strongest muscle and bust ones by a few millimetres, and the skin
// showed through a shirt on a Strong body. skinUnder() finds the skin
// vertex nearest each garment vertex (once, when it is put on);
// keepOutside() then pushes every garment vertex out along that skin
// vertex's normal until it is at least `gap` over the skin.
std::vector<uint32_t> skinUnder(const ModelMesh& garment, const ModelMesh& skin);
void keepOutside(ModelMesh& garment, const ModelMesh& skin, const std::vector<uint32_t>& under, float gap);

// Adds the clothes to a bake of `human` (Human::bake(), skinned or not):
// a mesh and a material in the garment's colour per piece, on the same
// bones, so the game animates a dressed person as one model. Skirts are
// added as skinned meshes too (they follow the hips and legs); a game
// that wants them to swing runs them as cloth instead (GarmentShape::
// cloth, GarmentInfo::fabric). Every piece is kept outside the skin
// (keepOutside).
void dress(ModelData& model, const Human& human, const std::vector<HumanLook::Worn>& clothes, bool skeleton = true);

} // namespace kke
