#include "kke/FluidSurface.h"

#include "kke/Application.h"
#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <cmath>

namespace kke {

namespace {
struct BlurPush { glm::ivec2 dir; float projScale; float worldRadius; float depthFalloff; };
struct CompositePush { glm::mat4 invProj; glm::mat4 invView; };
} // namespace

FluidSurfaceRenderer::FluidSurfaceRenderer(Application& app) : m_app(app), m_device(app.device().device()) {
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_NEAREST; // depth must not be interpolated across silhouettes
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(m_device, &si, nullptr, &m_sampler));

    auto makeLayout = [&](VkDescriptorType type, VkShaderStageFlags stage) {
        VkDescriptorSetLayoutBinding b[2]{};
        for (uint32_t i = 0; i < 2; ++i) b[i] = { i, type, 1, stage, nullptr };
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 2;
        li.pBindings = b;
        VkDescriptorSetLayout layout;
        VK_CHECK(vkCreateDescriptorSetLayout(m_device, &li, nullptr, &layout));
        return layout;
    };
    m_blurLayout = makeLayout(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);
    m_sampleLayout = makeLayout(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    VkDescriptorPoolSize sizes[2] = { { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4 }, { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 } };
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = 3;
    pi.poolSizeCount = 2;
    pi.pPoolSizes = sizes;
    VK_CHECK(vkCreateDescriptorPool(m_device, &pi, nullptr, &m_pool));
    VkDescriptorSetLayout layouts[3] = { m_blurLayout, m_blurLayout, m_sampleLayout };
    VkDescriptorSet sets[3];
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_pool;
    ai.descriptorSetCount = 3;
    ai.pSetLayouts = layouts;
    VK_CHECK(vkAllocateDescriptorSets(m_device, &ai, sets));
    m_blurAB = sets[0];
    m_blurBA = sets[1];
    m_sampleSet = sets[2];

    // Blur compute pipeline.
    VkPushConstantRange range{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BlurPush) };
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &m_blurLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &range;
    VK_CHECK(vkCreatePipelineLayout(m_device, &pl, nullptr, &m_blurPipelineLayout));
    VkShaderModule comp = loadShaderModule(app.device(), "shaders/fluid_blur.comp.spv");
    VkComputePipelineCreateInfo cp{};
    cp.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cp.stage.module = comp;
    cp.stage.pName = "main";
    cp.layout = m_blurPipelineLayout;
    VK_CHECK(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &cp, nullptr, &m_blurPipeline));
    vkDestroyShaderModule(m_device, comp, nullptr);

    // Offscreen pass: [0] linear depth R32F, [1] colour+glow RGBA8, [2] depth.
    VkAttachmentDescription att[3]{};
    att[0] = { 0, VK_FORMAT_R32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
               VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL };
    att[1] = { 0, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
               VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    att[2] = { 0, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
               VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkAttachmentReference colorRefs[2] = { { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }, { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } };
    VkAttachmentReference depthRef{ 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 2;
    sub.pColorAttachments = colorRefs;
    sub.pDepthStencilAttachment = &depthRef;
    VkSubpassDependency deps[2]{};
    // Last frame's blur/composite reads finish before we overwrite.
    deps[0] = { VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0 };
    // Our writes are visible to the blur (compute) and composite (fragment).
    deps[1] = { 0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT, 0 };
    VkRenderPassCreateInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 3;
    rp.pAttachments = att;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 2;
    rp.pDependencies = deps;
    VK_CHECK(vkCreateRenderPass(m_device, &rp, nullptr, &m_pass));

    PipelineConfig pc;
    pc.cullMode = VK_CULL_MODE_NONE;
    pc.colorAttachmentCount = 2;
    pc.pushConstantRange = { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4) };
    pc.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout() };
    m_particlePipeline = std::make_unique<Pipeline>(app.device(), m_pass, "shaders/impostor.vert.spv", "shaders/fluid_depth.frag.spv", pc);

    PipelineConfig cc;
    cc.useVertexInput = false;
    cc.cullMode = VK_CULL_MODE_NONE;
    cc.pushConstantRange = { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CompositePush) };
    cc.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), m_sampleLayout };
    m_compositePipeline = std::make_unique<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/sky.vert.spv",
                                                     "shaders/fluid_composite.frag.spv", cc);
}

FluidSurfaceRenderer::~FluidSurfaceRenderer() {
    vkDeviceWaitIdle(m_device);
    destroyTargets();
    m_particlePipeline.reset();
    m_compositePipeline.reset();
    if (m_blurPipeline) vkDestroyPipeline(m_device, m_blurPipeline, nullptr);
    if (m_blurPipelineLayout) vkDestroyPipelineLayout(m_device, m_blurPipelineLayout, nullptr);
    if (m_pool) vkDestroyDescriptorPool(m_device, m_pool, nullptr);
    if (m_blurLayout) vkDestroyDescriptorSetLayout(m_device, m_blurLayout, nullptr);
    if (m_sampleLayout) vkDestroyDescriptorSetLayout(m_device, m_sampleLayout, nullptr);
    if (m_pass) vkDestroyRenderPass(m_device, m_pass, nullptr);
    if (m_sampler) vkDestroySampler(m_device, m_sampler, nullptr);
}

FluidSurfaceRenderer::Image FluidSurfaceRenderer::makeImage(VkExtent2D e, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
    Image img;
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = { e.width, e.height, 1 };
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo ac{};
    ac.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    VK_CHECK(vmaCreateImage(m_app.device().allocator(), &ii, &ac, &img.image, &img.alloc, nullptr));
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = img.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = { aspect, 0, 1, 0, 1 };
    VK_CHECK(vkCreateImageView(m_device, &vi, nullptr, &img.view));
    return img;
}

void FluidSurfaceRenderer::destroyImage(Image& img) {
    if (img.view) vkDestroyImageView(m_device, img.view, nullptr);
    if (img.image) vmaDestroyImage(m_app.device().allocator(), img.image, img.alloc);
    img = {};
}

void FluidSurfaceRenderer::destroyTargets() {
    if (m_framebuffer) vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
    m_framebuffer = VK_NULL_HANDLE;
    destroyImage(m_depth);
    destroyImage(m_z);
    destroyImage(m_zTmp);
    destroyImage(m_color);
    m_extent = { 0, 0 };
}

void FluidSurfaceRenderer::createTargets(VkExtent2D e) {
    m_z = makeImage(e, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT);
    m_zTmp = makeImage(e, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    m_color = makeImage(e, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    m_depth = makeImage(e, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    VkImageView views[3] = { m_z.view, m_color.view, m_depth.view };
    VkFramebufferCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = m_pass;
    fi.attachmentCount = 3;
    fi.pAttachments = views;
    fi.width = e.width;
    fi.height = e.height;
    fi.layers = 1;
    VK_CHECK(vkCreateFramebuffer(m_device, &fi, nullptr, &m_framebuffer));
    m_extent = e;

    VkDescriptorImageInfo zInfo{ VK_NULL_HANDLE, m_z.view, VK_IMAGE_LAYOUT_GENERAL };
    VkDescriptorImageInfo tmpInfo{ VK_NULL_HANDLE, m_zTmp.view, VK_IMAGE_LAYOUT_GENERAL };
    VkDescriptorImageInfo zSample{ m_sampler, m_z.view, VK_IMAGE_LAYOUT_GENERAL };
    VkDescriptorImageInfo colorSample{ m_sampler, m_color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet w[6]{};
    auto write = [&](int i, VkDescriptorSet set, uint32_t binding, VkDescriptorType type, const VkDescriptorImageInfo* info) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = set;
        w[i].dstBinding = binding;
        w[i].descriptorCount = 1;
        w[i].descriptorType = type;
        w[i].pImageInfo = info;
    };
    write(0, m_blurAB, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &zInfo);
    write(1, m_blurAB, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &tmpInfo);
    write(2, m_blurBA, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &tmpInfo);
    write(3, m_blurBA, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &zInfo);
    write(4, m_sampleSet, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &zSample);
    write(5, m_sampleSet, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &colorSample);
    vkUpdateDescriptorSets(m_device, 6, w, 0, nullptr);
}

void FluidSurfaceRenderer::prepass(const PrepassContext& ctx, const std::vector<Particle>& particles) {
    m_hasContent = false;
    if (particles.empty() || ctx.extent.width == 0 || ctx.extent.height == 0) return;
    if (ctx.extent.width != m_extent.width || ctx.extent.height != m_extent.height) {
        vkDeviceWaitIdle(m_device); // resize only: descriptors and images are about to change
        destroyTargets();
        createTargets(ctx.extent);
    }
    // Particles -> quads (same packing as SphereImpostorRenderer).
    static const glm::vec2 corners[6] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } };
    m_scratch.resize(particles.size() * 6);
    for (size_t i = 0; i < particles.size(); ++i) {
        const Particle& p = particles[i];
        for (int k = 0; k < 6; ++k) m_scratch[i * 6 + k] = Vertex{ p.center, p.color, glm::vec3(p.radius, p.glow, 0.5f), corners[k] };
    }
    const uint32_t f = ctx.frameIndex;
    if (m_capacity[f] < m_scratch.size()) {
        m_capacity[f] = std::max<size_t>(m_scratch.size() * 3 / 2, 6 * 512);
        m_buffers[f] = std::make_unique<Buffer>(m_app.device(), m_capacity[f] * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
    m_buffers[f]->upload(m_scratch.data(), m_scratch.size() * sizeof(Vertex));

    // 1. Spheres -> linear depth + colour.
    VkClearValue clears[3];
    clears[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } }; // 0 = no liquid here
    clears[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    clears[2].depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    bi.renderPass = m_pass;
    bi.framebuffer = m_framebuffer;
    bi.renderArea = { { 0, 0 }, m_extent };
    bi.clearValueCount = 3;
    bi.pClearValues = clears;
    vkCmdBeginRenderPass(ctx.cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{ 0.0f, 0.0f, float(m_extent.width), float(m_extent.height), 0.0f, 1.0f };
    VkRect2D sc{ { 0, 0 }, m_extent };
    vkCmdSetViewport(ctx.cmd, 0, 1, &vp);
    vkCmdSetScissor(ctx.cmd, 0, 1, &sc);
    m_particlePipeline->bind(ctx.cmd);
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_particlePipeline->layout(), 0, 1, &ctx.lightingDescriptorSet, 0, nullptr);
    glm::vec4 forward(-ctx.view[0][2], -ctx.view[1][2], -ctx.view[2][2], 0.0f);
    vkCmdPushConstants(ctx.cmd, m_particlePipeline->layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(forward), &forward);
    VkBuffer vb = m_buffers[f]->handle();
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(ctx.cmd, 0, 1, &vb, &off);
    vkCmdDraw(ctx.cmd, static_cast<uint32_t>(m_scratch.size()), 1, 0, 0);
    vkCmdEndRenderPass(ctx.cmd);

    // 2. Bilateral blur, z -> tmp -> z, in GENERAL layout.
    VkImageMemoryBarrier toGeneral{};
    toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // fully overwritten by the first dispatch
    toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGeneral.srcQueueFamilyIndex = toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.image = m_zTmp.image;
    toGeneral.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(ctx.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toGeneral);
    vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_blurPipeline);
    const float projScale = float(m_extent.height) * std::fabs(ctx.proj[1][1]) * 0.5f;
    const uint32_t gx = (m_extent.width + 15) / 16, gy = (m_extent.height + 15) / 16;
    VkMemoryBarrier mb{};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    for (int it = 0; it < std::max(1, m_settings.blurIterations); ++it) {
        for (int pass = 0; pass < 2; ++pass) {
            // Alternate H-then-V and V-then-H between iterations: always
            // ending on the same direction left streaks along it.
            const bool horizontal = (pass == 0) == (it % 2 == 0);
            BlurPush bp{ horizontal ? glm::ivec2(1, 0) : glm::ivec2(0, 1), projScale, m_settings.blurWorldRadius, m_settings.depthFalloff };
            VkDescriptorSet set = pass == 0 ? m_blurAB : m_blurBA;
            vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_blurPipelineLayout, 0, 1, &set, 0, nullptr);
            vkCmdPushConstants(ctx.cmd, m_blurPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(bp), &bp);
            vkCmdDispatch(ctx.cmd, gx, gy, 1);
            vkCmdPipelineBarrier(ctx.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
        }
    }
    m_invProj = glm::inverse(ctx.proj);
    m_invView = glm::inverse(ctx.view);
    m_hasContent = true;
}

void FluidSurfaceRenderer::draw(const RenderContext& ctx) {
    if (!m_hasContent) return;
    m_compositePipeline->bind(ctx.cmd);
    VkDescriptorSet sets[2] = { ctx.lightingDescriptorSet, m_sampleSet };
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePipeline->layout(), 0, 2, sets, 0, nullptr);
    CompositePush pc{ m_invProj, m_invView };
    vkCmdPushConstants(ctx.cmd, m_compositePipeline->layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
}

} // namespace kke
