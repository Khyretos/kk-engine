#pragma once

#include "engine/Buffer.h"

#include <volk.h>
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <cstdint>
#include <memory>

namespace engine {

class VulkanDevice;

struct Vertex {
    glm::vec3 position;
    glm::vec3 color;

    static VkVertexInputBindingDescription bindingDescription();
    static std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions();
};

// A GPU-resident mesh: vertex + index buffer, ready to bind and draw.
class Mesh {
public:
    Mesh(VulkanDevice& device, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    void bind(VkCommandBuffer cmd) const;
    void draw(VkCommandBuffer cmd) const;

    static Mesh createCube(VulkanDevice& device);

private:
    std::unique_ptr<Buffer> m_vertexBuffer;
    std::unique_ptr<Buffer> m_indexBuffer;
    uint32_t m_indexCount = 0;
};

} // namespace engine
