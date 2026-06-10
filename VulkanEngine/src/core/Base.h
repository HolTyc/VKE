#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#define ENGINE_INFO(...)  do { std::fprintf(stdout, "[engine] " __VA_ARGS__); std::fputc('\n', stdout); } while (0)
#define ENGINE_WARN(...)  do { std::fprintf(stderr, "[warn]   " __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#define ENGINE_ERROR(...) do { std::fprintf(stderr, "[error]  " __VA_ARGS__); std::fputc('\n', stderr); } while (0)

#define VK_CHECK(expr)                                                         \
    do {                                                                       \
        VkResult vk_check_result = (expr);                                     \
        if (vk_check_result != VK_SUCCESS) {                                   \
            ENGINE_ERROR("Vulkan call failed (%d): %s at %s:%d",               \
                         (int)vk_check_result, #expr, __FILE__, __LINE__);     \
            std::abort();                                                      \
        }                                                                      \
    } while (0)
