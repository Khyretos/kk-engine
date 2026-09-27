#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>
#include <unordered_map>
#include <vector>
#include <optional>
#include <cstdint>

#if KKE_ENABLE_GPU_PROFILER
// VulkanProfiler's extension header (see docs/HISTORY.md "GPU profiler
// (VulkanProfiler) integration") — only required when this build option
// is actually on. A default build never needs this header to exist, so
// it never needs the layer installed just to compile.
#include <VkProfilerEXT.h>
#endif

namespace kke {

class Window;

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    // A queue for compute work of its own (kke::ClothGpu), when the GPU has
    // one: a compute-only family, or a second queue of the graphics family.
    std::optional<uint32_t> compute;
    uint32_t computeIndex = 0;

    bool isComplete() const { return graphics.has_value() && present.has_value(); }
};

// Wraps VkInstance + VkPhysicalDevice + VkDevice + VMA allocator.
// One "lego piece": everything the rest of the renderer needs to talk to the GPU.
class VulkanDevice {
public:
    VulkanDevice(Window& window, bool enableValidation);
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    VkInstance instance() const { return m_instance; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkDevice device() const { return m_device; }
    VkSurfaceKHR surface() const { return m_surface; }
    VmaAllocator allocator() const { return m_allocator; }

    VkQueue graphicsQueue() const { return m_graphicsQueue; }
    VkQueue presentQueue() const { return m_presentQueue; }
    // The queue of its own for compute (null when the GPU has only the
    // graphics queue): work there runs beside the frame's, not after it.
    VkQueue computeQueue() const { return m_computeQueue; }
    const QueueFamilyIndices& queueFamilies() const { return m_queueFamilies; }

    VkCommandPool commandPool() const { return m_commandPool; }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

    // Nanoseconds per timestamp tick on this device — needed to convert
    // vkCmdWriteTimestamp results into real time for GPU profiling.
    double timestampPeriodNs() const { return m_timestampPeriodNs; }
    bool largePointsSupported() const { return m_largePointsSupported; }
    // 1.0 when samplerAnisotropy isn't supported (and so not enabled).
    float maxSamplerAnisotropy() const { return m_maxSamplerAnisotropy; }
    // Highest MSAA count both a colour and a depth attachment support.
    VkSampleCountFlagBits maxUsableSampleCount() const { return m_maxSampleCount; }
    // A CPU rasteriser (lavapipe/llvmpipe, the min-spec reference).
    bool isSoftwareRasterizer() const { return m_softwareRasterizer; }
    // Sample count of the attachments in a render pass, so kke::Pipeline
    // can match it without every caller passing it along. Passes never
    // registered are single-sampled (shadow, UI, offscreen passes).
    void setRenderPassSamples(VkRenderPass pass, VkSampleCountFlagBits samples) { m_passSamples[pass] = samples; }
    VkSampleCountFlagBits renderPassSamples(VkRenderPass pass) const {
        auto it = m_passSamples.find(pass);
        return it == m_passSamples.end() ? VK_SAMPLE_COUNT_1_BIT : it->second;
    }

    // True only if KKE_ENABLE_GPU_PROFILER was set at build time AND the
    // VulkanProfiler layer was actually found installed at instance
    // creation — see docs/HISTORY.md "GPU profiler (VulkanProfiler) integration".
    // Code that wants to call vkGetProfilerFrameDataEXT (StatsModule,
    // once that slice lands) should check this first rather than assume
    // the layer is present just because the build option was on.
    bool gpuProfilerEnabled() const { return m_gpuProfilerEnabled; }

    // What StatsModule actually pulls out of the layer's frame-data tree
    // each frame: total frame duration as the layer measured it (an
    // independent cross-check against this engine's own timestamp-query
    // GPU timing in Renderer), and a real count of leaf commands
    // (VK_PROFILER_REGION_TYPE_COMMAND_EXT nodes — actual draws/
    // dispatches/copies, not render passes or pipelines) recursively
    // walked from the returned tree. valid is false if the profiler
    // isn't enabled or the query failed — always check it.
    struct GpuProfilerFrameSummary {
        bool valid = false;
        float frameDurationMs = 0.0f;
        uint32_t commandCount = 0;
    };
    GpuProfilerFrameSummary queryGpuProfilerFrameSummary() const;

private:
    void createInstance(bool enableValidation);
    void setupDebugMessenger();
    void pickPhysicalDevice();
    void createLogicalDevice(bool enableValidation);
    void createAllocator();
    void createCommandPool();
    void loadGpuProfilerFunctions(); // no-op if m_gpuProfilerEnabled is false

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;
    bool isDeviceSuitable(VkPhysicalDevice device) const;

    Window& m_window;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;

    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    QueueFamilyIndices m_queueFamilies;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;

    double m_timestampPeriodNs = 1.0;
    bool m_largePointsSupported = false;
    float m_maxSamplerAnisotropy = 1.0f;
    VkSampleCountFlagBits m_maxSampleCount = VK_SAMPLE_COUNT_1_BIT;
    bool m_softwareRasterizer = false;
    std::unordered_map<VkRenderPass, VkSampleCountFlagBits> m_passSamples;

    bool m_validationEnabled = false;
    bool m_gpuProfilerEnabled = false;

#if KKE_ENABLE_GPU_PROFILER
    // Loaded via vkGetDeviceProcAddr, not volk — these are a third-party
    // layer's extension functions, not core Vulkan or something volk's
    // generated loader knows about. Null unless m_gpuProfilerEnabled.
    PFN_vkGetProfilerFrameDataEXT m_vkGetProfilerFrameDataEXT = nullptr;
    PFN_vkFreeProfilerFrameDataEXT m_vkFreeProfilerFrameDataEXT = nullptr;
#endif
};

} // namespace kke
