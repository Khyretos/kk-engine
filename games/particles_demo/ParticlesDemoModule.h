#pragma once

#include "kke/Module.h"
#include "kke/ParticleEffects.h"
#include "kke/ParticleLibrary.h"
#include "kke/SphereImpostors.h"

#include <memory>
#include <string>
#include <vector>

namespace kke_particles {

// A gallery of kke::ParticleLibrary: pick an effect and it plays on the
// stone in the middle (bursts again every few seconds, streams keep
// going), or turn on "all at once" to see the whole library round a
// circle. Effects are data: assets/effects/*.yaml beside the game is read
// at start, so an effect can be tried by editing a file.
class ParticlesDemoModule : public kke::Module {
public:
    const char* name() const override { return "ParticlesDemo"; }
    void init(kke::Application& app) override;
    void shutdown() override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void renderTranslucent(const kke::RenderContext& ctx) override;

private:
    void defineInput();
    void readInput();
    void buildPanel();
    void restart();       // stops everything, starts what's chosen
    void playOne(size_t index, const glm::vec3& at);
    bool weather(size_t index) const; // rain, snow, leaves: fills an area from above

    kke::Application* m_app = nullptr;
    std::unique_ptr<kke::ParticleEffects> m_fx;
    std::unique_ptr<kke::ParticleLibrary> m_library;
    std::unique_ptr<kke::DynamicMeshRenderer> m_ground;
    std::vector<std::string> m_names;
    int m_current = 0;
    bool m_all = false;
    bool m_repeat = true;
    float m_every = 2.5f;            // s between bursts
    float m_scale = 1.0f;
    float m_wind = 0.0f;             // m/s, along x
    float m_clock = 0.0f;
    std::vector<glm::vec3> m_stones; // where effects play (one, or the circle)
    std::string m_loaded;            // what assets/effects added, for the panel
};

} // namespace kke_particles
