// VMA is a header-only library — this is the one translation unit that
// actually compiles its implementation.
#define VMA_IMPLEMENTATION
// We load Vulkan through volk (VK_NO_PROTOTYPES), so VMA must fetch its own
// function pointers dynamically instead of linking against libvulkan directly.
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <volk.h>
#include <vk_mem_alloc.h>
