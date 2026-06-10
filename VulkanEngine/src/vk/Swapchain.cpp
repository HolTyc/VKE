#include "vk/Swapchain.h"
#include "vk/VulkanContext.h"
#include "core/Window.h"

#include <algorithm>

Swapchain::Swapchain(VulkanContext& context, Window& window)
    : m_Context(context), m_Window(window) {
    Create();
    CreateRenderPass();

    // Framebuffers need the render pass, which is created after the first
    // Create(); build them now.
    m_Framebuffers.resize(m_ImageViews.size());
    for (size_t i = 0; i < m_ImageViews.size(); i++) {
        VkFramebufferCreateInfo info{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        info.renderPass = m_RenderPass;
        info.attachmentCount = 1;
        info.pAttachments = &m_ImageViews[i];
        info.width = m_Extent.width;
        info.height = m_Extent.height;
        info.layers = 1;
        VK_CHECK(vkCreateFramebuffer(m_Context.GetDevice(), &info, nullptr, &m_Framebuffers[i]));
    }
}

Swapchain::~Swapchain() {
    Cleanup();
    vkDestroyRenderPass(m_Context.GetDevice(), m_RenderPass, nullptr);
}

void Swapchain::Create() {
    VkPhysicalDevice physical = m_Context.GetPhysicalDevice();
    VkSurfaceKHR surface = m_Context.GetSurface();

    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps));

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, formats.data());

    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFormat = f;
            break;
        }
    }
    m_Format = chosenFormat.format;

    // FIFO is guaranteed, vsynced, and power-friendly — the right default for
    // both games and event-driven applications.
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    if (caps.currentExtent.width != UINT32_MAX) {
        m_Extent = caps.currentExtent;
    } else {
        VkExtent2D fb = m_Window.GetFramebufferExtent();
        m_Extent.width = std::clamp(fb.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        m_Extent.height = std::clamp(fb.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR info{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    info.surface = surface;
    info.minImageCount = imageCount;
    info.imageFormat = chosenFormat.format;
    info.imageColorSpace = chosenFormat.colorSpace;
    info.imageExtent = m_Extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const QueueFamilies& families = m_Context.GetQueueFamilies();
    uint32_t indices[] = { families.Graphics, families.Present };
    if (families.Graphics != families.Present) {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = indices;
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(m_Context.GetDevice(), &info, nullptr, &m_Swapchain));

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(m_Context.GetDevice(), m_Swapchain, &actualCount, nullptr);
    m_Images.resize(actualCount);
    vkGetSwapchainImagesKHR(m_Context.GetDevice(), m_Swapchain, &actualCount, m_Images.data());

    m_ImageViews.resize(actualCount);
    for (uint32_t i = 0; i < actualCount; i++) {
        VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        view.image = m_Images[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = m_Format;
        view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VK_CHECK(vkCreateImageView(m_Context.GetDevice(), &view, nullptr, &m_ImageViews[i]));
    }
}

void Swapchain::CreateRenderPass() {
    VkAttachmentDescription color{};
    color.format = m_Format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    info.attachmentCount = 1;
    info.pAttachments = &color;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dep;
    VK_CHECK(vkCreateRenderPass(m_Context.GetDevice(), &info, nullptr, &m_RenderPass));
}

void Swapchain::Cleanup() {
    VkDevice device = m_Context.GetDevice();
    for (auto fb : m_Framebuffers)
        vkDestroyFramebuffer(device, fb, nullptr);
    m_Framebuffers.clear();
    for (auto view : m_ImageViews)
        vkDestroyImageView(device, view, nullptr);
    m_ImageViews.clear();
    vkDestroySwapchainKHR(device, m_Swapchain, nullptr);
    m_Swapchain = VK_NULL_HANDLE;
}

void Swapchain::Recreate() {
    m_Context.WaitIdle();
    Cleanup();
    Create();

    m_Framebuffers.resize(m_ImageViews.size());
    for (size_t i = 0; i < m_ImageViews.size(); i++) {
        VkFramebufferCreateInfo info{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        info.renderPass = m_RenderPass;
        info.attachmentCount = 1;
        info.pAttachments = &m_ImageViews[i];
        info.width = m_Extent.width;
        info.height = m_Extent.height;
        info.layers = 1;
        VK_CHECK(vkCreateFramebuffer(m_Context.GetDevice(), &info, nullptr, &m_Framebuffers[i]));
    }
}

VkResult Swapchain::AcquireNextImage(VkSemaphore signalSemaphore, uint32_t* outImageIndex) {
    return vkAcquireNextImageKHR(m_Context.GetDevice(), m_Swapchain, UINT64_MAX,
                                 signalSemaphore, VK_NULL_HANDLE, outImageIndex);
}

VkResult Swapchain::Present(VkSemaphore waitSemaphore, uint32_t imageIndex) {
    VkPresentInfoKHR info{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &waitSemaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &m_Swapchain;
    info.pImageIndices = &imageIndex;
    return vkQueuePresentKHR(m_Context.GetPresentQueue(), &info);
}
