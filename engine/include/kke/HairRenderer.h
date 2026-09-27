#pragma once

#include "kke/Buffer.h"
#include "kke/HairStrands.h"
#include "kke/Module.h"
#include "kke/Pipeline.h"
#include "kke/Renderer.h"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace kke {

class Application;

// Draws a head of hair on the GPU (docs/HAIR.md): each frame only the
// simulated guide strands are uploaded (RigidWorld::hairPositions, a few
// kilobytes); the vertex shader (shaders/hair.vert) builds every drawn
// hair around them as a camera-facing ribbon, and hair.frag shades it as
// a round fibre with two highlights. It casts shadows too.
//
//   HairRenderer hair(app);
//   hair.build(desc);                                    // once, and when the style changes
//   world.hairPositions(id, guides);                     // every frame, after world.step
//   hair.update(guides, headMatrix);
//   hair.draw(ctx);  hair.drawShadow(shadowCtx);         // in render() / renderShadow()
class HairRenderer {
public:
    explicit HairRenderer(Application& app);
    ~HairRenderer();
    HairRenderer(const HairRenderer&) = delete;
    HairRenderer& operator=(const HairRenderer&) = delete;

    void build(const HairDesc& desc);
    void update(const std::vector<glm::vec3>& guides, const glm::mat4& head);
    void draw(const RenderContext& ctx);
    void drawShadow(const ShadowRenderContext& ctx, const glm::vec3& towardsLight);
    size_t hairs() const { return m_strands.hairs(); }
    const HairStrands& strands() const { return m_strands; }

private:
    struct FrameData {
        std::unique_ptr<Buffer> buffer;
        size_t capacity = 0; // bytes
        uint64_t version = 0;
        VkDescriptorSet set = VK_NULL_HANDLE;
        bool bound = false;
    };
    void createPipelines();
    void prepare(uint32_t frame);
    uint32_t vertexCount() const;

    Application& m_app;
    HairStrands m_strands;
    std::unique_ptr<Buffer> m_hairBuffer;
    std::vector<float> m_frameBytes; // header + guides, as uploaded
    uint64_t m_version = 0;
    FrameData m_frames[Renderer::kMaxFramesInFlight];
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    std::unique_ptr<Pipeline> m_pipeline, m_shadowPipeline;
};

} // namespace kke
