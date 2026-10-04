#pragma once

#include "kke/AnimRig.h"
#include "kke/ModelAsset.h"
#include "kke/modules/ModelModule.h"

#include <memory>
#include <string>
#include <vector>

namespace kke {

// People a player can pick as their body in a start menu (docs/OUTFITS.md
// "Synty people"): the POLYGON City Characters, drawn over a game's own
// rig. The game keeps animating its mannequin (clips, IK, hands on holds)
// and each frame PoseRetarget copies that pose onto the person, so every
// body moves exactly as the mannequin would.
//
// Without the pack there are no people (any() is false): the menu offers
// only the mannequin, and a person picked on another machine is drawn as
// the mannequin here.
class PeopleLibrary {
public:
    // Every person there can be, in a fixed order (person 1 is the
    // first), the same on every machine whatever packs it has.
    static const std::vector<std::string>& names(); // "Jock", "Tourist", ...
    static const char* assetOf(int person);         // "SK_Character_Jock" (person >= 1)

    // Finds the pack (KKE_ASSETS_DIR, KKE_SYNTY_DIR, assets/synty).
    void scan(const std::string& logName);
    bool any() const { return !m_paths.empty() && m_found > 0; }
    bool has(int person) const;

    struct Body {
        ModelModule::ModelId model = 0;
        PoseRetarget retarget; // from the rig body() was asked with
    };
    // The person's model (added to `models` the first time) and how to
    // pose it from `rig`'s poses; nullptr when it can't be drawn here.
    const Body* body(int person, ModelModule& models, const ModelData& rig);
    // Poses and places `instance` (spawned from body.model) as the rig's
    // instance is: its bone locals this frame and its transform.
    static void follow(ModelModule& models, ModelModule::InstanceId instance, const Body& body, const std::vector<glm::mat4>& rigLocals,
                       const glm::mat4& rigTransform);

private:
    std::string m_log = "People";
    std::vector<std::string> m_paths;       // per person - 1: the FBX ("" when missing)
    std::vector<ModelLoadOptions> m_options;
    std::vector<std::unique_ptr<Body>> m_bodies;
    std::vector<bool> m_tried;
    int m_found = 0;
};

} // namespace kke
