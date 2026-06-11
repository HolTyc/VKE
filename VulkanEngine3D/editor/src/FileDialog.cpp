#include "FileDialog.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

void DirBrowser::reset(const std::string& start) {
    path = start;
    std::error_code ec;
    if (path.empty() || !fs::is_directory(path, ec)) {
        const char* home = std::getenv("HOME");
        path = home ? home : fs::current_path(ec).string();
    }
}

void DirBrowser::draw(const char* id, float listHeight) {
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##path", &path);

    std::error_code ec;
    const bool valid = fs::is_directory(path, ec);

    ImGui::BeginChild("##dirs", {0, listHeight}, ImGuiChildFlags_Borders);
    if (!valid) {
        ImGui::TextDisabled("Not a directory.");
    } else {
        const fs::path current(path);
        if (current.has_parent_path() && current != current.root_path()) {
            if (ImGui::Selectable(".."))
                path = current.parent_path().string();
        }

        std::vector<std::string> dirs;
        for (const auto& entry : fs::directory_iterator(current, ec)) {
            if (!entry.is_directory(ec)) continue;
            std::string name = entry.path().filename().string();
            if (!name.empty() && name.front() == '.') continue;
            dirs.push_back(std::move(name));
        }
        std::sort(dirs.begin(), dirs.end());
        for (const std::string& name : dirs)
            if (ImGui::Selectable((name + "/").c_str()))
                path = (current / name).string();
    }
    ImGui::EndChild();
    ImGui::PopID();
}
