#pragma once

#include "kke/Module.h"
#include "kke/Pipeline.h"
#include "kke/Buffer.h"

#include <memory>
#include <cstdint>

namespace kke {

// A GPU compute-driven particle system: particle state (position, velocity,
// life) lives entirely in a storage buffer on the GPU. Every frame, a
// compute shader integrates and respawns particles in place; a graphics
// pipeline then reads that same buffer to draw them as point sprites. The
// CPU never touches per-particle data — it only issues the dispatch, a
// barrier, and a draw call.
//
// This is the most complex module in the demo on purpose: it's the
// reference for "how do I get a compute shader talking to a graphics
// pipeline through a shared buffer" in this engine, which is the same
// pattern most non-trivial GPU-driven systems need (skinning, culling,
// destruction fragments, cloth, etc).
class ParticleModule : public Module {
public:
    explicit ParticleModule(uint32_t particleCount = 20000);
    ~ParticleModule() override;

    const char* name() const override { return "Particles"; }

    void init(Application& app) override;
    void update(const UpdateContext& ctx) override;
    void compute(VkCommandBuffer cmd) override;
    void render(const RenderContext& ctx) override;
    void renderUi() override;
    void shutdown() override;

private:
    void createBuffer();
    void createDescriptors();
    void createComputePipeline();

    VulkanDevice* m_device = nullptr;
    uint32_t m_particleCount;

    std::unique_ptr<Buffer> m_particleBuffer;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    VkPipelineLayout m_computeLayout = VK_NULL_HANDLE;
    VkPipeline m_computePipeline = VK_NULL_HANDLE;

    std::unique_ptr<Pipeline> m_renderPipeline;

    float m_totalTime = 0.0f;
    float m_lastDt = 0.0f;
    bool m_paused = false;
};

} // namespace kke
