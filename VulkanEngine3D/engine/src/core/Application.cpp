#include "vke/Application.hpp"

#include <algorithm>
#include <chrono>

namespace vke {

Application::Application(const AppConfig& config) : config_(config) {
    window_ = std::make_unique<Window>(config_.width, config_.height, config_.title,
                                       config_.fullscreen);
    renderer_ = std::make_unique<Renderer3D>(*window_);
    scene_ = std::make_unique<Scene>();
    if (config_.editor || config_.gui)
        editor_ = std::make_unique<EditorGUI>(*window_, *renderer_);
    editorVisible_ = config_.editor;
}

Application::~Application() {
    if (renderer_) renderer_->waitIdle();
}

void Application::setRenderMode(RenderMode mode) {
    config_.mode = mode;
    if (mode == RenderMode::OnDemand)
        window_->requestRedraw(); // ensure one more frame so the UI reflects the change
}

void Application::run() {
    running_ = true;
    onStart();

    using clock = std::chrono::steady_clock;
    auto lastTime = clock::now();

    while (running_ && !window_->shouldClose()) {
        // Continuous: classic game loop. OnDemand: block until input/window
        // events arrive (CAD-style apps burn zero CPU/GPU while idle).
        if (config_.mode == RenderMode::OnDemand)
            Window::waitEvents();
        else
            Window::pollEvents();

        auto now = clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        dt = std::min(dt, 0.1f); // clamp after long idle waits / debugger pauses

        onUpdate(dt);
        // The fly camera and editor panels are editor-only (and can be hidden
        // at runtime via setEditorVisible); with just config.gui the ImGui
        // layer exists purely for the app's onGui().
        bool editorActive = config_.editor && editorVisible_;
        if (editor_ && editorActive) editor_->processInput(*this, dt);

        if (VkCommandBuffer cmd = renderer_->beginFrame()) {
            if (editor_) {
                editor_->beginFrame();
                if (editorActive) editor_->buildUI(*this);
                onGui();
            }
            renderer_->renderScene(*scene_);
            // Composite the offscreen scene before ImGui so UI stays unfiltered
            // (no-op unless a post-process shader is enabled).
            renderer_->applyPostProcess();
            if (editor_) editor_->render(cmd);
            renderer_->endFrame();
        }
    }

    renderer_->waitIdle();
}

} // namespace vke
