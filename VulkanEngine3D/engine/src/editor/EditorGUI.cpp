#include "vke/EditorGUI.hpp"

#include "vke/Application.hpp"
#include "vke/Renderer3D.hpp"
#include "vke/Script.hpp"
#include "vke/ScriptHost.hpp"
#include "vke/Window.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <imgui_stdlib.h>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace vke {

EditorGUI::EditorGUI(Window& window, Renderer3D& renderer)
    : window_(window), renderer_(renderer) {
    auto& ctx = renderer_.context();

    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64},
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 64;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    VK_CHECK(vkCreateDescriptorPool(ctx.device, &poolInfo, nullptr, &imguiPool_));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 4.0f;
    ImGui::GetStyle().FrameRounding = 3.0f;

    ImGui_ImplGlfw_InitForVulkan(window_.handle(), true);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = ctx.instance;
    initInfo.PhysicalDevice = ctx.physicalDevice;
    initInfo.Device = ctx.device;
    initInfo.QueueFamily = ctx.graphicsFamily;
    initInfo.Queue = ctx.graphicsQueue;
    initInfo.DescriptorPool = imguiPool_;
    initInfo.RenderPass = renderer_.swapchain().renderPass();
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = renderer_.swapchain().imageCount();
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&initInfo);
    ImGui_ImplVulkan_CreateFontsTexture();
}

EditorGUI::~EditorGUI() {
    renderer_.waitIdle();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(renderer_.context().device, imguiPool_, nullptr);
}

void EditorGUI::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void EditorGUI::render(VkCommandBuffer cmd) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

void EditorGUI::buildUI(Application& app) {
    drawMenuBar(app);
    drawHierarchy(app);
    drawInspector(app);
    if (showStats_) drawStats(app);
}

// ----------------------------------------------------------------- fly camera

void EditorGUI::processInput(Application& app, float dt) {
    Entity* cam = flyCamId_ ? app.scene().find(flyCamId_) : app.scene().primaryCamera();
    if (!cam) return;

    GLFWwindow* win = window_.handle();
    ImGuiIO& io = ImGui::GetIO();
    bool rmb = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

    double cx, cy;
    glfwGetCursorPos(win, &cx, &cy);

    if (rmb && (flying_ || !io.WantCaptureMouse)) {
        if (!flying_) {
            flying_ = true;
            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            lastX_ = cx;
            lastY_ = cy;
        }
        auto& t = cam->transform();
        const float sensitivity = 0.15f;
        t.rotation.y -= static_cast<float>(cx - lastX_) * sensitivity;
        t.rotation.x = glm::clamp(
            t.rotation.x - static_cast<float>(cy - lastY_) * sensitivity, -89.0f, 89.0f);

        float speed = (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? 15.0f : 5.0f) * dt;
        glm::vec3 move{0.0f};
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) move += t.forward();
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) move -= t.forward();
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) move += t.right();
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) move -= t.right();
        if (glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS) move += glm::vec3{0, 1, 0};
        if (glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS) move -= glm::vec3{0, 1, 0};
        t.position += move * speed;
    } else if (flying_) {
        flying_ = false;
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    lastX_ = cx;
    lastY_ = cy;
}

// --------------------------------------------------------------------- panels

void EditorGUI::drawMenuBar(Application& app) {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (onFileMenu) onFileMenu(app);
        if (ImGui::MenuItem("Quit")) app.close();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Rendering")) {
        bool continuous = app.renderMode() == RenderMode::Continuous;
        if (ImGui::MenuItem("Continuous (game loop)", nullptr, continuous))
            app.setRenderMode(RenderMode::Continuous);
        if (ImGui::MenuItem("On-demand (event-driven)", nullptr, !continuous))
            app.setRenderMode(RenderMode::OnDemand);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Stats", nullptr, &showStats_);
        ImGui::EndMenu();
    }
    if (onMainMenu) onMainMenu(app);
    ImGui::EndMainMenuBar();
}

void EditorGUI::drawHierarchy(Application& app) {
    Scene& scene = app.scene();

    ImGui::SetNextWindowPos({10, 32}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({270, 480}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Hierarchy");

    if (ImGui::Button("+ Entity")) scene.createEntity();
    ImGui::SameLine();
    if (ImGui::Button("+ Cube")) {
        auto& e = scene.createEntity("Cube");
        e.add<MeshRendererComponent>().mesh = renderer_.primitive(Primitive::Cube);
        selected_ = e.id();
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Sphere")) {
        auto& e = scene.createEntity("Sphere");
        e.add<MeshRendererComponent>().mesh = renderer_.primitive(Primitive::Sphere);
        selected_ = e.id();
    }
    if (ImGui::Button("+ Plane")) {
        auto& e = scene.createEntity("Plane");
        e.add<MeshRendererComponent>().mesh = renderer_.primitive(Primitive::Plane);
        selected_ = e.id();
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Light")) {
        auto& e = scene.createEntity("Light");
        e.transform().position = {0.0f, 3.0f, 0.0f};
        e.add<LightComponent>();
        selected_ = e.id();
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Camera")) {
        auto& e = scene.createEntity("Camera");
        e.add<CameraComponent>().primary = (scene.primaryCamera() == nullptr);
        selected_ = e.id();
    }

    ImGui::Separator();

    uint32_t toDelete = 0;
    for (auto& entity : scene.entities()) {
        if (entity->name.rfind("__", 0) == 0) continue; // editor-internal (e.g. editor camera)
        ImGui::PushID(static_cast<int>(entity->id()));
        bool isSelected = entity->id() == selected_;
        if (ImGui::Selectable(entity->name.c_str(), isSelected))
            selected_ = entity->id();
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Delete")) toDelete = entity->id();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (toDelete != 0) {
        renderer_.waitIdle(); // mesh buffers may still be referenced by in-flight frames
        scene.destroyEntity(toDelete);
        if (selected_ == toDelete) selected_ = 0;
    }

    ImGui::End();
}

void EditorGUI::drawInspector(Application& app) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x - 330, 32}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({320, 560}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Inspector");

    Entity* entity = app.scene().find(selected_);
    if (!entity) {
        ImGui::TextDisabled("Select an entity in the Hierarchy.");
        ImGui::End();
        return;
    }

    char nameBuf[128];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", entity->name.c_str());
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        entity->name = nameBuf;

    // ---- Transform -------------------------------------------------------
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& t = entity->transform();
        ImGui::DragFloat3("Position", &t.position.x, 0.05f);
        ImGui::DragFloat3("Rotation", &t.rotation.x, 0.5f);
        ImGui::DragFloat3("Scale", &t.scale.x, 0.05f, 0.001f, 1000.0f);
    }

    // ---- MeshRenderer ----------------------------------------------------
    if (auto* mr = entity->get<MeshRendererComponent>()) {
        bool open = ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen);
        bool removed = false;
        if (ImGui::BeginPopupContextItem("##mr_ctx")) {
            if (ImGui::MenuItem("Remove Component")) removed = true;
            ImGui::EndPopup();
        }
        if (open && !removed) {
            ImGui::ColorEdit4("Albedo", &mr->material.albedo.x);
            ImGui::DragFloat("Shininess", &mr->material.shininess, 1.0f, 1.0f, 256.0f);
            ImGui::DragFloat("Specular", &mr->material.specular, 0.01f, 0.0f, 1.0f);

            if (ImGui::BeginCombo("Shader", mr->material.shader.c_str())) {
                for (const auto& name : renderer_.shaderNames()) {
                    if (ImGui::Selectable(name.c_str(), name == mr->material.shader))
                        mr->material.shader = name;
                }
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("%u verts / %u indices",
                                mr->mesh ? mr->mesh->vertexCount() : 0,
                                mr->mesh ? mr->mesh->indexCount() : 0);
        }
        if (removed) {
            renderer_.waitIdle();
            entity->remove<MeshRendererComponent>();
        }
    }

    // ---- Light -----------------------------------------------------------
    if (auto* light = entity->get<LightComponent>()) {
        bool open = ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen);
        bool removed = false;
        if (ImGui::BeginPopupContextItem("##light_ctx")) {
            if (ImGui::MenuItem("Remove Component")) removed = true;
            ImGui::EndPopup();
        }
        if (open && !removed) {
            const char* types[] = {"Directional", "Point", "Spot"};
            int type = static_cast<int>(light->type);
            if (ImGui::Combo("Type", &type, types, 3))
                light->type = static_cast<LightComponent::Type>(type);
            ImGui::ColorEdit3("Color", &light->color.x);
            ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0.0f, 50.0f);
            if (light->type != LightComponent::Type::Directional)
                ImGui::DragFloat("Range", &light->range, 0.1f, 0.1f, 200.0f);
            if (light->type == LightComponent::Type::Spot) {
                ImGui::DragFloat("Inner Angle", &light->innerAngle, 0.5f, 1.0f, 89.0f);
                ImGui::DragFloat("Outer Angle", &light->outerAngle, 0.5f, 1.0f, 89.0f);
                light->outerAngle = std::max(light->outerAngle, light->innerAngle);
            }
            if (light->type != LightComponent::Type::Point)
                ImGui::TextDisabled("Direction follows the Transform rotation.");
        }
        if (removed) entity->remove<LightComponent>();
    }

    // ---- Camera ----------------------------------------------------------
    if (auto* cam = entity->get<CameraComponent>()) {
        bool open = ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen);
        bool removed = false;
        if (ImGui::BeginPopupContextItem("##cam_ctx")) {
            if (ImGui::MenuItem("Remove Component")) removed = true;
            ImGui::EndPopup();
        }
        if (open && !removed) {
            ImGui::SliderFloat("FOV", &cam->fov, 10.0f, 120.0f, "%.0f deg");
            ImGui::DragFloat("Near", &cam->nearClip, 0.01f, 0.001f, 10.0f);
            ImGui::DragFloat("Far", &cam->farClip, 1.0f, 1.0f, 10000.0f);
            ImGui::Checkbox("Primary", &cam->primary);
        }
        if (removed) entity->remove<CameraComponent>();
    }

    // ---- Scripts -----------------------------------------------------------
    if (scriptHost_) {
        if (auto* sc = entity->get<ScriptComponent>()) {
            int removeIdx = -1;
            for (int i = 0; i < static_cast<int>(sc->slots.size()); ++i) {
                ScriptSlot& slot = sc->slots[i];
                ImGui::PushID(i);
                const std::string label = slot.type + " (Script)";
                bool open = ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                if (ImGui::BeginPopupContextItem("##script_ctx")) {
                    if (ImGui::MenuItem("Remove Script")) removeIdx = i;
                    ImGui::EndPopup();
                }
                if (open) {
                    const ScriptClassDesc* desc = scriptHost_->findClass(slot.type);
                    if (slot.instance && desc) {
                        // Widgets write straight into the live script object.
                        char* base = reinterpret_cast<char*>(slot.instance.get());
                        for (size_t p = 0; p < desc->propCount; ++p) {
                            const PropDesc& prop = desc->props[p];
                            void* mem = base + prop.offset;
                            switch (prop.type) {
                                case PropType::Float:
                                    ImGui::DragFloat(prop.name, static_cast<float*>(mem), 0.05f);
                                    break;
                                case PropType::Int:
                                    ImGui::DragInt(prop.name, static_cast<int*>(mem));
                                    break;
                                case PropType::Bool:
                                    ImGui::Checkbox(prop.name, static_cast<bool*>(mem));
                                    break;
                                case PropType::Vec3:
                                    ImGui::DragFloat3(prop.name, static_cast<float*>(mem), 0.05f);
                                    break;
                                case PropType::String:
                                    ImGui::InputText(prop.name, static_cast<std::string*>(mem));
                                    break;
                            }
                        }
                        if (desc->propCount == 0) ImGui::TextDisabled("No properties.");
                    } else {
                        ImGui::TextDisabled(scriptHost_->loaded()
                                                ? "Class missing from script library."
                                                : "Script library not loaded.");
                        for (const auto& [name, value] : slot.props) {
                            std::visit(
                                [&name](const auto& v) {
                                    using T = std::decay_t<decltype(v)>;
                                    if constexpr (std::is_same_v<T, glm::vec3>)
                                        ImGui::Text("%s: (%.3f, %.3f, %.3f)", name.c_str(), v.x, v.y, v.z);
                                    else if constexpr (std::is_same_v<T, std::string>)
                                        ImGui::Text("%s: %s", name.c_str(), v.c_str());
                                    else if constexpr (std::is_same_v<T, bool>)
                                        ImGui::Text("%s: %s", name.c_str(), v ? "true" : "false");
                                    else if constexpr (std::is_same_v<T, int>)
                                        ImGui::Text("%s: %d", name.c_str(), v);
                                    else
                                        ImGui::Text("%s: %.3f", name.c_str(), static_cast<double>(v));
                                },
                                value);
                        }
                    }
                }
                ImGui::PopID();
            }
            if (removeIdx >= 0) {
                sc->slots.erase(sc->slots.begin() + removeIdx); // no GPU resources: no waitIdle
                if (sc->slots.empty()) entity->remove<ScriptComponent>();
            }
        }
    }

    // ---- Add component ----------------------------------------------------
    ImGui::Spacing();
    if (ImGui::Button("Add Component", {-1, 0})) ImGui::OpenPopup("##add_component");
    if (ImGui::BeginPopup("##add_component")) {
        if (!entity->has<MeshRendererComponent>() && ImGui::MenuItem("Mesh Renderer"))
            entity->add<MeshRendererComponent>().mesh = renderer_.primitive(Primitive::Cube);
        if (!entity->has<LightComponent>() && ImGui::MenuItem("Light"))
            entity->add<LightComponent>();
        if (!entity->has<CameraComponent>() && ImGui::MenuItem("Camera"))
            entity->add<CameraComponent>().primary = false;
        if (scriptHost_ && scriptHost_->loaded()) {
            const auto classes = scriptHost_->classes();
            if (!classes.empty() && ImGui::BeginMenu("Script")) {
                for (const ScriptClassDesc* desc : classes) {
                    if (ImGui::MenuItem(desc->name)) {
                        auto* sc = entity->get<ScriptComponent>();
                        if (!sc) sc = &entity->add<ScriptComponent>();
                        ScriptSlot slot;
                        slot.type = desc->name;
                        sc->slots.push_back(std::move(slot));
                        scriptHost_->instantiateSlot(*entity, sc->slots.back());
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

void EditorGUI::drawStats(Application& app) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({10, io.DisplaySize.y - 110}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Stats", &showStats_,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::Text("%.1f FPS (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);
    ImGui::Text("Entities: %zu", app.scene().entities().size());
    ImGui::Text("Mode: %s", app.renderMode() == RenderMode::Continuous
                                ? "Continuous"
                                : "On-demand (event-driven)");
    ImGui::TextDisabled("RMB + WASD/QE: fly camera");
    ImGui::End();
}

} // namespace vke
