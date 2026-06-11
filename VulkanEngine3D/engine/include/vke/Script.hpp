#pragma once

// The one header game scripts include. Deliberately Vulkan/GLFW-free: scripts
// compile against engine headers only and resolve engine symbols from the
// running editor when their shared library is dlopen'ed.

#include "Entity.hpp"
#include "Scene.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace vke {

class Application;
class Window;
class Renderer3D;

// Bumped whenever Script / ScriptRegistry layout changes; the editor refuses
// to load a scripts.so built against a different ABI.
inline constexpr uint32_t VKE_SCRIPT_ABI = 1;

// Base class for game scripts. Subclass it, override the lifecycle hooks and
// expose inspector-editable members with VKE_SCRIPT / VKE_PROPERTY:
//
//     class Rotator : public vke::Script {
//     public:
//         float speed = 90.0f;
//         void onUpdate(float dt) override { transform().rotation.y += speed * dt; }
//         VKE_SCRIPT(Rotator, VKE_PROPERTY(speed))
//     };
//
// onStart/onUpdate run only while the game is playing; instances also exist in
// edit mode so the Inspector can edit live members.
class Script {
public:
    virtual ~Script() = default;

    virtual void onStart() {}
    virtual void onUpdate(float /*dt*/) {}
    virtual void onDestroy() {}

    Entity&    entity() const    { return *entity_; }
    Transform& transform() const { return entity_->transform(); }
    Scene&     scene() const     { return *scene_; }
    Application& app() const     { return *app_; }

    // Out-of-line on purpose (engine/src/script/Script.cpp): keeps Vulkan/GLFW
    // headers out of script translation units.
    Window&     window() const;
    Renderer3D& renderer() const;
    bool keyDown(int key) const;            // key codes: vke/Keys.hpp
    bool mouseButtonDown(int button) const;

    // Called by the engine when an instance is (re)attached to an entity.
    void _bind(Application* app, Scene* scene, Entity* entity) {
        app_ = app; scene_ = scene; entity_ = entity;
    }

private:
    Application* app_    = nullptr;
    Scene*       scene_  = nullptr;
    Entity*      entity_ = nullptr;
};

// ---------------------------------------------------------------- reflection

enum class PropType { Float, Int, Bool, Vec3, String };

using PropValue = std::variant<float, int, bool, glm::vec3, std::string>;

struct PropDesc {
    const char* name;
    PropType    type;
    size_t      offset; // byte offset of the member inside the script object
};

struct ScriptClassDesc {
    const char* name;
    Script* (*create)();
    void (*destroy)(Script*);
    const PropDesc* props;
    size_t propCount;
};

struct ScriptRegistry {
    uint32_t abi;
    const ScriptClassDesc* const* classes;
    size_t count;
};

// ------------------------------------------------------------- the component

// Frees a script through the factory that created it, so instances built by a
// dlopen'ed library are deleted by that same library.
struct ScriptDeleter {
    void (*destroy)(Script*) = nullptr;
    void operator()(Script* s) const {
        if (!s) return;
        if (destroy) destroy(s);
        else delete s;
    }
};

// One script attached to an entity. `props` is the serialized state and is
// authoritative whenever `instance` is null (library unloaded, class missing);
// while an instance lives, its members are the live state and props are
// refreshed from them via ScriptHost::captureProps.
struct ScriptSlot {
    std::string type;
    std::vector<std::pair<std::string, PropValue>> props;
    std::unique_ptr<Script, ScriptDeleter> instance;
    bool started = false;
};

// Entity component holding any number of scripts. A vector (rather than one
// component type per script) because Entity stores components by type_index —
// one component per type per entity.
struct ScriptComponent : Component {
    std::vector<ScriptSlot> slots;
};

// ------------------------------------------------------ registration plumbing

namespace detail {

// Hidden visibility so the editor executable and each dlopen'ed scripts.so get
// their own registry storage even with --export-dynamic in play.
__attribute__((visibility("hidden"))) inline std::vector<const ScriptClassDesc*>&
localClasses() {
    static std::vector<const ScriptClassDesc*> v;
    return v;
}

struct AutoRegister {
    explicit AutoRegister(const ScriptClassDesc* d) { localClasses().push_back(d); }
};

template <class C> constexpr PropType propTypeOf(float C::*)       { return PropType::Float; }
template <class C> constexpr PropType propTypeOf(int C::*)         { return PropType::Int; }
template <class C> constexpr PropType propTypeOf(bool C::*)        { return PropType::Bool; }
template <class C> constexpr PropType propTypeOf(glm::vec3 C::*)   { return PropType::Vec3; }
template <class C> constexpr PropType propTypeOf(std::string C::*) { return PropType::String; }

} // namespace detail

} // namespace vke

// Declares one inspector-editable member. Only usable inside VKE_SCRIPT's
// argument list. Supported types: float, int, bool, glm::vec3, std::string.
#define VKE_PROPERTY(member)                                                     \
    ::vke::PropDesc{#member, ::vke::detail::propTypeOf(&Self_::member),          \
                    offsetof(Self_, member)}

// Registers the script class with the engine. Place it last in the class body;
// list properties with VKE_PROPERTY (or none).
#define VKE_SCRIPT(CLASS, ...)                                                   \
public:                                                                          \
    using Self_ = CLASS;                                                         \
    static const ::vke::ScriptClassDesc* vkeDesc_() {                            \
        static const std::vector<::vke::PropDesc> props_{__VA_ARGS__};           \
        static const ::vke::ScriptClassDesc desc_{                               \
            #CLASS,                                                              \
            []() -> ::vke::Script* { return new CLASS(); },                      \
            [](::vke::Script* s) { delete static_cast<CLASS*>(s); },             \
            props_.data(), props_.size()};                                       \
        return &desc_;                                                           \
    }                                                                            \
    inline static ::vke::detail::AutoRegister vkeReg_{CLASS::vkeDesc_()};

// The entry point the editor dlsym()s out of a compiled scripts.so. Only
// emitted when building scripts (the editor passes -DVKE_SCRIPT_BUILD), so the
// editor executable itself never defines it. Weak so multi-TU script builds
// link even though every TU that includes this header emits it.
#ifdef VKE_SCRIPT_BUILD
extern "C" __attribute__((weak, visibility("default"))) const ::vke::ScriptRegistry*
vkeGetScriptRegistry() {
    static ::vke::ScriptRegistry registry{};
    registry.abi     = ::vke::VKE_SCRIPT_ABI;
    registry.classes = ::vke::detail::localClasses().data();
    registry.count   = ::vke::detail::localClasses().size();
    return &registry;
}
#endif
