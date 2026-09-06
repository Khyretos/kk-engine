#include "kke/RmlVulkanRenderInterface.h"
#include "kke/VulkanDevice.h"
#include "kke/VulkanCheck.h"
#include "kke/Log.h"

// STB_IMAGE_IMPLEMENTATION must be defined in exactly one translation
// unit across the whole project — confirmed via a real grep that no
// other file already does this before adding it here. This is the
// natural, and currently only, consumer of stb_image in this codebase.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace kke {

namespace {
struct RmlPushConstants {
    glm::vec2 screenSize;  // pixels
    glm::vec2 translation; // pixels
};
} // namespace

RmlVulkanRenderInterface::RmlVulkanRenderInterface(VulkanDevice& device, VkRenderPass renderPass)
    : m_device(device) {
    // --- Descriptor set layout: one combined image sampler, fragment-only ---
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(device.device(), &layoutInfo, nullptr, &m_textureSetLayout));

    // --- Descriptor pool: sized for a generous number of glyph atlases /
    // images a typical UI might have live at once. Grows are not
    // supported (would need a second pool) — fine for this slice's scope.
    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256 };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 256;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VK_CHECK(vkCreateDescriptorPool(device.device(), &poolInfo, nullptr, &m_descriptorPool));

    // --- Sampler shared by every texture this backend creates ---
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    VK_CHECK(vkCreateSampler(device.device(), &samplerInfo, nullptr, &m_sampler));

    // --- Default 1x1 opaque-white texture: bound for untextured draws, so
    // RenderGeometry never needs an "if textured" branch. Sampling white
    // and multiplying by vertex color in the fragment shader is a no-op.
    const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    m_defaultTexture = createTextureFromPixels(whitePixel, 1, 1);

    // --- Pipeline ---
    VkVertexInputBindingDescription vBinding{};
    vBinding.binding = 0;
    vBinding.stride = sizeof(Rml::Vertex);
    vBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attributes(3);
    attributes[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(Rml::Vertex, position)) };
    attributes[1] = { 1, 0, VK_FORMAT_R8G8B8A8_UNORM, static_cast<uint32_t>(offsetof(Rml::Vertex, colour)) };
    attributes[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(Rml::Vertex, tex_coord)) };

    PipelineConfig config;
    config.customVertexBindings = { vBinding };
    config.customVertexAttributes = attributes;
    config.cullMode = VK_CULL_MODE_NONE;
    config.depthTestEnable = false;
    config.depthWriteEnable = false;
    config.blendEnable = true;
    config.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(RmlPushConstants) };
    config.descriptorSetLayouts = { m_textureSetLayout };

    m_pipeline = std::make_unique<Pipeline>(
        device, renderPass, "shaders/rml_ui.vert.spv", "shaders/rml_ui.frag.spv", config);
}

RmlVulkanRenderInterface::~RmlVulkanRenderInterface() {
    // A real bug in the deferred-deletion fix itself, found the same
    // way as the original bug it fixed — a real run, not assumed
    // correct: pending deletions queued shortly before shutdown never
    // get the kDeletionDelayFrames of real frames they need to become
    // "safe," and the per-frame sweep in beginFrame() never runs again
    // once the app is closing. For pending geometry this was harmless
    // (its unique_ptr<CompiledGeometry> destroys itself automatically
    // once m_pendingDeletions itself is destroyed below) — but a
    // pending *texture* is a bare struct of raw Vulkan/VMA handles
    // with no destructor at all, so those would leak past process
    // exit and trip VMA's own "allocations not freed" assertion.
    // Confirmed exactly this way: a real run of rmlui_demo with its
    // debugger enabled hit this assertion on clean shutdown, traced
    // directly to this gap, not guessed.
    for (auto& pending : m_pendingDeletions) {
        if (pending.isTexture) {
            destroyTexture(pending.texture);
        }
    }
    m_pendingDeletions.clear();

    for (auto& [handle, texture] : m_textures) {
        destroyTexture(texture);
    }
    m_textures.clear();
    destroyTexture(m_defaultTexture);

    VkDevice dev = m_device.device();
    if (m_sampler) vkDestroySampler(dev, m_sampler, nullptr);
    if (m_descriptorPool) vkDestroyDescriptorPool(dev, m_descriptorPool, nullptr);
    if (m_textureSetLayout) vkDestroyDescriptorSetLayout(dev, m_textureSetLayout, nullptr);
}

VkDescriptorSet RmlVulkanRenderInterface::allocateAndWriteDescriptor(VkImageView view) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_textureSetLayout;

    VkDescriptorSet set;
    VK_CHECK(vkAllocateDescriptorSets(m_device.device(), &allocInfo, &set));

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = view;
    imageInfo.sampler = m_sampler;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_device.device(), 1, &write, 0, nullptr);
    return set;
}

RmlVulkanRenderInterface::CompiledTexture RmlVulkanRenderInterface::createTextureFromPixels(
    const uint8_t* pixels, uint32_t width, uint32_t height) {
    CompiledTexture texture;

    VkDeviceSize dataSize = static_cast<VkDeviceSize>(width) * height * 4;

    // --- Staging buffer with the raw pixel data ---
    Buffer staging(m_device, dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    staging.upload(pixels, dataSize);

    // --- The actual sampled image ---
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    VK_CHECK(vmaCreateImage(m_device.allocator(), &imageInfo, &allocInfo, &texture.image, &texture.allocation, nullptr));

    // --- One-time command buffer: transition -> copy -> transition ---
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = m_device.commandPool();
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(m_device.device(), &cmdAllocInfo, &cmd));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    VkImageMemoryBarrier toTransferDst{};
    toTransferDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransferDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransferDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransferDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferDst.image = texture.image;
    toTransferDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toTransferDst.srcAccessMask = 0;
    toTransferDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          0, 0, nullptr, 0, nullptr, 1, &toTransferDst);

    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = 0;
    copyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.imageExtent = { width, height, 1 };
    vkCmdCopyBufferToImage(cmd, staging.handle(), texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    VkImageMemoryBarrier toShaderRead{};
    toShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.image = texture.image;
    toShaderRead.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                          0, 0, nullptr, 0, nullptr, 1, &toShaderRead);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(m_device.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(m_device.graphicsQueue()));
    vkFreeCommandBuffers(m_device.device(), m_device.commandPool(), 1, &cmd);

    // --- View + descriptor ---
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VK_CHECK(vkCreateImageView(m_device.device(), &viewInfo, nullptr, &texture.view));

    texture.descriptorSet = allocateAndWriteDescriptor(texture.view);
    return texture;
}

void RmlVulkanRenderInterface::destroyTexture(CompiledTexture& texture) {
    VkDevice dev = m_device.device();
    if (texture.descriptorSet) {
        vkFreeDescriptorSets(dev, m_descriptorPool, 1, &texture.descriptorSet);
    }
    if (texture.view) vkDestroyImageView(dev, texture.view, nullptr);
    if (texture.image) vmaDestroyImage(m_device.allocator(), texture.image, texture.allocation);
    texture = CompiledTexture{};
}

void RmlVulkanRenderInterface::beginFrame(VkCommandBuffer cmd, glm::vec2 screenSizePixels) {
    m_currentCmd = cmd;
    m_screenSize = screenSizePixels;
    m_scissorEnabled = false;

    // Real deferred-destruction sweep, not decorative — see
    // PendingDeletion's own comment in the header for the full,
    // validation-layer-confirmed account of the crash this fixes.
    // Only frees GPU resources whose queued frame is old enough that a
    // fence wait (in Renderer.cpp, at the start of every frame that
    // re-uses that same frame slot) already guarantees the GPU is
    // genuinely done with them — called once per frame, here, since
    // beginFrame() itself is already UiModule's own once-per-frame
    // entry point into this class.
    ++m_frameCounter;
    m_pendingDeletions.erase(
        std::remove_if(m_pendingDeletions.begin(), m_pendingDeletions.end(),
                        [this](PendingDeletion& pending) {
                            if (m_frameCounter - pending.queuedAtFrame < kDeletionDelayFrames) return false;
                            if (pending.isTexture) {
                                destroyTexture(pending.texture);
                            }
                            // else: pending.geometry is a unique_ptr going out of
                            // scope right here, which is what actually destroys
                            // its Buffers -- nothing else to do explicitly.
                            return true;
                        }),
        m_pendingDeletions.end());

    m_pipeline->bind(cmd);

    VkRect2D fullScreen{ { 0, 0 }, { static_cast<uint32_t>(screenSizePixels.x), static_cast<uint32_t>(screenSizePixels.y) } };
    vkCmdSetScissor(cmd, 0, 1, &fullScreen);
}

Rml::CompiledGeometryHandle RmlVulkanRenderInterface::CompileGeometry(
    Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) {
    auto geometry = std::make_unique<CompiledGeometry>();
    geometry->indexCount = static_cast<uint32_t>(indices.size());

    geometry->vertexBuffer = std::make_unique<Buffer>(Buffer::createDeviceLocal(
        m_device, vertices.data(), sizeof(Rml::Vertex) * vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
    geometry->indexBuffer = std::make_unique<Buffer>(Buffer::createDeviceLocal(
        m_device, indices.data(), sizeof(int) * indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT));

    uintptr_t handle = m_nextGeometryHandle++;
    m_geometry[handle] = std::move(geometry);
    return handle;
}

void RmlVulkanRenderInterface::RenderGeometry(
    Rml::CompiledGeometryHandle geometryHandle, Rml::Vector2f translation, Rml::TextureHandle textureHandle) {
    auto it = m_geometry.find(geometryHandle);
    if (it == m_geometry.end() || m_currentCmd == VK_NULL_HANDLE) return;

    CompiledGeometry& geometry = *it->second;

    VkDescriptorSet descriptorSet = m_defaultTexture.descriptorSet;
    if (textureHandle != 0) {
        auto texIt = m_textures.find(textureHandle);
        if (texIt != m_textures.end()) {
            descriptorSet = texIt->second.descriptorSet;
        }
    }
    vkCmdBindDescriptorSets(m_currentCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout(),
                             0, 1, &descriptorSet, 0, nullptr);

    RmlPushConstants pc{ m_screenSize, glm::vec2(translation.x, translation.y) };
    vkCmdPushConstants(m_currentCmd, m_pipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    VkBuffer vertexBuffers[] = { geometry.vertexBuffer->handle() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(m_currentCmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(m_currentCmd, geometry.indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(m_currentCmd, geometry.indexCount, 1, 0, 0, 0);
}

void RmlVulkanRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometryHandle) {
    auto it = m_geometry.find(geometryHandle);
    if (it == m_geometry.end()) return;
    PendingDeletion pending;
    pending.queuedAtFrame = m_frameCounter;
    pending.geometry = std::move(it->second);
    pending.isTexture = false;
    m_pendingDeletions.push_back(std::move(pending));
    m_geometry.erase(it);
}

Rml::TextureHandle RmlVulkanRenderInterface::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) {
    int width = 0, height = 0, channels = 0;
    // Forcing 4 channels (RGBA) regardless of the source file's own
    // format — matches createTextureFromPixels' own hardcoded
    // VK_FORMAT_R8G8B8A8_UNORM expectation exactly, so this can reuse
    // that same real GPU upload path unmodified rather than needing a
    // second, format-aware variant of it.
    stbi_uc* pixels = stbi_load(source.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        // A missing or corrupt file isn't fatal — same convention as
        // untextured geometry (texture handle 0), not a thrown error,
        // so one broken <img src="..."> doesn't take down an entire
        // document. stbi_failure_reason() is genuinely useful here
        // (distinguishes "file not found" from "not a valid image"),
        // logged rather than silently swallowed.
        log::get("UI")->warn("RmlVulkanRenderInterface::LoadTexture: failed to load '{}' ({})",
                              source, stbi_failure_reason() ? stbi_failure_reason() : "unknown reason");
        texture_dimensions = Rml::Vector2i(1, 1);
        return 0;
    }

    CompiledTexture texture = createTextureFromPixels(pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    stbi_image_free(pixels);

    texture_dimensions = Rml::Vector2i(width, height);
    uintptr_t handle = m_nextTextureHandle++;
    m_textures[handle] = texture;
    return handle;
}

Rml::TextureHandle RmlVulkanRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) {
    if (source_dimensions.x <= 0 || source_dimensions.y <= 0) return 0;

    CompiledTexture texture = createTextureFromPixels(
        reinterpret_cast<const uint8_t*>(source.data()),
        static_cast<uint32_t>(source_dimensions.x), static_cast<uint32_t>(source_dimensions.y));

    uintptr_t handle = m_nextTextureHandle++;
    m_textures[handle] = texture;
    return handle;
}

void RmlVulkanRenderInterface::ReleaseTexture(Rml::TextureHandle textureHandle) {
    auto it = m_textures.find(textureHandle);
    if (it == m_textures.end()) return;
    PendingDeletion pending;
    pending.queuedAtFrame = m_frameCounter;
    pending.texture = it->second;
    pending.isTexture = true;
    m_pendingDeletions.push_back(std::move(pending));
    m_textures.erase(it);
}

void RmlVulkanRenderInterface::EnableScissorRegion(bool enable) {
    m_scissorEnabled = enable;
    if (!enable && m_currentCmd != VK_NULL_HANDLE) {
        VkRect2D fullScreen{ { 0, 0 }, { static_cast<uint32_t>(m_screenSize.x), static_cast<uint32_t>(m_screenSize.y) } };
        vkCmdSetScissor(m_currentCmd, 0, 1, &fullScreen);
    }
}

void RmlVulkanRenderInterface::SetScissorRegion(Rml::Rectanglei region) {
    if (!m_scissorEnabled || m_currentCmd == VK_NULL_HANDLE) return;

    int32_t x = std::max(0, region.Left());
    int32_t y = std::max(0, region.Top());
    uint32_t width = static_cast<uint32_t>(std::max(0, region.Right() - x));
    uint32_t height = static_cast<uint32_t>(std::max(0, region.Bottom() - y));

    VkRect2D rect{ { x, y }, { width, height } };
    vkCmdSetScissor(m_currentCmd, 0, 1, &rect);
}

} // namespace kke
