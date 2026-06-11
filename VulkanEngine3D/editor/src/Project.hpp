#pragma once

// A VKE project on disk:
//   <dir>/project.json    name, main scene, optional shaders/post-process
//   <dir>/scenes/         *.scene (JSON, see SceneSerializer)
//   <dir>/scripts/        C++ scripts, hot-compiled by ScriptHost
//   <dir>/assets/         models etc., resolved via Renderer3D::setAssetRoot
//   <dir>/.vke/           build artifacts (compiled scripts.so), gitignored

#include <glm/glm.hpp>

#include <filesystem>
#include <string>
#include <vector>

struct Project {
    struct ShaderEntry {
        std::string name, vert, frag; // .spv paths, project-relative
    };

    std::filesystem::path dir;
    std::string name;
    std::string mainScene = "scenes/main.scene";

    bool hasPostProcess = false;
    std::string postVert, postFrag; // .spv paths, project-relative
    float postStrength = 1.0f;
    glm::vec4 postParams{0.0f};

    std::vector<ShaderEntry> shaders;

    // Parses <dir>/project.json.
    static bool load(const std::filesystem::path& dir, Project& out, std::string& error);

    // Creates the folder skeleton from editor/templates (sample scene + script).
    static bool scaffold(const std::filesystem::path& dir, const std::string& name,
                         std::string& error);
};
