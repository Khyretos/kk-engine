#include "kke/RayScene.h"

#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// kke::rayQuerySelfTest (ENG-F02): rays through a small kke::RayScene on the
// GPU (shaders/ray_selftest.comp), checked against the same rays on the CPU.
// KKE_RAY_SELFTEST=1 runs it at start; CI runs it on lavapipe.

namespace kke {

namespace {

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

// The nearest hit of a ray with a triangle list, on the CPU (Moller-Trumbore).
float hitTriangles(const std::vector<glm::vec3>& p, const std::vector<uint32_t>& idx, const glm::vec3& o, const glm::vec3& d, float tMax) {
    float best = -1.0f;
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        const glm::vec3 a = p[idx[i]], e1 = p[idx[i + 1]] - a, e2 = p[idx[i + 2]] - a;
        const glm::vec3 h = glm::cross(d, e2);
        const float det = glm::dot(e1, h);
        if (std::abs(det) < 1e-9f) continue;
        const glm::vec3 s = o - a;
        const float u = glm::dot(s, h) / det;
        if (u < 0.0f || u > 1.0f) continue;
        const glm::vec3 q = glm::cross(s, e1);
        const float v = glm::dot(d, q) / det;
        if (v < 0.0f || u + v > 1.0f) continue;
        const float t = glm::dot(e2, q) / det;
        if (t > 0.0f && t < tMax && (best < 0.0f || t < best)) best = t;
    }
    return best;
}

} // namespace

bool rayQuerySelfTest(VulkanDevice& device, std::string* report) {
    auto say = [&](const std::string& s) {
        if (report) *report = s;
    };
    if (!device.rayQuery()) {
        say("no ray query on this device");
        return false;
    }
    // Two boxes (one turned 45 degrees) and a floor, each instance with its own number.
    struct Shape { std::vector<glm::vec3> p; std::vector<uint32_t> idx; };
    std::vector<Shape> shapes(2);
    for (int i = 0; i < 8; ++i) shapes[0].p.push_back(glm::vec3(i & 1 ? 0.5f : -0.5f, i & 2 ? 0.5f : -0.5f, i & 4 ? 0.5f : -0.5f));
    shapes[0].idx = { 0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5 };
    shapes[1].p = { { -10, 0, -10 }, { 10, 0, -10 }, { -10, 0, 10 }, { 10, 0, 10 } };
    shapes[1].idx = { 0, 2, 1, 1, 2, 3 };
    struct Inst { int shape; glm::mat4 t; uint32_t custom; };
    const std::vector<Inst> insts = {
        { 0, glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -5)), 7 },
        { 0, glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(3, 0, -5)), glm::radians(45.0f), glm::vec3(0, 1, 0)), 9 },
        { 1, glm::translate(glm::mat4(1.0f), glm::vec3(0, -1, 0)), 3 },
    };
    struct Ray { glm::vec3 o, d; float tMax; };
    const std::vector<Ray> rays = {
        { { 0, 0, 0 }, { 0, 0, -1 }, 100.0f },                          // box 7's front face
        { { 3, 0, 0 }, { 0, 0, -1 }, 100.0f },                          // box 9's corner
        { { 0, 0, 0 }, glm::normalize(glm::vec3(0, -1, -1)), 100.0f },  // the floor
        { { 0, 5, 0 }, { 0, 1, 0 }, 100.0f },                           // the sky: a miss
        { { 0, 0, 0 }, { 0, 0, -1 }, 2.0f },                            // too short to reach anything
        { { 0, 0, -10 }, { 0, 0, 1 }, 100.0f },                         // box 7 from behind
    };
    try {
        RayScene scene(device);
        std::vector<RayScene::Geometry> geo;
        for (const Shape& s : shapes) geo.push_back(scene.addTriangles(s.p, s.idx));
        for (const Inst& i : insts) scene.add(geo[static_cast<size_t>(i.shape)], i.t, i.custom);

        std::vector<glm::vec4> rayData;
        for (const Ray& r : rays) {
            rayData.push_back(glm::vec4(r.o, r.tMax));
            rayData.push_back(glm::vec4(r.d, 0.0f));
        }
        Buffer rayBuffer = Buffer::createDeviceLocal(device, rayData.data(), rayData.size() * sizeof(glm::vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        Buffer hitBuffer(device, rays.size() * sizeof(glm::vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);

        VkDevice dev = device.device();
        std::array<VkDescriptorSetLayoutBinding, 3> b{};
        b[0] = { 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = static_cast<uint32_t>(b.size());
        li.pBindings = b.data();
        VkDescriptorSetLayout setLayout;
        VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &setLayout));
        VkPushConstantRange range{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) };
        VkPipelineLayoutCreateInfo pl{};
        pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &setLayout;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &range;
        VkPipelineLayout layout;
        VK_CHECK(vkCreatePipelineLayout(dev, &pl, nullptr, &layout));
        VkShaderModule comp = loadShaderModule(device, "shaders/ray_selftest.comp.spv");
        VkComputePipelineCreateInfo cp{};
        cp.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cp.stage.module = comp;
        cp.stage.pName = "main";
        cp.layout = layout;
        VkPipeline pipeline;
        VK_CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cp, nullptr, &pipeline));
        vkDestroyShaderModule(dev, comp, nullptr);
        const std::array<VkDescriptorPoolSize, 2> sizes{ { { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 } } };
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 1;
        pi.poolSizeCount = static_cast<uint32_t>(sizes.size());
        pi.pPoolSizes = sizes.data();
        VkDescriptorPool pool;
        VK_CHECK(vkCreateDescriptorPool(dev, &pi, nullptr, &pool));
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &setLayout;
        VkDescriptorSet set;
        VK_CHECK(vkAllocateDescriptorSets(dev, &ai, &set));

        submitNow(device, [&](VkCommandBuffer cmd) {
            scene.build(cmd, 0);
            const VkAccelerationStructureKHR tlas = scene.topLevel(0);
            VkWriteDescriptorSetAccelerationStructureKHR asw{};
            asw.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            asw.accelerationStructureCount = 1;
            asw.pAccelerationStructures = &tlas;
            VkDescriptorBufferInfo rb{ rayBuffer.handle(), 0, VK_WHOLE_SIZE }, hb{ hitBuffer.handle(), 0, VK_WHOLE_SIZE };
            std::array<VkWriteDescriptorSet, 3> w{};
            for (uint32_t i = 0; i < 3; ++i) {
                w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[i].dstSet = set;
                w[i].dstBinding = i;
                w[i].descriptorCount = 1;
                w[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            }
            w[0].pNext = &asw;
            w[1].pBufferInfo = &rb;
            w[2].pBufferInfo = &hb;
            vkUpdateDescriptorSets(dev, static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
            const uint32_t count = static_cast<uint32_t>(rays.size());
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(count), &count);
            vkCmdDispatch(cmd, (count + 63) / 64, 1, 1);
            VkMemoryBarrier mb{};
            mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
        });
        std::vector<glm::vec4> hits(rays.size());
        hitBuffer.download(hits.data(), hits.size() * sizeof(glm::vec4));
        vkDestroyPipeline(dev, pipeline, nullptr);
        vkDestroyPipelineLayout(dev, layout, nullptr);
        vkDestroyDescriptorPool(dev, pool, nullptr);
        vkDestroyDescriptorSetLayout(dev, setLayout, nullptr);

        // The same rays on the CPU, through each instance's inverse transform.
        int good = 0;
        std::string bad;
        for (size_t r = 0; r < rays.size(); ++r) {
            float best = -1.0f;
            uint32_t custom = 0;
            for (const Inst& i : insts) {
                const glm::mat4 inv = glm::inverse(i.t);
                const glm::vec3 o = glm::vec3(inv * glm::vec4(rays[r].o, 1.0f)), d = glm::vec3(inv * glm::vec4(rays[r].d, 0.0f));
                const Shape& s = shapes[static_cast<size_t>(i.shape)];
                const float t = hitTriangles(s.p, s.idx, o, d, rays[r].tMax);
                if (t > 0.0f && (best < 0.0f || t < best)) {
                    best = t;
                    custom = i.custom;
                }
            }
            const bool ok = best < 0.0f ? hits[r].x < 0.0f
                                        : std::abs(hits[r].x - best) < 1e-3f * std::max(1.0f, best) &&
                                              static_cast<uint32_t>(hits[r].y + 0.5f) == custom;
            if (ok) {
                ++good;
            } else {
                char buf[128];
                std::snprintf(buf, sizeof(buf), " ray %zu: GPU t %.3f #%u, expected t %.3f #%u;", r, static_cast<double>(hits[r].x),
                              static_cast<unsigned>(hits[r].y + 0.5f), static_cast<double>(best), custom);
                bad += buf;
            }
        }
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%d of %zu rays as expected", good, rays.size());
        say(std::string(buf) + bad);
        return good == static_cast<int>(rays.size());
    } catch (const std::exception& e) {
        say(e.what());
        return false;
    }
}

} // namespace kke
