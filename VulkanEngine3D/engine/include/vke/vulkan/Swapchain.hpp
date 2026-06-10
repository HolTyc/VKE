#pragma once

#include "Context.hpp"

#include <vector>

namespace vke {

// Swapchain + depth buffer + render pass + framebuffers + frame synchronization.
// The render pass is created once and survives swapchain recreation (recreated
// swapchains use an identical, therefore compatible, attachment layout).
class Swapchain {
public:
    Swapchain(VulkanContext& ctx, VkExtent2D windowExtent);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    // Caller must ensure the device is idle before recreating.
    void recreate(VkExtent2D windowExtent);

    // Waits for the frame's fence and acquires the next swapchain image.
    VkResult acquireNextImage(uint32_t frameIndex, uint32_t* imageIndex);

    // Submits the command buffer and presents the image.
    VkResult submitAndPresent(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex);

    VkRenderPass  renderPass() const { return renderPass_; }
    VkFramebuffer framebuffer(uint32_t imageIndex) const { return framebuffers_[imageIndex]; }
    VkExtent2D    extent() const { return extent_; }
    VkFormat      imageFormat() const { return imageFormat_; }
    uint32_t      imageCount() const { return static_cast<uint32_t>(images_.size()); }
    float aspectRatio() const {
        return static_cast<float>(extent_.width) / static_cast<float>(extent_.height);
    }

private:
    void createSwapchain(VkExtent2D windowExtent);
    void createImageViews();
    void createDepthResources();
    void createRenderPass();
    void createFramebuffers();
    void createSyncObjects();
    void destroyImageResources();
    void destroyPerImageSemaphores();

    VulkanContext& ctx_;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat   imageFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat   depthFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};

    std::vector<VkImage>     images_;
    std::vector<VkImageView> imageViews_;

    VkImage        depthImage_  = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView    depthView_   = VK_NULL_HANDLE;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    std::vector<VkSemaphore> imageAvailable_; // per frame in flight
    std::vector<VkSemaphore> renderFinished_; // per swapchain image
    std::vector<VkFence>     inFlight_;       // per frame in flight
};

} // namespace vke
