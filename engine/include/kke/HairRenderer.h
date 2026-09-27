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
// kilobytes); a compute pass (shaders/hair_points.comp) builds every drawn
// hair's points around them once, and the vertex shaders (hair.vert,
// hair_shadow.vert) make each a camera-facing ribbon. hair.frag shades it
// as a round fibre with two highlights. It casts shadows too.
//
//   HairRenderer hair(app);
//   hair.build(desc);                                    // once, and when the style changes
//   world.hairPositions(id, guides);                     // every frame, after world.step
//   hair.update(guides, headMatrix);
//   hair.compute(cmd);                                   // in Module::compute()
//   hair.draw(ctx);  hair.drawShadow(shadowCtx);         // in render() / renderShadow()
//
// While a frame may still draw it, destroy it through renderer().retire()
// (as the cloth demo does when it changes scene).
class HairRenderer {
public:
    explicit HairRenderer(Application& app);
    ~HairRenderer();
    HairRenderer(const HairRenderer&) = delete;
    HairRenderer& operator=(const HairRenderer&) = delete;

    void build(const HairDesc& desc);
    void update(const std::vector<glm::vec3>& guides, const glm::mat4& head);
    // Builds the hairs' points for this frame (before any render pass:
    // Module::compute). Nothing is drawn until it has run once.
    void compute(VkCommandBuffer cmd);
    // At most this share of the hairs drawn (0.05 .. 1, default 1), each
    // wider so the hair stays as full: for slower GPUs and phones. Far
    // away heads draw fewer by themselves (level of detail).
    void setDetail(float detail);
    float detail() const { return m_detail; }
    void draw(const RenderContext& ctx);
    void drawShadow(const ShadowRenderContext& ctx, const glm::vec3& towardsLight);
    size_t hairs() const { return m_strands.hairs(); }
    // Hairs the last draw() drew: fewer than hairs() when the head is small
    // on screen (level of detail, docs/HAIR.md).
    size_t drawnHairs() const;
    const HairStrands& strands() const { return m_strands; }

private:
    struct FrameData {
        std::unique_ptr<Buffer> buffer;
        size_t capacity = 0; // bytes
        uint64_t version = 0;
        VkDescriptorSet set = VK_NULL_HANDLE;
        bool bound = false;
    };
    // The descriptor set layout and pipelines, shared by every HairRenderer
    // of an application (compiling them per head took about 20 ms each).
    struct Shared;
    void prepare(uint32_t frame);
    uint32_t vertexCount() const;
    void chooseDetail(float pixel, const glm::vec3& camera);
    bool ready() const;

    Application& m_app;
    HairStrands m_strands;
    std::unique_ptr<Buffer> m_hairBuffer;
    std::unique_ptr<Buffer> m_points; // every hair's built points (hair_points.comp), one set shared by the frames
    bool m_built = false;             // compute() has run since build()
    uint32_t m_builtStride = 0;       // the level of detail it built
    float m_detail = 1.0f;
    float m_lastPixel = 0.0f;         // from the last draw(), for the next compute()
    glm::vec3 m_lastCamera{0.0f};
    std::vector<float> m_frameBytes; // header + guides, as uploaded
    std::vector<glm::vec4> m_guideFrames; // scratch: the guides' frames (HairStrands::frames)
    uint64_t m_version = 0;
    glm::vec3 m_centre{0.0f};      // the guides' bounds, from update()
    float m_radius = 0.0f;
    float m_strandLength = 0.0f;   // a guide's mean length
    uint32_t m_stride = 1;         // level of detail: every m_stride-th hair is drawn
    FrameData m_frames[Renderer::kMaxFramesInFlight];
    std::shared_ptr<Shared> m_shared;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
};

} // namespace kke
