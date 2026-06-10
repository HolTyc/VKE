#pragma once

#include "core/Base.h"

struct GLFWwindow;

// Thin GLFW wrapper. Owns the OS window and feeds resize/key events to the
// engine. All Vulkan objects that depend on the framebuffer size watch the
// WasResized() flag.
class Window {
public:
    Window(const std::string& title, uint32_t width, uint32_t height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool ShouldClose() const;
    void RequestClose();

    void PollEvents();
    // Blocks until an event arrives or `timeoutSeconds` elapses (event-driven mode).
    void WaitEvents(double timeoutSeconds);
    // Wakes up a thread blocked in WaitEvents (used by RequestRedraw).
    void PostEmptyEvent();

    VkSurfaceKHR CreateSurface(VkInstance instance);

    VkExtent2D GetFramebufferExtent() const;
    bool WasResized() const { return m_Resized; }
    void ResetResized() { m_Resized = false; }

    GLFWwindow* GetNative() const { return m_Window; }

    // Engine-level key hook (chained before ImGui installs its own callbacks).
    void SetKeyCallback(std::function<void(int key, int action)> cb) { m_KeyCallback = std::move(cb); }

private:
    GLFWwindow* m_Window = nullptr;
    bool m_Resized = false;
    std::function<void(int, int)> m_KeyCallback;
};
