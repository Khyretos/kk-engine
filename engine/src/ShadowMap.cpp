#include "kke/ShadowMap.h"
#include "kke/VulkanDevice.h"
#include "kke/VulkanCheck.h"

#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>

namespace kke {

ShadowMap::ShadowMap(VulkanDevice& device, uint32_t resolution)
    : m_device(device), m_tile(resolution), m_resolution(resolution) {
    // Depth-compare sampler: each lookup in computeShadow()
    // (pbr_common.glsl, a sampler2DShadow) returns the lit fraction of
    // the 2x2 texels around it, bilinearly weighted, so a 3x3 kernel of
    // them gives a smooth 4x4-texel penumbra for the cost of 9 taps.
    // Needs linear filtering on the depth format; without it the
    // compare still works, just per texel. CLAMP_TO_BORDER with an opaque-white border color specifically:
    // a fragment whose light-space position falls outside the shadow
    // map's covered region (see computeLightViewProj's own sceneRadius
    // parameter) samples the border and reads back the maximum
    // possible depth (1.0) — which the shadow comparison in cube.frag
    // then correctly treats as "nothing recorded here is closer to the
    // light than this fragment," i.e. not in shadow, rather than an
    // arbitrary wrapped/clamped depth value that could read as
    // incorrectly shadowed or unshadowed depending on chance.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    VkFormatProperties formatProps{};
    vkGetPhysicalDeviceFormatProperties(m_device.physicalDevice(), m_format, &formatProps);
    const VkFilter filter = (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
        ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL; // lit when the fragment is no farther than the stored depth
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    VK_CHECK(vkCreateSampler(m_device.device(), &samplerInfo, nullptr, &m_sampler));

    // Depth-only render pass -- no color attachment at all. finalLayout
    // is SHADER_READ_ONLY directly: the render pass's own subpass
    // dependency below handles the transition, so the main color pass
    // can sample this image immediately afterward with no separate,
    // manually-recorded barrier needed in Application's own frame loop.
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = m_format;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 0;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthRef;

    // Two dependencies, not one: entering the pass (external ->
    // subpass 0, so anything reading this image as a texture in a
    // *previous* frame's main pass finishes before this frame starts
    // writing depth into it) and leaving it (subpass 0 -> external, so
    // the main pass's fragment shader sampling doesn't start before
    // this depth write has actually finished).
    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();
    VK_CHECK(vkCreateRenderPass(m_device.device(), &renderPassInfo, nullptr, &m_renderPass));

    createTarget();
}

ShadowMap::~ShadowMap() {
    VkDevice dev = m_device.device();
    destroyTarget();
    if (m_renderPass) vkDestroyRenderPass(dev, m_renderPass, nullptr);
    if (m_sampler) vkDestroySampler(dev, m_sampler, nullptr);
}

// The depth image, its view and the framebuffer: m_resolution square
// (a grid of tiles, one per view, when there are several).
void ShadowMap::createTarget() {
    // Depth image — same VMA-backed creation pattern already proven in
    // SwapChain::createDepthResources(), just GPU-only depth used as a
    // sampled texture afterward rather than the swapchain's own
    // per-frame depth buffer, hence the extra SAMPLED_BIT usage flag.
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { m_resolution, m_resolution, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VK_CHECK(vmaCreateImage(m_device.allocator(), &imageInfo, &allocInfo, &m_image, &m_allocation, nullptr));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_format;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
    VK_CHECK(vkCreateImageView(m_device.device(), &viewInfo, nullptr, &m_imageView));

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = m_renderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &m_imageView;
    fbInfo.width = m_resolution;
    fbInfo.height = m_resolution;
    fbInfo.layers = 1;
    VK_CHECK(vkCreateFramebuffer(m_device.device(), &fbInfo, nullptr, &m_framebuffer));
}

void ShadowMap::destroyTarget() {
    VkDevice dev = m_device.device();
    if (m_framebuffer) vkDestroyFramebuffer(dev, m_framebuffer, nullptr);
    if (m_imageView) vkDestroyImageView(dev, m_imageView, nullptr);
    if (m_image) vmaDestroyImage(m_device.allocator(), m_image, m_allocation);
    m_framebuffer = VK_NULL_HANDLE;
    m_imageView = VK_NULL_HANDLE;
    m_image = VK_NULL_HANDLE;
    m_allocation = VK_NULL_HANDLE;
}

bool ShadowMap::setTiles(uint32_t tiles) {
    const uint32_t grid = tiles <= 1 ? 1u : 2u; // kMaxViews is 4: at most 2 x 2
    if (grid == m_grid) return false;
    vkDeviceWaitIdle(m_device.device()); // frames in flight still sample the old image
    destroyTarget();
    m_grid = grid;
    m_resolution = m_tile * grid;
    createTarget();
    return true;
}

glm::vec4 ShadowMap::tileRect(uint32_t tile) const {
    const float size = 1.0f / static_cast<float>(m_grid);
    const uint32_t t = tile % (m_grid * m_grid);
    return { static_cast<float>(t % m_grid) * size, static_cast<float>(t / m_grid) * size, size, size };
}

void ShadowMap::beginTile(VkCommandBuffer cmd, uint32_t tile) {
    const glm::vec4 r = tileRect(tile);
    const float res = static_cast<float>(m_resolution);
    VkViewport viewport{ r.x * res, r.y * res, static_cast<float>(m_tile), static_cast<float>(m_tile), 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ { static_cast<int32_t>(r.x * res), static_cast<int32_t>(r.y * res) }, { m_tile, m_tile } };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

PipelineConfig ShadowMap::casterConfig() {
    PipelineConfig config;
    config.cullMode = VK_CULL_MODE_NONE;
    // Slope factor does the work: bias grows with how steeply the
    // surface faces away from the light. The constant is tiny for D32
    // (it's scaled by the depth's exponent) and only covers flat floors.
    config.depthBiasConstant = 1.25f;
    config.depthBiasSlope = 1.75f;
    return config;
}

glm::mat4 ShadowMap::computeLightViewProj(const glm::vec3& lightDirection, const glm::vec3& sceneCenter, float sceneRadius,
                                          uint32_t shadowMapResolution) {
    glm::vec3 dir = glm::normalize(lightDirection);
    // The light's own "camera" sits back along the reverse of its
    // direction, far enough that sceneRadius fits comfortably inside
    // the near/far range set on the ortho projection below.
    glm::vec3 eye = sceneCenter - dir * (sceneRadius * 3.0f);

    // A degenerate up vector (dir parallel to world-up) would make
    // glm::lookAt produce a NaN/garbage matrix — genuinely possible for
    // a directional light aimed straight down, which this project's own
    // default key light direction (see Application.h's Lighting
    // default) is close to. Falls back to world-forward as the up hint
    // in that case, same trick used for camera-look code elsewhere.
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(dir, up)) > 0.999f) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    glm::vec3 center = sceneCenter;
    if (shadowMapResolution > 0) {
        // Express the centre in the light's rotation-only frame, round its
        // across-the-light coordinates to whole texels, and map it back.
        const float texel = 2.0f * sceneRadius / static_cast<float>(shadowMapResolution);
        const glm::mat4 rot = glm::lookAt(glm::vec3(0.0f), dir, up);
        glm::vec4 c = rot * glm::vec4(sceneCenter, 1.0f);
        c.x = std::round(c.x / texel) * texel;
        c.y = std::round(c.y / texel) * texel;
        center = glm::vec3(glm::inverse(rot) * c);
        eye = center - dir * (sceneRadius * 3.0f);
    }

    glm::mat4 view = glm::lookAt(eye, center, up);
    glm::mat4 proj = glm::ortho(-sceneRadius, sceneRadius, -sceneRadius, sceneRadius, 0.1f, sceneRadius * 6.0f);
    // Same Vulkan Y-flip every other projection matrix in this engine
    // already applies (see Application.h/Camera) — glm's ortho(), like
    // its perspective(), assumes OpenGL's clip space convention.
    proj[1][1] *= -1.0f;
    return proj * view;
}

void ShadowMap::beginRenderPass(VkCommandBuffer cmd) {
    VkClearValue clearValue{};
    clearValue.depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass = m_renderPass;
    beginInfo.framebuffer = m_framebuffer;
    beginInfo.renderArea.offset = { 0, 0 };
    beginInfo.renderArea.extent = { m_resolution, m_resolution };
    beginInfo.clearValueCount = 1;
    beginInfo.pClearValues = &clearValue;
    vkCmdBeginRenderPass(cmd, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(m_resolution), static_cast<float>(m_resolution), 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ { 0, 0 }, { m_resolution, m_resolution } };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void ShadowMap::endRenderPass(VkCommandBuffer cmd) {
    vkCmdEndRenderPass(cmd);
}

} // namespace kke
