#include "core/Application.h"
#include "core/Input.h"

#include <GLFW/glfw3.h>

#include <chrono>

Application::Application(const ApplicationConfig& config) {
    s_Instance = this;
    m_Mode = config.Mode;
    m_Playing = config.StartPlaying;
    m_EditorVisible = config.ShowEditor;

    m_Window = std::make_unique<Window>(config.Name, config.Width, config.Height);
    Input::Init(m_Window->GetNative());

    // Registered before ImGui's GLFW backend installs its callbacks, so ImGui
    // chains into this one.
    m_Window->SetKeyCallback([this](int key, int action) {
        if (key == GLFW_KEY_F1 && action == GLFW_PRESS)
            m_EditorVisible = !m_EditorVisible;
    });

    m_Context = std::make_unique<VulkanContext>(*m_Window);
    m_Renderer = std::make_unique<Renderer2D>(*m_Context, *m_Window);
    m_ImGui = std::make_unique<ImGuiLayer>(*m_Context, *m_Window, *m_Renderer);
    m_Editor = std::make_unique<EditorLayer>();
    m_Scene = std::make_unique<Scene>();

    ENGINE_INFO("Application '%s' initialized", config.Name.c_str());
}

Application::~Application() {
    m_Context->WaitIdle();
    // Destroy in dependency order; everything that owns GPU resources must go
    // before the VulkanContext.
    m_Scene.reset();
    m_Editor.reset();
    m_ImGui.reset();
    m_Renderer.reset();
    m_Context.reset();
    m_Window.reset();
    s_Instance = nullptr;
}

void Application::Close() {
    m_Window->RequestClose();
    m_Window->PostEmptyEvent();
}

void Application::RequestRedraw() {
    m_Window->PostEmptyEvent();
}

void Application::Run() {
    auto lastTime = std::chrono::steady_clock::now();

    while (!m_Window->ShouldClose()) {
        if (m_Mode == RenderMode::EventDriven) {
            // Sleep until input arrives (or a heartbeat tick so ImGui anims
            // like the cursor blink stay alive). Idle cost is near zero.
            m_Window->WaitEvents(0.5);
        } else {
            m_Window->PollEvents();
        }

        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        dt = std::min(dt, 0.1f); // clamp after stalls (resize, debugger, wait)

        VkExtent2D extent = m_Window->GetFramebufferExtent();
        if (extent.width == 0 || extent.height == 0)
            continue; // minimized

        if (m_Playing)
            m_Scene->OnUpdate(dt);
        if (m_OnUpdate)
            m_OnUpdate(dt);

        if (!m_Renderer->BeginFrame())
            continue;

        m_Renderer->BeginScene(m_Camera);
        m_Scene->OnRender(*m_Renderer);
        m_Renderer->EndScene();

        if (m_OnRender)
            m_OnRender(m_Renderer->GetActiveCommandBuffer());

        m_ImGui->Begin();
        if (m_EditorVisible)
            m_Editor->OnImGuiRender(*this);
        m_ImGui->End(m_Renderer->GetActiveCommandBuffer());

        m_Renderer->EndFrame();
    }

    m_Context->WaitIdle();
}
