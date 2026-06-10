#include "editor/ImGuiLayer.h"
#include "vk/VulkanContext.h"
#include "core/Window.h"
#include "render/Renderer2D.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

ImGuiLayer::ImGuiLayer(VulkanContext& context, Window& window, Renderer2D& renderer)
    : m_Context(context) {
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128 },
    };
    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 128;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    VK_CHECK(vkCreateDescriptorPool(context.GetDevice(), &poolInfo, nullptr, &m_Pool));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window.GetNative(), true);

    ImGui_ImplVulkan_InitInfo init{};
    init.Instance = context.GetInstance();
    init.PhysicalDevice = context.GetPhysicalDevice();
    init.Device = context.GetDevice();
    init.QueueFamily = context.GetQueueFamilies().Graphics;
    init.Queue = context.GetGraphicsQueue();
    init.DescriptorPool = m_Pool;
    init.RenderPass = renderer.GetRenderPass();
    init.MinImageCount = 2;
    init.ImageCount = renderer.GetSwapchain().GetImageCount();
    init.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init.Subpass = 0;
    ImGui_ImplVulkan_Init(&init);
    ImGui_ImplVulkan_CreateFontsTexture();
}

ImGuiLayer::~ImGuiLayer() {
    m_Context.WaitIdle();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(m_Context.GetDevice(), m_Pool, nullptr);
}

void ImGuiLayer::Begin() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Dockable side panels with a transparent central node, so the scene
    // (rendered fullscreen behind the UI) stays visible in the middle.
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                 ImGuiDockNodeFlags_PassthruCentralNode);
}

void ImGuiLayer::End(VkCommandBuffer cmd) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}
