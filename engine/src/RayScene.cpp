#include "kke/RayScene.h"

#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kke {

namespace {

constexpr VkBufferUsageFlags kInput =
    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;

// Records `record` into a one-time command buffer and waits for it (load time only).
template <typename F>
void submitNow(VulkanDevice& device, F&& record) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandPool = device.commandPool();
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(device.device(), &ai, &cmd));
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    record(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueWaitIdle(device.graphicsQueue()));
    VK_CHECK(vkQueueSubmit(device.graphicsQueue(), 1, &si, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(device.graphicsQueue()));
    vkFreeCommandBuffers(device.device(), device.commandPool(), 1, &cmd);
}

VkDeviceSize alignUp(VkDeviceSize v, VkDeviceSize a) { return (v + a - 1) / a * a; }

} // namespace

RayScene::RayScene(VulkanDevice& device) : m_device(device) {
    if (!device.rayQuery()) throw std::runtime_error("RayScene needs a device with ray query (VulkanDevice::rayQuery)");
    VkPhysicalDeviceAccelerationStructurePropertiesKHR as{};
    as.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 props{};
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props.pNext = &as;
    vkGetPhysicalDeviceProperties2(device.physicalDevice(), &props);
    m_scratchAlign = std::max<VkDeviceSize>(1, as.minAccelerationStructureScratchOffsetAlignment);
}

RayScene::~RayScene() {
    vkDeviceWaitIdle(m_device.device());
    for (Structure& s : m_geometries) destroy(s);
    for (Top& t : m_top)
        if (t.handle) vkDestroyAccelerationStructureKHR(m_device.device(), t.handle, nullptr);
}

RayScene::Structure RayScene::makeStructure(VkAccelerationStructureTypeKHR type, VkDeviceSize size) {
    Structure s;
    s.buffer = std::make_unique<Buffer>(m_device, size,
                                        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                        VMA_MEMORY_USAGE_GPU_ONLY);
    VkAccelerationStructureCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    ci.buffer = s.buffer->handle();
    ci.size = size;
    ci.type = type;
    VK_CHECK(vkCreateAccelerationStructureKHR(m_device.device(), &ci, nullptr, &s.handle));
    return s;
}

void RayScene::destroy(Structure& s) {
    if (s.handle) vkDestroyAccelerationStructureKHR(m_device.device(), s.handle, nullptr);
    s.handle = VK_NULL_HANDLE;
    s.buffer.reset();
}

RayScene::Geometry RayScene::addTriangles(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices) {
    if (positions.empty() || indices.size() < 3) throw std::runtime_error("RayScene::addTriangles: no triangles");
    Buffer vertices = Buffer::createDeviceLocal(m_device, positions.data(), positions.size() * sizeof(glm::vec3), kInput);
    Buffer index = Buffer::createDeviceLocal(m_device, indices.data(), indices.size() * sizeof(uint32_t), kInput);

    VkAccelerationStructureGeometryKHR g{};
    g.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    g.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    g.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    g.geometry.triangles.vertexData.deviceAddress = vertices.deviceAddress();
    g.geometry.triangles.vertexStride = sizeof(glm::vec3);
    g.geometry.triangles.maxVertex = static_cast<uint32_t>(positions.size() - 1);
    g.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
    g.geometry.triangles.indexData.deviceAddress = index.deviceAddress();
    VkAccelerationStructureBuildGeometryInfoKHR build{};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &g;
    const uint32_t triangles = static_cast<uint32_t>(indices.size() / 3);
    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(m_device.device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &triangles, &sizes);

    Structure s = makeStructure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, sizes.accelerationStructureSize);
    Buffer scratch(m_device, sizes.buildScratchSize + m_scratchAlign,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    build.dstAccelerationStructure = s.handle;
    build.scratchData.deviceAddress = alignUp(scratch.deviceAddress(), m_scratchAlign);
    VkAccelerationStructureBuildRangeInfoKHR range{ triangles, 0, 0, 0 };
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    submitNow(m_device, [&](VkCommandBuffer cmd) { vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, &ranges); });

    VkAccelerationStructureDeviceAddressInfoKHR ai{};
    ai.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    ai.accelerationStructure = s.handle;
    m_geometryAddress.push_back(vkGetAccelerationStructureDeviceAddressKHR(m_device.device(), &ai));
    m_geometries.push_back(std::move(s));
    return static_cast<Geometry>(m_geometries.size() - 1);
}

RayScene::Geometry RayScene::addBox(const glm::vec3& h) {
    std::vector<glm::vec3> p;
    for (int i = 0; i < 8; ++i) p.push_back(glm::vec3(i & 1 ? h.x : -h.x, i & 2 ? h.y : -h.y, i & 4 ? h.z : -h.z));
    const std::vector<uint32_t> idx = { 0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4,
                                        2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5 };
    return addTriangles(p, idx);
}

namespace {

VkAccelerationStructureGeometryKHR dynamicGeometry(VkDeviceAddress vertices, uint32_t stride, uint32_t vertexCount, VkDeviceAddress indices) {
    VkAccelerationStructureGeometryKHR g{};
    g.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    g.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    g.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    g.geometry.triangles.vertexData.deviceAddress = vertices;
    g.geometry.triangles.vertexStride = stride;
    g.geometry.triangles.maxVertex = vertexCount > 0 ? vertexCount - 1 : 0;
    g.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
    g.geometry.triangles.indexData.deviceAddress = indices;
    return g;
}

} // namespace

RayScene::Geometry RayScene::addDynamic(VkDeviceAddress vertices, uint32_t stride, uint32_t vertexCount, VkDeviceAddress indices,
                                        uint32_t indexCount) {
    VkAccelerationStructureGeometryKHR g = dynamicGeometry(vertices, stride, vertexCount, indices);
    VkAccelerationStructureBuildGeometryInfoKHR build{};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &g;
    const uint32_t triangles = indexCount / 3;
    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(m_device.device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &triangles, &sizes);
    Structure s = makeStructure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, sizes.accelerationStructureSize);
    // One scratch big enough to build it and, later, to update it.
    s.scratch = std::make_unique<Buffer>(m_device, std::max(sizes.buildScratchSize, sizes.updateScratchSize) + m_scratchAlign,
                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    s.stride = stride;
    s.vertexCount = vertexCount;
    s.indexCount = indexCount;
    s.indices = indices;
    build.dstAccelerationStructure = s.handle;
    build.scratchData.deviceAddress = alignUp(s.scratch->deviceAddress(), m_scratchAlign);
    VkAccelerationStructureBuildRangeInfoKHR range{ triangles, 0, 0, 0 };
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    submitNow(m_device, [&](VkCommandBuffer cmd) { vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, &ranges); });
    VkAccelerationStructureDeviceAddressInfoKHR ai{};
    ai.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    ai.accelerationStructure = s.handle;
    m_geometryAddress.push_back(vkGetAccelerationStructureDeviceAddressKHR(m_device.device(), &ai));
    m_geometries.push_back(std::move(s));
    return static_cast<Geometry>(m_geometries.size() - 1);
}

void RayScene::refit(VkCommandBuffer cmd, Geometry geometry, VkDeviceAddress vertices) {
    if (geometry >= m_geometries.size()) return;
    Structure& s = m_geometries[geometry];
    if (!s.handle || !s.scratch || s.refitFrame == m_frame) return;
    s.refitFrame = m_frame;
    if (!m_refitBarrier) {
        // GPU skinning wrote the vertices in a compute shader earlier this frame.
        VkMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &b, 0, nullptr, 0, nullptr);
        m_refitBarrier = true;
    }
    VkAccelerationStructureGeometryKHR g = dynamicGeometry(vertices, s.stride, s.vertexCount, s.indices);
    VkAccelerationStructureBuildGeometryInfoKHR build{};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
    build.srcAccelerationStructure = s.handle;
    build.dstAccelerationStructure = s.handle;
    build.geometryCount = 1;
    build.pGeometries = &g;
    build.scratchData.deviceAddress = alignUp(s.scratch->deviceAddress(), m_scratchAlign);
    VkAccelerationStructureBuildRangeInfoKHR range{ s.indexCount / 3, 0, 0, 0 };
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, &ranges);
}

void RayScene::remove(Geometry geometry) {
    if (geometry >= m_geometries.size() || !m_geometries[geometry].handle) return;
    // A frame in flight may still trace it.
    vkDeviceWaitIdle(m_device.device());
    destroy(m_geometries[geometry]);
    m_geometries[geometry].scratch.reset();
}

void RayScene::add(Geometry geometry, const glm::mat4& t, uint32_t custom, uint8_t mask) {
    if (geometry >= m_geometries.size() || !m_geometries[geometry].handle) return;
    VkAccelerationStructureInstanceKHR inst{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c) inst.transform.matrix[r][c] = t[c][r]; // row-major 3x4
    inst.instanceCustomIndex = custom & 0xffffffu;
    inst.mask = mask;
    inst.instanceShaderBindingTableRecordOffset = 0;
    inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    inst.accelerationStructureReference = m_geometryAddress[geometry];
    m_instances.push_back(inst);
}

void RayScene::growTop(Top& top, uint32_t count) {
    // Sized for at least `count` instances, doubling: a frame in flight may
    // still trace the old one, so wait for the GPU (rare: growth only).
    vkDeviceWaitIdle(m_device.device());
    if (top.handle) vkDestroyAccelerationStructureKHR(m_device.device(), top.handle, nullptr);
    top.capacity = std::max<uint32_t>(64, std::max(count, top.capacity * 2));
    top.instances = std::make_unique<Buffer>(m_device, top.capacity * sizeof(VkAccelerationStructureInstanceKHR), kInput,
                                             VMA_MEMORY_USAGE_CPU_TO_GPU);
    VkAccelerationStructureGeometryKHR g{};
    g.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    g.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    g.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    VkAccelerationStructureBuildGeometryInfoKHR build{};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &g;
    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(m_device.device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &top.capacity, &sizes);
    top.buffer = std::make_unique<Buffer>(m_device, sizes.accelerationStructureSize,
                                          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                          VMA_MEMORY_USAGE_GPU_ONLY);
    VkAccelerationStructureCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    ci.buffer = top.buffer->handle();
    ci.size = sizes.accelerationStructureSize;
    ci.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    VK_CHECK(vkCreateAccelerationStructureKHR(m_device.device(), &ci, nullptr, &top.handle));
    top.scratch = std::make_unique<Buffer>(m_device, sizes.buildScratchSize + m_scratchAlign,
                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                           VMA_MEMORY_USAGE_GPU_ONLY);
}

void RayScene::build(VkCommandBuffer cmd, uint32_t slot) {
    Top& top = m_top[slot % kSlots];
    const uint32_t count = static_cast<uint32_t>(m_instances.size());
    if (m_refitBarrier) {
        // The refits recorded this frame finish before the top level reads them.
        VkMemoryBarrier rb{};
        rb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        rb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        rb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             0, 1, &rb, 0, nullptr, 0, nullptr);
    }
    m_refitBarrier = false;
    ++m_frame;
    if (!top.handle || count > top.capacity) growTop(top, count);
    if (count > 0) top.instances->upload(m_instances.data(), count * sizeof(VkAccelerationStructureInstanceKHR));

    VkAccelerationStructureGeometryKHR g{};
    g.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    g.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    g.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    g.geometry.instances.data.deviceAddress = top.instances->deviceAddress();
    VkAccelerationStructureBuildGeometryInfoKHR build{};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.dstAccelerationStructure = top.handle;
    build.geometryCount = 1;
    build.pGeometries = &g;
    build.scratchData.deviceAddress = alignUp(top.scratch->deviceAddress(), m_scratchAlign);
    VkAccelerationStructureBuildRangeInfoKHR range{ count, 0, 0, 0 };
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, &ranges);

    // Built before any ray query reads it.
    VkMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    b.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    b.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &b, 0, nullptr, 0, nullptr);
}

} // namespace kke
