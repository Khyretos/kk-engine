#pragma once

#include "kke/Module.h"
#include "kke/modules/ModelModule.h"
#include "kke/Capabilities.h"
#include "kke/Ragdoll.h"
#include "kke/AssetCatalog.h"
#include "kke/Breakables.h"

#include <memory>
#include <string>
#include <vector>

namespace kke_demo {

// Builds a small level out of Synty POLYGON Prototype pieces and puts the
// pack's skinned characters in it, each showing something different:
// the FBX's own animation clip, a procedural wave, idle breathing, and a
// hand-posed skeleton you edit from the "Characters" panel.
class SyntySceneModule : public kke::Module {
public:
    const char* name() const override { return "SyntyScene"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void shutdown() override;
    void onEvent(const SDL_Event& event) override; // F1: developer panels, Shift+R: everyone falls

    // The pack's root folder (containing _SourceFiles/), or empty.
    const std::string& packDir() const { return m_packDir; }
    // Engine debug panels hidden behind F1 so the scene stays visible.
    void setEnginePanels(std::vector<kke::Module*> panels) {
        m_enginePanels = std::move(panels);
        for (kke::Module* m : m_enginePanels) m->setUiVisible(false);
    }

private:
    struct Character {
        std::string label;
        kke::ModelModule::InstanceId instance = 0;
        kke::ModelModule::ModelId model = 0;
        std::string behavior; // "clip", "wave", "breathe", "pose"
        glm::vec3 position{0.0f};
        // Four legs (buildQuadrupedRagdoll) and how heavy, for ragdolls.
        bool animal = false;
        float mass = 70.0f;
        // Ragdoll state (0 = standing, driven by animation/posing).
        kke::IRagdollPhysics::RagdollHandle ragdoll = 0;
        kke::RagdollDesc ragdollDesc;
        kke::RagdollSkinBinding binding;
        std::string behaviorBeforeRagdoll;
        // Standing up: the last ragdoll pose (model space) blends back to
        // the animated one over kStandUpSeconds. < 0 = not blending.
        std::vector<glm::mat4> blendFrom;
        float blendAge = -1.0f;
        // The pose sliders' angles per bone, kept per character so they
        // still match the bones after switching away and back.
        std::vector<glm::vec3> poseEuler;
    };
    void blendToAnimation(Character& c, float dt);
    void addJoltLevel(); // floor and walls for Jolt ragdolls
    void ragdoll(Character& c, const glm::vec3& push);
    void standUp(Character& c);
    kke::IRagdollPhysics* m_physics = nullptr; // optional: whatever module offers ragdolls
    void throughGlass(Character& c); // FEMFX build only: see the .cpp
    kke::ModelModule::ModelId load(const std::string& relative);
    // Returns the instance (0 if the asset isn't there).
    kke::ModelModule::InstanceId place(const std::string& relative, glm::vec3 position, float yawDegrees = 0.0f, glm::vec3 scale = glm::vec3(1.0f));
    // FEMFX builds: the props named like what they're made of (crates,
    // barrels, the chest, the barrier) bend and break (kke::Breakables).
    void makePropsBreakable();
    void throwAtProps();
    void rotateBone(Character& c, const char* bone, glm::vec3 eulerDegrees);
    void defineInput();
    void readInput();
    void buildPanel();
    void select(int index);
    void ragdollAll(bool everyone);

    kke::Application* m_app = nullptr;
    kke::ModelModule* m_models = nullptr;
    std::string m_packDir;          // the asset folder (may hold several packs)
    kke::AssetCatalog m_catalog;
    std::unique_ptr<kke::Breakables> m_breakables;
    std::vector<std::pair<kke::ModelModule::InstanceId, std::string>> m_props; // every placed prop and its file name
    std::vector<glm::vec3> m_crates; // tops of the props that splinter: where "onto a crate" drops someone
    size_t m_nextCrate = 0;
    std::vector<std::string> m_searched;
    std::vector<Character> m_characters;
    float m_time = 0.0f;
    int m_selected = 3;
    int m_selectedBone = 0;
    bool m_showBones = false;
    size_t m_propCount = 0;
    std::vector<kke::Module*> m_enginePanels;
    bool m_showEnginePanels = false;
    int m_selectedIndex = 0; // the panel's view of m_selected
};

} // namespace kke_demo
