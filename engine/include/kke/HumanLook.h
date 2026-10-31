#pragma once

#include "kke/Human.h"
#include "kke/HumanPack.h"

#include <glm/glm.hpp>

#include <map>
#include <string>
#include <vector>

namespace kke {

// What a player made in a character creator (docs/HUMANS.md): the
// sliders by name and the colours. Saved as JSON (a .kkhuman file), and
// sent to other players as a short share code: only values travel, never
// meshes, and every machine bakes the same body from them.
//
// Pure CPU, unit-tested in tests/test_human.cpp.
struct HumanLook {
    std::map<std::string, float> shape; // slider -> value; missing = default
    glm::vec3 skin{ 0.80f, 0.62f, 0.52f };
    glm::vec3 eyes{ 0.24f, 0.36f, 0.45f };
    glm::vec3 hairRoot{ 0.20f, 0.13f, 0.07f };
    glm::vec3 hairTip{ 0.35f, 0.24f, 0.13f };
    std::string hair;   // a hairstyle name (kke::hairStyle), "" = none
    float hairLength = 1.0f;
    // What it wears (kke/HumanClothes.h): a garment id and its colour (sRGB).
    struct Worn {
        std::string id;
        glm::vec3 colour{ 0.9f };
    };
    std::vector<Worn> clothes;
};

// The look's sliders as a body. Values are set as they are (the
// ancestry three are only scaled to add up to 1), so a look decoded from
// a share code bakes the same body on every machine.
void applyLook(Human& human, const HumanLook& look);
// A human's sliders (only those away from their defaults) into `look`,
// leaving its colours and hair as they are.
void takeShape(const Human& human, HumanLook& look);

// JSON, for saving: {"format": "kk-human", "version": 1, "shape": {...}, ...}.
// Unknown keys are ignored, missing ones keep the defaults.
std::string lookToJson(const HumanLook& look);
bool lookFromJson(const std::string& json, HumanLook& look, std::string* error = nullptr);

// A MakeHuman .mhm save (its "modifier <name> <value>" lines and the skin
// and eye colours where it has them), so a face made in MakeHuman comes
// across. Lines this doesn't know are skipped.
HumanLook lookFromMhm(const std::string& mhm);

// The share code: "kh1." and URL-safe base64 of the colours, one bit per
// slider saying whether it is off its default, and those sliders' values
// (8 bits each, 0.4% steps of their range). About 90 characters for a few
// changed sliders, 250 for a random person with 130. Tied to the pack's slider list:
// another pack refuses it instead of building a different body.
std::string shareCode(const HumanLook& look, const HumanPack& pack);
bool fromShareCode(const std::string& code, const HumanPack& pack, HumanLook& look, std::string* error = nullptr);
// A look rounded the way the share code rounds it: what other players
// will see. Apply this to the player's own body too, so it matches theirs.
HumanLook quantised(const HumanLook& look, const HumanPack& pack);

} // namespace kke
