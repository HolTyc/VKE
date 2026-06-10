#pragma once

#include "../Core.hpp"

#include <vector>

namespace vke {

class Window;

// Owns the Vulkan instance, surface, physical/logical device, queues and the
// primary command pool. Also provides low-level helpers (buffers, images,
// one-shot command buffers) used by the rest of the backend.
class VulkanContext {
public:
    explicit VulkanContext(Window& window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkInstance       instance       = VK_NULL_HANDLE;
    VkSurfaceKHR     surface        = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice         device         = VK_NULL_HANDLE;
    VkQueue          graphicsQueue  = VK_NULL_HANDLE;
    VkQueue          presentQueue   = VK_NULL_HANDLE;
    uint32_t         graphicsFamily = 0;
    uint32_t         presentFamily  = 0;
    VkCommandPool    commandPool    = VK_NULL_HANDLE;

    VkPhysicalDeviceProperties properties{};

    void waitIdle() const;

    // ---- helpers -------------------------------------------------------
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates,
                                 VkImageTiling tiling,
                                 VkFormatFeatureFlags features) const;
    VkFormat findDepthFormat() const;

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags props,
                      VkBuffer& buffer, VkDeviceMemory& memory) const;
    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const;

    void createImage(uint32_t width, uint32_t height, VkFormat format,
                     VkImageTiling tiling, VkImageUsageFlags usage,
                     VkMemoryPropertyFlags props,
                     VkImage& image, VkDeviceMemory& memory) const;
    VkImageView createImageView(VkImage image, VkFormat format,
                                VkImageAspectFlags aspect) const;

    VkCommandBuffer beginSingleTimeCommands() const;
    void endSingleTimeCommands(VkCommandBuffer cmd) const;

private:
    void createInstance();
    void setupDebugMessenger();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();

    bool isDeviceSuitable(VkPhysicalDevice device) const;
    bool findQueueFamilies(VkPhysicalDevice device,
                           uint32_t& graphics, uint32_t& present) const;

    Window& window_;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    bool validationEnabled_ = false;
};

} // namespace vke
