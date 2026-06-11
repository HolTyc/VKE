#include "vke/Script.hpp"

#include "vke/Application.hpp"

namespace vke {

// Out-of-line so script translation units never see Vulkan/GLFW headers;
// these resolve from the editor executable when scripts.so is dlopen'ed.

Window& Script::window() const {
    return app_->window();
}

Renderer3D& Script::renderer() const {
    return app_->renderer();
}

bool Script::keyDown(int key) const {
    return app_->window().keyDown(key);
}

bool Script::mouseButtonDown(int button) const {
    return app_->window().mouseButtonDown(button);
}

} // namespace vke
