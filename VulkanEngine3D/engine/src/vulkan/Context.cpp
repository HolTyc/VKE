#include "vke/vulkan/Context.hpp"

#include "vke/Window.hpp"

#include <GLFW/glfw3.h>

#include <cstring>
#include <iostream>
#include <set>
#include <vector>

namespace vke {

namespace {

const std::vector<const char*> kValidationLayers = {"VK_LAYER_KHRONOS_validation"};
const std::vector<const char*> kDeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

bool validationLayersAvailable() {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());

    for (const char* wanted : kValidationLayers) {
        bool found = false;
        for (const auto& layer : layers)
            if (std::strcmp(layer.layerName, wanted) == 0) { found = true; break; }
        if (!found) return false;
    }
    return true;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::cerr << "[vulkan] " << data->pMessage << "\n";
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo() {
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugCallback;
    return info;
}

} // namespace

VulkanContext::VulkanContext(Window& window) : window_(window) {
#ifndef NDEBUG
    validationEnabled_ = validationLayersAvailable();
    if (!validationEnabled_)
        std::cerr << "[vke] Validation layers requested but not available — continuing without.\n";
#endif
    createInstance();
    setupDebugMessenger();
    window_.createSurface(instance, &surface);
    pickPhysicalDevice();
    createLogicalDevice();
    createCommandPool();
}

VulkanContext::~VulkanContext() {
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyDevice(device, nullptr);
    if (debugMessenger_ != VK_NULL_HANDLE) {
        auto destroyFn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyFn) destroyFn(instance, debugMessenger_, nullptr);
    }
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
}

void VulkanContext::waitIdle() const {
    vkDeviceWaitIdle(device);
}

void VulkanContext::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "VKE Application";
    appInfo.pEngineName = "VKE";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    uint32_t glfwCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwCount);
    if (validationEnabled_)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &appInfo;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugInfo = debugMessengerCreateInfo();
    if (validationEnabled_) {
        info.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
        info.ppEnabledLayerNames = kValidationLayers.data();
        info.pNext = &debugInfo;
    }

    VK_CHECK(vkCreateInstance(&info, nullptr, &instance));
}

void VulkanContext::setupDebugMessenger() {
    if (!validationEnabled_) return;
    auto createFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (!createFn) return;
    VkDebugUtilsMessengerCreateInfoEXT info = debugMessengerCreateInfo();
    createFn(instance, &info, nullptr, &debugMessenger_);
}

bool VulkanContext::findQueueFamilies(VkPhysicalDevice dev,
                                      uint32_t& graphics, uint32_t& present) const {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, families.data());

    bool foundGraphics = false, foundPresent = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!foundGraphics && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            graphics = i;
            foundGraphics = true;
        }
        VkBool32 supportsPresent = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &supportsPresent);
        if (!foundPresent && supportsPresent) {
            present = i;
            foundPresent = true;
        }
        if (foundGraphics && foundPresent) return true;
    }
    return foundGraphics && foundPresent;
}

bool VulkanContext::isDeviceSuitable(VkPhysicalDevice dev) const {
    uint32_t g = 0, p = 0;
    if (!findQueueFamilies(dev, g, p)) return false;

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> available(extCount);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, available.data());

    std::set<std::string> required(kDeviceExtensions.begin(), kDeviceExtensions.end());
    for (const auto& ext : available)
        required.erase(ext.extensionName);
    if (!required.empty()) return false;

    uint32_t formatCount = 0, presentModeCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &formatCount, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &presentModeCount, nullptr);
    return formatCount > 0 && presentModeCount > 0;
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0)
        throw std::runtime_error("No GPU with Vulkan support found");
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    int bestScore = -1;
    for (auto dev : devices) {
        if (!isDeviceSuitable(dev)) continue;
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);
        int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU   ? 100
                  : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 50
                                                                               : 10;
        if (score > bestScore) {
            bestScore = score;
            best = dev;
        }
    }
    if (best == VK_NULL_HANDLE)
        throw std::runtime_error("No suitable GPU found (graphics + present + swapchain required)");

    physicalDevice = best;
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    findQueueFamilies(physicalDevice, graphicsFamily, presentFamily);
    std::cout << "[vke] GPU: " << properties.deviceName << "\n";
}

void VulkanContext::createLogicalDevice() {
    std::set<uint32_t> uniqueFamilies = {graphicsFamily, presentFamily};
    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo q{};
        q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        q.queueFamilyIndex = family;
        q.queueCount = 1;
        q.pQueuePriorities = &priority;
        queueInfos.push_back(q);
    }

    VkPhysicalDeviceFeatures features{};

    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    info.pQueueCreateInfos = queueInfos.data();
    info.pEnabledFeatures = &features;
    info.enabledExtensionCount = static_cast<uint32_t>(kDeviceExtensions.size());
    info.ppEnabledExtensionNames = kDeviceExtensions.data();

    VK_CHECK(vkCreateDevice(physicalDevice, &info, nullptr, &device));
    vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);
}

void VulkanContext::createCommandPool() {
    VkCommandPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = graphicsFamily;
    VK_CHECK(vkCreateCommandPool(device, &info, nullptr, &commandPool));
}

// ---------------------------------------------------------------- helpers

uint32_t VulkanContext::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

VkFormat VulkanContext::findSupportedFormat(const std::vector<VkFormat>& candidates,
                                            VkImageTiling tiling,
                                            VkFormatFeatureFlags features) const {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
        if (tiling == VK_IMAGE_TILING_LINEAR &&
            (props.linearTilingFeatures & features) == features)
            return format;
        if (tiling == VK_IMAGE_TILING_OPTIMAL &&
            (props.optimalTilingFeatures & features) == features)
            return format;
    }
    throw std::runtime_error("Failed to find supported format");
}

VkFormat VulkanContext::findDepthFormat() const {
    return findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void VulkanContext::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags props,
                                 VkBuffer& buffer, VkDeviceMemory& memory) const {
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(device, &info, nullptr, &buffer));

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, buffer, &memReq);

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = memReq.size;
    alloc.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, props);
    VK_CHECK(vkAllocateMemory(device, &alloc, nullptr, &memory));
    VK_CHECK(vkBindBufferMemory(device, buffer, memory, 0));
}

void VulkanContext::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const {
    VkCommandBuffer cmd = beginSingleTimeCommands();
    VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    endSingleTimeCommands(cmd);
}

void VulkanContext::createImage(uint32_t width, uint32_t height, VkFormat format,
                                VkImageTiling tiling, VkImageUsageFlags usage,
                                VkMemoryPropertyFlags props,
                                VkImage& image, VkDeviceMemory& memory) const {
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.format = format;
    info.tiling = tiling;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage = usage;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateImage(device, &info, nullptr, &image));

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device, image, &memReq);

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = memReq.size;
    alloc.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, props);
    VK_CHECK(vkAllocateMemory(device, &alloc, nullptr, &memory));
    VK_CHECK(vkBindImageMemory(device, image, memory, 0));
}

VkImageView VulkanContext::createImageView(VkImage image, VkFormat format,
                                           VkImageAspectFlags aspect) const {
    VkImageViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange = {aspect, 0, 1, 0, 1};

    VkImageView view;
    VK_CHECK(vkCreateImageView(device, &info, nullptr, &view));
    return view;
}

VkCommandBuffer VulkanContext::beginSingleTimeCommands() const {
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandPool = commandPool;
    alloc.commandBufferCount = 1;

    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(device, &alloc, &cmd));

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin));
    return cmd;
}

void VulkanContext::endSingleTimeCommands(VkCommandBuffer cmd) const {
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE));
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
}

} // namespace vke
