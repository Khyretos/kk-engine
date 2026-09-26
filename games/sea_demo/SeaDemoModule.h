#pragma once

#include "kke/FloatingBodies.h"
#include "kke/Module.h"
#include "kke/Ocean.h"
#include "kke/OceanRenderer.h"
#include "kke/SphereImpostors.h"

#include <memory>
#include <string>
#include <vector>

namespace kke_sea {

// A boat on an open sea. Drive it (arrow keys), throw things in (left
// click) and watch density decide: foam and wood ride high, ice floats low,
// barrels bob, iron sinks. Waves come from the wind sliders. Built from
// kke::OceanWaves (the sea), kke::FloatingBodies (buoyancy), and
// kke::OceanRenderer / SphereImpostorRenderer / DynamicMeshRenderer.
class SeaDemoModule : public kke::Module {
public:
    const char* name() const override { return "SeaDemo"; }
    void init(kke::Application& app) override;
    void fixedUpdate(const kke::FixedUpdateContext& ctx) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void renderUi() override;
    void onEvent(const SDL_Event& event) override;

    struct Kind { const char* name; float density; glm::vec3 halfExtents; glm::vec3 color; };

private:
    void reset();
    void throwObject(int kind);
    void splash(const glm::vec3& at, float strength);

    kke::Application* m_app = nullptr;
    kke::OceanWaves m_waves;
    kke::FloatingBodies m_bodies;
    std::unique_ptr<kke::OceanRenderer> m_ocean;
    std::unique_ptr<kke::SphereImpostorRenderer> m_spheres;
    std::unique_ptr<kke::DynamicMeshRenderer> m_boatMesh;
    std::vector<std::unique_ptr<kke::DynamicMeshRenderer>> m_kindMeshes; // unit cube per kind colour
    std::vector<int> m_kindOf;           // per body: -1 = boat
    size_t m_boat = 0;

    struct Spray { glm::vec3 pos, vel; float life; };
    std::vector<Spray> m_spray;
    std::vector<kke::SphereImpostorRenderer::Sphere> m_sphereScratch;

    float m_time = 0.0f;
    float m_windSpeed = 7.0f, m_windDir = 0.4f, m_chop = 0.6f;
    int m_kind = 1;
    bool m_followBoat = true;
    float m_throttle = 0.0f, m_rudder = 0.0f;
    uint32_t m_rng = 777;
};

} // namespace kke_sea
