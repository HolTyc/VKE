#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <stdexcept>
#include <string>

#define VK_CHECK(call)                                                          \
    do {                                                                        \
        VkResult vk_check_result_ = (call);                                     \
        if (vk_check_result_ != VK_SUCCESS) {                                   \
            throw std::runtime_error(std::string("Vulkan error ") +             \
                                     std::to_string(vk_check_result_) +         \
                                     " at " #call);                             \
        }                                                                       \
    } while (0)

namespace vke {

inline constexpr int MAX_FRAMES_IN_FLIGHT = 2;
inline constexpr int MAX_LIGHTS = 8;

// Resolves a path relative to the asset directory (configured at build time).
std::string assetPath(const std::string& relative);

} // namespace vke
