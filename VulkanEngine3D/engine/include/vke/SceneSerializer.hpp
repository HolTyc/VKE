#pragma once

// Scene <-> JSON. Mesh references round-trip through Mesh::source
// ("primitive:cube" / "model:<path>"); script slots serialize their type and
// property values only — instances are recreated afterwards by ScriptHost.

#include <cstdint>
#include <filesystem>
#include <string>

namespace vke {

class Scene;
class Renderer3D;

class SceneSerializer {
public:
    SceneSerializer(Scene& scene, Renderer3D& renderer)
        : scene_(scene), renderer_(renderer) {}

    // skipEntityId: an entity to leave out (the editor's own camera).
    // Callers with live script instances must ScriptHost::captureProps first.
    std::string serialize(uint32_t skipEntityId = 0) const;

    // Replaces the scene (waitIdle + clear) with the JSON's contents. Script
    // slots come back without instances — call ScriptHost::instantiate after.
    bool deserialize(const std::string& json, std::string& error);

    bool saveToFile(const std::filesystem::path& path, uint32_t skipEntityId = 0) const;
    bool loadFromFile(const std::filesystem::path& path, std::string& error);

private:
    Scene& scene_;
    Renderer3D& renderer_;
};

} // namespace vke
