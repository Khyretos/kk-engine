#pragma once

#include "kke/Module.h"
#include "kke/Mesh.h"
#include "kke/Pipeline.h"

#include <memory>
#include <vector>

namespace kke_demo {

// A small lit scene behind the UI showcase — a floor and a ring of slowly
// turning blocks with different metallic/roughness values — so the
// settings screen's shadows, brightness and field-of-view changes have
// something visible to act on. Deliberately simple; the Synty-asset
// scene work replaces blocks with real props.
class BackdropModule : public kke::Module {
public:
    const char* name() const override { return "Backdrop"; }
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;

private:
    struct Block { glm::vec3 position; glm::vec3 scale; float metallic, roughness, spin; };
    std::unique_ptr<kke::Mesh> makeBox(kke::Application& app, glm::vec3 color);
    glm::mat4 blockMatrix(const Block& b) const;

    std::unique_ptr<kke::Pipeline> m_pipeline, m_shadowPipeline;
    std::vector<std::unique_ptr<kke::Mesh>> m_meshes; // [0] floor, then one per block
    std::vector<Block> m_blocks;
    float m_time = 0.0f;
};

} // namespace kke_demo
