#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>

namespace vke {

class Window;
class Renderer3D;
class Application;
class Entity;
class ScriptHost;

// Lightweight Dear ImGui editor layer: scene hierarchy, component inspector,
// stats overlay and a fly camera. Deliberately minimal — no asset browser,
// no timelines, no node graphs.
class EditorGUI {
public:
    EditorGUI(Window& window, Renderer3D& renderer);
    ~EditorGUI();

    EditorGUI(const EditorGUI&) = delete;
    EditorGUI& operator=(const EditorGUI&) = delete;

    // Fly-camera input (RMB look + WASD/QE move). Call once per frame, outside the ImGui frame.
    void processInput(Application& app, float dt);

    void beginFrame();
    void buildUI(Application& app);
    void render(VkCommandBuffer cmd);

    // ---- host-app extension points (all optional; defaults = old behavior) --
    // Extra menu items, drawn inside the File menu (before Quit) and after the
    // built-in menus respectively.
    std::function<void(Application&)> onFileMenu;
    std::function<void(Application&)> onMainMenu;

    // Entity the fly camera drives; 0 = the scene's primary camera (default).
    void setFlyCamera(uint32_t entityId) { flyCamId_ = entityId; }

    // Enables script slots in the Inspector and the Add Component menu.
    void setScriptHost(ScriptHost* host) { scriptHost_ = host; }

private:
    void drawMenuBar(Application& app);
    void drawHierarchy(Application& app);
    void drawInspector(Application& app);
    void drawStats(Application& app);

    Window& window_;
    Renderer3D& renderer_;

    VkDescriptorPool imguiPool_ = VK_NULL_HANDLE;

    uint32_t selected_ = 0; // entity id, 0 = none
    uint32_t flyCamId_ = 0;
    ScriptHost* scriptHost_ = nullptr;
    bool showStats_ = true;
    bool flying_ = false;
    double lastX_ = 0.0, lastY_ = 0.0;
};

} // namespace vke
