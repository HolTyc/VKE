#include "EditorApp.hpp"

#include <vke/Keys.hpp>
#include <vke/SceneSerializer.hpp>
#include <vke/Script.hpp>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <utility>

namespace fs = std::filesystem;

void EditorApp::onStart() {
    setEditorVisible(false); // NoProject screen first
    browser_.reset();

    vke::EditorGUI* gui = editorGui();
    gui->setScriptHost(&scripts_);
    gui->onFileMenu = [this](vke::Application&) {
        if (ImGui::MenuItem("New Project...")) requestNewPopup_ = true;
        if (ImGui::MenuItem("Open Project...")) requestOpenPopup_ = true;
        if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, state_ == State::Edit)) saveScene();
        ImGui::Separator();
        ImGui::MenuItem("Console", nullptr, &showConsole_);
        ImGui::Separator();
    };

    if (!startupProject_.empty()) openProject(startupProject_);
}

void EditorApp::onUpdate(float dt) {
    if (state_ == State::Play) {
        scripts_.update(scene(), dt);
        renderer().postProcessTime += dt;

        const bool esc = window().keyDown(vke::Key::Escape);
        if (esc && !prevEsc_) stopPlay();
        prevEsc_ = esc;
        return;
    }
    prevEsc_ = window().keyDown(vke::Key::Escape);

    if (state_ == State::Edit) {
        const bool ctrl = window().keyDown(vke::Key::LeftControl) ||
                          window().keyDown(vke::Key::RightControl);
        const bool s = ctrl && window().keyDown(vke::Key::S);
        const bool r = ctrl && window().keyDown(vke::Key::R);
        if (s && !prevCtrlS_) saveScene();
        if (r && !prevCtrlR_) reloadScripts();
        prevCtrlS_ = s;
        prevCtrlR_ = r;
    }
}

void EditorApp::onGui() {
    if (state_ == State::NoProject)
        drawNoProjectWindow();
    else
        drawToolbar();

    if (showConsole_) drawConsole();
    drawProjectDialogs();
}

// ------------------------------------------------------------------ projects

void EditorApp::newProject(const fs::path& dir, const std::string& name) {
    std::string error;
    const fs::path projectDir = dir / name;
    if (!Project::scaffold(projectDir, name, error)) {
        log("New project failed: " + error);
        showConsole_ = true;
        return;
    }
    log("Created project " + projectDir.string());
    openProject(projectDir);
}

void EditorApp::openProject(const fs::path& dir) {
    Project loaded;
    std::string error;
    if (!Project::load(fs::absolute(dir), loaded, error)) {
        log("Open project failed: " + error);
        showConsole_ = true;
        return;
    }

    // Tear down whatever is loaded (script instances first, then the scene).
    renderer().waitIdle();
    scripts_.unload(scene());
    scene().clear();
    renderer().setPostProcessEnabled(false);
    snapshot_.clear();

    project_ = std::move(loaded);
    renderer().setAssetRoot(project_.dir.string());

    for (const auto& s : project_.shaders) {
        if (s.name.empty()) continue;
        renderer().registerShader(s.name, s.vert, s.frag);
        log("Registered shader \"" + s.name + "\"");
    }
    if (project_.hasPostProcess) {
        renderer().setPostProcessShader(project_.postVert, project_.postFrag);
        renderer().setPostProcessEnabled(false); // play-mode only
        renderer().postProcessStrength = project_.postStrength;
        renderer().postProcessParams = project_.postParams;
    }

    scripts_.setProjectDir(project_.dir);
    std::string buildLog;
    if (scripts_.compile(buildLog)) {
        if (!buildLog.empty()) log(buildLog);
        std::string loadLog;
        if (scripts_.load(loadLog))
            log("Scripts loaded (" + std::to_string(scripts_.classes().size()) + " classes)");
        else {
            log("Script load failed: " + loadLog);
            showConsole_ = true;
        }
    } else {
        log("Script compile failed:\n" + buildLog);
        showConsole_ = true; // non-fatal: slots stay data-only
    }

    if (!vke::SceneSerializer(scene(), renderer())
             .loadFromFile(project_.dir / project_.mainScene, error)) {
        log("Scene load failed: " + error);
        showConsole_ = true;
    }

    createEditorCamera();
    scripts_.instantiate(scene());
    setEditorVisible(true);
    state_ = State::Edit;
    log("Opened project \"" + project_.name + "\" (" + project_.dir.string() + ")");
}

void EditorApp::saveScene() {
    if (state_ != State::Edit) return;
    scene().forEach<vke::ScriptComponent>([this](vke::Entity&, vke::ScriptComponent& sc) {
        for (auto& slot : sc.slots) scripts_.captureProps(slot);
    });
    const fs::path file = project_.dir / project_.mainScene;
    if (vke::SceneSerializer(scene(), renderer()).saveToFile(file, editorCamId_))
        log("Saved " + file.string());
    else {
        log("Save failed: cannot write " + file.string());
        showConsole_ = true;
    }
}

// ---------------------------------------------------------------- play / stop

void EditorApp::enterPlay() {
    if (state_ != State::Edit) return;

    scene().forEach<vke::ScriptComponent>([this](vke::Entity&, vke::ScriptComponent& sc) {
        for (auto& slot : sc.slots) scripts_.captureProps(slot);
    });
    snapshot_ = vke::SceneSerializer(scene(), renderer()).serialize(editorCamId_);

    if (vke::Entity* cam = scene().find(editorCamId_)) {
        editorCamPos_ = cam->transform().position;
        editorCamRot_ = cam->transform().rotation;
    }

    renderer().setCameraOverride(0); // the game's primary camera takes over
    setEditorVisible(false);
    if (project_.hasPostProcess) {
        renderer().postProcessTime = 0.0f;
        renderer().setPostProcessEnabled(true);
    }
    scripts_.start(scene());
    state_ = State::Play;
}

void EditorApp::stopPlay() {
    if (state_ != State::Play) return;

    // Restores the exact pre-play scene; script instances created during play
    // are destroyed by the clear inside deserialize (library stays loaded).
    std::string error;
    if (!vke::SceneSerializer(scene(), renderer()).deserialize(snapshot_, error)) {
        log("Stop failed to restore the scene: " + error);
        showConsole_ = true;
    }
    snapshot_.clear();

    renderer().setPostProcessEnabled(false);
    createEditorCamera();
    scripts_.instantiate(scene());
    setEditorVisible(true);
    state_ = State::Edit;
}

void EditorApp::reloadScripts() {
    if (state_ == State::Play) stopPlay();
    if (state_ != State::Edit) return;

    std::string buildLog;
    if (scripts_.reload(scene(), buildLog)) {
        if (!buildLog.empty()) log(buildLog);
        log("Scripts reloaded (" + std::to_string(scripts_.classes().size()) + " classes)");
    } else {
        log("Script reload failed:\n" + buildLog);
        showConsole_ = true;
    }
}

void EditorApp::createEditorCamera() {
    vke::Entity& cam = scene().createEntity("__EditorCamera");
    cam.add<vke::CameraComponent>().primary = false; // never the game's camera
    cam.transform().position = editorCamPos_;
    cam.transform().rotation = editorCamRot_;
    editorCamId_ = cam.id();
    renderer().setCameraOverride(editorCamId_);
    editorGui()->setFlyCamera(editorCamId_);
}

// ------------------------------------------------------------------------ ui

void EditorApp::log(const std::string& text) {
    console_.push_back(text);
    if (console_.size() > 500) console_.erase(console_.begin(), console_.begin() + 100);
    consoleToBottom_ = true;
}

void EditorApp::drawNoProjectWindow() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                             vp->WorkPos.y + vp->WorkSize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::Begin("Welcome", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("VKE Editor");
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button("New Project...", {220, 0})) requestNewPopup_ = true;
    if (ImGui::Button("Open Project...", {220, 0})) requestOpenPopup_ = true;
    ImGui::End();
}

void EditorApp::drawToolbar() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 8.0f},
                            ImGuiCond_Always, {0.5f, 0.0f});
    ImGui::Begin("##toolbar", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

    if (state_ == State::Edit) {
        if (ImGui::Button("\xe2\x96\xb6 Play")) enterPlay();
        ImGui::SameLine();
        if (ImGui::Button("Reload Scripts")) reloadScripts();
    } else {
        if (ImGui::Button("\xe2\x96\xa0 Stop")) stopPlay();
        ImGui::SameLine();
        ImGui::TextDisabled("Esc stops too");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("| %s — %s", project_.name.c_str(),
                        state_ == State::Play ? "Playing" : "Editing");
    ImGui::End();
}

void EditorApp::drawConsole() {
    ImGui::SetNextWindowSize({640, 220}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Console", &showConsole_);
    if (ImGui::Button("Clear")) console_.clear();
    ImGui::Separator();
    ImGui::BeginChild("##console_lines", {0, 0}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (const std::string& line : console_)
        ImGui::TextUnformatted(line.c_str());
    if (consoleToBottom_) {
        ImGui::SetScrollHereY(1.0f);
        consoleToBottom_ = false;
    }
    ImGui::EndChild();
    ImGui::End();
}

void EditorApp::drawProjectDialogs() {
    if (requestNewPopup_) {
        ImGui::OpenPopup("New Project");
        requestNewPopup_ = false;
    }
    if (requestOpenPopup_) {
        ImGui::OpenPopup("Open Project");
        requestOpenPopup_ = false;
    }

    if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", &newProjectName_);
        ImGui::TextDisabled("Location (the project folder is created inside):");
        browser_.draw("new_project");
        ImGui::Spacing();

        const bool canCreate = !newProjectName_.empty();
        if (!canCreate) ImGui::BeginDisabled();
        if (ImGui::Button("Create", {120, 0})) {
            newProject(browser_.path, newProjectName_);
            ImGui::CloseCurrentPopup();
        }
        if (!canCreate) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Open Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("Navigate into the project folder:");
        browser_.draw("open_project");
        const bool isProject = fs::exists(fs::path(browser_.path) / "project.json");
        ImGui::Spacing();
        if (isProject)
            ImGui::Text("project.json found.");
        else
            ImGui::TextDisabled("No project.json here.");

        if (!isProject) ImGui::BeginDisabled();
        if (ImGui::Button("Open", {120, 0})) {
            openProject(browser_.path);
            ImGui::CloseCurrentPopup();
        }
        if (!isProject) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
