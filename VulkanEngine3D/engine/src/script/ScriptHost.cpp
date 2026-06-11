#include "vke/ScriptHost.hpp"

#include "vke/Application.hpp"

#include <dlfcn.h>

#include <array>
#include <cstdio>
#include <cstring>

namespace vke {

namespace fs = std::filesystem;

// Include paths handed to the script compiler, baked in at engine build time.
#ifndef VKE_ENGINE_INCLUDE_DIR
#define VKE_ENGINE_INCLUDE_DIR ""
#endif
#ifndef VKE_GLM_INCLUDE_DIR
#define VKE_GLM_INCLUDE_DIR ""
#endif

namespace {

std::string quoted(const fs::path& p) {
    return "\"" + p.string() + "\"";
}

// INTERFACE_INCLUDE_DIRECTORIES can be a ;-list and may contain generator
// expressions — keep plain absolute paths only.
std::string extraIncludeFlags() {
    std::string flags;
    std::string dirs = VKE_GLM_INCLUDE_DIR;
    size_t start = 0;
    while (start < dirs.size()) {
        size_t end = dirs.find(';', start);
        if (end == std::string::npos) end = dirs.size();
        std::string dir = dirs.substr(start, end - start);
        if (!dir.empty() && dir.find('$') == std::string::npos)
            flags += " -I\"" + dir + "\"";
        start = end + 1;
    }
    return flags;
}

} // namespace

ScriptHost::~ScriptHost() {
    if (handle_) {
        dlclose(handle_);
        std::error_code ec;
        fs::remove(loadedLib_, ec);
    }
}

void ScriptHost::setProjectDir(const fs::path& dir) {
    projectDir_ = dir;
}

bool ScriptHost::compile(std::string& log) {
    log.clear();
    const fs::path scriptsDir = projectDir_ / "scripts";
    const fs::path outDir = projectDir_ / ".vke";

    std::vector<fs::path> sources;
    if (fs::is_directory(scriptsDir))
        for (const auto& entry : fs::recursive_directory_iterator(scriptsDir))
            if (entry.is_regular_file() && entry.path().extension() == ".cpp")
                sources.push_back(entry.path());
    if (sources.empty()) {
        log = "no .cpp files in " + scriptsDir.string();
        return false;
    }

    std::error_code ec;
    fs::create_directories(outDir, ec);

    std::string cmd = "g++ -std=c++20 -shared -fPIC -g -O2";
    cmd += " -I" + quoted(VKE_ENGINE_INCLUDE_DIR) + extraIncludeFlags();
    cmd += " -DGLM_FORCE_RADIANS -DGLM_FORCE_DEPTH_ZERO_TO_ONE -DVKE_SCRIPT_BUILD";
    cmd += " -Wall -Wno-invalid-offsetof";
    for (const auto& src : sources) cmd += " " + quoted(src);
    cmd += " -o " + quoted(outDir / "scripts.so") + " 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        log = "failed to run compiler: " + cmd;
        return false;
    }
    std::array<char, 1024> buf;
    while (fgets(buf.data(), buf.size(), pipe)) log += buf.data();
    const int status = pclose(pipe);
    return status == 0;
}

bool ScriptHost::load(std::string& log) {
    const fs::path lib = projectDir_ / ".vke" / "scripts.so";
    if (!fs::exists(lib)) {
        log = "no compiled script library at " + lib.string();
        return false;
    }

    // dlopen caches by path; a fresh per-generation copy guarantees the newly
    // compiled code is what gets mapped.
    const fs::path copy =
        projectDir_ / ".vke" / ("scripts.gen" + std::to_string(generation_++) + ".so");
    std::error_code ec;
    fs::copy_file(lib, copy, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        log = "failed to stage " + copy.string() + ": " + ec.message();
        return false;
    }

    void* handle = dlopen(copy.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        log = std::string("dlopen failed: ") + dlerror();
        fs::remove(copy, ec);
        return false;
    }

    using GetRegistryFn = const ScriptRegistry* (*)();
    auto getRegistry =
        reinterpret_cast<GetRegistryFn>(dlsym(handle, "vkeGetScriptRegistry"));
    if (!getRegistry) {
        log = "scripts.so has no vkeGetScriptRegistry (no VKE_SCRIPT classes?)";
        dlclose(handle);
        fs::remove(copy, ec);
        return false;
    }

    const ScriptRegistry* registry = getRegistry();
    if (!registry || registry->abi != VKE_SCRIPT_ABI) {
        log = "script ABI mismatch (library " +
              std::to_string(registry ? registry->abi : 0) + ", engine " +
              std::to_string(VKE_SCRIPT_ABI) + ") — rebuild against current headers";
        dlclose(handle);
        fs::remove(copy, ec);
        return false;
    }

    handle_ = handle;
    loadedLib_ = copy;
    registry_ = registry;
    return true;
}

void ScriptHost::unload(Scene& scene) {
    scene.forEach<ScriptComponent>([](Entity&, ScriptComponent& sc) {
        for (auto& slot : sc.slots) {
            if (slot.instance) slot.instance->onDestroy();
            slot.instance.reset(); // ScriptDeleter calls into the still-open library
            slot.started = false;
        }
    });
    registry_ = nullptr;
    if (handle_) {
        dlclose(handle_);
        handle_ = nullptr;
        std::error_code ec;
        fs::remove(loadedLib_, ec);
        loadedLib_.clear();
    }
}

bool ScriptHost::reload(Scene& scene, std::string& log) {
    scene.forEach<ScriptComponent>([this](Entity&, ScriptComponent& sc) {
        for (auto& slot : sc.slots) captureProps(slot);
    });
    unload(scene);
    if (!compile(log)) return false;
    std::string loadLog;
    if (!load(loadLog)) {
        log += (log.empty() ? "" : "\n") + loadLog;
        return false;
    }
    instantiate(scene);
    return true;
}

void ScriptHost::instantiate(Scene& scene) {
    scene.forEach<ScriptComponent>([this, &scene](Entity& e, ScriptComponent& sc) {
        for (auto& slot : sc.slots)
            if (!slot.instance) instantiateSlot(e, slot);
    });
}

void ScriptHost::instantiateSlot(Entity& entity, ScriptSlot& slot) {
    const ScriptClassDesc* desc = findClass(slot.type);
    if (!desc) return; // missing class: slot keeps its data, Inspector shows it

    Script* raw = desc->create();
    slot.instance = std::unique_ptr<Script, ScriptDeleter>(raw, ScriptDeleter{desc->destroy});
    slot.instance->_bind(app_, &app_->scene(), &entity);
    slot.started = false;
    applyProps(slot);
}

void ScriptHost::start(Scene& scene) {
    scene.forEach<ScriptComponent>([](Entity&, ScriptComponent& sc) {
        for (auto& slot : sc.slots)
            if (slot.instance && !slot.started) {
                slot.instance->onStart();
                slot.started = true;
            }
    });
}

void ScriptHost::update(Scene& scene, float dt) {
    scene.forEach<ScriptComponent>([this, dt](Entity& e, ScriptComponent& sc) {
        for (auto& slot : sc.slots) {
            if (!slot.instance) instantiateSlot(e, slot); // added during play
            if (!slot.instance) continue;
            if (!slot.started) {
                slot.instance->onStart();
                slot.started = true;
            }
            slot.instance->onUpdate(dt);
        }
    });
}

void ScriptHost::captureProps(ScriptSlot& slot) const {
    if (!slot.instance) return; // props already authoritative
    const ScriptClassDesc* desc = findClass(slot.type);
    if (!desc) return;

    slot.props.clear();
    const char* base = reinterpret_cast<const char*>(slot.instance.get());
    for (size_t i = 0; i < desc->propCount; ++i) {
        const PropDesc& p = desc->props[i];
        const char* mem = base + p.offset;
        PropValue value;
        switch (p.type) {
            case PropType::Float:  value = *reinterpret_cast<const float*>(mem); break;
            case PropType::Int:    value = *reinterpret_cast<const int*>(mem); break;
            case PropType::Bool:   value = *reinterpret_cast<const bool*>(mem); break;
            case PropType::Vec3:   value = *reinterpret_cast<const glm::vec3*>(mem); break;
            case PropType::String: value = *reinterpret_cast<const std::string*>(mem); break;
        }
        slot.props.emplace_back(p.name, std::move(value));
    }
}

void ScriptHost::applyProps(ScriptSlot& slot) const {
    if (!slot.instance) return;
    const ScriptClassDesc* desc = findClass(slot.type);
    if (!desc) return;

    char* base = reinterpret_cast<char*>(slot.instance.get());
    for (const auto& [name, value] : slot.props) {
        for (size_t i = 0; i < desc->propCount; ++i) {
            const PropDesc& p = desc->props[i];
            if (name != p.name) continue;
            char* mem = base + p.offset;
            // Type must still match — members whose type changed since the
            // value was saved keep their in-class default instead.
            switch (p.type) {
                case PropType::Float:
                    if (auto* v = std::get_if<float>(&value)) *reinterpret_cast<float*>(mem) = *v;
                    // hand-edited scene JSON often writes floats as integers
                    else if (auto* i = std::get_if<int>(&value))
                        *reinterpret_cast<float*>(mem) = static_cast<float>(*i);
                    break;
                case PropType::Int:
                    if (auto* v = std::get_if<int>(&value)) *reinterpret_cast<int*>(mem) = *v;
                    break;
                case PropType::Bool:
                    if (auto* v = std::get_if<bool>(&value)) *reinterpret_cast<bool*>(mem) = *v;
                    break;
                case PropType::Vec3:
                    if (auto* v = std::get_if<glm::vec3>(&value)) *reinterpret_cast<glm::vec3*>(mem) = *v;
                    break;
                case PropType::String:
                    if (auto* v = std::get_if<std::string>(&value)) *reinterpret_cast<std::string*>(mem) = *v;
                    break;
            }
            break;
        }
    }
}

std::vector<const ScriptClassDesc*> ScriptHost::classes() const {
    std::vector<const ScriptClassDesc*> out;
    if (registry_)
        for (size_t i = 0; i < registry_->count; ++i) out.push_back(registry_->classes[i]);
    return out;
}

const ScriptClassDesc* ScriptHost::findClass(const std::string& name) const {
    if (registry_)
        for (size_t i = 0; i < registry_->count; ++i)
            if (name == registry_->classes[i]->name) return registry_->classes[i];
    return nullptr;
}

} // namespace vke
