#pragma once

#include "core/Base.h"

class VulkanContext;
class Window;
class Renderer2D;

// Owns the Dear ImGui context and its GLFW/Vulkan backends. ImGui records
// into the same render pass as the scene, after the scene, so editor panels
// composite on top of the rendered world.
class ImGuiLayer {
public:
    ImGuiLayer(VulkanContext& context, Window& window, Renderer2D& renderer);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void Begin();                      // start a new ImGui frame + dockspace
    void End(VkCommandBuffer cmd);     // render draw data into the command buffer

private:
    VulkanContext& m_Context;
    VkDescriptorPool m_Pool = VK_NULL_HANDLE;
};
