#pragma once

#include "kke/Module.h"
#include "kke/Pipeline.h"
#include "kke/Mesh.h"

#include <memory>

namespace kke_demo {

// This is the module to copy when starting a new gameplay system: it owns
// its own GPU resources (mesh + pipeline), updates its own state each
// frame, and draws itself — nothing about it is special-cased by the
// engine. See the README's "Adding a module" section for the walkthrough.
class CubeModule : public kke::Module {
public:
    const char* name() const override { return "Cube"; }

    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderUi() override;

private:
    std::unique_ptr<kke::Pipeline> m_pipeline;
    std::unique_ptr<kke::Mesh> m_mesh;

    bool m_spinning = true;
    float m_spinSpeedDegPerSec = 45.0f;
    float m_accumulatedAngle = 0.0f;
    glm::vec3 m_spinAxis{0.3f, 1.0f, 0.0f};
};

} // namespace kke_demo
