#include "editor/EditorLayer.h"
#include "core/Application.h"
#include "ecs/Entity.h"
#include "ecs/Components.h"

#include <imgui.h>

#include <glm/gtc/constants.hpp>

void EditorLayer::OnImGuiRender(Application& app) {
    DrawHierarchy(app);
    DrawInspector(app);
    DrawEnginePanel(app);
}

void EditorLayer::DrawHierarchy(Application& app) {
    Scene& scene = app.GetScene();
    ImGui::Begin("Hierarchy");

    if (ImGui::Button("+ Create Entity", ImVec2(-1, 0)))
        m_Selected = scene.CreateEntity().GetID();

    ImGui::Separator();

    EntityID toDestroy = NullEntity;
    for (EntityID e : scene.GetEntities()) {
        auto* tag = scene.TryGet<TagComponent>(e);
        const char* name = tag ? tag->Name.c_str() : "Entity";

        ImGui::PushID((int)e);
        if (ImGui::Selectable(name, m_Selected == e))
            m_Selected = e;
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Destroy"))
                toDestroy = e;
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    if (toDestroy != NullEntity) {
        scene.DestroyEntity(toDestroy);
        if (m_Selected == toDestroy)
            m_Selected = NullEntity;
    }

    ImGui::End();
}

void EditorLayer::DrawInspector(Application& app) {
    Scene& scene = app.GetScene();
    ImGui::Begin("Inspector");

    if (m_Selected == NullEntity || !scene.IsValid(m_Selected)) {
        ImGui::TextDisabled("No entity selected");
        ImGui::End();
        return;
    }

    // --- Tag ---------------------------------------------------------------
    if (auto* tag = scene.TryGet<TagComponent>(m_Selected)) {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "%s", tag->Name.c_str());
        if (ImGui::InputText("Name", buffer, sizeof(buffer)))
            tag->Name = buffer;
    }
    ImGui::Spacing();

    // --- Transform ----------------------------------------------------------
    if (auto* transform = scene.TryGet<TransformComponent>(m_Selected)) {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Position", &transform->Position.x, 0.05f);
            ImGui::DragFloat2("Scale", &transform->Scale.x, 0.05f);
            float degrees = glm::degrees(transform->Rotation);
            if (ImGui::DragFloat("Rotation", &degrees, 1.0f))
                transform->Rotation = glm::radians(degrees);
        }
    }

    // --- Sprite -------------------------------------------------------------
    if (auto* sprite = scene.TryGet<SpriteComponent>(m_Selected)) {
        bool open = ImGui::CollapsingHeader("Sprite", ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::BeginPopupContextItem("sprite_ctx")) {
            if (ImGui::MenuItem("Remove Component")) {
                scene.Remove<SpriteComponent>(m_Selected);
                ImGui::EndPopup();
                ImGui::End();
                return;
            }
            ImGui::EndPopup();
        }
        if (open) {
            ImGui::ColorEdit4("Color", &sprite->Color.x);
            ImGui::Text("Texture: %s", sprite->Tex ? sprite->Tex->GetName().c_str() : "(none)");
            if (sprite->Tex && ImGui::SmallButton("Clear Texture"))
                sprite->Tex = nullptr;
        }
    }

    // --- Script (read-only indicator) ----------------------------------------
    if (scene.Has<NativeScriptComponent>(m_Selected)) {
        if (ImGui::CollapsingHeader("Native Script"))
            ImGui::TextDisabled("C++ behavior attached");
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Add Component", ImVec2(-1, 0)))
        ImGui::OpenPopup("add_component");
    if (ImGui::BeginPopup("add_component")) {
        if (!scene.Has<SpriteComponent>(m_Selected) && ImGui::MenuItem("Sprite"))
            scene.Add<SpriteComponent>(m_Selected);
        ImGui::EndPopup();
    }

    ImGui::End();
}

void EditorLayer::DrawEnginePanel(Application& app) {
    ImGui::Begin("Engine");

    // --- Playback -------------------------------------------------------------
    if (ImGui::Button(app.IsPlaying() ? "Pause Scripts" : "Play Scripts", ImVec2(-1, 0)))
        app.SetPlaying(!app.IsPlaying());

    ImGui::Separator();

    // --- Stats ---------------------------------------------------------------
    const auto& stats = app.GetRenderer().GetStats();
    ImGui::Text("FPS: %.1f (%.2f ms)", ImGui::GetIO().Framerate,
                1000.0f / std::max(ImGui::GetIO().Framerate, 1.0f));
    ImGui::Text("Entities: %zu", app.GetScene().GetEntityCount());
    ImGui::Text("Quads: %u | Draw calls: %u", stats.QuadCount, stats.DrawCalls);

    ImGui::Separator();

    // --- Render settings -------------------------------------------------------
    int mode = app.GetRenderMode() == RenderMode::Continuous ? 0 : 1;
    if (ImGui::Combo("Render Mode", &mode, "Continuous (game loop)\0Event-driven (on input)\0"))
        app.SetRenderMode(mode == 0 ? RenderMode::Continuous : RenderMode::EventDriven);

    glm::vec4 clear = app.GetRenderer().GetClearColor();
    if (ImGui::ColorEdit3("Clear Color", &clear.x))
        app.GetRenderer().SetClearColor(clear);

    // --- Camera ----------------------------------------------------------------
    Camera2D& camera = app.GetCamera();
    ImGui::DragFloat2("Camera Pos", &camera.Position.x, 0.05f);
    ImGui::DragFloat("Camera Zoom", &camera.Zoom, 0.01f, 0.05f, 50.0f);

    ImGui::Separator();
    ImGui::TextDisabled("F1 toggles the editor");

    ImGui::End();
}
