#include "Project.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

#ifndef VKE_EDITOR_TEMPLATE_DIR
#define VKE_EDITOR_TEMPLATE_DIR ""
#endif

namespace {

bool readFile(const fs::path& path, std::string& out) {
    std::ifstream in(path);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool writeFile(const fs::path& path, const std::string& content) {
    std::ofstream out(path);
    if (!out) return false;
    out << content;
    return static_cast<bool>(out);
}

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size())
        s.replace(pos, from.size(), to);
}

} // namespace

bool Project::load(const fs::path& dir, Project& out, std::string& error) {
    std::string text;
    if (!readFile(dir / "project.json", text)) {
        error = "no project.json in " + dir.string();
        return false;
    }

    json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) {
        error = dir.string() + "/project.json is not valid JSON";
        return false;
    }

    out = Project{};
    out.dir = dir;
    out.name = j.value("name", dir.filename().string());
    out.mainScene = j.value("mainScene", std::string("scenes/main.scene"));

    if (j.contains("postProcess")) {
        const json& pp = j["postProcess"];
        out.postVert = pp.value("vert", "");
        out.postFrag = pp.value("frag", "");
        out.hasPostProcess = !out.postVert.empty() && !out.postFrag.empty();
        out.postStrength = pp.value("strength", 1.0f);
        if (pp.contains("params") && pp["params"].is_array() && pp["params"].size() >= 4)
            out.postParams = {pp["params"][0].get<float>(), pp["params"][1].get<float>(),
                              pp["params"][2].get<float>(), pp["params"][3].get<float>()};
    }

    if (j.contains("shaders"))
        for (const json& s : j["shaders"])
            out.shaders.push_back({s.value("name", ""), s.value("vert", ""), s.value("frag", "")});

    return true;
}

bool Project::scaffold(const fs::path& dir, const std::string& name, std::string& error) {
    if (fs::exists(dir / "project.json")) {
        error = dir.string() + " already contains a project";
        return false;
    }

    std::error_code ec;
    for (const char* sub : {"", "scenes", "scripts", "assets", ".vke"}) {
        fs::create_directories(dir / sub, ec);
        if (ec) {
            error = "cannot create " + (dir / sub).string() + ": " + ec.message();
            return false;
        }
    }

    const fs::path tpl = VKE_EDITOR_TEMPLATE_DIR;
    const std::pair<const char*, fs::path> files[] = {
        {"project.json", dir / "project.json"},
        {"main.scene", dir / "scenes" / "main.scene"},
        {"Rotator.cpp", dir / "scripts" / "Rotator.cpp"},
        {"gitignore", dir / ".gitignore"},
    };
    for (const auto& [src, dst] : files) {
        std::string content;
        if (!readFile(tpl / src, content)) {
            error = "missing editor template " + (tpl / src).string();
            return false;
        }
        replaceAll(content, "{{NAME}}", name);
        if (!writeFile(dst, content)) {
            error = "cannot write " + dst.string();
            return false;
        }
    }
    return true;
}
