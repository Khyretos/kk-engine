#pragma once

#include <volk.h>
#include <stdexcept>
#include <string>

namespace engine {

inline void vk_check(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string("Vulkan error (") +
                                  std::to_string(static_cast<int>(result)) +
                                  ") in: " + what);
    }
}

} // namespace engine

#define VK_CHECK(expr) ::engine::vk_check((expr), #expr)
