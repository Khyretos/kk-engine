#pragma once

// The art: Synty characters and props found in the asset folder (packs
// named in data/*.yml), animated with Quaternius' Universal Animation
// Library (UAL 1 and 2) retargeted onto each skeleton once. Without a
// pack a character is the UAL mannequin; without UAL, nothing can move
// and the game draws blocks. Never committed (docs/SCENES.md).

#include "Roster.h"

#include "kke/AnimRig.h"
#include "kke/Animator.h"
#include "kke/AssetCatalog.h"
#include "kke/modules/ModelModule.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace horde {

// A skeleton with the clips the game uses, retargeted onto it. Every model
// made for it (the parts of one Characters.fbx) shares it.
struct Rig {
    kke::ModelData data; // bones and clips, no meshes
    std::unique_ptr<kke::AnimationSet> set;
    float yaw = 0.0f; // degrees the model turns so it faces +z
    int handR = -1, handL = -1, spine = -1, chest = -1, head = -1, forearmR = -1, forearmL = -1;
    std::vector<glm::mat4> rest; // model-space rest pose
    float height = 1.8f;         // feet to the top of the head, as made
    // The clip whose name ends in "|<name>" (or is <name>), else -1.
    int clip(const std::string& name) const;
    // The first of these the rig has, else -1.
    int clip(const std::vector<std::string>& names) const;
};

struct Look {
    std::string name;
    kke::ModelModule::ModelId model = 0;
    Rig* rig = nullptr;
    size_t triangles = 0;
};

class Art {
public:
    explicit Art(kke::ModelModule& models);
    ~Art();

    // Finds the asset folders, loads the animation library (only the clips
    // in `clips`, plus what moving and getting hit need) and lists the
    // packs in `packs`. False: no animation library (nothing can move).
    bool init(const std::set<std::string>& clips, const std::vector<std::string>& packs);
    bool animated() const { return m_ual != nullptr; }
    bool haveAssets() const { return !m_packDir.empty(); }

    // A character (cached): `skin` picks the pack's colour variant of its
    // texture (0 = as made). nullptr when the pack or the file isn't there.
    const Look* character(const ArtRef& ref, int skin = 0);
    // The UAL mannequin in a colour (always there when animated()).
    const Look* mannequin(const glm::vec3& color);
    // How many skins the character's texture has (1 = just its own).
    int skins(const ArtRef& ref);
    // A prop (cached; 0 when missing). Props far from their origin (an
    // arrow) are moved onto it.
    kke::ModelModule::ModelId prop(const ArtRef& ref);
    glm::vec3 propSize(kke::ModelModule::ModelId prop) const;

    // Where a held prop sits in the hand bone's space: `grip` from the
    // prop's ArtRef ("" one-handed, "twohand", "staff", "bow", "crossbow").
    glm::mat4 grip(const Rig& rig, const std::string& grip, bool left) const;
    // An attachment made on the head (or the back) in the rest pose, in
    // that bone's space.
    glm::mat4 attach(const Rig& rig, int bone) const;

    // Packs asked for that aren't there (said once in the log).
    const std::vector<std::string>& missingPacks() const { return m_missing; }

private:
    const kke::CatalogAsset* find(const std::string& pack, const std::string& file) const;
    Rig* rigFor(const std::string& key, const kke::ModelData& body);
    std::vector<std::string> skinTextures(const kke::ModelData& body) const;

    kke::ModelModule& m_models;
    std::string m_packDir, m_animDir;
    kke::AssetCatalog m_catalog;
    std::unique_ptr<kke::ModelData> m_ual; // the mannequin with the kept clips
    std::map<std::string, std::unique_ptr<Rig>> m_rigs;
    std::map<std::string, std::unique_ptr<kke::ModelData>> m_files; // multi-part files, loaded once
    std::map<std::string, std::unique_ptr<Look>> m_looks;
    std::map<std::string, kke::ModelModule::ModelId> m_props;
    std::map<kke::ModelModule::ModelId, glm::vec3> m_propSize;
    std::vector<std::string> m_missing;
    std::set<std::string> m_warned;
};

} // namespace horde
