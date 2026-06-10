#pragma once

#include "core/Base.h"
#include "core/Window.h"
#include "vk/VulkanContext.h"
#include "render/Renderer2D.h"
#include "editor/ImGuiLayer.h"
#include "editor/EditorLayer.h"
#include "ecs/Scene.h"
#include "ecs/Entity.h"
#include "ecs/Components.h"

// Continuous  : classic game loop, renders every frame (vsynced).
// EventDriven : blocks until input arrives — near-zero CPU/GPU when idle.
//               Right choice for tools and standard GUI applications.
enum class RenderMode { Continuous, EventDriven };

struct ApplicationConfig {
    std::string Name = "VulkanEngine App";
    uint32_t Width = 1600;
    uint32_t Height = 900;
    RenderMode Mode = RenderMode::Continuous;
    bool ShowEditor = true;
    bool StartPlaying = true; // run scripts immediately (editor can pause)
};

// The engine facade. Typical usage:
//   Application app({ .Name = "My Game" });
//   Entity e = app.GetScene().CreateEntity("Player");
//   e.Add<SpriteComponent>().Color = { 1, 0, 0, 1 };
//   app.Run();
class Application {
public:
    explicit Application(const ApplicationConfig& config = {});
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void Run();
    void Close();

    static Application& Get() { return *s_Instance; }

    Scene&      GetScene()    { return *m_Scene; }
    Renderer2D& GetRenderer() { return *m_Renderer; }
    Window&     GetWindow()   { return *m_Window; }
    Camera2D&   GetCamera()   { return m_Camera; }

    RenderMode GetRenderMode() const  { return m_Mode; }
    void SetRenderMode(RenderMode mode) { m_Mode = mode; }
    // In EventDriven mode: force one redraw (e.g. after changing app state
    // from a worker thread or timer).
    void RequestRedraw();

    bool IsPlaying() const      { return m_Playing; }
    void SetPlaying(bool play)  { m_Playing = play; }
    bool IsEditorVisible() const     { return m_EditorVisible; }
    void SetEditorVisible(bool show) { m_EditorVisible = show; }

    // Per-frame logic outside the ECS (optional).
    void SetUpdateCallback(std::function<void(float)> fn) { m_OnUpdate = std::move(fn); }
    // Advanced: record custom Vulkan draws after the sprite pass, inside the
    // active render pass (use GetRenderer().GetRenderPass() to build pipelines).
    void SetCustomRenderCallback(std::function<void(VkCommandBuffer)> fn) { m_OnRender = std::move(fn); }

private:
    std::unique_ptr<Window>        m_Window;
    std::unique_ptr<VulkanContext> m_Context;
    std::unique_ptr<Renderer2D>    m_Renderer;
    std::unique_ptr<ImGuiLayer>    m_ImGui;
    std::unique_ptr<EditorLayer>   m_Editor;
    std::unique_ptr<Scene>         m_Scene;

    Camera2D m_Camera;
    RenderMode m_Mode = RenderMode::Continuous;
    bool m_Playing = true;
    bool m_EditorVisible = true;

    std::function<void(float)> m_OnUpdate;
    std::function<void(VkCommandBuffer)> m_OnRender;

    static inline Application* s_Instance = nullptr;
};
