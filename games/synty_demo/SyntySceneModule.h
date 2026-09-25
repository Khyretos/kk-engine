#pragma once

#include "kke/Module.h"
#include "kke/modules/ModelModule.h"
#include "kke/Capabilities.h"
#include "kke/Ragdoll.h"
#include "kke/AssetCatalog.h"

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
    void renderUi() override;
    void onEvent(const SDL_Event& event) override; // B toggles the bone view

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
        // Ragdoll state (0 = standing, driven by animation/posing).
        kke::IRagdollPhysics::RagdollHandle ragdoll = 0;
        kke::RagdollDesc ragdollDesc;
        kke::RagdollSkinBinding binding;
        std::string behaviorBeforeRagdoll;
    };
    void ragdoll(Character& c, const glm::vec3& push);
    void standUp(Character& c);
    kke::IRagdollPhysics* m_physics = nullptr; // optional: whatever module offers ragdolls
    void throughGlass(Character& c); // FEMFX build only: see the .cpp
    kke::ModelModule::ModelId load(const std::string& relative);
    void place(const std::string& relative, glm::vec3 position, float yawDegrees = 0.0f, glm::vec3 scale = glm::vec3(1.0f));
    void rotateBone(Character& c, const char* bone, glm::vec3 eulerDegrees);

    kke::Application* m_app = nullptr;
    kke::ModelModule* m_models = nullptr;
    std::string m_packDir;          // the asset folder (may hold several packs)
    kke::AssetCatalog m_catalog;
    std::vector<std::string> m_searched;
    std::vector<Character> m_characters;
    float m_time = 0.0f;
    int m_selected = 3;
    int m_selectedBone = 0;
    std::vector<glm::vec3> m_poseEuler; // per bone of the posed character
    bool m_showBones = false;
    size_t m_propCount = 0;
    std::vector<kke::Module*> m_enginePanels;
    bool m_showEnginePanels = false;
};

} // namespace kke_demo
