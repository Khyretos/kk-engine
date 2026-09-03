#pragma once

#include <volk.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace kke {

class VulkanDevice;

// Every knob a graphics pipeline in this engine might need to flip.
// Grid, particles, and the cube all go through this one config struct
// instead of each getting a bespoke Pipeline subclass — new draw styles
// (wireframe, skybox, UI-in-world, ...) are new configs, not new classes.
struct PipelineConfig {
    bool useVertexInput = true;          // false = shader synthesizes its own vertices (e.g. fullscreen tri, SSBO-driven particles)
    // If useVertexInput is true and this is left empty, the pipeline uses
    // kke::Vertex's layout (position+color, see Mesh.h) — the common case
    // for this engine's own meshes. Set both of these explicitly instead
    // when binding a *different* vertex format (e.g. RmlUi's own Vertex
    // struct in RmlVulkanRenderInterface) — they must describe the same
    // buffer layout the caller will actually bind.
    std::vector<VkVertexInputBindingDescription> customVertexBindings;
    std::vector<VkVertexInputAttributeDescription> customVertexAttributes;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE;
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    bool blendEnable = false;
    VkPushConstantRange pushConstantRange{}; // size == 0 => pipeline has no push constants
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts; // empty => no descriptor sets
};

// One graphics pipeline: shaders + fixed-function state for drawing
// geometry, configured by PipelineConfig above. This is the "lego piece"
// boundary — a new visual style is a new Pipeline instance with a new
// config, not a new code path in the renderer.
class Pipeline {
public:
    Pipeline(VulkanDevice& device, VkRenderPass renderPass,
              const std::string& vertPath, const std::string& fragPath,
              const PipelineConfig& config = {});
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    void bind(VkCommandBuffer cmd) const;
    VkPipelineLayout layout() const { return m_layout; }

private:
    VulkanDevice& m_device;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
};

} // namespace kke
