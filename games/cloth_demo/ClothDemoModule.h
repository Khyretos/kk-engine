#pragma once

#include "kke/Cloth.h"
#include "kke/HairRenderer.h"
#include "kke/Module.h"
#include "kke/RigidWorld.h"
#include "kke/SphereImpostors.h"

#include <memory>
#include <string>
#include <vector>

namespace kke {
class OrbitCameraModule;
}

namespace kke_cloth {

// Cloth and hair (kke/Cloth.h, kke/Hair.h, docs/CLOTH.md, docs/HAIR.md),
// six scenes (1-6, Tab, or KKE_CLOTH_SCENE=fabrics|bed|nets|cape|stress|hair):
//  - Fabrics: silk, cotton, denim, wool, leather and satin, each dropped
//    on a little table and hanging as a banner in gusty wind.
//  - Bed: a wool blanket, a silk sheet and a denim throw dropped on a bed:
//    cloth on cloth, folding on itself, never through itself.
//  - Nets: a hammock catching balls and a net stopping shots.
//  - Cape: a runner with a cape (skinned back-stops keep it off the body)
//    running through a curtain.
//  - Stress: N sheets over balls (KKE_CLOTH_COUNT, KKE_CLOTH_RES) to
//    measure what cloth costs.
//  - Hair: a hairdresser's catalog on heads turning and nodding in the
//    wind, a page at a time (KKE_HAIR_SHOW=types34|styles|types12|classic):
//    every Andre Walker type 1A to 4C, and hairstyles (afro, puff, high-top
//    fade, twist-out, bantu knots); KKE_HAIR_GUIDES and KKE_HAIR_PER_GUIDE
//    set the cost, KKE_HAIR_STYLES=4c,afro picks the heads.
// KKE_CLOTH_PROTECTION=full|basic|off, KKE_CLOTH_TOUR=1 (cycles the scenes;
// on by itself under KKE_BENCHMARK), KKE_CLOTH_WIND=<m/s>.
class ClothDemoModule : public kke::Module {
public:
    const char* name() const override { return "ClothDemo"; }
    void init(kke::Application& app) override;
    void fixedUpdate(const kke::FixedUpdateContext& ctx) override;
    void update(const kke::UpdateContext& ctx) override;
    void compute(VkCommandBuffer cmd) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;

    enum class Scene { Fabrics, Bed, Nets, Cape, Stress, Hair };

private:
    struct Piece {
        kke::RigidWorld::ClothId id = 0;
        kke::Fabric fabric;
        kke::ClothMesh mesh;
        std::string label;
        std::unique_ptr<kke::DynamicMeshRenderer> mesh3d;
        std::vector<glm::vec3> pos, nrm;
        std::vector<kke::Vertex> verts;
        std::vector<uint32_t> idx; // both sides for nets' threads
    };
    struct Solid {
        std::unique_ptr<kke::DynamicMeshRenderer> mesh;
        float roughness = 0.7f;
    };
    struct Ball {
        kke::RigidWorld::BodyId body;
        float radius;
        glm::vec3 color;
        float age = 0.0f;
    };
    struct Runner {
        std::vector<kke::RigidWorld::BodyId> proxies; // cloth-only capsules: torso, 2 legs, 2 arms
        float angle = 0.0f, phase = 0.0f, speed = 3.0f, radius = 3.0f;
        glm::mat4 torso{1.0f};
        std::unique_ptr<kke::DynamicMeshRenderer> body;
    };

    struct Head {
        kke::RigidWorld::BodyId collider{};  // the head, a sphere only cloth and hair feel
        kke::RigidWorld::HairId hair = 0;
        glm::vec3 neck{0.0f};                // it turns about here
        glm::mat4 bind{1.0f}, now{1.0f};
        float phase = 0.0f;
        std::string label;
        std::vector<glm::vec3> guides;
        std::unique_ptr<kke::HairRenderer> drawn;
        std::unique_ptr<kke::DynamicMeshRenderer> mesh; // bind pose; drawn with now * inverse(bind)
    };

    void setScene(Scene s);
    void clear();
    Piece& addCloth(const kke::ClothDesc& desc, const std::string& label);
    void addSolidBox(const glm::vec3& centre, const glm::vec3& half, const glm::vec3& color, float roughness = 0.7f, bool collide = true);
    void addSolidSphere(const glm::vec3& centre, float radius, const glm::vec3& color, float roughness = 0.5f);
    void dropBall(const glm::vec3& at, const glm::vec3& velocity, float radius, const glm::vec3& color);
    void buildFabrics();
    void buildBed();
    void buildNets();
    void buildCape();
    void buildStress();
    void buildHair();
    void stepHeads(float dt);
    void redrop();
    void stepRunner(float dt);
    void updateMeshes(const glm::vec3& cameraPos);
    void applyProtection();
    void defineInput();
    void readInput();
    void buildPanel();

    kke::Application* m_app = nullptr;
    kke::OrbitCameraModule* m_camera = nullptr;
    std::unique_ptr<kke::RigidWorld> m_world;
    std::shared_ptr<kke::ClothGpu> m_gpu; // null: the CPU searches
    std::unique_ptr<kke::SphereImpostorRenderer> m_spheres;
    std::unique_ptr<kke::DynamicMeshRenderer> m_floor;
    std::vector<std::unique_ptr<Piece>> m_cloth;
    std::vector<Solid> m_solids;
    std::vector<Ball> m_balls;
    std::unique_ptr<Runner> m_runner;
    std::vector<std::unique_ptr<Head>> m_heads;
    int m_hairGuides = 160, m_hairsPerGuide = 0; // 0 = as many as the style draws
    int m_hairShow = 0;                          // the catalog's page (kHairPages)
    float m_headMotion = 1.0f;
    float m_hairMotion = 1.0f;                   // RigidWorld::setHairMotion: 1 natural .. 0 solid
    float m_hairDetail = 1.0f;                   // HairRenderer::setDetail: the share of hairs drawn
    void applyHairSettings();
    double m_hairUploadMs = 0.0;
    Scene m_scene = Scene::Fabrics;
    kke::ClothProtection m_protection = kke::ClothProtection::Full;

    float m_time = 0.0f, m_sceneTime = 0.0f, m_ballTimer = 0.0f;
    float m_windSpeed = 4.0f, m_windYaw = 0.35f;
    bool m_gusts = true, m_tour = false, m_rain = true, m_showThreads = true;
    int m_stressCount = 16, m_stressRes = 24;
    uint32_t m_rng = 0x2545f491u;
    std::vector<kke::SphereImpostorRenderer::Sphere> m_sphereScratch;
    int m_sceneIndex = 0, m_protectionIndex = 2; // the panel's choices
    float m_windDegrees = 20.0f;
    // Measured, smoothed: whole physics step, the protection pass, the mesh rebuild.
    double m_stepMs = 0.0, m_protectMs = 0.0, m_meshMs = 0.0;
    float m_pixelAngle = 0.001f; // radians per pixel (thread width)
};

} // namespace kke_cloth
