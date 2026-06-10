#pragma once

#include "core/Base.h"

class Window;

struct QueueFamilies {
    uint32_t Graphics = UINT32_MAX;
    uint32_t Present  = UINT32_MAX;
    bool Complete() const { return Graphics != UINT32_MAX && Present != UINT32_MAX; }
};

// Owns the global Vulkan objects: instance, device, queues, command pool and
// the shared descriptor pool/layout used for sprite textures. Accessible as a
// singleton (VulkanContext::Get()) so resources like Texture can self-manage.
class VulkanContext {
public:
    explicit VulkanContext(Window& window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    static VulkanContext* Get() { return s_Instance; }

    VkInstance       GetInstance() const       { return m_Instance; }
    VkSurfaceKHR     GetSurface() const        { return m_Surface; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
    VkDevice         GetDevice() const         { return m_Device; }
    VkQueue          GetGraphicsQueue() const  { return m_GraphicsQueue; }
    VkQueue          GetPresentQueue() const   { return m_PresentQueue; }
    const QueueFamilies& GetQueueFamilies() const { return m_Families; }
    VkCommandPool    GetCommandPool() const    { return m_CommandPool; }
    VkDescriptorPool GetDescriptorPool() const { return m_DescriptorPool; }
    // set = 0, binding = 0: combined image sampler. Shared by every sprite
    // texture and by the default + custom 2D pipelines.
    VkDescriptorSetLayout GetTextureSetLayout() const { return m_TextureSetLayout; }

    // ---- Resource helpers -------------------------------------------------
    uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;

    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags props,
                      VkBuffer& outBuffer, VkDeviceMemory& outMemory) const;

    void CreateImage2D(uint32_t width, uint32_t height, VkFormat format,
                       VkImageUsageFlags usage,
                       VkImage& outImage, VkDeviceMemory& outMemory) const;

    VkCommandBuffer BeginSingleTimeCommands() const;
    void EndSingleTimeCommands(VkCommandBuffer cmd) const;
    void CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const;

    void WaitIdle() const { vkDeviceWaitIdle(m_Device); }

private:
    void CreateInstance(const std::string& appName);
    void SetupDebugMessenger();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateCommandPool();
    void CreateDescriptorResources();

    QueueFamilies FindQueueFamilies(VkPhysicalDevice device) const;

    VkInstance               m_Instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR             m_Surface = VK_NULL_HANDLE;
    VkPhysicalDevice         m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice                 m_Device = VK_NULL_HANDLE;
    VkQueue                  m_GraphicsQueue = VK_NULL_HANDLE;
    VkQueue                  m_PresentQueue = VK_NULL_HANDLE;
    QueueFamilies            m_Families;
    VkCommandPool            m_CommandPool = VK_NULL_HANDLE;
    VkDescriptorPool         m_DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout    m_TextureSetLayout = VK_NULL_HANDLE;
    bool                     m_ValidationEnabled = false;

    static inline VulkanContext* s_Instance = nullptr;
};
