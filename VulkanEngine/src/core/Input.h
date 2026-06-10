#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

// Static polling input. Key/button codes are the GLFW_KEY_* / GLFW_MOUSE_BUTTON_*
// constants, so user code can write Input::IsKeyDown(GLFW_KEY_SPACE).
class Input {
public:
    static void Init(GLFWwindow* window) { s_Window = window; }

    static bool IsKeyDown(int key) {
        int state = glfwGetKey(s_Window, key);
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    static bool IsMouseButtonDown(int button) {
        return glfwGetMouseButton(s_Window, button) == GLFW_PRESS;
    }

    static glm::vec2 GetMousePosition() {
        double x = 0.0, y = 0.0;
        glfwGetCursorPos(s_Window, &x, &y);
        return { (float)x, (float)y };
    }

private:
    static inline GLFWwindow* s_Window = nullptr;
};
