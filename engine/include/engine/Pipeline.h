#pragma once

#include <volk.h>
#include <glm/glm.hpp>
#include <string>

namespace engine {

class VulkanDevice;

struct PushConstants {
    glm::mat4 mvp;
};

// One graphics pipeline: shaders + fixed-function state for drawing colored,
// depth-tested geometry. More pipelines (wireframe, skybox, UI...) become
// more instances of this class later — that's the "lego piece" boundary.
class Pipeline {
public:
    Pipeline(VulkanDevice& device, VkRenderPass renderPass,
              const std::string& vertPath, const std::string& fragPath);
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    void bind(VkCommandBuffer cmd) const;
    VkPipelineLayout layout() const { return m_layout; }

private:
    VkShaderModule loadShaderModule(const std::string& path) const;

    VulkanDevice& m_device;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
};

} // namespace engine
