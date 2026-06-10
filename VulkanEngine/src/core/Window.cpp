#include "core/Window.h"

#include <GLFW/glfw3.h>

static void GlfwErrorCallback(int code, const char* description) {
    ENGINE_ERROR("GLFW error %d: %s", code, description);
}

Window::Window(const std::string& title, uint32_t width, uint32_t height) {
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) {
        ENGINE_ERROR("Failed to initialize GLFW");
        std::abort();
    }
    if (!glfwVulkanSupported()) {
        ENGINE_ERROR("GLFW reports no Vulkan support (is the loader/driver installed?)");
        std::abort();
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_Window = glfwCreateWindow((int)width, (int)height, title.c_str(), nullptr, nullptr);
    if (!m_Window) {
        ENGINE_ERROR("Failed to create window");
        std::abort();
    }

    glfwSetWindowUserPointer(m_Window, this);

    glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow* win, int, int) {
        auto* self = (Window*)glfwGetWindowUserPointer(win);
        self->m_Resized = true;
    });
    glfwSetKeyCallback(m_Window, [](GLFWwindow* win, int key, int, int action, int) {
        auto* self = (Window*)glfwGetWindowUserPointer(win);
        if (self->m_KeyCallback)
            self->m_KeyCallback(key, action);
    });
}

Window::~Window() {
    glfwDestroyWindow(m_Window);
    glfwTerminate();
}

bool Window::ShouldClose() const { return glfwWindowShouldClose(m_Window); }
void Window::RequestClose() { glfwSetWindowShouldClose(m_Window, GLFW_TRUE); }

void Window::PollEvents() { glfwPollEvents(); }
void Window::WaitEvents(double timeoutSeconds) { glfwWaitEventsTimeout(timeoutSeconds); }
void Window::PostEmptyEvent() { glfwPostEmptyEvent(); }

VkSurfaceKHR Window::CreateSurface(VkInstance instance) {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VK_CHECK(glfwCreateWindowSurface(instance, m_Window, nullptr, &surface));
    return surface;
}

VkExtent2D Window::GetFramebufferExtent() const {
    int w = 0, h = 0;
    glfwGetFramebufferSize(m_Window, &w, &h);
    return { (uint32_t)w, (uint32_t)h };
}
