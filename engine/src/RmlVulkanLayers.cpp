// RmlUi layers and filters for kke::RmlVulkanRenderInterface (ENG-G09):
// CSS `filter`, `backdrop-filter`, `box-shadow` and `mask-image`. The same
// scheme as RmlUi's GL3 backend: every layer is a full-frame image, and
// filters and compositing are full-screen passes (shaders/rml_filter.frag).
// Layer 0 is the frame's own image (kke::OverlayTarget); RmlUi's calls arrive
// while its pass is open, and every switch to another image ends that pass
// and begins the next one, with explicit barriers between them.

#include "kke/RmlVulkanRenderInterface.h"
#include "kke/VulkanDevice.h"
#include "kke/VulkanCheck.h"
#include "kke/Log.h"

#include <algorithm>
#include <array>
#include <utility>

namespace kke {

namespace {
// Every way a layer image is used, so one barrier covers whatever came before:
// drawing, sampling and copying.
void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    switch (to) {
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        default:
            b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
    }
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                         dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}
} // namespace

RmlVulkanRenderInterface::LayerTarget* RmlVulkanRenderInterface::layerTarget(Rml::LayerHandle handle) {
    if (handle == 0 || handle > m_layers.size()) return nullptr;
    return m_layers[handle - 1].get();
}

Rml::LayerHandle RmlVulkanRenderInterface::topLayer() const {
    return m_layerStack.empty() ? 0 : m_layerStack.back();
}

void RmlVulkanRenderInterface::transition(LayerTarget& target, VkImageLayout layout) {
    imageBarrier(m_currentCmd, target.image, target.layout, layout);
    target.layout = layout;
}

// The render pass the layer images draw in: laid out like the swapchain's
// overlay pass (same formats and dependency, so every RmlUi pipeline works in
// it) but the colour stays an attachment between passes. Then the shared
// depth-stencil image (clip masks), the three filter images and the mask.
void RmlVulkanRenderInterface::createLayerResources() {
    m_layerExtent = m_base.extent;
    m_layerFormat = m_base.colorFormat;
    m_layerDepthFormat = m_base.depthFormat;
    VkDevice dev = m_device.device();

    std::array<VkAttachmentDescription, 2> a{};
    a[0].format = m_layerFormat;
    a[0].samples = VK_SAMPLE_COUNT_1_BIT;
    a[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    a[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    a[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    a[1].format = m_layerDepthFormat;
    a[1].samples = VK_SAMPLE_COUNT_1_BIT;
    a[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    a[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[1].stencilLoadOp = m_stencilAvailable ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;
    // Identical to SwapChain's: passes are only compatible when it matches.
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    passInfo.attachmentCount = static_cast<uint32_t>(a.size());
    passInfo.pAttachments = a.data();
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = &subpass;
    passInfo.dependencyCount = 1;
    passInfo.pDependencies = &dependency;
    VK_CHECK(vkCreateRenderPass(dev, &passInfo, nullptr, &m_layerPass));

    VkImageCreateInfo depthInfo{};
    depthInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthInfo.imageType = VK_IMAGE_TYPE_2D;
    depthInfo.extent = { m_layerExtent.width, m_layerExtent.height, 1 };
    depthInfo.mipLevels = 1;
    depthInfo.arrayLayers = 1;
    depthInfo.format = m_layerDepthFormat;
    depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    VK_CHECK(vmaCreateImage(m_device.allocator(), &depthInfo, &alloc, &m_layerDepth, &m_layerDepthAllocation, nullptr));
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_layerDepth;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_layerDepthFormat;
    viewInfo.subresourceRange = { static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT |
                                                                  (m_stencilAvailable ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)),
                                  0, 1, 0, 1 };
    VK_CHECK(vkCreateImageView(dev, &viewInfo, nullptr, &m_layerDepthView));

    for (auto& post : m_post) post = createLayerTarget();
    m_mask = createLayerTarget();
}

// Waits for the GPU: only on a resize or a format change.
void RmlVulkanRenderInterface::destroyLayerResources() {
    if (m_layerPass == VK_NULL_HANDLE) return;
    VkDevice dev = m_device.device();
    vkDeviceWaitIdle(dev);
    for (auto& layer : m_layers) destroyLayerTarget(*layer);
    m_layers.clear();
    for (auto& post : m_post) {
        destroyLayerTarget(*post);
        post.reset();
    }
    destroyLayerTarget(*m_mask);
    m_mask.reset();
    vkDestroyImageView(dev, m_layerDepthView, nullptr);
    vmaDestroyImage(m_device.allocator(), m_layerDepth, m_layerDepthAllocation);
    vkDestroyRenderPass(dev, m_layerPass, nullptr);
    m_layerDepthView = VK_NULL_HANDLE;
    m_layerDepth = VK_NULL_HANDLE;
    m_layerDepthAllocation = VK_NULL_HANDLE;
    m_layerPass = VK_NULL_HANDLE;
    m_layerExtent = {};
    m_layerFormat = m_layerDepthFormat = VK_FORMAT_UNDEFINED;
}

// A full-frame colour image: drawn into, sampled, copied from and to.
std::unique_ptr<RmlVulkanRenderInterface::LayerTarget> RmlVulkanRenderInterface::createLayerTarget() {
    auto t = std::make_unique<LayerTarget>();
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = { m_layerExtent.width, m_layerExtent.height, 1 };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.format = m_layerFormat;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    VK_CHECK(vmaCreateImage(m_device.allocator(), &info, &alloc, &t->image, &t->allocation, nullptr));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = t->image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_layerFormat;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VK_CHECK(vkCreateImageView(m_device.device(), &viewInfo, nullptr, &t->view));

    std::array<VkImageView, 2> attachments = { t->view, m_layerDepthView };
    VkFramebufferCreateInfo fb{};
    fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb.renderPass = m_layerPass;
    fb.attachmentCount = static_cast<uint32_t>(attachments.size());
    fb.pAttachments = attachments.data();
    fb.width = m_layerExtent.width;
    fb.height = m_layerExtent.height;
    fb.layers = 1;
    VK_CHECK(vkCreateFramebuffer(m_device.device(), &fb, nullptr, &t->framebuffer));

    t->descriptorSet = allocateAndWriteDescriptor(t->view);
    return t;
}

void RmlVulkanRenderInterface::destroyLayerTarget(LayerTarget& target) {
    VkDevice dev = m_device.device();
    if (target.descriptorSet) vkFreeDescriptorSets(dev, m_descriptorPool, 1, &target.descriptorSet);
    if (target.framebuffer) vkDestroyFramebuffer(dev, target.framebuffer, nullptr);
    if (target.view) vkDestroyImageView(dev, target.view, nullptr);
    if (target.image) vmaDestroyImage(m_device.allocator(), target.image, target.allocation);
    target = LayerTarget{};
}

// The frame's own pass and the layer pass both end with the colour in a
// known layout: PRESENT_SRC and COLOR_ATTACHMENT_OPTIMAL.
void RmlVulkanRenderInterface::endPass() {
    if (!m_passOpen) return;
    vkCmdEndRenderPass(m_currentCmd);
    m_passOpen = false;
    if (m_current) {
        m_current->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    } else {
        m_baseLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
}

// Begins drawing into a layer, or into the frame's own image again, and
// restores what the pass switch reset: viewport, scissor, the regular
// pipeline and the clip mask (the stencil is cleared by every begin).
void RmlVulkanRenderInterface::beginPass(LayerTarget* target) {
    std::array<VkClearValue, 2> clear{};
    clear[1].depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    if (target) {
        transition(*target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        info.renderPass = m_layerPass;
        info.framebuffer = target->framebuffer;
    } else {
        imageBarrier(m_currentCmd, m_base.image, m_baseLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        m_baseLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        info.renderPass = m_base.resumePass;
        info.framebuffer = m_base.framebuffer;
    }
    info.renderArea = { { 0, 0 }, m_layerExtent };
    info.clearValueCount = static_cast<uint32_t>(clear.size());
    info.pClearValues = clear.data();
    vkCmdBeginRenderPass(m_currentCmd, &info, VK_SUBPASS_CONTENTS_INLINE);
    m_passOpen = true;
    m_current = target;

    VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(m_layerExtent.width), static_cast<float>(m_layerExtent.height), 0.0f, 1.0f };
    vkCmdSetViewport(m_currentCmd, 0, 1, &viewport);
    vkCmdSetScissor(m_currentCmd, 0, 1, &m_scissor);
    for (const ClipMaskOp& op : m_clipOps) drawClipMask(op);
    m_pipeline->bind(m_currentCmd);
    applyStencilState();
}

// The scissor rectangle inside the frame (RmlUi's can reach past its edges).
VkRect2D RmlVulkanRenderInterface::clampedScissor() const {
    const int32_t x0 = std::clamp(m_scissor.offset.x, 0, static_cast<int32_t>(m_layerExtent.width));
    const int32_t y0 = std::clamp(m_scissor.offset.y, 0, static_cast<int32_t>(m_layerExtent.height));
    const int32_t x1 = std::clamp(m_scissor.offset.x + static_cast<int32_t>(m_scissor.extent.width), x0,
                                  static_cast<int32_t>(m_layerExtent.width));
    const int32_t y1 = std::clamp(m_scissor.offset.y + static_cast<int32_t>(m_scissor.extent.height), y0,
                                  static_cast<int32_t>(m_layerExtent.height));
    return { { x0, y0 }, { static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0) } };
}

namespace {
// One colour region copied between two images of the same format.
VkImageCopy copyRegion(VkRect2D from, VkOffset2D to) {
    VkImageCopy region{};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.srcOffset = { from.offset.x, from.offset.y, 0 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstOffset = { to.x, to.y, 0 };
    region.extent = { from.extent.width, from.extent.height, 1 };
    return region;
}
} // namespace

// The scissor rectangle of a layer (or of the frame's own image) into
// m_post[0], where the filters start. The frame's image can only be read
// once the swapchain is a copy source: until then the backdrop is empty and
// UiModule is asked, once, to make it one.
void RmlVulkanRenderInterface::copyToPostprocess(Rml::LayerHandle source) {
    endPass();
    LayerTarget& dst = *m_post[0];
    transition(dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkRect2D rect = clampedScissor();
    VkImage image = VK_NULL_HANDLE;
    if (LayerTarget* layer = layerTarget(source)) {
        transition(*layer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        image = layer->image;
    } else if (m_base.readable) {
        imageBarrier(m_currentCmd, m_base.image, m_baseLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        m_baseLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        image = m_base.image;
    } else {
        if (!m_readableAsked) m_readableRequest = m_readableAsked = true;
        VkClearColorValue transparent{};
        VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdClearColorImage(m_currentCmd, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &transparent, 1, &range);
        return;
    }
    if (rect.extent.width == 0 || rect.extent.height == 0) return;
    const VkImageCopy region = copyRegion(rect, rect.offset);
    vkCmdCopyImage(m_currentCmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

// One full-screen pass from source into destination (nullptr = the frame's
// own image), under the scissor rectangle. Leaves destination's pass open.
void RmlVulkanRenderInterface::drawFullscreen(LayerTarget* destination, LayerTarget& source, const Pipeline& pipeline,
                                              const RmlFilterPush& push, VkDescriptorSet mask) {
    endPass();
    transition(source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (mask == m_mask->descriptorSet) transition(*m_mask, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    beginPass(destination);
    pipeline.bind(m_currentCmd);
    // Only the composite pipelines test the clip mask (dynamic stencil state).
    if (&pipeline == m_compositeBlend.get() || &pipeline == m_compositeReplace.get()) applyStencilState();
    const std::array<VkDescriptorSet, 2> sets = { source.descriptorSet, mask ? mask : m_defaultTexture.descriptorSet };
    vkCmdBindDescriptorSets(m_currentCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0, static_cast<uint32_t>(sets.size()),
                            sets.data(), 0, nullptr);
    vkCmdPushConstants(m_currentCmd, pipeline.layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(m_currentCmd, 3, 1, 0, 0);
    m_pipeline->bind(m_currentCmd);
    applyStencilState();
}

// A Gaussian blur of image, in place: per pass of rmlBlurPlan, vertically
// into temp, then horizontally back.
void RmlVulkanRenderInterface::blur(float sigma, std::unique_ptr<LayerTarget>& image, std::unique_ptr<LayerTarget>& temp,
                                    const RmlFilterPush& region) {
    const RmlBlurPlan plan = rmlBlurPlan(sigma);
    RmlFilterPush push = region;
    push.mode = kRmlFilterBlur;
    push.sigma = plan.sigma;
    push.radius = plan.radius;
    for (int i = 0; i < plan.passes; ++i) {
        push.offset = glm::vec2(0.0f, 1.0f / static_cast<float>(m_layerExtent.height));
        drawFullscreen(temp.get(), *image, *m_filterReplace, push, VK_NULL_HANDLE);
        push.offset = glm::vec2(1.0f / static_cast<float>(m_layerExtent.width), 0.0f);
        drawFullscreen(image.get(), *temp, *m_filterReplace, push, VK_NULL_HANDLE);
    }
}

// Runs each filter on m_post[0] in order; the result ends in m_post[0].
void RmlVulkanRenderInterface::applyFilters(Rml::Span<const Rml::CompiledFilterHandle> filters) {
    const glm::vec2 size(static_cast<float>(m_layerExtent.width), static_cast<float>(m_layerExtent.height));
    const VkRect2D rect = clampedScissor();
    RmlFilterPush region;
    region.uvMin = (glm::vec2(static_cast<float>(rect.offset.x), static_cast<float>(rect.offset.y)) + 0.5f) / size;
    region.uvMax = (glm::vec2(static_cast<float>(rect.offset.x + static_cast<int32_t>(rect.extent.width)),
                              static_cast<float>(rect.offset.y + static_cast<int32_t>(rect.extent.height))) - 0.5f) / size;
    for (Rml::CompiledFilterHandle handle : filters) {
        auto it = m_filters.find(static_cast<uintptr_t>(handle));
        if (it == m_filters.end()) continue;
        const RmlFilter& f = it->second;
        RmlFilterPush push = region;
        switch (f.type) {
            case RmlFilterType::Opacity:
                push.mode = kRmlFilterOpacity;
                push.opacity = f.opacity;
                drawFullscreen(m_post[1].get(), *m_post[0], *m_filterReplace, push, VK_NULL_HANDLE);
                std::swap(m_post[0], m_post[1]);
                break;
            case RmlFilterType::ColorMatrix:
                push.mode = kRmlFilterColorMatrix;
                push.matrix = f.matrix;
                drawFullscreen(m_post[1].get(), *m_post[0], *m_filterReplace, push, VK_NULL_HANDLE);
                std::swap(m_post[0], m_post[1]);
                break;
            case RmlFilterType::Blur:
                blur(f.sigma, m_post[0], m_post[1], region);
                break;
            case RmlFilterType::DropShadow:
                // The shadow into m_post[1], blurred, then the source drawn over it.
                push.mode = kRmlFilterDropShadow;
                push.color = f.color;
                push.offset = f.offset / size;
                drawFullscreen(m_post[1].get(), *m_post[0], *m_filterReplace, push, VK_NULL_HANDLE);
                blur(f.sigma, m_post[1], m_post[2], region);
                push = region;
                push.mode = kRmlFilterOpacity;
                drawFullscreen(m_post[1].get(), *m_post[0], *m_filterBlend, push, VK_NULL_HANDLE);
                std::swap(m_post[0], m_post[1]);
                break;
            case RmlFilterType::MaskImage:
                push.mode = kRmlFilterMask;
                drawFullscreen(m_post[1].get(), *m_post[0], *m_filterReplace, push, m_mask->descriptorSet);
                std::swap(m_post[0], m_post[1]);
                break;
            case RmlFilterType::Invalid:
                break;
        }
    }
}

// A new layer on top, cleared to transparent inside the scissor rectangle.
Rml::LayerHandle RmlVulkanRenderInterface::PushLayer() {
    if (m_layerPass == VK_NULL_HANDLE || m_currentCmd == VK_NULL_HANDLE) return 0;
    const Rml::LayerHandle handle = m_layerStack.size() + 1;
    while (m_layers.size() < handle) m_layers.push_back(createLayerTarget());
    m_layerStack.push_back(handle);
    endPass();
    beginPass(m_layers[handle - 1].get());
    const VkRect2D rect = clampedScissor();
    if (rect.extent.width > 0 && rect.extent.height > 0) {
        VkClearAttachment clear{};
        clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clear.colorAttachment = 0;
        VkClearRect clearRect{ rect, 0, 1 };
        vkCmdClearAttachments(m_currentCmd, 1, &clear, 1, &clearRect);
    }
    return handle;
}

// source's scissor rectangle, filtered, onto destination; then back to the top layer.
void RmlVulkanRenderInterface::CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode,
                                               Rml::Span<const Rml::CompiledFilterHandle> filters) {
    if (m_layerPass == VK_NULL_HANDLE || m_currentCmd == VK_NULL_HANDLE) return;
    copyToPostprocess(source);
    applyFilters(filters);
    RmlFilterPush push;
    push.mode = kRmlFilterOpacity;
    const Pipeline& pipeline = blend_mode == Rml::BlendMode::Replace ? *m_compositeReplace : *m_compositeBlend;
    drawFullscreen(layerTarget(destination), *m_post[0], pipeline, push, VK_NULL_HANDLE);
    LayerTarget* top = layerTarget(topLayer());
    if (top != m_current) {
        endPass();
        beginPass(top);
    }
}

void RmlVulkanRenderInterface::PopLayer() {
    if (m_layerStack.empty()) return;
    m_layerStack.pop_back();
    endPass();
    beginPass(layerTarget(topLayer()));
}

// The top layer's scissor rectangle as a new texture (a box-shadow's image).
Rml::TextureHandle RmlVulkanRenderInterface::SaveLayerAsTexture() {
    LayerTarget* top = layerTarget(topLayer());
    const VkRect2D rect = clampedScissor();
    if (!top || rect.extent.width == 0 || rect.extent.height == 0) return 0;

    CompiledTexture texture;
    texture.linear = true;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = { rect.extent.width, rect.extent.height, 1 };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.format = m_layerFormat;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    VK_CHECK(vmaCreateImage(m_device.allocator(), &info, &alloc, &texture.image, &texture.allocation, nullptr));
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_layerFormat;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VK_CHECK(vkCreateImageView(m_device.device(), &viewInfo, nullptr, &texture.view));
    texture.descriptorSet = allocateAndWriteDescriptor(texture.view);

    endPass();
    transition(*top, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    imageBarrier(m_currentCmd, texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkImageCopy region = copyRegion(rect, { 0, 0 });
    vkCmdCopyImage(m_currentCmd, top->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &region);
    imageBarrier(m_currentCmd, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    beginPass(top);

    const uintptr_t handle = m_nextTextureHandle++;
    m_textures[handle] = texture;
    return handle;
}

// The top layer into m_mask; the returned filter multiplies by its alpha.
Rml::CompiledFilterHandle RmlVulkanRenderInterface::SaveLayerAsMaskImage() {
    LayerTarget* top = layerTarget(topLayer());
    if (!top) return 0;
    endPass();
    transition(*top, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transition(*m_mask, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkImageCopy region = copyRegion({ { 0, 0 }, m_layerExtent }, { 0, 0 });
    vkCmdCopyImage(m_currentCmd, top->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_mask->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &region);
    beginPass(top);
    RmlFilter f;
    f.type = RmlFilterType::MaskImage;
    const uintptr_t handle = m_nextFilterHandle++;
    m_filters[handle] = f;
    return static_cast<Rml::CompiledFilterHandle>(handle);
}

Rml::CompiledFilterHandle RmlVulkanRenderInterface::CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) {
    const RmlFilter f = compileRmlFilter(name, parameters);
    if (f.type == RmlFilterType::Invalid) {
        log::get("UI")->warn("RmlVulkanRenderInterface: unsupported filter '{}'", name);
        return 0;
    }
    const uintptr_t handle = m_nextFilterHandle++;
    m_filters[handle] = f;
    return static_cast<Rml::CompiledFilterHandle>(handle);
}

void RmlVulkanRenderInterface::ReleaseFilter(Rml::CompiledFilterHandle filter) {
    m_filters.erase(static_cast<uintptr_t>(filter));
}

// RmlUi pops every layer it pushes; this only makes sure the frame's own
// image is drawn into again whatever happened.
void RmlVulkanRenderInterface::endFrame() {
    m_layerStack.clear();
    if (m_currentCmd == VK_NULL_HANDLE || (m_passOpen && m_current == nullptr)) return;
    endPass();
    beginPass(nullptr);
}

bool RmlVulkanRenderInterface::takeReadableRequest() {
    return std::exchange(m_readableRequest, false);
}

} // namespace kke
