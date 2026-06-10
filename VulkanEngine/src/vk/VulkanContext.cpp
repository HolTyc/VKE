#include "vk/VulkanContext.h"
#include "core/Window.h"

#include <GLFW/glfw3.h>

#include <set>

static const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
static const char* kDeviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        ENGINE_WARN("[vulkan] %s", data->pMessage);
    return VK_FALSE;
}

static bool HasValidationLayer() {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto& l : layers)
        if (std::strcmp(l.layerName, kValidationLayer) == 0)
            return true;
    return false;
}

VulkanContext::VulkanContext(Window& window) {
    s_Instance = this;

    CreateInstance("VulkanEngine App");
    SetupDebugMessenger();
    m_Surface = window.CreateSurface(m_Instance);
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateCommandPool();
    CreateDescriptorResources();
}

VulkanContext::~VulkanContext() {
    vkDestroyDescriptorSetLayout(m_Device, m_TextureSetLayout, nullptr);
    vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
    vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
    vkDestroyDevice(m_Device, nullptr);
    if (m_DebugMessenger) {
        auto destroyFn = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroyFn) destroyFn(m_Instance, m_DebugMessenger, nullptr);
    }
    vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    vkDestroyInstance(m_Instance, nullptr);
    s_Instance = nullptr;
}

void VulkanContext::CreateInstance(const std::string& appName) {
#ifdef NDEBUG
    m_ValidationEnabled = false;
#else
    m_ValidationEnabled = HasValidationLayer();
    if (!m_ValidationEnabled)
        ENGINE_WARN("Validation layers requested but not available; continuing without them.");
#endif

    VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = appName.c_str();
    appInfo.pEngineName = "VulkanEngine";
    appInfo.apiVersion = VK_API_VERSION_1_2;

    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCount);
    if (m_ValidationEnabled)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkInstanceCreateInfo info{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    info.pApplicationInfo = &appInfo;
    info.enabledExtensionCount = (uint32_t)extensions.size();
    info.ppEnabledExtensionNames = extensions.data();
    if (m_ValidationEnabled) {
        info.enabledLayerCount = 1;
        info.ppEnabledLayerNames = &kValidationLayer;
    }
    VK_CHECK(vkCreateInstance(&info, nullptr, &m_Instance));
}

void VulkanContext::SetupDebugMessenger() {
    if (!m_ValidationEnabled)
        return;
    VkDebugUtilsMessengerCreateInfoEXT info{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = DebugCallback;

    auto createFn = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT");
    if (createFn)
        VK_CHECK(createFn(m_Instance, &info, nullptr, &m_DebugMessenger));
}

QueueFamilies VulkanContext::FindQueueFamilies(VkPhysicalDevice device) const {
    QueueFamilies families;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, props.data());

    for (uint32_t i = 0; i < count; i++) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            if (families.Graphics == UINT32_MAX)
                families.Graphics = i;
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &present);
        if (present && families.Present == UINT32_MAX)
            families.Present = i;
        if (families.Complete())
            break;
    }
    return families;
}

void VulkanContext::PickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_Instance, &count, nullptr);
    if (count == 0) {
        ENGINE_ERROR("No Vulkan-capable GPU found");
        std::abort();
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_Instance, &count, devices.data());

    int bestScore = -1;
    for (auto device : devices) {
        QueueFamilies families = FindQueueFamilies(device);
        if (!families.Complete())
            continue;

        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, exts.data());
        bool hasSwapchain = false;
        for (const auto& e : exts)
            if (std::strcmp(e.extensionName, kDeviceExtensions[0]) == 0)
                hasSwapchain = true;
        if (!hasSwapchain)
            continue;

        uint32_t formatCount = 0, presentModeCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, nullptr);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, nullptr);
        if (formatCount == 0 || presentModeCount == 0)
            continue;

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        int score = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ? 1000 : 100;
        if (score > bestScore) {
            bestScore = score;
            m_PhysicalDevice = device;
            m_Families = families;
        }
    }

    if (m_PhysicalDevice == VK_NULL_HANDLE) {
        ENGINE_ERROR("No suitable GPU found (need graphics+present queues and swapchain support)");
        std::abort();
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_PhysicalDevice, &props);
    ENGINE_INFO("Using GPU: %s", props.deviceName);
}

void VulkanContext::CreateLogicalDevice() {
    std::set<uint32_t> uniqueFamilies = { m_Families.Graphics, m_Families.Present };
    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo qi{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        qi.queueFamilyIndex = family;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        queueInfos.push_back(qi);
    }

    VkPhysicalDeviceFeatures features{};
    VkDeviceCreateInfo info{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    info.queueCreateInfoCount = (uint32_t)queueInfos.size();
    info.pQueueCreateInfos = queueInfos.data();
    info.pEnabledFeatures = &features;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = kDeviceExtensions;
    VK_CHECK(vkCreateDevice(m_PhysicalDevice, &info, nullptr, &m_Device));

    vkGetDeviceQueue(m_Device, m_Families.Graphics, 0, &m_GraphicsQueue);
    vkGetDeviceQueue(m_Device, m_Families.Present, 0, &m_PresentQueue);
}

void VulkanContext::CreateCommandPool() {
    VkCommandPoolCreateInfo info{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = m_Families.Graphics;
    VK_CHECK(vkCreateCommandPool(m_Device, &info, nullptr, &m_CommandPool));
}

void VulkanContext::CreateDescriptorResources() {
    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024 };
    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1024;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VK_CHECK(vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool));

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_TextureSetLayout));
}

uint32_t VulkanContext::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    ENGINE_ERROR("No suitable memory type found");
    std::abort();
}

void VulkanContext::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags props,
                                 VkBuffer& outBuffer, VkDeviceMemory& outMemory) const {
    VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(m_Device, &info, nullptr, &outBuffer));

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_Device, outBuffer, &req);

    VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, props);
    VK_CHECK(vkAllocateMemory(m_Device, &alloc, nullptr, &outMemory));
    VK_CHECK(vkBindBufferMemory(m_Device, outBuffer, outMemory, 0));
}

void VulkanContext::CreateImage2D(uint32_t width, uint32_t height, VkFormat format,
                                  VkImageUsageFlags usage,
                                  VkImage& outImage, VkDeviceMemory& outMemory) const {
    VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = { width, height, 1 };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.format = format;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage = usage;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateImage(m_Device, &info, nullptr, &outImage));

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(m_Device, outImage, &req);

    VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(m_Device, &alloc, nullptr, &outMemory));
    VK_CHECK(vkBindImageMemory(m_Device, outImage, outMemory, 0));
}

VkCommandBuffer VulkanContext::BeginSingleTimeCommands() const {
    VkCommandBufferAllocateInfo alloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    alloc.commandPool = m_CommandPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;

    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(m_Device, &alloc, &cmd));

    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin));
    return cmd;
}

void VulkanContext::EndSingleTimeCommands(VkCommandBuffer cmd) const {
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(m_GraphicsQueue, 1, &submit, VK_NULL_HANDLE));
    vkQueueWaitIdle(m_GraphicsQueue);
    vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &cmd);
}

void VulkanContext::CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const {
    VkCommandBuffer cmd = BeginSingleTimeCommands();
    VkBufferCopy region{ 0, 0, size };
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    EndSingleTimeCommands(cmd);
}
