#include "vke/vulkan/Swapchain.hpp"

#include <algorithm>
#include <array>

namespace vke {

Swapchain::Swapchain(VulkanContext& ctx, VkExtent2D windowExtent) : ctx_(ctx) {
    createSwapchain(windowExtent);
    createImageViews();
    createRenderPass();
    createDepthResources();
    createFramebuffers();
    createSyncObjects();
}

Swapchain::~Swapchain() {
    destroyImageResources();
    vkDestroyRenderPass(ctx_.device, renderPass_, nullptr);
    destroyPerImageSemaphores();
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(ctx_.device, imageAvailable_[i], nullptr);
        vkDestroyFence(ctx_.device, inFlight_[i], nullptr);
    }
}

void Swapchain::destroyImageResources() {
    vkDestroyImageView(ctx_.device, depthView_, nullptr);
    vkDestroyImage(ctx_.device, depthImage_, nullptr);
    vkFreeMemory(ctx_.device, depthMemory_, nullptr);
    for (auto fb : framebuffers_)
        vkDestroyFramebuffer(ctx_.device, fb, nullptr);
    framebuffers_.clear();
    for (auto view : imageViews_)
        vkDestroyImageView(ctx_.device, view, nullptr);
    imageViews_.clear();
    vkDestroySwapchainKHR(ctx_.device, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

void Swapchain::destroyPerImageSemaphores() {
    for (auto sem : renderFinished_)
        vkDestroySemaphore(ctx_.device, sem, nullptr);
    renderFinished_.clear();
}

void Swapchain::recreate(VkExtent2D windowExtent) {
    destroyImageResources();
    createSwapchain(windowExtent);
    createImageViews();
    createDepthResources();
    createFramebuffers();

    // Image count may have changed; renderFinished is per swapchain image.
    destroyPerImageSemaphores();
    renderFinished_.resize(images_.size());
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (auto& sem : renderFinished_)
        VK_CHECK(vkCreateSemaphore(ctx_.device, &semInfo, nullptr, &sem));
}

void Swapchain::createSwapchain(VkExtent2D windowExtent) {
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx_.physicalDevice, ctx_.surface, &caps));

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx_.physicalDevice, ctx_.surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx_.physicalDevice, ctx_.surface, &formatCount, formats.data());

    // Prefer a UNORM format: the lighting shader applies gamma manually, and
    // ImGui colors render exactly as authored.
    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    imageFormat_ = chosen.format;

    // FIFO is always available and vsynced — the right default for both game
    // loops and event-driven apps.
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    if (caps.currentExtent.width != UINT32_MAX) {
        extent_ = caps.currentExtent;
    } else {
        extent_.width = std::clamp(windowExtent.width,
                                   caps.minImageExtent.width, caps.maxImageExtent.width);
        extent_.height = std::clamp(windowExtent.height,
                                    caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = ctx_.surface;
    info.minImageCount = imageCount;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;

    uint32_t families[] = {ctx_.graphicsFamily, ctx_.presentFamily};
    if (ctx_.graphicsFamily != ctx_.presentFamily) {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = families;
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VK_CHECK(vkCreateSwapchainKHR(ctx_.device, &info, nullptr, &swapchain_));

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(ctx_.device, swapchain_, &count, nullptr);
    images_.resize(count);
    vkGetSwapchainImagesKHR(ctx_.device, swapchain_, &count, images_.data());
}

void Swapchain::createImageViews() {
    imageViews_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); ++i)
        imageViews_[i] = ctx_.createImageView(images_[i], imageFormat_, VK_IMAGE_ASPECT_COLOR_BIT);
}

void Swapchain::createDepthResources() {
    depthFormat_ = ctx_.findDepthFormat();
    ctx_.createImage(extent_.width, extent_.height, depthFormat_,
                     VK_IMAGE_TILING_OPTIMAL,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     depthImage_, depthMemory_);
    depthView_ = ctx_.createImageView(depthImage_, depthFormat_, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void Swapchain::createRenderPass() {
    if (depthFormat_ == VK_FORMAT_UNDEFINED)
        depthFormat_ = ctx_.findDepthFormat();

    VkAttachmentDescription color{};
    color.format = imageFormat_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format = depthFormat_;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {color, depth};
    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = static_cast<uint32_t>(attachments.size());
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;

    VK_CHECK(vkCreateRenderPass(ctx_.device, &info, nullptr, &renderPass_));
}

void Swapchain::createFramebuffers() {
    framebuffers_.resize(imageViews_.size());
    for (size_t i = 0; i < imageViews_.size(); ++i) {
        std::array<VkImageView, 2> attachments = {imageViews_[i], depthView_};

        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = renderPass_;
        info.attachmentCount = static_cast<uint32_t>(attachments.size());
        info.pAttachments = attachments.data();
        info.width = extent_.width;
        info.height = extent_.height;
        info.layers = 1;
        VK_CHECK(vkCreateFramebuffer(ctx_.device, &info, nullptr, &framebuffers_[i]));
    }
}

void Swapchain::createSyncObjects() {
    imageAvailable_.resize(MAX_FRAMES_IN_FLIGHT);
    inFlight_.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinished_.resize(images_.size());

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkCreateSemaphore(ctx_.device, &semInfo, nullptr, &imageAvailable_[i]));
        VK_CHECK(vkCreateFence(ctx_.device, &fenceInfo, nullptr, &inFlight_[i]));
    }
    for (auto& sem : renderFinished_)
        VK_CHECK(vkCreateSemaphore(ctx_.device, &semInfo, nullptr, &sem));
}

VkResult Swapchain::acquireNextImage(uint32_t frameIndex, uint32_t* imageIndex) {
    vkWaitForFences(ctx_.device, 1, &inFlight_[frameIndex], VK_TRUE, UINT64_MAX);
    return vkAcquireNextImageKHR(ctx_.device, swapchain_, UINT64_MAX,
                                 imageAvailable_[frameIndex], VK_NULL_HANDLE, imageIndex);
}

VkResult Swapchain::submitAndPresent(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex) {
    vkResetFences(ctx_.device, 1, &inFlight_[frameIndex]);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &imageAvailable_[frameIndex];
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderFinished_[imageIndex];
    VK_CHECK(vkQueueSubmit(ctx_.graphicsQueue, 1, &submit, inFlight_[frameIndex]));

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished_[imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &imageIndex;
    return vkQueuePresentKHR(ctx_.presentQueue, &present);
}

} // namespace vke
