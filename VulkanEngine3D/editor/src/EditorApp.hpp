#pragma once

#include "FileDialog.hpp"
#include "Project.hpp"

#include <vke/vke.hpp>
#include <vke/ScriptHost.hpp>

#include <filesystem>
#include <string>
#include <vector>

// The standalone engine editor: create/open projects anywhere on disk, edit
// scenes, attach hot-reloadable C++ scripts and play/stop the game in place.
class EditorApp : public vke::Application {
public:
    explicit EditorApp(const vke::AppConfig& config)
        : vke::Application(config), scripts_(this) {}

    // Project to open right after startup (CLI argument).
    void setStartupProject(std::filesystem::path dir) { startupProject_ = std::move(dir); }

protected:
    void onStart() override;
    void onUpdate(float dt) override;
    void onGui() override;

private:
    enum class State { NoProject, Edit, Play };

    void newProject(const std::filesystem::path& dir, const std::string& name);
    void openProject(const std::filesystem::path& dir);
    void saveScene();
    void enterPlay();
    void stopPlay();
    void reloadScripts();

    void createEditorCamera();
    void log(const std::string& text);

    void drawNoProjectWindow();
    void drawToolbar();
    void drawConsole();
    void drawProjectDialogs();

    State state_ = State::NoProject;
    Project project_;
    vke::ScriptHost scripts_;

    std::string snapshot_;          // scene JSON captured on Play
    uint32_t editorCamId_ = 0;      // "__EditorCamera", excluded from save/snapshot
    glm::vec3 editorCamPos_{4.0f, 3.0f, 8.0f};
    glm::vec3 editorCamRot_{-15.0f, 25.0f, 0.0f};

    std::vector<std::string> console_;
    bool showConsole_ = false;
    bool consoleToBottom_ = false;

    // New/Open Project dialog state
    bool requestNewPopup_ = false, requestOpenPopup_ = false;
    DirBrowser browser_;
    std::string newProjectName_ = "MyGame";

    bool prevEsc_ = false, prevCtrlS_ = false, prevCtrlR_ = false;
    std::filesystem::path startupProject_;
};
