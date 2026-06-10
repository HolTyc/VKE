#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

struct GLFWwindow;

namespace vke {

// Thin RAII wrapper over a GLFW window configured for Vulkan rendering.
// Tracks resize state and an input "dirty" flag used by event-driven rendering.
class Window {
public:
    // fullscreen = borderless fullscreen on the primary monitor at the desktop
    // resolution (width/height are ignored in that case).
    Window(uint32_t width, uint32_t height, const std::string& title, bool fullscreen = false);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const;
    VkExtent2D framebufferExtent() const;

    static void pollEvents();
    static void waitEvents();

    // Wakes up a blocked waitEvents() — used to force a redraw in event-driven mode.
    void requestRedraw();

    // Runtime switch between borderless fullscreen (primary monitor, desktop
    // resolution) and a regular window (restores the previous windowed size).
    // The swapchain rebuild happens automatically through the resize flag.
    void setFullscreen(bool fullscreen);
    bool isFullscreen() const;

    bool wasResized() const { return resized_; }
    void clearResized() { resized_ = false; }

    void createSurface(VkInstance instance, VkSurfaceKHR* surface);

    bool keyDown(int glfwKey) const;
    bool mouseButtonDown(int glfwButton) const;
    void cursorPos(double& x, double& y) const;

    GLFWwindow* handle() const { return window_; }

private:
    GLFWwindow* window_ = nullptr;
    bool resized_ = false;
    // Last windowed geometry, restored when leaving fullscreen.
    int windowedX_ = 100, windowedY_ = 100, windowedW_ = 1280, windowedH_ = 720;

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};

} // namespace vke
