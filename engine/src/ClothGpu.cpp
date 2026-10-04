#include "kke/ClothGpu.h"

#include "kke/Buffer.h"
#include "kke/Log.h"
#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace kke {

namespace {

// shaders/cloth_pairs.comp's header: where the answers may go.
constexpr uint32_t kVtCapWord = 7, kEeCapWord = 8;

} // namespace

struct ClothGpu::Impl {
    VulkanDevice& device;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptors = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    // In: staged on the CPU, copied to the GPU. Out: the counters and a
    // flag per vertex (read back by a copy), and the pairs (written where
    // the CPU reads them).
    std::unique_ptr<Buffer> staging, input, out, readback, vtPairs, eePairs;
    size_t inputBytes = 0, outBytes = 0, vtBytes = 0, eeBytes = 0, stagingBytes = 0, readbackBytes = 0;
    uint32_t vtCap = 16384, eeCap = 16384;
    std::mutex lock;

    explicit Impl(VulkanDevice& d) : device(d), dev(d.device()) {}
    ~Impl() {
        if (dev == VK_NULL_HANDLE) return;
        if (fence) vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
        staging.reset();
        input.reset();
        out.reset();
        readback.reset();
        vtPairs.reset();
        eePairs.reset();
        if (pipeline) vkDestroyPipeline(dev, pipeline, nullptr);
        if (layout) vkDestroyPipelineLayout(dev, layout, nullptr);
        if (descriptors) vkDestroyDescriptorPool(dev, descriptors, nullptr);
        if (setLayout) vkDestroyDescriptorSetLayout(dev, setLayout, nullptr);
        if (fence) vkDestroyFence(dev, fence, nullptr);
        if (pool) vkDestroyCommandPool(dev, pool, nullptr);
    }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void init() {
        VkCommandPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pi.queueFamilyIndex = family;
        VK_CHECK(vkCreateCommandPool(dev, &pi, nullptr, &pool));
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(dev, &ai, &cmd));
        // Made signalled: ~Impl waits on it, and a game with no cloth never
        // submits, so an unsignalled fence there hung quitting forever.
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(dev, &fi, nullptr, &fence));

        VkDescriptorSetLayoutBinding b[4]{};
        for (uint32_t i = 0; i < 4; ++i) {
            b[i].binding = i;
            b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[i].descriptorCount = 1;
            b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 4;
        li.pBindings = b;
        VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &setLayout));
        VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 };
        VkDescriptorPoolCreateInfo dp{};
        dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dp.maxSets = 1;
        dp.poolSizeCount = 1;
        dp.pPoolSizes = &size;
        VK_CHECK(vkCreateDescriptorPool(dev, &dp, nullptr, &descriptors));
        VkDescriptorSetAllocateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        si.descriptorPool = descriptors;
        si.descriptorSetCount = 1;
        si.pSetLayouts = &setLayout;
        VK_CHECK(vkAllocateDescriptorSets(dev, &si, &set));

        VkPushConstantRange push{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) };
        VkPipelineLayoutCreateInfo pl{};
        pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &setLayout;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &push;
        VK_CHECK(vkCreatePipelineLayout(dev, &pl, nullptr, &layout));
        VkShaderModule module = loadShaderModule(device, "shaders/cloth_pairs.comp.spv");
        VkComputePipelineCreateInfo cp{};
        cp.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cp.stage.module = module;
        cp.stage.pName = "main";
        cp.layout = layout;
        const VkResult r = vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cp, nullptr, &pipeline);
        vkDestroyShaderModule(dev, module, nullptr);
        VK_CHECK(r);
    }

    // Buffers big enough (grown by half again, so they settle); rebinds
    // the descriptors when one changes.
    void reserve(size_t in, size_t outWords) {
        bool changed = false;
        auto grow = [&](std::unique_ptr<Buffer>& b, size_t& have, size_t need, VkBufferUsageFlags usage, VmaMemoryUsage mem) {
            if (b && have >= need) return;
            have = std::max<size_t>(need + need / 2, 4096);
            b = std::make_unique<Buffer>(device, have, usage, mem);
            changed = true;
        };
        grow(input, inputBytes, in, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        grow(out, outBytes, outWords * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
             VMA_MEMORY_USAGE_GPU_ONLY);
        // (Only the CPU's side of these: no descriptors to change.)
        if (!staging || stagingBytes < inputBytes) {
            stagingBytes = inputBytes;
            staging = std::make_unique<Buffer>(device, stagingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        }
        if (!readback || readbackBytes < outBytes) {
            readbackBytes = outBytes;
            readback = std::make_unique<Buffer>(device, readbackBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
        }
        grow(vtPairs, vtBytes, size_t(vtCap) * 8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
        grow(eePairs, eeBytes, size_t(eeCap) * 8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
        if (!changed) return;
        VkDescriptorBufferInfo infos[4] = { { input->handle(), 0, VK_WHOLE_SIZE },
                                            { out->handle(), 0, VK_WHOLE_SIZE },
                                            { vtPairs->handle(), 0, VK_WHOLE_SIZE },
                                            { eePairs->handle(), 0, VK_WHOLE_SIZE } };
        VkWriteDescriptorSet w[4]{};
        for (uint32_t i = 0; i < 4; ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = set;
            w[i].dstBinding = i;
            w[i].descriptorCount = 1;
            w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(dev, 4, w, 0, nullptr);
    }

    static void barrier(VkCommandBuffer c, VkPipelineStageFlags from, VkAccessFlags fromAccess, VkPipelineStageFlags to, VkAccessFlags toAccess) {
        VkMemoryBarrier m{};
        m.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        m.srcAccessMask = fromAccess;
        m.dstAccessMask = toAccess;
        vkCmdPipelineBarrier(c, from, to, 0, 1, &m, 0, nullptr, 0, nullptr);
    }
};

std::shared_ptr<ClothGpu> ClothGpu::create(VulkanDevice& device) {
    const char* e = std::getenv("KKE_CLOTH_GPU");
    const std::string mode = e ? e : "";
    if (mode == "0" || mode == "off") return nullptr;
    auto impl = std::make_unique<Impl>(device);
    if (device.computeQueue() != VK_NULL_HANDLE) {
        impl->queue = device.computeQueue();
        impl->family = *device.queueFamilies().compute;
    } else if (mode == "shared") {
        impl->queue = device.graphicsQueue();
        impl->family = *device.queueFamilies().graphics;
    } else {
        log::get("ClothGpu")->info("no compute queue of its own on this GPU: cloth pairs are searched on the CPU (KKE_CLOTH_GPU=shared to try the graphics queue)");
        return nullptr;
    }
    impl->init();
    log::get("ClothGpu")->info("cloth pair search on the GPU ({} queue)", device.computeQueue() != VK_NULL_HANDLE ? "its own" : "the graphics");
    return std::shared_ptr<ClothGpu>(new ClothGpu(std::move(impl)));
}

ClothGpu::ClothGpu(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
ClothGpu::~ClothGpu() = default;

bool ClothGpu::search(std::vector<uint32_t>& words, uint32_t vertices, uint32_t edges, std::vector<uint32_t>& vt, std::vector<uint32_t>& ee,
                      std::vector<uint32_t>& flags) {
    Impl& g = *m_impl;
    const std::lock_guard<std::mutex> hold(g.lock);
    const auto t0 = std::chrono::steady_clock::now();
    if (words.size() <= kEeCapWord) return false;
    words[kVtCapWord] = g.vtCap;
    words[kEeCapWord] = g.eeCap;
    const size_t outWords = 2 + size_t(vertices);
    g.reserve(words.size() * 4, outWords);
    g.staging->upload(words.data(), words.size() * 4);

    VK_CHECK(vkResetCommandBuffer(g.cmd, 0));
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(g.cmd, &bi));
    const VkBufferCopy in{ 0, 0, words.size() * 4 };
    vkCmdCopyBuffer(g.cmd, g.staging->handle(), g.input->handle(), 1, &in);
    vkCmdFillBuffer(g.cmd, g.out->handle(), 0, outWords * 4, 0);
    Impl::barrier(g.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.pipeline);
    vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.layout, 0, 1, &g.set, 0, nullptr);
    // Vertices against triangles, which also flags who is near whom; then
    // edges against edges, which reads the flags.
    const uint32_t kernels[2] = { 0u, 1u };
    const uint32_t threads[2] = { vertices, edges };
    for (int k = 0; k < 2; ++k) {
        if (k == 1)
            Impl::barrier(g.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        vkCmdPushConstants(g.cmd, g.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &kernels[k]);
        if (threads[k] > 0) vkCmdDispatch(g.cmd, (threads[k] + 63) / 64, 1, 1);
    }
    Impl::barrier(g.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    const VkBufferCopy back{ 0, 0, outWords * 4 };
    vkCmdCopyBuffer(g.cmd, g.out->handle(), g.readback->handle(), 1, &back);
    Impl::barrier(g.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                  VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
    VK_CHECK(vkEndCommandBuffer(g.cmd));
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g.cmd;
    VK_CHECK(vkResetFences(g.dev, 1, &g.fence));
    VK_CHECK(vkQueueSubmit(g.queue, 1, &si, g.fence));
    // A second is far longer than any search: something is wrong, so the
    // CPU answers (and waits for the GPU before it is used again).
    if (vkWaitForFences(g.dev, 1, &g.fence, VK_TRUE, 1000000000ull) != VK_SUCCESS) {
        VK_CHECK(vkWaitForFences(g.dev, 1, &g.fence, VK_TRUE, UINT64_MAX));
        ++m_stats.declined;
        return false;
    }
    flags.resize(outWords);
    g.readback->download(flags.data(), outWords * 4);
    const uint32_t vtCount = flags[0], eeCount = flags[1];
    if (vtCount > g.vtCap || eeCount > g.eeCap) {
        // More than the buffers hold: the CPU does this one, and they grow.
        g.vtCap = std::max(g.vtCap, vtCount + vtCount / 2);
        g.eeCap = std::max(g.eeCap, eeCount + eeCount / 2);
        ++m_stats.declined;
        return false;
    }
    flags.erase(flags.begin(), flags.begin() + 2);
    vt.resize(size_t(vtCount) * 2);
    ee.resize(size_t(eeCount) * 2);
    if (vtCount) g.vtPairs->download(vt.data(), vt.size() * 4);
    if (eeCount) g.eePairs->download(ee.data(), ee.size() * 4);
    ++m_stats.searches;
    m_stats.lastMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return true;
}

} // namespace kke
