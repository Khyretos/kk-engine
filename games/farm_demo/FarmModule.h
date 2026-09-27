#pragma once

#include "kke/AssetCatalog.h"
#include "kke/CameraRig.h"
#include "kke/Module.h"
#include "kke/SceneFile.h"
#include "kke/SceneLoader.h"
#include "kke/SphereImpostors.h"
#include "kke/ai/AiWorld.h"
#include "kke/ai/NavMesh.h"
#include "kke/modules/ModelModule.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kke {
class InputModule;
}

namespace farm {

// The wildlife farm (README.md): you are a dog on a farm, and every
// animal lives its own life on the AI core (kke::ai, docs/AI.md). Sheep
// graze in their meadow and bolt as a flock when you run at them, cows
// come over to have a look, pigs root in their pen, horses spook, and a
// fox in the woods keeps away from you. Barking (Space) is a noise every
// animal in range hears. Fences, the barn and the trees are obstacles on
// a navmesh built from the scene itself (scenes/farm.scene.json).
class FarmModule : public kke::Module {
public:
    const char* name() const override { return "Farm"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void renderUi() override;
    void onEvent(const SDL_Event& event) override;

private:
    // How one kind of animal looks: which model, how big, which clips.
    struct Look {
        kke::ModelModule::ModelId model = 0;
        float scale = 1.0f;
        float yawOffset = 0.0f;          // model's own facing vs +Z
        std::map<std::string, int> clips; // ai anim name -> clip index
        std::map<std::string, float> clipSpeed;
    };
    struct Animal {
        kke::ai::AgentId id = 0;
        std::string species;
        kke::ModelModule::InstanceId instance = 0;
        std::string playing;
    };

    bool loadLevel();
    void buildNavMesh();
    void setupAi();
    bool makeLook(const std::string& species, Look& look);
    kke::ModelModule::ModelId loadDog(const std::string& breed);
    void spawnAnimal(const std::string& species, const glm::vec3& at, float yaw);
    void updatePlayer(float dt);
    void updateCamera(float dt);
    void showAnim(kke::ModelModule::InstanceId instance, const Look& look, const std::string& anim, std::string& playing);

    kke::Application* m_app = nullptr;
    kke::ModelModule* m_models = nullptr;
    kke::InputModule* m_input = nullptr;
    std::string m_status;

    kke::AssetCatalog m_catalog;
    std::string m_packDir;
    kke::SceneFile m_scene;
    kke::LoadedScene m_loaded;
    std::unique_ptr<kke::DynamicMeshRenderer> m_ground;

    kke::ai::NavMesh m_nav;
    std::string m_navStatus;
    kke::ai::AiWorld m_ai{ 2026 };
    std::map<std::string, Look> m_looks;
    std::vector<Animal> m_animals;
    uint32_t m_nextId = 100;

    // The player's dog.
    static constexpr kke::ai::AgentId kPlayer = 1;
    kke::ModelModule::InstanceId m_dog = 0;
    Look m_dogLook;
    std::string m_dogPlaying;
    glm::vec3 m_pos{0.0f}, m_vel{0.0f};
    float m_yaw = 180.0f;
    float m_barkTimer = 0.0f;
    kke::CameraRig m_rig;
    bool m_captured = false;
    float m_mouseSensitivity = 0.15f, m_stickSpeed = 140.0f;

    // Autopilot (KKE_FARM_AUTOPILOT=1): the dog runs a lap through the
    // meadow for headless screenshots and checks.
    bool m_autopilot = false;
    float m_time = 0.0f;
    bool m_lineup = false; // KKE_FARM_LINEUP=1: every kind in a row, facing +X
    bool m_showDebug = false;
    bool m_showNav = false;
    std::unique_ptr<kke::DynamicMeshRenderer> m_navMesh;
    std::vector<std::string> m_log; // recent events, newest last
};

} // namespace farm
