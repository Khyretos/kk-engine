#include "kke/MpmWaterGpu.h"

#include "kke/Buffer.h"
#include "kke/Log.h"
#include "kke/MpmWater.h"
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

// shaders/mpm_common.glsl's push constants.
struct Push {
    glm::vec4 origin, wallLo, wallHi, gravity;
    glm::ivec4 dims;
    glm::vec4 misc, misc2;
};
static_assert(sizeof(Push) == 112, "mpm_common.glsl reads seven vec4");

constexpr size_t kParticleWords = 20; // 5 vec4
constexpr size_t kBodyWords = 32;     // 8 vec4
constexpr uint32_t kMaxGroupsX = 65535;

enum Pass { kP2g, kGrid, kBodies, kG2p, kPasses };
const char* const kShaders[kPasses] = { "shaders/mpm_p2g.comp.spv", "shaders/mpm_grid.comp.spv", "shaders/mpm_bodies.comp.spv",
                                        "shaders/mpm_g2p.comp.spv" };

} // namespace

struct MpmWaterGpu::Impl {
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
    VkPipeline pipelines[kPasses] = {};
    // On the GPU: particles, grid, bodies, impulses, out. Host side:
    // staging for uploads, readback for the results.
    std::unique_ptr<Buffer> particles, grid, bodies, impulses, out;
    std::unique_ptr<Buffer> staging, readback, bodyStaging, bodyReadback;
    size_t particleCap = 0, gridBytes = 0, stagingBytes = 0, readbackBytes = 0;
    std::mutex lock;
    bool late = false; // a step outlived its second: the GPU still owns the buffers
    std::vector<float> words;

    explicit Impl(VulkanDevice& d) : device(d), dev(d.device()) {}
    ~Impl() {
        if (dev == VK_NULL_HANDLE) return;
        if (fence) vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
        particles.reset();
        grid.reset();
        bodies.reset();
        impulses.reset();
        out.reset();
        staging.reset();
        readback.reset();
        bodyStaging.reset();
        bodyReadback.reset();
        for (VkPipeline p : pipelines)
            if (p) vkDestroyPipeline(dev, p, nullptr);
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
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT; // ~Impl waits on it before the first step
        VK_CHECK(vkCreateFence(dev, &fi, nullptr, &fence));

        VkDescriptorSetLayoutBinding b[5]{};
        for (uint32_t i = 0; i < 5; ++i) {
            b[i].binding = i;
            b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[i].descriptorCount = 1;
            b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 5;
        li.pBindings = b;
        VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &setLayout));
        VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5 };
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
        for (int i = 0; i < kPasses; ++i) {
            VkShaderModule module = loadShaderModule(device, kShaders[i]);
            VkComputePipelineCreateInfo cp{};
            cp.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cp.stage.module = module;
            cp.stage.pName = "main";
            cp.layout = layout;
            const VkResult r = vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cp, nullptr, &pipelines[i]);
            vkDestroyShaderModule(dev, module, nullptr);
            VK_CHECK(r);
        }
        const VkBufferUsageFlags storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        const size_t bodyBytes = MpmWater::kMaxBodies * kBodyWords * 4;
        bodies = std::make_unique<Buffer>(device, bodyBytes, storage, VMA_MEMORY_USAGE_GPU_ONLY);
        impulses = std::make_unique<Buffer>(device, MpmWater::kMaxBodies * 8 * 4, storage, VMA_MEMORY_USAGE_GPU_ONLY);
        bodyStaging = std::make_unique<Buffer>(device, bodyBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        bodyReadback = std::make_unique<Buffer>(device, bodyBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
    }

    void bind() {
        VkDescriptorBufferInfo infos[5] = { { particles->handle(), 0, VK_WHOLE_SIZE }, { grid->handle(), 0, VK_WHOLE_SIZE },
                                            { bodies->handle(), 0, VK_WHOLE_SIZE }, { impulses->handle(), 0, VK_WHOLE_SIZE },
                                            { out->handle(), 0, VK_WHOLE_SIZE } };
        VkWriteDescriptorSet w[5]{};
        for (uint32_t i = 0; i < 5; ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = set;
            w[i].dstBinding = i;
            w[i].descriptorCount = 1;
            w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(dev, 5, w, 0, nullptr);
    }

    static void barrier(VkCommandBuffer c, VkPipelineStageFlags from, VkAccessFlags fromAccess, VkPipelineStageFlags to, VkAccessFlags toAccess) {
        VkMemoryBarrier m{};
        m.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        m.srcAccessMask = fromAccess;
        m.dstAccessMask = toAccess;
        vkCmdPipelineBarrier(c, from, to, 0, 1, &m, 0, nullptr, 0, nullptr);
    }
    static void computeBarrier(VkCommandBuffer c) {
        barrier(c, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }
    void dispatch(VkCommandBuffer c, Pass pass, size_t threads) {
        const size_t groups = (threads + 63) / 64;
        if (groups == 0) return;
        const uint32_t x = static_cast<uint32_t>(std::min<size_t>(groups, kMaxGroupsX));
        const uint32_t y = static_cast<uint32_t>((groups + x - 1) / x);
        vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[pass]);
        vkCmdDispatch(c, x, y, 1);
    }

    void begin() {
        VK_CHECK(vkResetCommandBuffer(cmd, 0));
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    }
    // Submits and waits at most `timeoutNs`; false = still running.
    bool submit(uint64_t timeoutNs) {
        VK_CHECK(vkEndCommandBuffer(cmd));
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        VK_CHECK(vkResetFences(dev, 1, &fence));
        VK_CHECK(vkQueueSubmit(queue, 1, &si, fence));
        return vkWaitForFences(dev, 1, &fence, VK_TRUE, timeoutNs) == VK_SUCCESS;
    }
    void finishLate() {
        if (!late) return;
        vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
        late = false;
    }

    // The particles on the GPU into the water's arrays, [0, count).
    void download(MpmWater& w, size_t count) {
        if (count == 0 || !particles) return;
        finishLate();
        const size_t bytes = count * kParticleWords * 4;
        if (readbackBytes < bytes) {
            readbackBytes = bytes + bytes / 2;
            readback = std::make_unique<Buffer>(device, readbackBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
        }
        begin();
        const VkBufferCopy copy{ 0, 0, bytes };
        vkCmdCopyBuffer(cmd, particles->handle(), readback->handle(), 1, &copy);
        barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
        submit(UINT64_MAX);
        words.resize(count * kParticleWords);
        readback->download(words.data(), bytes);
        for (size_t p = 0; p < count; ++p) {
            const float* f = &words[p * kParticleWords];
            w.m_x[p] = glm::vec3(f[0], f[1], f[2]);
            w.m_J[p] = f[3];
            w.m_v[p] = glm::vec3(f[4], f[5], f[6]);
            w.m_C[p] = glm::mat3(glm::vec3(f[8], f[9], f[10]), glm::vec3(f[12], f[13], f[14]), glm::vec3(f[16], f[17], f[18]));
        }
    }
};

std::shared_ptr<MpmWaterGpu> MpmWaterGpu::create(VulkanDevice& device) {
    const char* e = std::getenv("KKE_MPM_GPU");
    const std::string mode = e ? e : "";
    if (mode == "0" || mode == "off") return nullptr;
    if (device.isSoftwareRasterizer() && mode != "1") {
        log::get("MpmWater")->info("a software GPU: the water steps on the CPU (KKE_MPM_GPU=1 to try the GPU anyway)");
        return nullptr;
    }
    auto impl = std::make_unique<Impl>(device);
    const bool own = device.computeQueue() != VK_NULL_HANDLE;
    impl->queue = own ? device.computeQueue() : device.graphicsQueue();
    impl->family = own ? *device.queueFamilies().compute : *device.queueFamilies().graphics;
    impl->init();
    log::get("MpmWater")->info("water on the GPU ({} queue)", own ? "its own" : "the graphics");
    return std::shared_ptr<MpmWaterGpu>(new MpmWaterGpu(std::move(impl)));
}

MpmWaterGpu::MpmWaterGpu(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
MpmWaterGpu::~MpmWaterGpu() = default;

void MpmWaterGpu::download(MpmWater& water) {
    Impl& g = *m_impl;
    const std::lock_guard<std::mutex> hold(g.lock);
    g.download(water, std::min(water.m_gpuValid, water.m_x.size()));
}

bool MpmWaterGpu::step(MpmWater& w, float dt, int substeps) {
    Impl& g = *m_impl;
    const std::lock_guard<std::mutex> hold(g.lock);
    // A late step still running: the water waits (never blocking here, so
    // a hung GPU can't stall the frame).
    if (g.late) {
        if (vkGetFenceStatus(g.dev, g.fence) != VK_SUCCESS) return false;
        g.late = false;
    }
    const size_t n = w.m_x.size();
    if (n == 0 || substeps <= 0) return false;
    const VkBufferUsageFlags storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    // Room for every particle (grown by half again, so it settles). A new
    // buffer starts empty: the particles there come back first.
    bool rebind = false;
    if (g.particleCap < n) {
        if (w.m_gpuOwns) g.download(w, std::min(w.m_gpuValid, n));
        w.m_gpuValid = 0;
        g.particleCap = std::max<size_t>(n + n / 2, 4096);
        g.particles = std::make_unique<Buffer>(g.device, g.particleCap * kParticleWords * 4, storage, VMA_MEMORY_USAGE_GPU_ONLY);
        g.out = std::make_unique<Buffer>(g.device, 16 + g.particleCap * 32, storage, VMA_MEMORY_USAGE_GPU_ONLY);
        rebind = true;
    }
    const size_t gridBytes = w.m_nodes * 16;
    if (g.gridBytes != gridBytes) {
        g.gridBytes = gridBytes;
        g.grid = std::make_unique<Buffer>(g.device, gridBytes, storage, VMA_MEMORY_USAGE_GPU_ONLY);
        rebind = true;
    }
    if (rebind) g.bind();
    const size_t outBytes = 16 + n * 32;
    if (g.readbackBytes < outBytes) {
        g.readbackBytes = outBytes + outBytes / 2;
        g.readback = std::make_unique<Buffer>(g.device, g.readbackBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
    }

    // New particles (or all of them, after the CPU had them) go up.
    const size_t from = std::min(w.m_gpuValid, n);
    const size_t upload = n - from;
    if (upload > 0) {
        const size_t bytes = upload * kParticleWords * 4;
        if (g.stagingBytes < bytes) {
            g.stagingBytes = bytes + bytes / 2;
            g.staging = std::make_unique<Buffer>(g.device, g.stagingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        }
        g.words.assign(upload * kParticleWords, 0.0f);
        for (size_t i = 0; i < upload; ++i) {
            const size_t p = from + i;
            float* f = &g.words[i * kParticleWords];
            f[0] = w.m_x[p].x, f[1] = w.m_x[p].y, f[2] = w.m_x[p].z, f[3] = w.m_J[p];
            f[4] = w.m_v[p].x, f[5] = w.m_v[p].y, f[6] = w.m_v[p].z;
            for (int c = 0; c < 3; ++c)
                for (int r = 0; r < 3; ++r) f[8 + c * 4 + r] = w.m_C[p][c][r];
        }
        g.staging->upload(g.words.data(), bytes);
    }
    // The bodies, as mpm_common.glsl reads them.
    const size_t bodies = w.m_proxies.size();
    if (bodies > 0) {
        std::vector<float> b(bodies * kBodyWords, 0.0f);
        for (size_t i = 0; i < bodies; ++i) {
            const MpmWater::Proxy& p = w.m_proxies[i];
            float* f = &b[i * kBodyWords];
            const glm::vec4 rows[6] = { glm::vec4(p.position, p.sphere ? 1.0f : 0.0f), glm::vec4(p.rotation.x, p.rotation.y, p.rotation.z, p.rotation.w),
                                        glm::vec4(p.halfExtents, p.invMass), glm::vec4(p.velocity, p.friction),
                                        glm::vec4(p.angularVelocity, 0.0f), glm::vec4(p.invInertia, 0.0f) };
            std::memcpy(f, rows, sizeof(rows)); // rows 6 and 7 (the step's velocity changes) start at 0
        }
        g.bodyStaging->upload(b.data(), b.size() * 4);
    }

    const float dx = w.m_dx;
    Push push{};
    push.origin = glm::vec4(w.m_origin, dx);
    push.wallLo = glm::vec4(w.m_params.boundsMin, w.m_invDx);
    push.wallHi = glm::vec4(w.m_params.boundsMax, dt);
    push.gravity = glm::vec4(w.m_params.gravity, w.m_params.soundSpeed * w.m_params.soundSpeed);
    push.dims = glm::ivec4(w.m_dims, static_cast<int>(n));
    push.misc = glm::vec4(std::min(w.m_params.viscosity, 0.1f * dx * dx / dt), std::max(0.0f, w.m_params.tension),
                          1.0f - std::clamp(w.m_params.wallFriction, 0.0f, 1.0f), w.m_params.maxSpeed);
    push.misc2 = glm::vec4(w.m_particleMass, float(bodies), 0.0f, 0.0f);

    g.begin();
    VkCommandBuffer c = g.cmd;
    if (upload > 0) {
        const VkBufferCopy copy{ 0, from * kParticleWords * 4, upload * kParticleWords * 4 };
        vkCmdCopyBuffer(c, g.staging->handle(), g.particles->handle(), 1, &copy);
    }
    if (bodies > 0) {
        const VkBufferCopy copy{ 0, 0, bodies * kBodyWords * 4 };
        vkCmdCopyBuffer(c, g.bodyStaging->handle(), g.bodies->handle(), 1, &copy);
    }
    vkCmdFillBuffer(c, g.impulses->handle(), 0, VK_WHOLE_SIZE, 0);
    vkCmdFillBuffer(c, g.out->handle(), 0, 16, 0);
    vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_COMPUTE, g.layout, 0, 1, &g.set, 0, nullptr);
    for (int s = 0; s < substeps; ++s) {
        push.misc2.z = s == substeps - 1 ? 1.0f : 0.0f;
        vkCmdFillBuffer(c, g.grid->handle(), 0, VK_WHOLE_SIZE, 0);
        Impl::barrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        vkCmdPushConstants(c, g.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &push);
        g.dispatch(c, kP2g, n);
        Impl::computeBarrier(c);
        g.dispatch(c, kGrid, w.m_nodes);
        Impl::computeBarrier(c);
        if (bodies > 0) {
            g.dispatch(c, kBodies, bodies);
            Impl::computeBarrier(c);
        }
        g.dispatch(c, kG2p, n);
        // The next substep clears the grid (transfer) and reads the particles (compute).
        Impl::barrier(c, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }
    if (bodies > 0) {
        // The bodies take what the last substep's particles stopped, without moving.
        push.wallHi.w = 0.0f;
        vkCmdPushConstants(c, g.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &push);
        g.dispatch(c, kBodies, bodies);
        Impl::barrier(c, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    }
    const VkBufferCopy outCopy{ 0, 0, outBytes };
    vkCmdCopyBuffer(c, g.out->handle(), g.readback->handle(), 1, &outCopy);
    if (bodies > 0) {
        const VkBufferCopy copy{ 0, 0, bodies * kBodyWords * 4 };
        vkCmdCopyBuffer(c, g.bodies->handle(), g.bodyReadback->handle(), 1, &copy);
    }
    Impl::barrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
    w.m_gpuValid = n;
    // A second is far longer than any step: something is wrong, so the
    // water waits, and goes on once the GPU has finished.
    if (!g.submit(1000000000ull)) {
        g.late = true;
        return false;
    }

    g.words.resize(outBytes / 4);
    g.readback->download(g.words.data(), outBytes);
    float fastest;
    std::memcpy(&fastest, &g.words[0], 4);
    w.m_lastMaxSpeed = fastest;
    for (size_t p = 0; p < n; ++p) {
        const float* f = &g.words[4 + p * 8];
        w.m_x[p] = glm::vec3(f[0], f[1], f[2]);
        w.m_v[p] = glm::vec3(f[4], f[5], f[6]);
    }
    if (bodies > 0) {
        std::vector<float> b(bodies * kBodyWords);
        g.bodyReadback->download(b.data(), b.size() * 4);
        for (size_t i = 0; i < bodies; ++i) {
            const float* f = &b[i * kBodyWords];
            w.m_proxies[i].dv = glm::vec3(f[24], f[25], f[26]);
            w.m_proxies[i].dw = glm::vec3(f[28], f[29], f[30]);
        }
    }
    return true;
}

} // namespace kke
