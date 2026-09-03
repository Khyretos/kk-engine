#include "kke/Mesh.h"
#include "kke/VulkanDevice.h"

namespace kke {

VkVertexInputBindingDescription Vertex::bindingDescription() {
    VkVertexInputBindingDescription desc{};
    desc.binding = 0;
    desc.stride = sizeof(Vertex);
    desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return desc;
}

std::array<VkVertexInputAttributeDescription, 2> Vertex::attributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 2> attrs{};
    attrs[0].binding = 0;
    attrs[0].location = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(Vertex, position);

    attrs[1].binding = 0;
    attrs[1].location = 1;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(Vertex, color);

    return attrs;
}

Mesh::Mesh(VulkanDevice& device, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
    : m_indexCount(static_cast<uint32_t>(indices.size())) {
    m_vertexBuffer = std::make_unique<Buffer>(
        Buffer::createDeviceLocal(device, vertices.data(), sizeof(Vertex) * vertices.size(),
                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
    m_indexBuffer = std::make_unique<Buffer>(
        Buffer::createDeviceLocal(device, indices.data(), sizeof(uint32_t) * indices.size(),
                                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
}

void Mesh::bind(VkCommandBuffer cmd) const {
    VkBuffer buffers[] = { m_vertexBuffer->handle() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
}

void Mesh::draw(VkCommandBuffer cmd) const {
    vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
}

Mesh Mesh::createCube(VulkanDevice& device) {
    // Each face gets its own 4 vertices (not shared) so every face can have a
    // distinct flat color — makes it immediately obvious on screen that this
    // is a cube rotating in 3D, not a flat quad.
    const glm::vec3 red{1.0f, 0.2f, 0.2f};
    const glm::vec3 green{0.2f, 1.0f, 0.2f};
    const glm::vec3 blue{0.2f, 0.2f, 1.0f};
    const glm::vec3 yellow{1.0f, 1.0f, 0.2f};
    const glm::vec3 cyan{0.2f, 1.0f, 1.0f};
    const glm::vec3 magenta{1.0f, 0.2f, 1.0f};

    std::vector<Vertex> vertices = {
        // +Z (front) - red
        {{-0.5f, -0.5f,  0.5f}, red}, {{0.5f, -0.5f,  0.5f}, red},
        {{ 0.5f,  0.5f,  0.5f}, red}, {{-0.5f, 0.5f,  0.5f}, red},
        // -Z (back) - green
        {{ 0.5f, -0.5f, -0.5f}, green}, {{-0.5f, -0.5f, -0.5f}, green},
        {{-0.5f,  0.5f, -0.5f}, green}, {{ 0.5f,  0.5f, -0.5f}, green},
        // +X (right) - blue
        {{0.5f, -0.5f,  0.5f}, blue}, {{0.5f, -0.5f, -0.5f}, blue},
        {{0.5f,  0.5f, -0.5f}, blue}, {{0.5f,  0.5f,  0.5f}, blue},
        // -X (left) - yellow
        {{-0.5f, -0.5f, -0.5f}, yellow}, {{-0.5f, -0.5f,  0.5f}, yellow},
        {{-0.5f,  0.5f,  0.5f}, yellow}, {{-0.5f,  0.5f, -0.5f}, yellow},
        // +Y (top) - cyan
        {{-0.5f, 0.5f,  0.5f}, cyan}, {{0.5f, 0.5f,  0.5f}, cyan},
        {{ 0.5f, 0.5f, -0.5f}, cyan}, {{-0.5f, 0.5f, -0.5f}, cyan},
        // -Y (bottom) - magenta
        {{-0.5f, -0.5f, -0.5f}, magenta}, {{0.5f, -0.5f, -0.5f}, magenta},
        {{ 0.5f, -0.5f,  0.5f}, magenta}, {{-0.5f, -0.5f,  0.5f}, magenta},
    };

    std::vector<uint32_t> indices;
    for (uint32_t face = 0; face < 6; ++face) {
        uint32_t base = face * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
        indices.push_back(base + 0);
    }

    return Mesh(device, vertices, indices);
}

} // namespace kke
