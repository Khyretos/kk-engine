#include "kke/LightingBuffer.h"
#include "kke/Application.h"
#include "kke/Renderer.h"
#include "kke/ShadowMap.h"
#include "kke/VulkanDevice.h"
#include "kke/VulkanCheck.h"
#include "kke/Texture.h"

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace kke {

static_assert(static_cast<int>(LightingBuffer::kSlotCount) == Renderer::kMaxFramesInFlight, "one lighting buffer per frame in flight");

namespace {

// Mirrors cube.frag's `LightingUBO` GLSL block byte-for-byte. Every
// field is a vec4 deliberately, even where fewer components are
// meaningful -- std140 (the layout Vulkan UBOs use) pads vec3 up to
// vec4-alignment anyway, so using vec4 throughout sidesteps that
// padding rather than fighting it with manual alignas() bookkeeping.
struct GPULight {
    glm::vec4 directionOrPosition; // xyz = direction or position; w = 1.0 if positional (point), 0.0 if directional
    glm::vec4 colorIntensity;      // rgb = color; a = intensity (<=0 means "treat as disabled")
    glm::vec4 params;              // x = range in metres (0 = no falloff), y = point-shadow slot (Lighting::pointShadowSlots, -1 = none)
};

struct LightingUBOData {
    GPULight lights[Lighting::kMaxLights];
    glm::vec4 ambient;   // rgb = ambient color; a unused (padding)
    glm::vec4 cameraPos; // rgb = world-space camera position, needed for specular; a unused
    glm::mat4 lightViewProj; // appended -- see ShadowMap.h
    glm::mat4 viewProj; // appended -- the real camera's view*proj, computed once here instead of
                         // redundantly inside every single object's own push constants (see
                         // PBRPushConstants in cube.vert/frag's own comment for why that mattered:
                         // freeing this 64 bytes out of the push constant was what made room for
                         // real per-object metallic/roughness within the 128-byte guaranteed-minimum
                         // push constant limit)
    // The sky and fog (kke/Sky.h); shaders/lighting_ubo.glsl names each.
    // The curve, exposure and look are the tone-map pass's (kke/ToneMap.h).
    glm::vec4 skyZenith;
    glm::vec4 skyHorizon;
    glm::vec4 skyGround;
    glm::vec4 sunDisc;
    glm::vec4 sunGlow;
    glm::vec4 fogColor;
    glm::vec4 fogParams;
    glm::vec4 ambientSH[9];
    glm::vec4 shadowTiles[ShadowMap::kCascades];        // each cascade's tile of the shadow map: x, y (top left), width, height in 0..1
    glm::vec4 cascadeScaleOffset[ShadowMap::kCascades]; // cascade c's clip xy = the far cascade's xy * (x, y) + (z, w)
    glm::mat4 pointShadowViewProj[ShadowMap::kMaxPointShadows * ShadowMap::kPointFaces]; // light * kPointFaces + face (ShadowMap::pointFaceViewProj)
    glm::vec4 pointShadowTiles[ShadowMap::kMaxPointShadows * ShadowMap::kPointFaces];    // that face's tile of the point map: x, y, width, height in 0..1
    glm::vec4 envMap; // x = the reflections' top mip (levels - 1), 0 = no map: reflect the ambient SH
    glm::mat4 giViewProj; // the camera the screen-space GI images were made with (ENG-F01)
    glm::vec4 gi;         // x = 1 when they hold a frame's result, yz = their size in pixels, w = KKE_GI_VIEW
    glm::mat4 reflViewProj; // the camera the reflection images were made with (ENG-F02)
    glm::vec4 refl;         // x = 1 when they hold a frame's result, yz = their size, w = the roughest surface they're for
    glm::vec4 behind;       // x = 1 when this frame's behind image is there (refraction, ENG-F02 part 4), yz = its size
    glm::vec4 fogVolume;    // x = 1 when volumetric fog is on (ENG-F03), y = its near distance, z = its far distance, w = its slices
    glm::mat4 shadowRayViewProj; // the camera the shadow rays were cast with (ENG-F07)
    glm::vec4 shadowRays;        // x = 1 when they hold a frame's result, yz = their size, w = reprojection slack
    glm::vec4 clusterParams;     // x = how many many-lights (ENG-F08), y = near, z = far distance, w = 1: walk them all
    glm::vec4 clusterGrid;       // cells across, down, deep; lights a cell lists at most
    glm::vec4 clusterScreen;     // the scene's size in pixels
    glm::vec4 decalParams;       // x = how many decals (ENG-F12; 0 = none), y = near, z = far distance of their cells
    glm::vec4 decalScreen;       // xy = the scene's size in pixels (their cells')
};
static_assert(sizeof(LightingUBOData) == Lighting::kMaxLights * 3 * 16 + 2 * 16 + 2 * 64 + 7 * 16 + 9 * 16 + 2 * ShadowMap::kCascades * 16 + ShadowMap::kMaxPointShadows * ShadowMap::kPointFaces * (64 + 16) + 16 + 64 + 16 + 64 + 16 + 16 + 16 + 64 + 16 + 3 * 16 + 2 * 16, "must match shaders/lighting_ubo.glsl");
static_assert(ShadowMap::kMaxPointShadows <= Lighting::kMaxLights, "every point-shadow slot needs a light");

} // namespace

LightingBuffer::LightingBuffer(VulkanDevice& device) : m_device(device) {
    // --- Set 0's layout, made by the RHI device. Binding 0: the uniform buffer, read by the
    // vertex and fragment stages and by compute (volumetric fog lights its cells from it).
    // 1: the sky prefiltered for reflections (setEnvironmentMap); 2-3: screen-space GI's light
    // and depth (setScreenGi); 4-5: reflections' light and depth (setReflections); 6: what
    // see-through surfaces show behind them (setBehind); 7: volumetric fog's grid, a 3D image
    // (setFogVolume); 8: last frame's ray-traced sun shadows (setShadowRays); 9-11: the many
    // lights and their per-cell lists (setManyLights); 12-14: the decals and theirs, 15 their
    // textures (setDecals).
    std::array<rhi::BindingLayout, 16> bindings;
    bindings[0] = { 0, rhi::BindingType::UniformBuffer, rhi::kVertex | rhi::kFragment | rhi::kCompute };
    for (uint32_t i = 1; i < 16; ++i)
        bindings[i] = { i, i >= 9 && i < 15 ? rhi::BindingType::StorageBuffer : rhi::BindingType::Texture, rhi::kFragment };
    m_layout = m_gpu.createBindGroupLayout(bindings);

    const std::array<uint16_t, 4> black{ glm::packHalf1x16(0.0f), glm::packHalf1x16(0.0f), glm::packHalf1x16(0.0f), glm::packHalf1x16(1.0f) };
    m_placeholder = std::make_unique<Texture>(device, black.data(), 1, 1, Texture::HalfFloatSky{});
    // Bound at bindings 9-11 while there are no many lights (never read then).
    m_placeholderBuffer = std::make_unique<Buffer>(device, 64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    createVolumePlaceholder();

    for (uint32_t i = 0; i < kSlotCount; ++i) {
        m_descriptorSets[i] = rhi::VulkanGpuDevice::set(m_gpu.createBindGroup(*m_layout));

        // Host-visible, updated directly every frame -- this buffer is
        // tiny, so a staging-buffer round trip would be pure overhead.
        m_buffers[i] = std::make_unique<Buffer>(device, sizeof(LightingUBOData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                VMA_MEMORY_USAGE_CPU_TO_GPU);

        const rhi::BindingWrite write{ .binding = 0, .buffer = rhi::VulkanCommandList::buffer(m_buffers[i]->handle()),
                                       .range = sizeof(LightingUBOData) };
        m_gpu.writeBindGroup(rhi::VulkanCommandList::group(m_descriptorSets[i]), *m_layout, { &write, 1 });
        writeEnvironment(i);
    }
}

LightingBuffer::~LightingBuffer() {
    if (m_decalArraySampler) vkDestroySampler(m_device.device(), m_decalArraySampler, nullptr);
    if (m_decalArrayView) vkDestroyImageView(m_device.device(), m_decalArrayView, nullptr);
    if (m_decalArray) vmaDestroyImage(m_device.allocator(), m_decalArray, m_decalArrayAlloc);
    if (m_volumeSampler) vkDestroySampler(m_device.device(), m_volumeSampler, nullptr);
    if (m_volumeView) vkDestroyImageView(m_device.device(), m_volumeView, nullptr);
    if (m_volumeImage) vmaDestroyImage(m_device.allocator(), m_volumeImage, m_volumeAlloc);
}

void LightingBuffer::createVolumePlaceholder() {
    // A 1x1x1 3D image (0, 0, 0, 1: no fog scattered, all gets through), bound while volumetric fog is off.
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_3D;
    ii.extent = { 1, 1, 1 };
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    VK_CHECK(vmaCreateImage(m_device.allocator(), &ii, &ai, &m_volumeImage, &m_volumeAlloc, nullptr));
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = m_volumeImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_3D;
    vi.format = ii.format;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VK_CHECK(vkCreateImageView(m_device.device(), &vi, nullptr, &m_volumeView));
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(m_device.device(), &si, nullptr, &m_volumeSampler));
    // And a 1x1 array of one transparent layer, bound while there are no decals (ENG-F12).
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    VK_CHECK(vmaCreateImage(m_device.allocator(), &ii, &ai, &m_decalArray, &m_decalArrayAlloc, nullptr));
    vi.image = m_decalArray;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vi.format = ii.format;
    VK_CHECK(vkCreateImageView(m_device.device(), &vi, nullptr, &m_decalArrayView));
    VK_CHECK(vkCreateSampler(m_device.device(), &si, nullptr, &m_decalArraySampler));

    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandPool = m_device.commandPool();
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(m_device.device(), &cai, &cmd));
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = m_volumeImage;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    const VkClearColorValue clear{ { 0.0f, 0.0f, 0.0f, 1.0f } };
    vkCmdClearColorImage(cmd, m_volumeImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &b.subresourceRange);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkImageMemoryBarrier d = b;
    d.image = m_decalArray;
    d.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    d.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    d.srcAccessMask = 0;
    d.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &d);
    const VkClearColorValue transparent{ { 0.0f, 0.0f, 0.0f, 0.0f } };
    vkCmdClearColorImage(cmd, m_decalArray, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &transparent, 1, &d.subresourceRange);
    d.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    d.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    d.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    d.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &d);
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(m_device.graphicsQueue(), 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(m_device.graphicsQueue()));
    vkFreeCommandBuffers(m_device.device(), m_device.commandPool(), 1, &cmd);
}

void LightingBuffer::writeEnvironment(uint32_t slot) {
    const Texture& map = m_envMap ? *m_envMap : *m_placeholder;
    // Without GI the placeholder (0, 0, 0, 1) reads as no bounce, all visible.
    const bool gi = m_giLight && m_giDepth && m_giSampler;
    // A texture as a binding sees it; `written`: a compute pass writes it (read in the GENERAL layout).
    const auto tex = [](VkSampler sampler, VkImageView view, bool written) {
        return rhi::TextureRef{ rhi::VulkanCommandList::toHandle(view), rhi::VulkanCommandList::toHandle(sampler), written };
    };
    std::array<rhi::BindingWrite, 15> w;
    w[0].texture = map.ref();
    w[1].texture = gi ? tex(m_giSampler, m_giLight, true) : m_placeholder->ref();
    w[2].texture = gi ? tex(m_giSampler, m_giDepth, true) : w[1].texture;
    // Without reflections the placeholder's a = 1 is ignored: refl.x = 0 skips the lookup.
    const bool refl = m_reflLight && m_reflDepth && m_reflSampler;
    const rhi::TextureRef none = m_placeholder->ref();
    w[3].texture = refl ? tex(m_reflSampler, m_reflLight, true) : none;
    w[4].texture = refl ? tex(m_reflSampler, m_reflDepth, true) : none;
    w[5].texture = m_behind && m_behindSampler ? tex(m_behindSampler, m_behind, true) : none;
    w[6].texture = m_fogVolume && m_fogVolumeSampler ? tex(m_fogVolumeSampler, m_fogVolume, true) : tex(m_volumeSampler, m_volumeView, false);
    w[7].texture = m_shadowRays && m_shadowRaySampler ? tex(m_shadowRaySampler, m_shadowRays, true) : none;
    for (uint32_t i = 0; i < 8; ++i) w[i].binding = 1 + i;
    const bool many = m_manyLights && m_clusterCounts && m_clusterIndices;
    const bool decals = m_decals && m_decalCounts && m_decalIndices && m_decalTextures;
    const VkBuffer placeholder = m_placeholderBuffer->handle();
    const std::array<VkBuffer, 6> buffers{ many ? m_manyLights : placeholder,   many ? m_clusterCounts : placeholder,
                                           many ? m_clusterIndices : placeholder, decals ? m_decals : placeholder,
                                           decals ? m_decalCounts : placeholder,  decals ? m_decalIndices : placeholder };
    for (uint32_t i = 0; i < 6; ++i) {
        w[8 + i].binding = 9 + i;
        w[8 + i].buffer = rhi::VulkanCommandList::buffer(buffers[i]);
    }
    // Without decals the black placeholder stands in for their textures (never read: decalParams.x = 0).
    w[14].binding = 15;
    w[14].texture = decals ? tex(m_decalSampler, m_decalTextures, false) : tex(m_decalArraySampler, m_decalArrayView, false);
    m_gpu.writeBindGroup(rhi::VulkanCommandList::group(m_descriptorSets[slot]), *m_layout, w);
}

void LightingBuffer::update(const Lighting& lighting, const glm::vec3& cameraPos, const glm::mat4& lightViewProj, const glm::mat4& viewProj,
                            const SkyEnvironment* sky, const glm::vec4* shadowTiles, const glm::vec4* cascadeScaleOffsets,
                            const glm::mat4* pointViewProjs, const glm::vec4* pointTiles) {
    LightingUBOData data{};
    for (uint32_t c = 0; c < ShadowMap::kCascades; ++c) {
        data.shadowTiles[c] = shadowTiles ? shadowTiles[c] : glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        data.cascadeScaleOffset[c] = cascadeScaleOffsets ? cascadeScaleOffsets[c] : glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    }
    for (uint32_t f = 0; f < ShadowMap::kMaxPointShadows * ShadowMap::kPointFaces; ++f) {
        data.pointShadowTiles[f] = pointTiles ? pointTiles[f] : glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        if (pointViewProjs) {
            data.pointShadowViewProj[f] = pointViewProjs[f];
        }
    }
    const std::array<int, Lighting::kMaxLights> slots = lighting.pointShadowSlots();
    for (int i = 0; i < Lighting::kMaxLights; ++i) {
        const Light& src = lighting.lights[i];
        GPULight& dst = data.lights[i];
        if (!src.enabled) {
            dst.colorIntensity.a = 0.0f; // the shader's own "disabled" convention
            continue;
        }
        if (src.isDirectional) {
            dst.directionOrPosition = glm::vec4(src.direction, 0.0f);
        } else {
            dst.directionOrPosition = glm::vec4(src.position, 1.0f);
        }
        dst.colorIntensity = glm::vec4(src.color, src.intensity);
        // x = range (Light::falloffAt), y = its point-shadow slot, -1 when it has none or none is drawn this frame.
        dst.params = glm::vec4(src.range, pointViewProjs ? static_cast<float>(slots[i]) : -1.0f, 0.0f, 0.0f);
    }
    data.ambient = glm::vec4(lighting.ambientColor, lighting.shadowsEnabled ? 1.0f : 0.0f); // a = shadows on/off, read by cube.frag
    data.cameraPos = glm::vec4(cameraPos, 0.0f);
    data.lightViewProj = lightViewProj;
    data.viewProj = viewProj;

    SkySH flat;
    if (!sky) flat = SkySH::constant(lighting.ambientColor);
    const SkySH& sh = sky ? sky->ambient : flat;
    for (int i = 0; i < 9; ++i) data.ambientSH[i] = glm::vec4(sh.c[i], 0.0f);
    if (sky) {
        // Shaders that take one ambient colour (translucent, fluid) get the average.
        data.ambient = glm::vec4(sky->ambientAverage, data.ambient.a);
        data.skyZenith = glm::vec4(sky->zenith, static_cast<float>(sky->kind));
        data.skyHorizon = glm::vec4(sky->horizon, sky->horizonFalloff);
        data.skyGround = glm::vec4(sky->ground, sky->yawRadians);
        data.sunDisc = glm::vec4(sky->sunDisc, sky->sunCosRadius);
        data.sunGlow = glm::vec4(sky->sunGlow, sky->imageScale);
        data.fogColor = glm::vec4(sky->fogColor, 0.0f);
    } else {
        data.fogColor = glm::vec4(lighting.fog.color, 0.0f);
    }
    const Fog& f = lighting.fog;
    if (f.enabled) data.fogColor.a = std::max(f.density, 0.0f);
    data.fogParams = glm::vec4(f.heightFalloff, f.height, std::clamp(f.maxOpacity, 0.0f, 1.0f), f.sunScatter);

    data.envMap = glm::vec4(m_envMap && m_envLevels > 1 ? static_cast<float>(m_envLevels - 1) : 0.0f, 0.0f, 0.0f, 0.0f);
    data.giViewProj = m_giViewProj;
    static const float view = [] {
        const char* v = std::getenv("KKE_GI_VIEW"); // 1: lit surfaces show the GI's visibility alone (a check)
        return v ? static_cast<float>(std::atoi(v)) : 0.0f;
    }();
    data.gi = glm::vec4(m_giLight ? 1.0f : 0.0f, m_giSize.x, m_giSize.y, m_giLight ? view : 0.0f);
    data.reflViewProj = m_reflViewProj;
    data.refl = glm::vec4(m_reflLight ? 1.0f : 0.0f, m_reflSize.x, m_reflSize.y, m_reflMaxRoughness);
    data.behind = glm::vec4(m_behind ? 1.0f : 0.0f, m_behindSize.x, m_behindSize.y, 0.0f);
    const bool many = m_manyLights && m_clusterCounts && m_clusterIndices;
    data.clusterParams = glm::vec4(many ? static_cast<float>(m_manyCount) : 0.0f, m_clusterGrid.nearDistance, m_clusterGrid.farDistance,
                                   m_clusterAll ? 1.0f : 0.0f);
    data.clusterGrid = glm::vec4(static_cast<float>(m_clusterGrid.x), static_cast<float>(m_clusterGrid.y), static_cast<float>(m_clusterGrid.z),
                                 static_cast<float>(m_clusterGrid.maxPerCluster));
    data.clusterScreen = glm::vec4(m_clusterScreen, 0.0f, 0.0f);
    const bool decals = m_decals && m_decalCounts && m_decalIndices && m_decalTextures;
    data.decalParams = glm::vec4(decals ? static_cast<float>(m_decalCount) : 0.0f, m_decalGrid.nearDistance, m_decalGrid.farDistance, 0.0f);
    data.decalScreen = glm::vec4(m_decalScreen, 0.0f, 0.0f);
    data.shadowRayViewProj = m_shadowRayViewProj;
    data.shadowRays = glm::vec4(m_shadowRays ? 1.0f : 0.0f, m_shadowRaySize.x, m_shadowRaySize.y, m_shadowRaySlack);
    data.fogVolume = m_fogVolume ? glm::vec4(1.0f, m_fogVolumeSettings.nearDistance, m_fogVolumeSettings.farDistance,
                                             static_cast<float>(m_fogVolumeSettings.slices))
                                 : glm::vec4(0.0f);

    m_buffers[m_slot]->upload(&data, sizeof(data));
    // This slot's set is free now (setFrame came after the frame's fence wait).
    writeEnvironment(m_slot);
}

} // namespace kke
