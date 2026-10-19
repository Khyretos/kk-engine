#include "kke/Mesh.h"
#include "kke/VulkanDevice.h"
#include "kke/rhi/VulkanDevice.h"

namespace kke {

rhi::VertexBinding Vertex::bindingDescription() { return { 0, sizeof(Vertex), false }; }

std::array<rhi::VertexAttribute, 4> Vertex::attributeDescriptions() {
    return { { { 0, 0, rhi::VertexFormat::Float3, static_cast<uint32_t>(offsetof(Vertex, position)) },
               { 1, 0, rhi::VertexFormat::Float3, static_cast<uint32_t>(offsetof(Vertex, color)) },
               { 2, 0, rhi::VertexFormat::Float3, static_cast<uint32_t>(offsetof(Vertex, normal)) },
               { 3, 0, rhi::VertexFormat::Float2, static_cast<uint32_t>(offsetof(Vertex, uv)) } } };
}

Mesh::Mesh(VulkanDevice& device, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
    : m_indexCount(static_cast<uint32_t>(indices.size())) {
    rhi::VulkanGpuDevice gpu(device);
    m_vertexBuffer = gpu.createBuffer({ sizeof(Vertex) * vertices.size(), rhi::kVertexBuffer, rhi::Memory::Device }, vertices.data());
    m_indexBuffer = gpu.createBuffer({ sizeof(uint32_t) * indices.size(), rhi::kIndexBuffer, rhi::Memory::Device }, indices.data());
}

Mesh::Mesh(VulkanDevice& device, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, Memory memory)
    : m_indexCount(static_cast<uint32_t>(indices.size())) {
    if (memory == Memory::DeviceLocal) {
        *this = Mesh(device, vertices, indices);
        return;
    }
    rhi::VulkanGpuDevice gpu(device);
    m_vertexBuffer = gpu.createBuffer({ sizeof(Vertex) * vertices.size(), rhi::kVertexBuffer, rhi::Memory::Upload }, vertices.data());
    m_indexBuffer = gpu.createBuffer({ sizeof(uint32_t) * indices.size(), rhi::kIndexBuffer, rhi::Memory::Upload }, indices.data());
}

void Mesh::bind(VkCommandBuffer cmd) const {
    VkBuffer buffers[] = { rhi::VulkanBuffer::handle(*m_vertexBuffer) };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, rhi::VulkanBuffer::handle(*m_indexBuffer), 0, VK_INDEX_TYPE_UINT32);
}

void Mesh::draw(VkCommandBuffer cmd) const {
    vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
}

void Mesh::drawInstanced(VkCommandBuffer cmd, uint32_t instances, uint32_t firstInstance) const {
    vkCmdDrawIndexed(cmd, m_indexCount, instances, 0, 0, firstInstance);
}

void Mesh::bind(rhi::CommandList& list) const {
    list.bindVertexBuffer(0, m_vertexBuffer->ref(), 0);
    list.bindIndexBuffer(m_indexBuffer->ref(), 0, rhi::IndexType::U32);
}

void Mesh::draw(rhi::CommandList& list) const { list.drawIndexed(m_indexCount, 1, 0, 0, 0); }

void Mesh::drawInstanced(rhi::CommandList& list, uint32_t instances, uint32_t firstInstance) const {
    list.drawIndexed(m_indexCount, instances, 0, 0, firstInstance);
}

Mesh Mesh::createCube(VulkanDevice& device) {
    // Each face gets its own 4 vertices (not shared) so every face can have a
    // distinct flat color — makes it immediately obvious on screen that this
    // is a cube rotating in 3D, not a flat quad. Not sharing vertices per-face
    // also means each face gets its own exact outward normal below, rather
    // than needing to average normals at shared corners the way a smooth-
    // shaded mesh would — the right approach for a cube, where every face is
    // meant to look flat, not rounded.
    const glm::vec3 red{1.0f, 0.2f, 0.2f};
    const glm::vec3 green{0.2f, 1.0f, 0.2f};
    const glm::vec3 blue{0.2f, 0.2f, 1.0f};
    const glm::vec3 yellow{1.0f, 1.0f, 0.2f};
    const glm::vec3 cyan{0.2f, 1.0f, 1.0f};
    const glm::vec3 magenta{1.0f, 0.2f, 1.0f};

    const glm::vec3 nPosZ{0.0f, 0.0f, 1.0f};
    const glm::vec3 nNegZ{0.0f, 0.0f, -1.0f};
    const glm::vec3 nPosX{1.0f, 0.0f, 0.0f};
    const glm::vec3 nNegX{-1.0f, 0.0f, 0.0f};
    const glm::vec3 nPosY{0.0f, 1.0f, 0.0f};
    const glm::vec3 nNegY{0.0f, -1.0f, 0.0f};

    std::vector<Vertex> vertices = {
        // +Z (front) - red
        {{-0.5f, -0.5f,  0.5f}, red, nPosZ, {0.0f, 0.0f}}, {{0.5f, -0.5f,  0.5f}, red, nPosZ, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, red, nPosZ, {1.0f, 1.0f}}, {{-0.5f, 0.5f,  0.5f}, red, nPosZ, {0.0f, 1.0f}},
        // -Z (back) - green
        {{ 0.5f, -0.5f, -0.5f}, green, nNegZ, {0.0f, 0.0f}}, {{-0.5f, -0.5f, -0.5f}, green, nNegZ, {1.0f, 0.0f}},
        {{-0.5f,  0.5f, -0.5f}, green, nNegZ, {1.0f, 1.0f}}, {{ 0.5f,  0.5f, -0.5f}, green, nNegZ, {0.0f, 1.0f}},
        // +X (right) - blue
        {{0.5f, -0.5f,  0.5f}, blue, nPosX, {0.0f, 0.0f}}, {{0.5f, -0.5f, -0.5f}, blue, nPosX, {1.0f, 0.0f}},
        {{0.5f,  0.5f, -0.5f}, blue, nPosX, {1.0f, 1.0f}}, {{0.5f,  0.5f,  0.5f}, blue, nPosX, {0.0f, 1.0f}},
        // -X (left) - yellow
        {{-0.5f, -0.5f, -0.5f}, yellow, nNegX, {0.0f, 0.0f}}, {{-0.5f, -0.5f,  0.5f}, yellow, nNegX, {1.0f, 0.0f}},
        {{-0.5f,  0.5f,  0.5f}, yellow, nNegX, {1.0f, 1.0f}}, {{-0.5f,  0.5f, -0.5f}, yellow, nNegX, {0.0f, 1.0f}},
        // +Y (top) - cyan
        {{-0.5f, 0.5f,  0.5f}, cyan, nPosY, {0.0f, 0.0f}}, {{0.5f, 0.5f,  0.5f}, cyan, nPosY, {1.0f, 0.0f}},
        {{ 0.5f, 0.5f, -0.5f}, cyan, nPosY, {1.0f, 1.0f}}, {{-0.5f, 0.5f, -0.5f}, cyan, nPosY, {0.0f, 1.0f}},
        // -Y (bottom) - magenta
        {{-0.5f, -0.5f, -0.5f}, magenta, nNegY, {0.0f, 0.0f}}, {{0.5f, -0.5f, -0.5f}, magenta, nNegY, {1.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, magenta, nNegY, {1.0f, 1.0f}}, {{-0.5f, -0.5f,  0.5f}, magenta, nNegY, {0.0f, 1.0f}},
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
