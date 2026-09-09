#pragma once

#include "kke/Buffer.h"

#include <volk.h>
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <cstdint>
#include <memory>

namespace kke {

class VulkanDevice;

struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec3 normal;
    // Appended, not inserted -- keeps every existing struct-initializer
    // call site (PhysicsModule, DestructionModule) valid without
    // needing every one updated just to add this field. Defaults to
    // (0,0) via each Vertex's own default member initializer below,
    // meaning code that doesn't set it explicitly gets a harmless,
    // well-defined UV rather than uninitialized memory -- correct for
    // objects sampling a flat, single-color material texture (see
    // kke::Application's own default white texture), where the exact
    // UV value genuinely doesn't matter.
    glm::vec2 uv{0.0f, 0.0f};

    static VkVertexInputBindingDescription bindingDescription();
    static std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions();
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

} // namespace kke
