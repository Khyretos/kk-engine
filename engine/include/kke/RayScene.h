#pragma once

#include "kke/Buffer.h"

#include <glm/glm.hpp>
#include <volk.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kke {

class VulkanDevice;

// What inline ray queries trace against (ENG-F02, VK_KHR_ray_query): triangle
// geometry as bottom-level acceleration structures, built once, and each
// frame's instances of them in a top-level structure. The engine has no
// scene graph, so whoever draws adds what rays should see: full meshes
// (classic ray tracing) or simple stand-ins for them (KKE ray tracing).
// Needs VulkanDevice::rayQuery().
class RayScene {
public:
    static constexpr uint32_t kSlots = 2; // one top-level structure per frame in flight (Renderer::kMaxFramesInFlight)
    using Geometry = uint32_t;

    explicit RayScene(VulkanDevice& device);
    ~RayScene();
    RayScene(const RayScene&) = delete;
    RayScene& operator=(const RayScene&) = delete;

    // Triangles (positions, three indices each) as a bottom-level structure,
    // built now: the call waits for the GPU (load time, not per frame).
    Geometry addTriangles(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices);
    // An axis-aligned box of half extents `half` round the origin: a stand-in.
    Geometry addBox(const glm::vec3& half);
    size_t geometryCount() const { return m_geometries.size(); }

    // This frame's instances: clear(), add() each, then build().
    void clear() { m_instances.clear(); }
    // `custom` comes back from the ray query (rayQueryGetIntersectionInstanceCustomIndexEXT,
    // 24 bits): an index into the caller's own table (material, colour).
    void add(Geometry geometry, const glm::mat4& transform, uint32_t custom, uint8_t mask = 0xff);
    size_t instanceCount() const { return m_instances.size(); }

    // Builds the top-level structure of slot `slot` from the instances,
    // recorded into cmd (outside a render pass), and makes it readable by
    // ray queries in fragment and compute shaders after it. The slot's
    // previous frame must be done with it (after Renderer::beginFrame).
    void build(VkCommandBuffer cmd, uint32_t slot);
    VkAccelerationStructureKHR topLevel(uint32_t slot) const { return m_top[slot % kSlots].handle; }

private:
    struct Structure {
        std::unique_ptr<Buffer> buffer;
        VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    };
    struct Top {
        std::unique_ptr<Buffer> instances, scratch, buffer;
        VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
        uint32_t capacity = 0;
    };
    Structure makeStructure(VkAccelerationStructureTypeKHR type, VkDeviceSize size);
    void destroy(Structure& s);
    void growTop(Top& top, uint32_t count);

    VulkanDevice& m_device;
    VkDeviceSize m_scratchAlign = 1;
    std::vector<Structure> m_geometries;
    std::vector<VkDeviceAddress> m_geometryAddress;
    std::vector<VkAccelerationStructureInstanceKHR> m_instances;
    Top m_top[kSlots];
};

// Builds a small scene, traces rays through it on the GPU (a compute shader
// with ray queries) and compares the hits with the expected ones: the
// check that ray tracing works on this device and driver. Runs when the
// game starts with KKE_RAY_SELFTEST=1. False (and why, in `report`) on a
// mismatch or without ray query.
bool rayQuerySelfTest(VulkanDevice& device, std::string* report);

} // namespace kke
