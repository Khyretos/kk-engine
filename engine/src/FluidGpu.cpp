#include "kke/FluidGpu.h"

#include "kke/Buffer.h"
#include "kke/Log.h"
#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace kke {

namespace {

// shaders/fluid_neighbours.comp's push constants.
struct Push {
    uint32_t count, tableSize, stride, cellOffset;
    float invCell, radius2;
};
static_assert(sizeof(Push) == 24, "fluid_neighbours.comp reads six 32-bit words");

constexpr uint32_t kMaxStride = 256; // more neighbours than this: the CPU keeps searching

} // namespace

struct FluidGpu::Impl {
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
    // In: staged on the CPU, copied to the GPU. Out: written by the shader
    // where the CPU reads it.
    std::unique_ptr<Buffer> staging, input, out;
    size_t stagingBytes = 0, inputBytes = 0, outBytes = 0;
    std::mutex lock;

    explicit Impl(VulkanDevice& d) : device(d), dev(d.device()) {}
    ~Impl() {
        if (dev == VK_NULL_HANDLE) return;
        if (fence) vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
        staging.reset();
        input.reset();
        out.reset();
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
        // Made signalled: ~Impl waits on it, and a liquid that never
        // reaches 1,024 particles never submits.
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(dev, &fi, nullptr, &fence));

        VkDescriptorSetLayoutBinding b[2]{};
        for (uint32_t i = 0; i < 2; ++i) {
            b[i].binding = i;
            b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[i].descriptorCount = 1;
            b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 2;
        li.pBindings = b;
        VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &setLayout));
        VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 };
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

        VkPushConstantRange push{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push) };
        VkPipelineLayoutCreateInfo pl{};
        pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &setLayout;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &push;
        VK_CHECK(vkCreatePipelineLayout(dev, &pl, nullptr, &layout));
        VkShaderModule module = loadShaderModule(device, "shaders/fluid_neighbours.comp.spv");
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
            if (b && have >= need) return false;
            have = std::max<size_t>(need + need / 2, 4096);
            b = std::make_unique<Buffer>(device, have, usage, mem);
            return true;
        };
        changed |= grow(input, inputBytes, in, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        changed |= grow(out, outBytes, outWords * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
        // (Only the CPU's side of this one: no descriptor to change.)
        grow(staging, stagingBytes, inputBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        if (!changed) return;
        VkDescriptorBufferInfo infos[2] = { { input->handle(), 0, VK_WHOLE_SIZE }, { out->handle(), 0, VK_WHOLE_SIZE } };
        VkWriteDescriptorSet w[2]{};
        for (uint32_t i = 0; i < 2; ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = set;
            w[i].dstBinding = i;
            w[i].descriptorCount = 1;
            w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(dev, 2, w, 0, nullptr);
    }

    static void barrier(VkCommandBuffer c, VkPipelineStageFlags from, VkAccessFlags fromAccess, VkPipelineStageFlags to, VkAccessFlags toAccess) {
        VkMemoryBarrier m{};
        m.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        m.srcAccessMask = fromAccess;
        m.dstAccessMask = toAccess;
        vkCmdPipelineBarrier(c, from, to, 0, 1, &m, 0, nullptr, 0, nullptr);
    }
};

std::shared_ptr<FluidGpu> FluidGpu::create(VulkanDevice& device) {
    const char* e = std::getenv("KKE_FLUID_GPU");
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
        log::get("FluidGpu")->info("no compute queue of its own on this GPU: the liquid's neighbours are found on the CPU (KKE_FLUID_GPU=shared to try the graphics queue)");
        return nullptr;
    }
    impl->init();
    log::get("FluidGpu")->info("liquid neighbour search on the GPU ({} queue)", device.computeQueue() != VK_NULL_HANDLE ? "its own" : "the graphics");
    std::shared_ptr<FluidGpu> gpu(new FluidGpu(std::move(impl)));
    const char* check = std::getenv("KKE_FLUID_GPU_CHECK");
    gpu->m_check = check && std::string(check) == "1";
    return gpu;
}

FluidGpu::FluidGpu(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
FluidGpu::~FluidGpu() = default;

bool FluidGpu::search(const Query& q, uint32_t& stride, std::vector<uint32_t>& list, std::vector<uint32_t>& counts) {
    Impl& g = *m_impl;
    const std::lock_guard<std::mutex> hold(g.lock);
    if (q.count == 0 || q.tableSize == 0) return false;
    const size_t n = q.count;
    const size_t cellWords = size_t(q.tableSize) + 1;
    m_words.resize(n * 4 + cellWords);
    for (size_t s = 0; s < n; ++s) {
        std::memcpy(&m_words[s * 4], &q.positions[s], sizeof(glm::vec3));
        m_words[s * 4 + 3] = 0;
    }
    std::memcpy(&m_words[n * 4], q.cellStart, cellWords * 4);
    const size_t outWords = 1 + n + n * size_t(stride);
    g.reserve(m_words.size() * 4, outWords);
    g.staging->upload(m_words.data(), m_words.size() * 4);

    VK_CHECK(vkResetCommandBuffer(g.cmd, 0));
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(g.cmd, &bi));
    const VkBufferCopy in{ 0, 0, m_words.size() * 4 };
    vkCmdCopyBuffer(g.cmd, g.staging->handle(), g.input->handle(), 1, &in);
    vkCmdFillBuffer(g.cmd, g.out->handle(), 0, 4, 0);
    Impl::barrier(g.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.pipeline);
    vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.layout, 0, 1, &g.set, 0, nullptr);
    const Push push{ static_cast<uint32_t>(n), q.tableSize, stride, static_cast<uint32_t>(n * 4), q.invCellSize, q.radius2 };
    vkCmdPushConstants(g.cmd, g.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &push);
    vkCmdDispatch(g.cmd, static_cast<uint32_t>((n + 63) / 64), 1, 1);
    Impl::barrier(g.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
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
        return false;
    }
    m_out.resize(1 + n);
    g.out->download(m_out.data(), m_out.size() * 4);
    if (m_out[0] > stride) {
        // A slot with more neighbours than the lists hold: the CPU does
        // this one, and the lists grow for the next.
        stride = std::min(kMaxStride, std::max(stride + stride / 2, m_out[0] + 8));
        return false;
    }
    counts.assign(m_out.begin() + 1, m_out.end());
    list.resize(n * size_t(stride));
    if (!list.empty()) g.out->downloadAt((1 + n) * 4, list.data(), list.size() * 4);
    return true;
}

} // namespace kke
