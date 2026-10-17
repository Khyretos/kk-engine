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

// How a ray hit looks (one per instance; the instance's custom index is its
// place in the table the shader reads, std430, 48 bytes): its colour, and in
// classic ray tracing where its vertices are, so the hit gets its real
// normal (and vertex colour) instead of a stand-in's flat look.
struct RayMaterial {
    glm::vec4 color{1.0f};         // linear albedo (times the vertex colour when there is one)
    VkDeviceAddress vertices = 0;  // 0: a stand-in, no vertex data
    VkDeviceAddress indices = 0;   // uint32 triangle list
    uint32_t stride = 0;           // bytes per vertex
    uint32_t normalOffset = 0;     // bytes to the normal (3 floats)
    uint32_t colorOffset = ~0u;    // bytes to an sRGB vertex colour (3 floats), ~0u: none
    uint32_t unused = 0;
};
static_assert(sizeof(RayMaterial) == 48, "matches shaders/reflect.glsl's RayMaterial");

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
    // With `normals` (one per position) the vertices keep them for shading
    // hits: material(geometry) says where.
    Geometry addTriangles(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices,
                          const std::vector<glm::vec3>* normals = nullptr);
    // A RayMaterial of `color` pointing at a geometry's own vertices (from
    // addTriangles with normals), or a flat one.
    RayMaterial material(Geometry geometry, const glm::vec4& color) const;
    // An axis-aligned box of half extents `half` round the origin: a stand-in.
    Geometry addBox(const glm::vec3& half);
    // Triangles whose vertices change every frame (a skinned character, for
    // classic ray tracing), read from GPU buffers by address: `stride` bytes
    // per vertex, the position (3 floats) first; uint32 indices. Built now
    // (waits for the GPU); refit() follows the vertices each frame.
    Geometry addDynamic(VkDeviceAddress vertices, uint32_t stride, uint32_t vertexCount, VkDeviceAddress indices, uint32_t indexCount);
    // Records an update of a dynamic geometry to the vertices now at
    // `vertices` (same count), into cmd before build(). The first refit of a
    // frame waits for compute shaders' writes (GPU skinning) to finish.
    void refit(VkCommandBuffer cmd, Geometry geometry, VkDeviceAddress vertices);
    // Frees a geometry (when what it stood for is gone); its id is not reused.
    void remove(Geometry geometry);
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
        // addTriangles with normals: positions and normals interleaved (24 bytes), kept for shading hits.
        std::unique_ptr<Buffer> vertexData, indexData;
        // Dynamic geometry only: how it was built, to update it the same way.
        std::unique_ptr<Buffer> scratch;
        uint32_t stride = 0, vertexCount = 0, indexCount = 0;
        VkDeviceAddress indices = 0;
        uint64_t refitFrame = 0;
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
    uint64_t m_frame = 1;           // counts build() calls: the first refit of each frame waits for compute
    bool m_refitBarrier = false;
    Top m_top[kSlots];
};

// Builds a small scene (two boxes, a floor, and a triangle refitted after it
// moved), traces rays through it on the GPU (a compute shader with ray
// queries) and compares the hits with the expected ones: the check that ray
// tracing works on this device and driver. Runs when the
// game starts with KKE_RAY_SELFTEST=1. False (and why, in `report`) on a
// mismatch or without ray query.
bool rayQuerySelfTest(VulkanDevice& device, std::string* report);

} // namespace kke
