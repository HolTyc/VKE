#include "vke/Window.hpp"

#include <GLFW/glfw3.h>

#include <stdexcept>

namespace vke {

namespace {
int s_windowCount = 0;
}

Window::Window(uint32_t width, uint32_t height, const std::string& title, bool fullscreen) {
    if (s_windowCount++ == 0) {
        if (!glfwInit())
            throw std::runtime_error("Failed to initialize GLFW");
        if (!glfwVulkanSupported())
            throw std::runtime_error("GLFW: Vulkan is not supported on this system");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    windowedW_ = static_cast<int>(width);
    windowedH_ = static_cast<int>(height);

    GLFWmonitor* monitor = nullptr;
    if (fullscreen) {
        // Borderless fullscreen: take the desktop video mode so no mode switch
        // happens and alt-tab stays instant.
        monitor = glfwGetPrimaryMonitor();
        if (const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr) {
            glfwWindowHint(GLFW_RED_BITS, mode->redBits);
            glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
            glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
            glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
            width  = static_cast<uint32_t>(mode->width);
            height = static_cast<uint32_t>(mode->height);
        }
    }

    window_ = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height),
                               title.c_str(), monitor, nullptr);
    if (!window_)
        throw std::runtime_error("Failed to create GLFW window");

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebufferResizeCallback);
}

Window::~Window() {
    glfwDestroyWindow(window_);
    if (--s_windowCount == 0)
        glfwTerminate();
}

void Window::framebufferResizeCallback(GLFWwindow* window, int, int) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    self->resized_ = true;
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(window_);
}

VkExtent2D Window::framebufferExtent() const {
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    return {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
}

void Window::pollEvents() { glfwPollEvents(); }
void Window::waitEvents() { glfwWaitEvents(); }
void Window::requestRedraw() { glfwPostEmptyEvent(); }

bool Window::isFullscreen() const {
    return glfwGetWindowMonitor(window_) != nullptr;
}

void Window::setFullscreen(bool fullscreen) {
    if (fullscreen == isFullscreen()) return;

    if (fullscreen) {
        glfwGetWindowPos(window_, &windowedX_, &windowedY_);
        glfwGetWindowSize(window_, &windowedW_, &windowedH_);
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        if (const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr)
            glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height,
                                 mode->refreshRate);
    } else {
        glfwSetWindowMonitor(window_, nullptr, windowedX_, windowedY_,
                             windowedW_, windowedH_, 0);
    }
    resized_ = true; // force a swapchain rebuild even if the callback is missed
}

void Window::createSurface(VkInstance instance, VkSurfaceKHR* surface) {
    if (glfwCreateWindowSurface(instance, window_, nullptr, surface) != VK_SUCCESS)
        throw std::runtime_error("Failed to create window surface");
}

bool Window::keyDown(int glfwKey) const {
    return glfwGetKey(window_, glfwKey) == GLFW_PRESS;
}

bool Window::mouseButtonDown(int glfwButton) const {
    return glfwGetMouseButton(window_, glfwButton) == GLFW_PRESS;
}

void Window::cursorPos(double& x, double& y) const {
    glfwGetCursorPos(window_, &x, &y);
}

} // namespace vke
