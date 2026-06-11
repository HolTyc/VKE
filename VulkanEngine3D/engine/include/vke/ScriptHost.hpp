#pragma once

// Compiles a project's scripts/ folder into a shared library, loads it with
// dlopen and manages the script instances living in ScriptComponent slots.
// Engine symbols inside the library resolve from the host executable, which
// must therefore be linked with ENABLE_EXPORTS (vke-editor is).

#include "Script.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace vke {

class Application;
class Scene;

class ScriptHost {
public:
    // app may be null for headless compile/load checks (--compile-test); it
    // must be set before any instantiate/start/update call.
    explicit ScriptHost(Application* app = nullptr) : app_(app) {}
    ~ScriptHost(); // dlcloses; all instances must already be destroyed

    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // Project directory containing scripts/; build artifacts go to <dir>/.vke/.
    void setProjectDir(const std::filesystem::path& dir);

    // g++ -shared over scripts/**.cpp. Returns false on failure with the full
    // compiler output in `log` (also filled on success, for warnings).
    bool compile(std::string& log);

    // dlopens the last compiled library (via a fresh per-generation copy so
    // dlopen never hands back a stale cached handle) and fetches its registry.
    bool load(std::string& log);

    // Destroys every script instance in the scene, then dlcloses the library.
    void unload(Scene& scene);

    // captureProps(all) -> unload -> compile -> load -> instantiate. On compile
    // failure the library stays unloaded and slots keep their data. Call only
    // in edit mode (stop play first).
    bool reload(Scene& scene, std::string& log);

    // Creates instances for every slot whose class exists in the registry and
    // applies the slot's saved property values.
    void instantiate(Scene& scene);

    // Creates an instance for one slot on one entity (Add Component path).
    void instantiateSlot(Entity& entity, ScriptSlot& slot);

    // Play-mode hooks. update() also starts instances added during play.
    void start(Scene& scene);
    void update(Scene& scene, float dt);

    // Live instance members -> slot.props / slot.props -> live members.
    void captureProps(ScriptSlot& slot) const;
    void applyProps(ScriptSlot& slot) const;

    bool loaded() const { return handle_ != nullptr; }
    std::vector<const ScriptClassDesc*> classes() const;
    const ScriptClassDesc* findClass(const std::string& name) const;

private:
    Application* app_;
    std::filesystem::path projectDir_;
    void* handle_ = nullptr;
    std::filesystem::path loadedLib_; // the per-generation copy currently open
    const ScriptRegistry* registry_ = nullptr;
    int generation_ = 0;
};

} // namespace vke
