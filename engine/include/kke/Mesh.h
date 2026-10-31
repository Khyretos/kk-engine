#pragma once

#include "kke/rhi/Device.h"
#include "kke/rhi/PipelineState.h"

#include <volk.h>
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <cstdint>
#include <memory>

namespace kke {

class VulkanDevice;
namespace rhi {
class CommandList;
}

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

    static rhi::VertexBinding bindingDescription();
    static std::array<rhi::VertexAttribute, 4> attributeDescriptions();
};

// A GPU-resident mesh: vertex + index buffer, ready to bind and draw.
class Mesh {
public:
    // Device-local buffers, copied through a staging buffer (the upload
    // waits for the GPU queue to be idle: fine at load time, a hitch mid-game).
    Mesh(VulkanDevice& device, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    // Host-visible buffers written directly: no copy and no wait, a little
    // slower to draw. For short-lived meshes made while the game runs
    // (thumbnails, previews).
    enum class Memory { DeviceLocal, HostVisible };
    Mesh(VulkanDevice& device, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, Memory memory);

    void bind(VkCommandBuffer cmd) const;
    void draw(VkCommandBuffer cmd) const;
    // The same through the RHI (docs/plans/rhi.md).
    void bind(rhi::CommandList& list) const;
    void draw(rhi::CommandList& list) const;
    void drawInstanced(rhi::CommandList& list, uint32_t instances, uint32_t firstInstance = 0) const;
    // `instances` copies in one call (per-instance data bound by the
    // caller at binding 1, see ModelModule's instanced pipeline).
    void drawInstanced(VkCommandBuffer cmd, uint32_t instances, uint32_t firstInstance = 0) const;

    // The vertex buffer, for a draw with another index list over the same vertices (a level of detail, kke/LodChain.h).
    const rhi::Buffer& vertexBuffer() const { return *m_vertexBuffer; }

    static Mesh createCube(VulkanDevice& device);

private:
    std::unique_ptr<rhi::Buffer> m_vertexBuffer;
    std::unique_ptr<rhi::Buffer> m_indexBuffer;
    uint32_t m_indexCount = 0;
};

} // namespace kke
