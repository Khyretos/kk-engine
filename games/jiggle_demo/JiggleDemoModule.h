#pragma once

#include "kke/Animator.h"
#include "kke/JigglePhysics.h"
#include "kke/Module.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/ModelModule.h"

#include <memory>
#include <string>
#include <vector>

namespace kke {
class OrbitCameraModule;
}

namespace kke_jiggle {

// Jiggle physics, two scenes (Tab switches, or KKE_JIGGLE_SCENE=jelly|body):
//  - Jelly: a strawberry jelly on a plate (kke::JellyBody) with balls
//    raining onto it; they dent it, it wobbles, and it throws them back.
//  - Body: a Synty character given extra curves and soft-tissue bones
//    (kke::addHumanoidSoftTissue), jogging, jumping and stopping on UAL
//    clips, jiggling through kke::JiggleRig + kke::JiggleSkin. A twin
//    without jiggle can run alongside for comparison.
class JiggleDemoModule : public kke::Module {
public:
    const char* name() const override { return "JiggleDemo"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void renderUi() override;
    void onEvent(const SDL_Event& event) override;

private:
    enum class Scene { Jelly, Body };
    enum class Move { Idle, Walk, Jog, Sprint, Tour };
    struct Dancer {
        kke::ModelModule::InstanceId instance = 0;
        bool jiggle = false;
        glm::vec3 centre{ 0.0f }; // of its running circle
        kke::JiggleRig rig;
        kke::JiggleSkin skin;
    };

    void setScene(Scene s);
    void resetJelly();
    void dropBall(float radius, float height);
    void updateJelly(float dt);
    void setupBodies();
    void updateBodies(float dt);
    void jump();

    kke::Application* m_app = nullptr;
    kke::ModelModule* m_models = nullptr;
    kke::OrbitCameraModule* m_camera = nullptr;
    Scene m_scene = Scene::Jelly;

    // --- jelly
    std::unique_ptr<kke::DynamicMeshRenderer> m_jellyMesh, m_plate, m_floor;
    std::unique_ptr<kke::SphereImpostorRenderer> m_spheres;
    kke::JellyBody m_jelly;
    std::vector<kke::JellyBody::Ball> m_balls;
    std::vector<glm::vec3> m_ballColors;
    std::vector<kke::SphereImpostorRenderer::Sphere> m_sphereScratch;
    bool m_rain = true;
    float m_rainTimer = 0.0f;
    float m_rainInterval = 0.7f;
    uint32_t m_rng = 0x9e3779b9u;
    double m_jellyMs = 0.0;
    int m_colorIndex = 0;
    int m_look = 0;
    float m_density = 1.0f, m_milkiness = 0.0f;
    bool m_translucent = true, m_fruit = true;

    // --- bodies
    bool m_bodiesReady = false;
    std::string m_bodyStatus;
    std::string m_characterName;
    kke::ModelData m_rig; // bones + retargeted clips of the reshaped character
    std::unique_ptr<kke::AnimationSet> m_animSet;
    std::unique_ptr<kke::Animator> m_anim;
    int m_stMove = -1, m_stJumpStart = -1, m_stJumpLoop = -1, m_stLand = -1;
    std::vector<Dancer> m_dancers;
    Move m_move = Move::Tour;
    float m_speed = 0.0f, m_angle = 0.0f;
    float m_jumpY = 0.0f, m_jumpV = 0.0f;
    bool m_airborne = false;
    float m_tourTime = 0.0f;
    float m_radius = 1.3f;
    bool m_showPoints = false;
    bool m_showTwin = false;
    bool m_follow = true;
    bool m_sideView = true;
    double m_jiggleUs = 0.0;
    float m_swing = 0.0f, m_stretchNow = 0.0f;
    kke::HumanoidSoftTissue m_tissue;
    size_t m_bones = 0, m_zones = 0;
};

} // namespace kke_jiggle
