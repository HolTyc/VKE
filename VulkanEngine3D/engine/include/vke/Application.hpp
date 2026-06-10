#pragma once

#include "EditorGUI.hpp"
#include "Renderer3D.hpp"
#include "Scene.hpp"
#include "Window.hpp"

#include <memory>
#include <string>

namespace vke {

enum class RenderMode {
    Continuous, // classic game loop: render every frame
    OnDemand    // event-driven: render only when input/window events arrive (CAD-style)
};

struct AppConfig {
    uint32_t    width  = 1600;
    uint32_t    height = 900;
    std::string title  = "VKE Application";
    RenderMode  mode   = RenderMode::Continuous;
    bool        editor = true;      // enable the ImGui editor layer (panels + fly cam)
    bool        gui    = false;     // enable ImGui *without* the editor panels, so
                                    // onGui() can draw game UI (menus, HUDs).
                                    // Implied by editor = true.
    bool        fullscreen = false; // borderless fullscreen on the primary monitor
                                    // (width/height ignored)
};

// Entry point of the engine. Subclass it, override onStart / onUpdate / onGui,
// then call run().
class Application {
public:
    explicit Application(const AppConfig& config = {});
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();
    void close() { running_ = false; }

    Scene&      scene() { return *scene_; }
    Renderer3D& renderer() { return *renderer_; }
    Window&     window() { return *window_; }

    RenderMode renderMode() const { return config_.mode; }
    void setRenderMode(RenderMode mode);

    // In OnDemand mode, forces one more frame to be rendered.
    void requestRedraw() { window_->requestRedraw(); }

    // Runtime fullscreen toggle (forwards to Window).
    void setFullscreen(bool fullscreen) { window_->setFullscreen(fullscreen); }
    bool isFullscreen() const { return window_->isFullscreen(); }

    // Shows/hides the editor panels + fly camera at runtime (needs
    // config.editor = true). While hidden the app behaves like a plain game;
    // onGui() keeps running either way.
    void setEditorVisible(bool visible) { editorVisible_ = visible; }
    bool editorVisible() const { return editorVisible_; }

protected:
    virtual void onStart() {}
    virtual void onUpdate(float /*dt*/) {}
    virtual void onGui() {} // extra ImGui windows (called when config.editor or config.gui is set)

private:
    AppConfig config_;
    std::unique_ptr<Window>     window_;
    std::unique_ptr<Renderer3D> renderer_;
    std::unique_ptr<Scene>      scene_;
    std::unique_ptr<EditorGUI>  editor_;
    bool running_ = false;
    bool editorVisible_ = true; // initialized from config.editor
};

} // namespace vke
