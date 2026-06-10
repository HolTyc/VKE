#pragma once

#include "core/Base.h"

class VulkanContext;
class Window;

// Swapchain + render pass + framebuffers. The render pass is created once
// (surface format never changes at runtime) so handles held by pipelines and
// ImGui stay valid across resizes; Recreate() only rebuilds images/views/FBs.
class Swapchain {
public:
    Swapchain(VulkanContext& context, Window& window);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    void Recreate();

    VkResult AcquireNextImage(VkSemaphore signalSemaphore, uint32_t* outImageIndex);
    VkResult Present(VkSemaphore waitSemaphore, uint32_t imageIndex);

    VkRenderPass  GetRenderPass() const            { return m_RenderPass; }
    VkFramebuffer GetFramebuffer(uint32_t i) const { return m_Framebuffers[i]; }
    VkExtent2D    GetExtent() const                { return m_Extent; }
    VkFormat      GetFormat() const                { return m_Format; }
    uint32_t      GetImageCount() const            { return (uint32_t)m_Images.size(); }

private:
    void Create();
    void Cleanup();
    void CreateRenderPass();

    VulkanContext& m_Context;
    Window&        m_Window;

    VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
    VkRenderPass   m_RenderPass = VK_NULL_HANDLE;
    VkFormat       m_Format = VK_FORMAT_UNDEFINED;
    VkExtent2D     m_Extent{};
    std::vector<VkImage>       m_Images;
    std::vector<VkImageView>   m_ImageViews;
    std::vector<VkFramebuffer> m_Framebuffers;
};
